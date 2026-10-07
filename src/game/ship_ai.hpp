// Racing-line autopilot ported from RaceAIControl (0x5135E) / 0x51688 (see docs/simulation.md).
// Not ported: slot avoidance (lane offsets [slot+0x42/0x46], neighbours 0x3B6D0), doors, the random branch choice and the
// rubber-band speed caps; AI ships follow the node chain's centre line.
#pragma once
#include "game/scene.hpp"
#include "game/ship_sim.hpp"

namespace slip {

struct AiTables {  // read from the user's SLIPSTRM.EXE (0x5027A / 0x502A2, indexed by track 1..10)
  int32_t minSpeed[11] = {0, 214500, 171600, 171600, 214500, 171600, 214500, 228800, 171600, 171600, 171600};
  int32_t speedRange[11] = {0, 178750, 221650, 221650, 178750, 221650, 178750, 164450, 221650, 221650, 221650};
  bool fromExecutable = false;
};
AiTables loadAiTables(const GameData& data);

struct AiState {
  int node = -1;  // current target node (index into Track::nodes)
};

// Controls the original would have produced for this ship this frame (raw axes, no keyboard ramp).
ShipInput aiControl(const ShipState& s, AiState& ai, const Scene& scene, const AiTables& tables, const ShipParams& params);

}  // namespace slip
