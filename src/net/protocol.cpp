#include "net/protocol.hpp"

#include <algorithm>
#include <cmath>

namespace slip::net {

int16_t toQ14(double v) { return int16_t(std::clamp(std::lround(v * 16384.0), -32767L, 32767L)); }
double fromQ14(int16_t v) { return double(v) / 16384.0; }

Msg msgType(const Bytes& b) { return b.empty() ? Msg(0) : Msg(b[0]); }

namespace {
Writer begin(Msg t) { Writer w; w.u8(uint8_t(t)); return w; }
bool check(const Bytes& b, Msg t, Reader* r) {
  if (msgType(b) != t) return false;
  *r = Reader(b.data() + 1, b.size() - 1);
  return true;
}
}  // namespace

Bytes encode(const Hello& m) { Writer w = begin(Msg::Hello); w.u32(m.version); w.u32(m.dataHash); w.u32(m.sessionId); w.u8(m.playerId); w.u16(m.listenPort); w.str(m.name); return w.buf; }
bool decode(const Bytes& b, Hello* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::Hello, &r)) return false;
  m->version = r.u32(); m->dataHash = r.u32(); m->sessionId = r.u32(); m->playerId = uint8_t(r.u8()); m->listenPort = uint16_t(r.u16()); m->name = r.str();
  return r.ok();
}

Bytes encode(const Welcome& m) {
  Writer w = begin(Msg::Welcome);
  w.u8(m.yourId); w.u32(m.sessionId); w.u8(uint32_t(m.peers.size()));
  for (const PeerAddr& p : m.peers) { w.u8(p.playerId); w.str(p.ip); w.u16(p.port); }
  return w.buf;
}
bool decode(const Bytes& b, Welcome* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::Welcome, &r)) return false;
  m->yourId = uint8_t(r.u8()); m->sessionId = r.u32();
  const uint32_t n = r.u8();
  m->peers.clear();
  for (uint32_t i = 0; i < n && r.ok(); ++i) { PeerAddr p; p.playerId = uint8_t(r.u8()); p.ip = r.str(); p.port = uint16_t(r.u16()); m->peers.push_back(p); }
  return r.ok();
}

Bytes encode(const Reject& m) { Writer w = begin(Msg::Reject); w.str(m.reason); return w.buf; }
bool decode(const Bytes& b, Reject* m) { Reader r(nullptr, 0); if (!check(b, Msg::Reject, &r)) return false; m->reason = r.str(); return r.ok(); }

Bytes encode(const Lobby& m) {
  Writer w = begin(Msg::Lobby);
  w.u8(m.track); w.u8(m.laps); w.u8(m.difficulty); w.u8(m.aiFill); w.u8(uint32_t(m.players.size()));
  for (const LobbyEntry& e : m.players) { w.u8(e.id); w.str(e.name); w.u8(e.ship); w.u8(e.ready); }
  return w.buf;
}
bool decode(const Bytes& b, Lobby* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::Lobby, &r)) return false;
  m->track = uint8_t(r.u8()); m->laps = uint8_t(r.u8()); m->difficulty = uint8_t(r.u8()); m->aiFill = uint8_t(r.u8());
  const uint32_t n = r.u8();
  m->players.clear();
  for (uint32_t i = 0; i < n && r.ok(); ++i) { LobbyEntry e; e.id = uint8_t(r.u8()); e.name = r.str(); e.ship = uint8_t(r.u8()); e.ready = uint8_t(r.u8()); m->players.push_back(e); }
  return r.ok();
}

Bytes encode(const LobbyIntent& m) { Writer w = begin(Msg::LobbyIntent); w.u8(m.ship); w.u8(m.ready); return w.buf; }
bool decode(const Bytes& b, LobbyIntent* m) { Reader r(nullptr, 0); if (!check(b, Msg::LobbyIntent, &r)) return false; m->ship = uint8_t(r.u8()); m->ready = uint8_t(r.u8()); return r.ok(); }

Bytes encode(const Start& m) {
  Writer w = begin(Msg::Start);
  w.u8(m.track); w.u8(m.laps); w.u8(m.difficulty); w.u32(m.seed); w.u32(m.startAtMs);
  for (const StartSlot& s : m.slots) { w.u8(s.kind); w.u8(s.playerId); w.str(s.name); }
  return w.buf;
}
bool decode(const Bytes& b, Start* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::Start, &r)) return false;
  m->track = uint8_t(r.u8()); m->laps = uint8_t(r.u8()); m->difficulty = uint8_t(r.u8()); m->seed = r.u32(); m->startAtMs = r.u32();
  for (StartSlot& s : m->slots) { s.kind = uint8_t(r.u8()); s.playerId = uint8_t(r.u8()); s.name = r.str(); }
  return r.ok();
}

Bytes encode(const State& m) {
  Writer w = begin(Msg::State);
  w.u8(m.slot); w.u16(m.seq); w.u32(m.tMs);
  for (int32_t v : m.pos) w.i32(v);
  for (int16_t v : m.m) w.i16(v);
  w.i32(m.speed);
  for (int32_t v : m.slide) w.i32(v);
  w.u16(m.damageA); w.u16(m.damageB); w.u8(m.flags);
  w.u8(m.reverse); w.u8(m.halfCap); w.u8(m.forced); w.u8(m.hyper); w.u8(m.freeBoost);
  w.u8(m.selected); w.u8(m.laps); w.u8(m.finishRank); w.u8(uint8_t(m.lockTarget)); w.u16(m.progress);
  return w.buf;
}
bool decode(const Bytes& b, State* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::State, &r)) return false;
  m->slot = uint8_t(r.u8()); m->seq = uint16_t(r.u16()); m->tMs = r.u32();
  for (int32_t& v : m->pos) v = r.i32();
  for (int16_t& v : m->m) v = int16_t(r.i16());
  m->speed = r.i32();
  for (int32_t& v : m->slide) v = r.i32();
  m->damageA = uint16_t(r.u16()); m->damageB = uint16_t(r.u16()); m->flags = uint8_t(r.u8());
  m->reverse = uint8_t(r.u8()); m->halfCap = uint8_t(r.u8()); m->forced = uint8_t(r.u8()); m->hyper = uint8_t(r.u8()); m->freeBoost = uint8_t(r.u8());
  m->selected = uint8_t(r.u8()); m->laps = uint8_t(r.u8()); m->finishRank = uint8_t(r.u8()); m->lockTarget = int8_t(r.u8()); m->progress = uint16_t(r.u16());
  return r.ok();
}

Bytes encode(const ProjSpawn& m) {
  Writer w = begin(Msg::ProjSpawn);
  w.u32(m.id); w.u8(m.owner); w.u8(m.kind); w.u8(uint8_t(m.target));
  for (int32_t v : m.pos) w.i32(v);
  for (int16_t v : m.m) w.i16(v);
  w.i32(m.speed); w.u16(m.lifeCs); w.u8(m.first);
  return w.buf;
}
bool decode(const Bytes& b, ProjSpawn* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::ProjSpawn, &r)) return false;
  m->id = r.u32(); m->owner = uint8_t(r.u8()); m->kind = uint8_t(r.u8()); m->target = int8_t(r.u8());
  for (int32_t& v : m->pos) v = r.i32();
  for (int16_t& v : m->m) v = int16_t(r.i16());
  m->speed = r.i32(); m->lifeCs = uint16_t(r.u16()); m->first = uint8_t(r.u8());
  return r.ok();
}

Bytes encode(const ProjHit& m) { Writer w = begin(Msg::ProjHit); w.u32(m.id); w.u8(m.victim); for (int32_t v : m.pos) w.i32(v); return w.buf; }
bool decode(const Bytes& b, ProjHit* m) { Reader r(nullptr, 0); if (!check(b, Msg::ProjHit, &r)) return false; m->id = r.u32(); m->victim = uint8_t(r.u8()); for (int32_t& v : m->pos) v = r.i32(); return r.ok(); }

Bytes encode(const PickupTaken& m) { Writer w = begin(Msg::PickupTaken); w.u8(m.index); w.u8(m.slot); w.u32(m.tMs); return w.buf; }
bool decode(const Bytes& b, PickupTaken* m) { Reader r(nullptr, 0); if (!check(b, Msg::PickupTaken, &r)) return false; m->index = uint8_t(r.u8()); m->slot = uint8_t(r.u8()); m->tMs = r.u32(); return r.ok(); }

Bytes encode(const PickupResult& m) { Writer w = begin(Msg::PickupResult); w.u8(m.index); w.u8(m.slot); w.u8(m.granted); return w.buf; }
bool decode(const Bytes& b, PickupResult* m) { Reader r(nullptr, 0); if (!check(b, Msg::PickupResult, &r)) return false; m->index = uint8_t(r.u8()); m->slot = uint8_t(r.u8()); m->granted = uint8_t(r.u8()); return r.ok(); }

Bytes encode(const LapEvent& m) { Writer w = begin(Msg::LapEvent); w.u8(m.slot); w.u8(m.laps); w.u8(m.finished); w.u8(m.finishRank); w.u32(m.lapTimeMs); w.u32(m.finishTimeMs); return w.buf; }
bool decode(const Bytes& b, LapEvent* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::LapEvent, &r)) return false;
  m->slot = uint8_t(r.u8()); m->laps = uint8_t(r.u8()); m->finished = uint8_t(r.u8()); m->finishRank = uint8_t(r.u8()); m->lapTimeMs = r.u32(); m->finishTimeMs = r.u32();
  return r.ok();
}

Bytes encode(const Ping& m) { Writer w = begin(Msg::Ping); w.u32(m.id); w.u32(m.tMs); return w.buf; }
bool decode(const Bytes& b, Ping* m) { Reader r(nullptr, 0); if (!check(b, Msg::Ping, &r)) return false; m->id = r.u32(); m->tMs = r.u32(); return r.ok(); }
Bytes encode(const Pong& m) { Writer w = begin(Msg::Pong); w.u32(m.id); w.u32(m.echoMs); w.u32(m.tMs); return w.buf; }
bool decode(const Bytes& b, Pong* m) { Reader r(nullptr, 0); if (!check(b, Msg::Pong, &r)) return false; m->id = r.u32(); m->echoMs = r.u32(); m->tMs = r.u32(); return r.ok(); }

Bytes encode(const DoorSync& m) {
  Writer w = begin(Msg::DoorSync);
  w.u8(uint32_t(m.doors.size()));
  for (const auto& d : m.doors) { w.i32(d.pos); w.u8(uint8_t(d.state)); w.u16(d.speed); }
  return w.buf;
}
bool decode(const Bytes& b, DoorSync* m) {
  Reader r(nullptr, 0);
  if (!check(b, Msg::DoorSync, &r)) return false;
  const uint32_t n = r.u8();
  m->doors.clear();
  for (uint32_t i = 0; i < n && r.ok(); ++i) { DoorSync::D d; d.pos = r.i32(); d.state = int8_t(r.u8()); d.speed = uint16_t(r.u16()); m->doors.push_back(d); }
  return r.ok();
}

Bytes encodeRaceOver() { return begin(Msg::RaceOver).buf; }
Bytes encodeLeave() { return begin(Msg::Leave).buf; }

}  // namespace slip::net
