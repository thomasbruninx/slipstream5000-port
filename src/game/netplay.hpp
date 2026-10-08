// Game side of the multiplayer mode (see docs/multiplayer.md): maps the 10 ship slots to the players of a net::Session, replicates the
// locally simulated ships as ~30 Hz snapshots, plays the other peers' ships back with interpolation, and relays projectiles, hits and
// pickups. Every peer simulates its own ship (owner = player id); the host also simulates the AI ships and ships of players that left.
#pragma once
#include <array>
#include <deque>
#include <map>
#include <string>

#include "game/doors.hpp"
#include "game/ship_ai.hpp"
#include "game/ship_sim.hpp"
#include "game/weapons.hpp"
#include "net/session.hpp"

namespace slip {

class Netplay {
 public:
  struct Bind {  // the application's race objects, indexed by ship slot
    std::array<ShipState*, 10> ship{};
    std::array<AiState*, 10> ai{};
    CombatWorld* combat = nullptr;
    RaceStatus* race = nullptr;
  };

  Netplay(net::Session* s, const net::Start& start);

  int mySlot() const { return mySlot_; }
  int myId() const { return myId_; }
  bool isHost() const { return s_->isHost(); }
  int hostId() const { return s_->hostId(); }
  // scoreboard data: ping in ms of the player who simulates `slot` (0 = me, -1 = not measured yet)
  double slotPing(int slot) const { const int o = slots_[size_t(slot)].owner; return o == myId_ ? 0.0 : s_->rttOf(o); }
  const std::string& notice() const { return notice_; }
  double noticeAge() const { return noticeAge_; }
  bool present(int slot) const { return slots_[size_t(slot)].kind != 0; }
  bool remote(int slot) const { return present(slot) && slots_[size_t(slot)].owner != myId_; }
  bool humanSlot(int slot) const { return slots_[size_t(slot)].kind == 1; }          // a person plays it (not an AI, not a vacated seat)
  bool localAi(int slot) const { return present(slot) && slots_[size_t(slot)].owner == myId_ && slot != mySlot_; }  // host: AI or vacated ship
  int owner(int slot) const { return slots_[size_t(slot)].owner; }
  const std::string& name(int slot) const { return slots_[size_t(slot)].name; }
  bool sessionClosed() const { return s_->state() == net::Session::State::Closed; }
  const std::string& closeReason() const { return s_->error(); }
  double pingMs() const { return s_->rttMs(); }
  int humans() const { int n = 0; for (const Slot& s : slots_) n += s.kind == 1; return n; }

  // once per frame: receive, replicate, relay combat events
  void update(double dt, const Bind& b);
  // pose of a remote ship at session time `now` seconds (interpolated; call per simulation step)
  void applyRemote(int slot, ShipState& s, AiState& a, double now) const;
  // Two peers can finish within one network delay of each other and both claim the same place: order the finished ships by (claimed
  // place, slot) and renumber, which every peer does identically.
  void reconcileFinishRanks(const Bind& b) const;
  void sendRaceOver();
  void leave() { s_->leave(); }
  net::Session& session() { return *s_; }
  struct Stats { int states = 0, spawnsIn = 0, spawnsOut = 0, hitsIn = 0, hitsOut = 0, pickupsIn = 0, pickupsOut = 0; } stats;

 private:
  struct Slot { uint8_t kind = 0; int owner = -1; std::string name; };
  struct Snap { double t; net::State st; };  // t = sender clock, seconds
  struct Track { std::deque<Snap> snaps; double offset = 0; bool haveOffset = false; uint16_t seq = 0; bool haveSeq = false; };
  void handle(int from, const Bytes& data, const Bind& b);
  void captureState(int slot, const Bind& b, net::State* out) const;
  net::Session* s_;
  int myId_ = 0, mySlot_ = 0;
  std::array<Slot, 10> slots_;
  std::array<Track, 10> tracks_;
  std::map<int, double> clockOffset_;  // sender player id -> (local arrival - sender clock), min filtered
  double sendTimer_ = 0;
  uint16_t seq_ = 0;
  bool raceOverSent_ = false;
  std::string notice_;
  double noticeAge_ = 1e9;
};

}  // namespace slip
