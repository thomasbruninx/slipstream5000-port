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
  int32_t lapEstimateMs[11] = {0, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000};  // 0x5A4C4: lap time used to project the finish
  bool fromExecutable = false;
  int difficulty = 1;  // which of the four tables RaceSlotMove picks ([0x47234], menu setting; default = second)
};
AiTables loadAiTables(const GameData& data);
// Difficulty level 0..2 ([0x492EA] = word at offset 157 of SLIPSTRM.CFG, read by 0x49F04; the file loads at 0x4924D). 1 when the
// file is missing; the original's default is 1 as well. The level selects the AI tier table (0x5409C) and doubles the blaster damage at 2.
int readConfiguredDifficulty(const GameData& data);
// Tier of a ship that starts in grid position `rank` (1..10): table at 0x586DB = {0,0,1,1,1,2,2,3,3,3}.
int aiTierForStartRank(int rank);

struct AiState {
  int node = -1;        // current target node (index into Track::nodes)
  bool branch = false;  // [rec+0x9C]: follow the alternative route at splits
  double startDelay = 3.0;  // slot data +0x4A = 0xBB8 ms: no avoidance after the start
  double offX = 0, offY = 0;  // lateral offset of the aim point in the node frame ([slot+0x42] right, [+0x46] up)
  int lap = 0;          // completed laps (laps - 1 once the start line has been crossed)
  double progress = 0;  // distance along the lap
  // Race record (stride 0x4E record of the original, RaceUpdate 0x5A4EC): laps counts line crossings (+0x20: 1 after the start
  // crossing), `back` = +0x22 (the line was crossed backwards, the next forward crossing is not counted).
  int piece = -1;       // piece the ship is in (slot cell, sticky: only changes to linked pieces)
  int laps = 0;
  bool back = false;
  bool finished = false;
  int finishRank = 0;
  int prevRank = 0;
  double finishTime = 0;  // clock at the finish, or the projection made when the race ended (0x5A461)
  bool projected = false;
  double raceTime = 0, lapTime = 0, bestLap = 0, lastLap = 0;  // seconds ([+0xE], lap timer, [+0x16], last lap)
  int cueRankAnnounce = 0;  // set when the lap line was crossed (the human hears the position)
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

struct RaceStatus {  // global race state ([0x5440C] end timer, 0x5A780)
  double endTimer = -1;  // < 0: not started; 5 s after the second AI ship finished (or all AI ships are done) the race ends
  bool over = false;
  bool settled = false;  // multiplayer: the unfinished ships got their projected finish after the race end
  int finishedCount = 0;
  int bestAiRank = 10, prevBestAiRank = 10;  // [0x50438] / [0x5043A]: best rank among the AI ships this / last frame
  struct Event { int ship; int kind; };  // kind 0 start crossing, 1 lap line, 2 finished, 3 human passed the best AI ship
  std::vector<Event> events;
};

struct RaceContext {
  RaceStatus* status = nullptr;
  double dt = 0;
  bool running = true;   // false during the countdown ([0x54408] != 0): the clocks stand still
  int totalLaps = 3;     // [0x5440E]
  const Scene* scene = nullptr;
  const AiTables* tables = nullptr;
  const RaceInfo* race = nullptr;
  Doors* doors = nullptr;
  std::vector<ShipState*> ships;
  std::vector<AiState*> state;
  std::vector<const ShipParams*> params;
  // multiplayer: ships simulated on another peer keep the race record they send (laps, finish); empty slots are not part of the race
  std::vector<bool> remote, absent;
  bool multiplayer = false;      // the race ends 5 s after the second finisher (humans included) instead of the second AI finisher
  bool authoritativeEnd = true;  // false on peers that wait for the host's race-over message
};

// RaceUpdate 0x5A4EC: lap line crossings between the TRD lap pieces, finishing, ranks, clocks and the race end timer.
void updateRace(RaceContext& ctx);

enum class LapEvent { None, Started, Lap, Finished };
// 0x5A56A..0x5A67A for one ship that moved from piece `oldPiece` to `newPiece` (pieceA = TRD header +4, pieceB = +6): crossing
// B -> A is the forward line crossing (the first one only starts the race, laps becomes 1), A -> B sets the `back` flag.
LapEvent lapCrossing(AiState& a, int oldPiece, int newPiece, int pieceA, int pieceB, int totalLaps, int finishedSoFar);
// 0x5A6C6..0x5A74F: ranks 1..n; finished ships keep their finishing rank, the others sort by (laps - back) descending, then by the
// distance still to go ascending.
void assignRanks(const std::vector<AiState*>& state, const std::vector<double>& remain);
// Start phase ([0x54404], first 15 s after GO): extra speed factor by rank (table 0x50252).
double startBonusForRank(int rank);

// Debris handler support (0x3E8F2 / 0x3E9F5): aim point for wrecked ships, recovery (re-alignment to the track) once the
// wreck has left its piece, hit something, touched another ship or timed out.
void updateWrecks(RaceContext& ctx);
// "Reset player" of the pause menu (a port addition): puts the ship on the path node of the piece it is in (the centre line of the track, horizontally and vertically), heading along
// the track, at rest, no longer wrecked. Falls back to the nearest node when the ship is outside every piece. Returns false without a track.
bool resetToTrackCentre(const Scene& scene, ShipState& s, AiState& a);

// Controls the original would have produced for ship `index` (raw axes, no keyboard ramp). Also opens doors the ship is
// standing on (TrackSlotFindDoor / 0x35564) and runs the branch decision.
ShipInput aiControl(RaceContext& ctx, size_t index, double dt);

// Catch-up boost of a human ship in last place (RaceSlotMove 0x51C70): +0.5 speed factor for 6 s when the ship ahead
// is more than 0x595B0 away.
void applyTrailingBoost(RaceContext& ctx, size_t humanIndex);

}  // namespace slip
