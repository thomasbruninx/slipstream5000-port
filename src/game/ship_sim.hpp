// Minimal, NOT original-accurate ship dynamics used for the "drive" demo (Milestone E).
// Only the thrust-vs-speed relation comes from the original code (a = f0 - (f0-f1)*v/vtop);
// steering rate, hover height and braking are placeholders until validated (docs/simulation.md).
#pragma once
#include "game/scene.hpp"
#include "game/ship_params.hpp"

namespace slip {

struct ShipState {
  double x = 0, y = 0, z = 0;  // world coordinates
  double yaw = 0;               // radians; 0 = looking along +z, positive turns towards +x
  double pitch = 0;
  double speed = 0;
  int ship = 0;
};

struct ShipInput {
  float throttle = 0;  // 0..1
  float brake = 0;     // 0..1
  float steer = 0;     // -1..1 (positive = right)
};

struct ShipSimConfig {
  double hoverHeight = 14000;   // world units above floor (PLACEHOLDER)
  double maxYawRate = 1.8;      // rad/s (PLACEHOLDER)
};

void stepShip(ShipState& s, const ShipInput& in, double dt, const ShipParams& p, const Scene& scene, const ShipSimConfig& cfg);

}  // namespace slip
