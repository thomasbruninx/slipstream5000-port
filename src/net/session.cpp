#include "net/session.hpp"

#include <algorithm>
#include <chrono>

namespace slip::net {

namespace {
int64_t steadyMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}  // namespace

Session::Session(std::unique_ptr<ITransport> t, uint16_t discoveryPort) : transport_(std::move(t)), discPort_(discoveryPort), t0_(steadyMs()) {}

uint32_t Session::nowMs() const { return uint32_t(steadyMs() - t0_); }

bool Session::host(const std::string& name, uint32_t dataHash, uint16_t port, std::string* err) {
  if (state_ != State::Idle) return false;
  if (!transport_->listen(port)) { if (err) *err = "cannot listen on TCP port " + std::to_string(port); return false; }
  name_ = name; dataHash_ = dataHash; myId_ = 0;
  sessionId_ = uint32_t(steadyMs()) ^ (uint32_t(port) << 16) ^ 0x5a17u;
  lobby_ = Lobby{};
  lobby_.players.push_back({0, name, 0, 1});
  known_.insert(0);
  state_ = State::Lobby;
  disc_ = makeLanDiscovery(discPort_);
  lobbyChanged();
  return true;
}

bool Session::join(const std::string& host, uint16_t port, const std::string& name, uint32_t dataHash, std::string* err) {
  if (state_ != State::Idle) return false;
  transport_->listen(0);  // the other players dial this port after the host introduces us
  name_ = name; dataHash_ = dataHash; hostAddr_ = host;
  hostLink_ = transport_->connect(host, port);
  if (!hostLink_) { if (err) *err = "cannot connect to " + host; return false; }
  Peer p; p.outgoing = true; p.isHostLink = true; p.player = 0; p.ip = host; p.port = port;
  peers_[hostLink_] = p;
  state_ = State::Joining;
  return true;
}

void Session::leave() {
  if (state_ == State::Idle || state_ == State::Closed) return;
  for (auto& [pid, p] : peers_) if (p.hello) { transport_->send(pid, encodeLeave()); }
  std::vector<NetEvent> ev;
  transport_->poll(&ev);
  for (auto& [pid, p] : peers_) transport_->disconnect(pid);
  peers_.clear();
  if (disc_) disc_->stopAnnounce();
  state_ = State::Closed;
}

void Session::close(const std::string& why) {
  if (state_ == State::Closed) return;
  for (auto& [pid, p] : peers_) transport_->disconnect(pid);
  peers_.clear();
  if (disc_) disc_->stopAnnounce();
  state_ = State::Closed;
  error_ = why;
  events_.push_back({Event::Closed, -1, why});
}

PeerId Session::pidOf(int player) const {
  for (const auto& [pid, p] : peers_) if (p.player == player && p.hello) return pid;
  return 0;
}

int Session::freePlayerId() const {
  for (int i = 0; i < kMaxPlayers; ++i) if (!known_.count(i)) return i;
  return -1;
}

int Session::freeShip(int except) const {
  for (int s = 0; s < 10; ++s) {
    bool used = false;
    for (const LobbyEntry& e : lobby_.players) if (e.id != except && e.ship == s) used = true;
    if (!used) return s;
  }
  return 0;
}

void Session::lobbyChanged() {
  if (!isHost()) return;
  const Bytes m = encode(lobby_);
  for (auto& [pid, p] : peers_) if (p.hello) transport_->send(pid, m);
  if (!disc_ && state_ == State::Lobby) disc_ = makeLanDiscovery(discPort_);
  if (disc_ && state_ == State::Lobby) {
    SessionInfo s;
    s.name = name_; s.track = lobby_.track; s.laps = lobby_.laps; s.players = uint8_t(lobby_.players.size()); s.maxPlayers = kMaxPlayers;
    s.version = kProtocolVersion; s.dataHash = dataHash_; s.port = transport_->listenPort();
    disc_->startAnnounce(s);
    disc_->updateAnnounce(s);
  }
}

void Session::setIntent(int ship, bool ready) {
  ship = std::clamp(ship, 0, 9);
  if (isHost()) {
    for (LobbyEntry& e : lobby_.players) {
      if (e.id != hostId_) continue;
      bool taken = false;
      for (const LobbyEntry& o : lobby_.players) if (o.id != hostId_ && o.ship == ship) taken = true;
      if (!taken) e.ship = uint8_t(ship);
      e.ready = 1;  // the host's own flag is implied
    }
    lobbyChanged();
  } else if (state_ == State::Lobby) {
    transport_->send(hostLink_, encode(LobbyIntent{uint8_t(ship), uint8_t(ready)}));
  }
}

void Session::hostSettings(int track, int laps, int difficulty, bool aiFill) {
  if (!isHost()) return;
  lobby_.track = uint8_t(std::clamp(track, 1, 10)); lobby_.laps = uint8_t(std::clamp(laps, 1, 20));
  lobby_.difficulty = uint8_t(std::clamp(difficulty, 0, 2)); lobby_.aiFill = aiFill;
  lobbyChanged();
}

bool Session::hostCanStart() const {
  if (!isHost() || state_ != State::Lobby) return false;
  for (const LobbyEntry& e : lobby_.players) if (e.id != hostId_ && !e.ready) return false;
  return true;
}

bool Session::hostStart(uint32_t seed) {
  if (!hostCanStart()) return false;
  Start s;
  s.track = lobby_.track; s.laps = lobby_.laps; s.difficulty = lobby_.difficulty; s.seed = seed;
  s.startAtMs = nowMs() + 2500;  // the host's clock is the session clock
  for (int i = 0; i < 10; ++i) s.slots[size_t(i)] = {lobby_.aiFill ? uint8_t(2) : uint8_t(0), kNoPlayer, "AI"};
  for (const LobbyEntry& e : lobby_.players) s.slots[e.ship] = {1, e.id, e.name};
  const Bytes m = encode(s);
  for (auto& [pid, p] : peers_) if (p.hello) transport_->send(pid, m);
  start_ = s; startLocal_ = s.startAtMs; startPending_ = true;
  state_ = State::Racing;
  if (disc_) disc_->stopAnnounce();
  return true;
}

bool Session::takeStart(Start* out, uint32_t* startLocalMs) {
  if (!startPending_) return false;
  startPending_ = false;
  *out = start_; *startLocalMs = startLocal_;
  return true;
}

void Session::returnToLobby() {
  if (state_ != State::Racing) return;
  state_ = State::Lobby;
  inbound_.clear();
  if (isHost()) lobbyChanged();
}

void Session::broadcast(const Bytes& msg) { for (auto& [pid, p] : peers_) if (p.hello) transport_->send(pid, msg); }
void Session::sendTo(int player, const Bytes& msg) { if (const PeerId pid = pidOf(player)) transport_->send(pid, msg); }
void Session::sendToHost(const Bytes& msg) { if (isHost()) return; transport_->send(hostLink_, msg); }
std::vector<Session::Inbound> Session::drain() { std::vector<Inbound> o; o.swap(inbound_); return o; }
std::vector<Session::Event> Session::drainEvents() { std::vector<Event> o; o.swap(events_); return o; }

std::vector<int> Session::connectedPlayers() const {
  std::vector<int> v;
  for (const auto& [pid, p] : peers_) if (p.hello && p.player >= 0) v.push_back(p.player);
  std::sort(v.begin(), v.end());
  return v;
}

std::string Session::playerName(int id) const {
  for (const LobbyEntry& e : lobby_.players) if (e.id == id) return e.name;
  for (const auto& [pid, p] : peers_) if (p.player == id) return p.name;
  return "Player " + std::to_string(id);
}

void Session::onHello(PeerId pid, Peer& p, const Hello& h) {
  if (h.version != kProtocolVersion) {
    if (isHost() || h.playerId == kNoPlayer) { transport_->send(pid, encode(Reject{"protocol version mismatch"})); transport_->disconnect(pid); }
    return;
  }
  if (h.dataHash != dataHash_) { transport_->send(pid, encode(Reject{"different game data files"})); transport_->disconnect(pid); return; }
  if (h.playerId == kNoPlayer) {  // a new player joins through the host
    if (!isHost()) { transport_->send(pid, encode(Reject{"not the host"})); transport_->disconnect(pid); return; }
    if (state_ != State::Lobby) { transport_->send(pid, encode(Reject{"race in progress"})); transport_->disconnect(pid); return; }
    const int id = freePlayerId();
    if (id < 0) { transport_->send(pid, encode(Reject{"session is full"})); transport_->disconnect(pid); return; }
    p.player = id; p.hello = true; p.name = h.name; p.ip = transport_->peerAddress(pid); p.port = h.listenPort;
    known_.insert(id);
    Welcome w;
    w.yourId = uint8_t(id); w.sessionId = sessionId_;
    w.peers.push_back({uint8_t(myId_), "", transport_->listenPort()});  // the joiner uses the address it dialled for the host
    for (const auto& [opid, o] : peers_) if (o.hello && o.player >= 0 && opid != pid) w.peers.push_back({uint8_t(o.player), o.ip, o.port});
    transport_->send(pid, encode(w));
    lobby_.players.push_back({uint8_t(id), h.name, uint8_t(freeShip(id)), 0});
    events_.push_back({Event::Joined, id, h.name});
    lobbyChanged();
    return;
  }
  // mesh link from another player (the dialler announces its id)
  p.player = h.playerId; p.hello = true; p.name = h.name; p.ip = transport_->peerAddress(pid); p.port = h.listenPort;
  if (known_.insert(p.player).second) events_.push_back({Event::Joined, p.player, h.name});
}

void Session::onWelcome(const Welcome& w) {
  myId_ = w.yourId; sessionId_ = w.sessionId;
  state_ = State::Lobby;
  known_.insert(myId_);
  known_.insert(0);
  Peer& hl = peers_[hostLink_];
  hl.hello = true;
  for (const PeerAddr& a : w.peers) {
    known_.insert(a.playerId);
    if (a.ip.empty()) { hostId_ = a.playerId; hl.player = a.playerId; continue; }  // the host introduces itself without an address
    const PeerId pid = transport_->connect(a.ip, a.port);
    if (!pid) continue;
    Peer p; p.outgoing = true; p.player = a.playerId; p.ip = a.ip; p.port = a.port;
    peers_[pid] = p;
  }
}

void Session::onGone(PeerId pid) {
  auto it = peers_.find(pid);
  if (it == peers_.end()) return;
  const Peer p = it->second;
  peers_.erase(it);
  if (pid == hostLink_ && !isHost()) {
    if (state_ == State::Joining) { close(error_.empty() ? "cannot connect to the host" : error_); return; }
    migrateHost();
    return;
  }
  if (!p.hello || p.player < 0) return;
  if (!known_.erase(p.player)) return;
  events_.push_back({Event::Left, p.player, p.name});
  if (isHost()) {
    lobby_.players.erase(std::remove_if(lobby_.players.begin(), lobby_.players.end(), [&](const LobbyEntry& e) { return e.id == p.player; }), lobby_.players.end());
    lobbyChanged();
  }
}

// The host's connection dropped: every peer picks the lowest remaining player id as the new host (all peers know the same list, so no
// election messages are needed). The new host keeps its copy of the lobby, announces the session again and takes over the AI ships (game layer).
void Session::migrateHost() {
  const int old = hostId_;
  std::string oldName = playerName(old);
  known_.erase(old);
  events_.push_back({Event::Left, old, oldName});
  lobby_.players.erase(std::remove_if(lobby_.players.begin(), lobby_.players.end(), [&](const LobbyEntry& e) { return e.id == old; }), lobby_.players.end());
  rttMap_.erase(old);
  hostId_ = known_.empty() ? myId_ : *known_.begin();
  hostLink_ = pidOf(hostId_);
  for (auto& [pid, p] : peers_) p.isHostLink = pid == hostLink_;
  if (hostId_ != myId_ && !hostLink_) { close("the host left the session"); return; }
  events_.push_back({Event::HostChanged, hostId_, playerName(hostId_)});
  bestRtt_ = 1e9; pingBurst_ = 6; pingTimer_ = 0;
  if (isHost()) {
    for (LobbyEntry& e : lobby_.players) if (e.id == myId_) e.ready = 1;
    lobbyChanged();
  }
}

void Session::handle(PeerId pid, const Bytes& data) {
  auto it = peers_.find(pid);
  if (it == peers_.end()) return;
  Peer& p = it->second;
  const Msg t = msgType(data);
  if (t == Msg::Hello) { Hello h; if (decode(data, &h)) onHello(pid, p, h); return; }
  if (t == Msg::Reject) { Reject r; if (decode(data, &r)) { error_ = r.reason; close(r.reason); } return; }
  if (t == Msg::Welcome) { Welcome w; if (pid == hostLink_ && state_ == State::Joining && decode(data, &w)) onWelcome(w); return; }
  if (!p.hello) return;
  switch (t) {
    case Msg::Lobby: {
      Lobby l;
      if (pid == hostLink_ && decode(data, &l)) {
        lobby_ = l;
        for (const LobbyEntry& e : l.players) if (known_.insert(e.id).second) events_.push_back({Event::Joined, e.id, e.name});
      }
      break;
    }
    case Msg::LobbyIntent: {
      LobbyIntent li;
      if (isHost() && state_ == State::Lobby && decode(data, &li)) {
        for (LobbyEntry& e : lobby_.players) {
          if (e.id != p.player) continue;
          bool taken = false;
          for (const LobbyEntry& o : lobby_.players) if (o.id != e.id && o.ship == li.ship) taken = true;
          if (!taken && li.ship < 10) e.ship = li.ship;
          e.ready = li.ready;
        }
        lobbyChanged();
      }
      break;
    }
    case Msg::Start: {
      Start s;
      if (pid == hostLink_ && decode(data, &s)) {
        start_ = s; startLocal_ = uint32_t(int64_t(s.startAtMs) - offset_); startPending_ = true;
        state_ = State::Racing;
      }
      break;
    }
    case Msg::Ping: {
      Ping pg;
      if (decode(data, &pg)) transport_->send(pid, encode(Pong{pg.id, pg.tMs, nowMs()}));
      break;
    }
    case Msg::Pong: {
      Pong pn;
      if (decode(data, &pn)) {
        const double rtt = double(nowMs() - pn.echoMs);
        double& r = rttMap_.try_emplace(p.player, rtt).first->second;
        r = r * 0.7 + rtt * 0.3;
      }
      if (pid == hostLink_ && decode(data, &pn)) {
        const double rtt = double(nowMs() - pn.echoMs);
        if (rtt <= bestRtt_ * 1.5 + 1 || pingBurst_ > 0) {  // NTP style: trust low-latency samples
          offset_ = int32_t(int64_t(pn.tMs) + int64_t(rtt / 2) - int64_t(nowMs()));
          bestRtt_ = std::min(bestRtt_ * 1.02 + 0.1, rtt);
        }
        rtt_ = rtt_ == 0 ? rtt : rtt_ * 0.8 + rtt * 0.2;
      }
      break;
    }
    case Msg::Leave: break;  // the closing socket reports the departure
    default: inbound_.push_back({p.player, data}); break;
  }
}

void Session::update(double dt) {
  if (state_ == State::Idle) return;
  if (disc_) disc_->update(dt);
  std::vector<NetEvent> ev;
  transport_->poll(&ev);
  for (NetEvent& e : ev) {
    if (e.type == NetEvent::Connected) {
      if (e.incoming) { Peer p; peers_[e.peer] = p; continue; }
      auto it = peers_.find(e.peer);
      if (it == peers_.end()) continue;
      Hello h;
      h.dataHash = dataHash_; h.sessionId = sessionId_; h.listenPort = transport_->listenPort(); h.name = name_;
      h.playerId = it->second.isHostLink ? kNoPlayer : uint8_t(myId_);
      transport_->send(e.peer, encode(h));
      if (!it->second.isHostLink) it->second.hello = true;  // our dial: the peer is known to us already
    } else if (e.type == NetEvent::Disconnected) {
      onGone(e.peer);
    } else {
      handle(e.peer, e.data);
    }
  }
  if (state_ == State::Lobby || state_ == State::Racing) {
    pingTimer_ -= dt;
    if (pingTimer_ <= 0) {  // everybody pings everybody: the host link also gives the session clock, the others the per-player ping
      pingTimer_ = pingBurst_ > 0 && !isHost() ? 0.1 : 1.0;
      if (pingBurst_ > 0) --pingBurst_;
      ++pingId_;
      for (auto& [pid, p] : peers_) if (p.hello) transport_->send(pid, encode(Ping{pingId_, nowMs()}));
    }
  }
}

}  // namespace slip::net
