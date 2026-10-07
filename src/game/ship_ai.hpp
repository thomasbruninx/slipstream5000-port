// Racing-line autopilot ported from RaceAIControl (0x5135E), 0x51688, TrackSlotGetNeighbours (0x3B6D0), 0x515D2/0x515F2,
// TrackSlotCheckBranch (0x3544F) and the per-tier speed factors of RaceSlotMove (see docs/simulation.md).
#pragma once
#include <vector>

#include "game/doors.hpp"
#include "game/scene.hpp"
#include "game/ship_sim.hpp"

namespace slip {

struct AiTables {  // read from the user's SLIPSTRM.EXE
  int32_t minSpeed[11] = {0, 214500, 171600, 171600, 214500, 171600, 214500, 228800, 171600, 171600, 171600};  // 0x5027A
  int32_t speedRange[11] = {0, 178750, 221650, 221650, 178750, 221650, 178750, 164450, 221650, 221650, 221650};  // 0x502A2
  int32_t tierFactor[4][11][4] = {};  // 0x5409C: [difficulty][track 1..10][tier 0..3], 2.14; used for AI controlled ships
  bool fromExecutable = false;
  int difficulty = 1;  // which of the four tables RaceSlotMove picks ([0x47234], menu setting; default = second)
};
AiTables loadAiTables(const GameData& data);
// Tier of a ship that starts in grid position `rank` (1..10): table at 0x586DB = {0,0,1,1,1,2,2,3,3,3}.
int aiTierForStartRank(int rank);

struct AiState {
  int node = -1;        // current target node (index into Track::nodes)
  bool branch = false;  // [rec+0x9C]: follow the alternative route at splits
  double startDelay = 3.0;  // slot data +0x4A = 0xBB8 ms: no avoidance after the start
  double offX = 0, offY = 0;  // lateral offset of the aim point in the node frame ([slot+0x42] right, [+0x46] up)
  int lap = 0;
  double progress = 0;  // distance along the lap
  int startRank = 1;
  int rank = 1;
  bool human = false;
  unsigned rng = 12345;
};

struct RaceInfo {
  std::vector<double> lapDist;  // per node, along the main route (alternative routes continue from their split node)
  double lapLength = 1;
  int shipCount = 10;
};
RaceInfo buildRaceInfo(const Scene& scene, int shipCount);

struct RaceContext {
  const Scene* scene = nullptr;
  const AiTables* tables = nullptr;
  const RaceInfo* race = nullptr;
  Doors* doors = nullptr;
  std::vector<ShipState*> ships;
  std::vector<AiState*> state;
  std::vector<const ShipParams*> params;
};

// Progress / laps / ranks of all ships (RaceUpdate role; ranks only feed the AI decisions and the trailing boost).
void updateRace(RaceContext& ctx);

// Debris handler support (0x3E8F2 / 0x3E9F5): aim point for wrecked ships, recovery (re-alignment to the track) once the
// wreck has left its piece, hit something, touched another ship or timed out.
void updateWrecks(RaceContext& ctx);

// Controls the original would have produced for ship `index` (raw axes, no keyboard ramp). Also opens doors the ship is
// standing on (TrackSlotFindDoor / 0x35564) and runs the branch decision.
ShipInput aiControl(RaceContext& ctx, size_t index, double dt);

// Catch-up boost of a human ship in last place (RaceSlotMove 0x51C70): +0.5 speed factor for 6 s when the ship ahead
// is more than 0x595B0 away.
void applyTrailingBoost(RaceContext& ctx, size_t humanIndex);

}  // namespace slip
