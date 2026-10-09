// Wire protocol of the multiplayer mode (version 1). Every message is `u8 type` + fields (little endian). The session model: a lobby host
// (player id 0) decides track / laps / difficulty / grid, every player simulates his own ship and broadcasts snapshots to all peers (full mesh).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "net/byte_io.hpp"

namespace slip::net {

constexpr uint32_t kProtocolVersion = 1;
constexpr int kMaxPlayers = 10;
constexpr uint16_t kDefaultPort = 51500;       // TCP game port
constexpr uint16_t kDiscoveryPort = 51501;     // UDP LAN announcements
constexpr uint8_t kNoPlayer = 0xFF;

enum class Msg : uint8_t {
  Hello = 1, Welcome, Reject, Lobby, LobbyIntent, Start, State, ProjSpawn, ProjHit, PickupTaken, PickupResult, LapEvent, RaceOver, Ping, Pong, Leave, DoorSync,
};

struct PeerAddr { uint8_t playerId = 0; std::string ip; uint16_t port = 0; };

struct Hello {            // first message in both directions of every connection
  uint32_t version = kProtocolVersion;
  uint32_t dataHash = 0;  // identifies the game data set (all players need identical files)
  uint32_t sessionId = 0;
  uint8_t playerId = kNoPlayer;  // kNoPlayer while joining the host
  uint16_t listenPort = 0;
  std::string name;
};
struct Welcome { uint8_t yourId = 0; uint32_t sessionId = 0; std::vector<PeerAddr> peers; };  // host -> joiner; the joiner dials `peers`
struct Reject { std::string reason; };

struct LobbyEntry { uint8_t id = 0; std::string name; uint8_t ship = 0; uint8_t ready = 0; };
struct Lobby {            // host -> everyone whenever something changes
  uint8_t track = 1, laps = 6, difficulty = 1, aiFill = 1;
  std::vector<LobbyEntry> players;
};
struct LobbyIntent { uint8_t ship = 0; uint8_t ready = 0; };  // player -> host

struct StartSlot { uint8_t kind = 0; uint8_t playerId = kNoPlayer; std::string name; };  // kind 0 empty, 1 human, 2 AI
struct Start {            // host -> everyone: the race begins at startInMs on the receiver's clock after offset correction
  uint8_t track = 1, laps = 6, difficulty = 1;
  uint32_t seed = 1;
  uint32_t startAtMs = 0;  // host clock (ms) at which the countdown starts
  std::array<StartSlot, kMaxPlayers> slots;
};

struct State {            // one ship snapshot (owner -> all peers, ~30 Hz); everything fixed point
  uint8_t slot = 0;
  uint16_t seq = 0;
  uint32_t tMs = 0;       // sender clock
  int32_t pos[3] = {0, 0, 0};
  int16_t m[9] = {16384, 0, 0, 0, 16384, 0, 0, 0, 16384};  // 2.14
  int32_t speed = 0;
  int32_t slide[3] = {0, 0, 0};
  uint16_t damageA = 0, damageB = 0;  // x100
  uint8_t flags = 0;      // bit0 wrecked, bit1 boosterOn, bit2 finished, bit3 projected, bit4 line crossed backwards
  uint8_t reverse = 0, halfCap = 0, forced = 0, hyper = 0, freeBoost = 0;  // tenths of a second
  uint8_t selected = 0;
  uint8_t laps = 0, finishRank = 0;
  int8_t lockTarget = -1;
  uint16_t progress = 0;  // distance to the line / 64 (ranking tie break)
};
struct ProjSpawn {
  uint32_t id = 0;
  uint8_t owner = 0, kind = 0;
  int8_t target = -1;
  int32_t pos[3] = {0, 0, 0};
  int16_t m[9] = {16384, 0, 0, 0, 16384, 0, 0, 0, 16384};
  int32_t speed = 0;
  uint16_t lifeCs = 0xFFFF;  // remaining life in 1/100 s, 0xFFFF = unlimited
  uint8_t first = 0;         // first projectile of a launch (plays the launch sound)
};
struct ProjHit { uint32_t id = 0; uint8_t victim = kNoPlayer; int32_t pos[3] = {0, 0, 0}; };
struct PickupTaken { uint8_t index = 0, slot = 0; uint32_t tMs = 0; };
struct PickupResult { uint8_t index = 0, slot = 0, granted = 0; };
struct LapEvent { uint8_t slot = 0, laps = 0, finished = 0, finishRank = 0; uint32_t lapTimeMs = 0, finishTimeMs = 0; };
struct Ping { uint32_t id = 0, tMs = 0; };
struct Pong { uint32_t id = 0, echoMs = 0, tMs = 0; };
struct DoorSync { struct D { int32_t pos = 0; int8_t state = 0; uint16_t speed = 0; }; std::vector<D> doors; };

// encode: full message including the type byte; decode: false on malformed data (type byte already checked by msgType()).
Msg msgType(const Bytes& b);  // 0 when empty
Bytes encode(const Hello&);        bool decode(const Bytes&, Hello*);
Bytes encode(const Welcome&);      bool decode(const Bytes&, Welcome*);
Bytes encode(const Reject&);       bool decode(const Bytes&, Reject*);
Bytes encode(const Lobby&);        bool decode(const Bytes&, Lobby*);
Bytes encode(const LobbyIntent&);  bool decode(const Bytes&, LobbyIntent*);
Bytes encode(const Start&);        bool decode(const Bytes&, Start*);
Bytes encode(const State&);        bool decode(const Bytes&, State*);
Bytes encode(const ProjSpawn&);    bool decode(const Bytes&, ProjSpawn*);
Bytes encode(const ProjHit&);      bool decode(const Bytes&, ProjHit*);
Bytes encode(const PickupTaken&);  bool decode(const Bytes&, PickupTaken*);
Bytes encode(const PickupResult&); bool decode(const Bytes&, PickupResult*);
Bytes encode(const LapEvent&);     bool decode(const Bytes&, LapEvent*);
Bytes encode(const Ping&);         bool decode(const Bytes&, Ping*);
Bytes encode(const Pong&);         bool decode(const Bytes&, Pong*);
Bytes encode(const DoorSync&);     bool decode(const Bytes&, DoorSync*);
Bytes encodeRaceOver();
Bytes encodeLeave();

// 2.14 matrix / fixed point helpers shared by sender and receiver
int16_t toQ14(double v);
double fromQ14(int16_t v);

}  // namespace slip::net
