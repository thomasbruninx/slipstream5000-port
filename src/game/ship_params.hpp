// Per-ship performance parameters. The original game keeps ten 28-byte blocks inside
// SLIPSTRM.EXE (VA 0x502F8, file offset 0x8DB4C). We read them from the user's own executable
// at run time instead of embedding them. Field roles are STRONGLY INFERRED from the thrust
// arithmetic in RaceSlotMove (see docs/simulation.md).
#pragma once
#include <array>
#include <string>

#include "original_formats/game_data.hpp"

namespace slip {

struct ShipParams {
  int32_t thrustAtRest = 71500;    // f0: acceleration at speed 0 (units/s^2)
  int32_t thrustAtTop = 35750;     // f1: acceleration at top speed
  int32_t coastDecel = 128700;     // f2: deceleration without throttle
  int32_t topSpeed = 289575;       // f3: units/s
  int32_t f4 = 19660, f5 = 16384;  // 2.14 factors (1.2, 1.0) – role UNKNOWN
  int32_t f6 = 244000;             // role UNKNOWN
  bool fromExecutable = false;
};

std::array<ShipParams, 10> loadShipParams(const GameData& data);

}  // namespace slip
