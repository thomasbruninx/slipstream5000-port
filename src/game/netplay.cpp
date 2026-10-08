#include "game/netplay.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

namespace {
constexpr double kSendInterval = 1.0 / 30.0;
constexpr double kInterpDelay = 0.10;  // seconds behind the newest snapshot
constexpr double kMaxExtrapolate = 0.25;

int32_t fx(double v) { return int32_t(std::lround(std::clamp(v, -2.0e9, 2.0e9))); }
}  // namespace

Netplay::Netplay(net::Session* s, const net::Start& st) : s_(s), myId_(s->myId()) {
  mySlot_ = 0;
  for (int i = 0; i < 10; ++i) {
    const net::StartSlot& in = st.slots[size_t(i)];
    Slot& sl = slots_[size_t(i)];
    sl.kind = in.kind;
    sl.name = in.name;
    sl.owner = in.kind == 1 ? int(in.playerId) : in.kind == 2 ? 0 : -1;  // the host simulates the AI ships
    if (in.kind == 1 && int(in.playerId) == myId_) mySlot_ = i;
  }
}

void Netplay::captureState(int slot, const Bind& b, net::State* o) const {
  const ShipState& s = *b.ship[size_t(slot)];
  const AiState& a = *b.ai[size_t(slot)];
  const CombatState& c = b.combat->combat[size_t(slot)];
  o->slot = uint8_t(slot);
  o->seq = seq_;
  o->tMs = s_->nowMs();
  o->pos[0] = fx(s.x); o->pos[1] = fx(s.y); o->pos[2] = fx(s.z);
  for (int k = 0; k < 9; ++k) o->m[k] = net::toQ14(s.m[k]);
  o->speed = fx(s.speed);
  for (int k = 0; k < 3; ++k) o->slide[k] = fx(s.slide[k]);
  o->damageA = uint16_t(std::clamp(s.damageA * 100.0, 0.0, 65535.0)); o->damageB = uint16_t(std::clamp(s.damageB * 100.0, 0.0, 65535.0));
  o->flags = uint8_t((s.wrecked ? 1 : 0) | (s.boosterOn ? 2 : 0) | (a.finished && !a.projected ? 4 : 0) | (a.projected ? 8 : 0) | (a.back ? 16 : 0));
  auto tenths = [](double t) { return uint8_t(std::clamp(std::ceil(t * 10.0), 0.0, 255.0)); };
  o->reverse = tenths(s.reverseTime); o->halfCap = tenths(s.halfCapTime); o->forced = tenths(s.forceThrottleTime); o->hyper = tenths(s.hyperTime); o->freeBoost = tenths(s.boosterFreeTime);
  o->selected = uint8_t(c.selected);
  o->laps = uint8_t(std::clamp(a.laps, 0, 255));
  o->finishRank = uint8_t(a.finishRank);
  o->lockTarget = int8_t(c.lockTarget);
}

void Netplay::update(double dt, const Bind& b) {
  noticeAge_ += dt;
  for (const net::Session::Event& e : s_->drainEvents()) {
    if (e.type == net::Session::Event::HostChanged) {
      notice_ = (e.player == myId_ ? std::string("You are the new host") : e.text + " is the new host");
      noticeAge_ = 0;
      continue;
    }
    if (e.type != net::Session::Event::Left) continue;
    if (notice_.empty() || noticeAge_ > 3) { notice_ = e.text + " left the race"; noticeAge_ = 0; }
    for (int i = 0; i < 10; ++i) {  // a player left: the host takes the ship over and flies it as an AI ship (after a host change, the AI ships move to the new host)
      Slot& sl = slots_[size_t(i)];
      if (sl.owner != e.player) continue;
      if (sl.kind == 1) { sl.kind = 2; sl.name = "AI"; }
      sl.owner = s_->hostId();
      if (sl.owner == myId_) { b.ai[size_t(i)]->piece = -1; b.ai[size_t(i)]->node = -1; tracks_[size_t(i)].snaps.clear(); }  // start tracking the ship from where it is
    }
  }
  const uint32_t now = s_->nowMs();
  for (const net::Session::Inbound& m : s_->drain()) handle(m.from, m.data, b);
  (void)now;
  sendTimer_ += dt;
  if (sendTimer_ >= kSendInterval) {
    sendTimer_ = std::fmod(sendTimer_, kSendInterval);
    ++seq_;
    for (int i = 0; i < 10; ++i) {
      if (!present(i) || remote(i)) continue;
      net::State st;
      captureState(i, b, &st);
      s_->broadcast(net::encode(st));
    }
  }
  CombatWorld& cw = *b.combat;
  for (const Projectile& p : cw.launched) {
    net::ProjSpawn m;
    m.id = p.id; m.owner = uint8_t(p.owner); m.kind = uint8_t(p.kind); m.target = int8_t(p.target);
    for (int k = 0; k < 3; ++k) m.pos[k] = fx(p.pos[k]);
    for (int k = 0; k < 9; ++k) m.m[k] = net::toQ14(p.m[k]);
    m.speed = fx(p.speed);
    m.lifeCs = p.life < 0 ? uint16_t(0xFFFF) : uint16_t(std::clamp(p.life * 100.0, 0.0, 65000.0));
    m.first = p.first;
    s_->broadcast(net::encode(m));
    ++stats.spawnsOut;
  }
  cw.launched.clear();
  for (const CombatWorld::HitRec& h : cw.hitLog) {
    net::ProjHit m;
    m.id = h.id; m.victim = uint8_t(h.victim);
    for (int k = 0; k < 3; ++k) m.pos[k] = fx(h.pos[k]);
    s_->broadcast(net::encode(m));
    ++stats.hitsOut;
  }
  cw.hitLog.clear();
  for (const auto& pk : cw.pickupLog) { s_->broadcast(net::encode(net::PickupTaken{uint8_t(pk.first), uint8_t(pk.second), now})); ++stats.pickupsOut; }
  cw.pickupLog.clear();
  if (isHost() && b.race->over && !raceOverSent_) sendRaceOver();
}

void Netplay::reconcileFinishRanks(const Bind& b) const {
  std::vector<std::array<int, 3>> done;  // (projected, claimed place, slot): real finishers first, then the projected finishes
  for (int i = 0; i < 10; ++i)
    if (present(i) && b.ai[size_t(i)]->finished) done.push_back({b.ai[size_t(i)]->projected ? 1 : 0, b.ai[size_t(i)]->finishRank, i});
  std::sort(done.begin(), done.end());
  for (size_t k = 0; k < done.size(); ++k) b.ai[size_t(done[k][2])]->finishRank = int(k) + 1;
}

void Netplay::sendRaceOver() {
  raceOverSent_ = true;
  s_->broadcast(net::encodeRaceOver());
}

void Netplay::handle(int from, const Bytes& data, const Bind& b) {
  using net::Msg;
  const double nowSec = s_->nowMs() / 1000.0;
  switch (net::msgType(data)) {
    case Msg::State: {
      net::State st;
      if (!net::decode(data, &st) || st.slot >= 10) break;
      Track& t = tracks_[st.slot];
      if (!remote(st.slot) || slots_[st.slot].owner != from) break;
      if (t.haveSeq && int16_t(uint16_t(st.seq - t.seq)) <= 0) break;  // old or duplicate
      t.seq = st.seq; t.haveSeq = true;
      const double ts = st.tMs / 1000.0;
      double& off = clockOffset_.try_emplace(from, nowSec - ts).first->second;
      off = std::min(off + 0.00005, nowSec - ts);  // lowest delay seen, drifting up slowly so a clock skew change is followed
      t.snaps.push_back({ts, st});
      ++stats.states;
      while (t.snaps.size() > 2 && t.snaps[1].t < ts - 2.0) t.snaps.pop_front();
      break;
    }
    case Msg::ProjSpawn: {
      net::ProjSpawn m;
      if (!net::decode(data, &m) || m.owner >= 10 || slots_[m.owner].owner != from || m.kind >= kWeaponCount) break;
      Projectile p;
      p.id = m.id; p.owner = m.owner; p.kind = m.kind; p.target = m.target; p.first = m.first != 0;
      for (int k = 0; k < 3; ++k) p.pos[k] = m.pos[k];
      for (int k = 0; k < 9; ++k) p.m[k] = net::fromQ14(m.m[k]);
      p.speed = m.speed;
      p.life = m.lifeCs == 0xFFFF ? -1.0 : m.lifeCs / 100.0;
      b.combat->spawnRemote(p);
      ++stats.spawnsIn;
      break;
    }
    case Msg::ProjHit: {
      net::ProjHit m;
      if (!net::decode(data, &m) || m.victim >= 10 || slots_[m.victim].owner != from) break;
      CombatContext cc;
      const double pos[3] = {double(m.pos[0]), double(m.pos[1]), double(m.pos[2])};
      cc.humanShip = mySlot_;
      for (int i = 0; i < 10; ++i) cc.shipClass.push_back(i + 1);
      b.combat->remoteHit(cc, m.id, m.victim, pos);
      ++stats.hitsIn;
      break;
    }
    case Msg::PickupTaken: {
      net::PickupTaken m;
      if (!net::decode(data, &m) || m.slot >= 10 || slots_[m.slot].owner != from) break;
      b.combat->remotePickup(m.index, m.slot);
      ++stats.pickupsIn;
      break;
    }
    case Msg::RaceOver:
      if (from == s_->hostId()) b.race->over = true;
      break;
    default: break;
  }
}

void Netplay::applyRemote(int slot, ShipState& s, AiState& a, double now) const {
  const Track& t = tracks_[size_t(slot)];
  if (t.snaps.empty()) return;
  const auto off = clockOffset_.find(slots_[size_t(slot)].owner);
  const double rt = now - (off == clockOffset_.end() ? 0.0 : off->second) - kInterpDelay;  // sender clock to render
  const Snap* a0 = &t.snaps.front();
  const Snap* a1 = nullptr;
  for (size_t i = 0; i + 1 < t.snaps.size(); ++i)
    if (t.snaps[i].t <= rt && rt <= t.snaps[i + 1].t) { a0 = &t.snaps[i]; a1 = &t.snaps[i + 1]; break; }
  double pos[3], m[9];
  const net::State& latest = t.snaps.back().st;
  if (a1) {
    const double u = a1->t > a0->t ? (rt - a0->t) / (a1->t - a0->t) : 1.0;
    for (int k = 0; k < 3; ++k) pos[k] = a0->st.pos[k] + (a1->st.pos[k] - a0->st.pos[k]) * u;
    for (int k = 0; k < 9; ++k) m[k] = net::fromQ14(a0->st.m[k]) + (net::fromQ14(a1->st.m[k]) - net::fromQ14(a0->st.m[k])) * u;
  } else if (rt < t.snaps.front().t) {
    for (int k = 0; k < 3; ++k) pos[k] = t.snaps.front().st.pos[k];
    for (int k = 0; k < 9; ++k) m[k] = net::fromQ14(t.snaps.front().st.m[k]);
  } else {  // newer than anything received: carry on with the last velocity for a moment
    double vel[3] = {0, 0, 0};
    if (t.snaps.size() >= 2) {
      const Snap& p = t.snaps[t.snaps.size() - 2];
      const double dtS = t.snaps.back().t - p.t;
      if (dtS > 1e-3) for (int k = 0; k < 3; ++k) vel[k] = (t.snaps.back().st.pos[k] - p.st.pos[k]) / dtS;
    }
    const double e = std::min(rt - t.snaps.back().t, kMaxExtrapolate);
    for (int k = 0; k < 3; ++k) pos[k] = latest.pos[k] + vel[k] * e;
    for (int k = 0; k < 9; ++k) m[k] = net::fromQ14(latest.m[k]);
  }
  for (int r = 0; r < 3; ++r) {
    const double l = std::sqrt(m[r * 3] * m[r * 3] + m[r * 3 + 1] * m[r * 3 + 1] + m[r * 3 + 2] * m[r * 3 + 2]);
    if (l > 1e-6) for (int k = 0; k < 3; ++k) m[r * 3 + k] /= l;
  }
  s.x = pos[0]; s.y = pos[1]; s.z = pos[2];
  for (int k = 0; k < 9; ++k) s.m[k] = m[k];
  s.matrixInit = true;
  s.yaw = std::atan2(m[6], m[8]);
  s.pitch = std::asin(std::clamp(m[7], -1.0, 1.0));
  s.speed = latest.speed;
  for (int k = 0; k < 3; ++k) s.slide[k] = latest.slide[k];
  s.damageA = latest.damageA / 100.0; s.damageB = latest.damageB / 100.0;
  s.wrecked = latest.flags & 1;
  s.boosterOn = latest.flags & 2;
  s.reverseTime = latest.reverse / 10.0; s.halfCapTime = latest.halfCap / 10.0; s.forceThrottleTime = latest.forced / 10.0;
  s.hyperTime = latest.hyper / 10.0; s.boosterFreeTime = latest.freeBoost / 10.0;
  a.laps = latest.laps;
  a.back = latest.flags & 16;
  a.finished = (latest.flags & 12) != 0;
  a.projected = (latest.flags & 8) != 0;
  a.finishRank = latest.finishRank;
}

}  // namespace slip
