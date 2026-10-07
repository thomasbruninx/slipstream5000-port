#include "game/ship_sim.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

void stepShip(ShipState& s, const ShipInput& in, double dt, const ShipParams& p, const Scene& scene, const ShipSimConfig& cfg) {
  double top = double(p.topSpeed);
  double a;
  if (in.throttle > 0.01f) {
    double ratio = std::clamp(s.speed / top, 0.0, 1.0);
    a = (double(p.thrustAtRest) - double(p.thrustAtRest - p.thrustAtTop) * ratio) * in.throttle;  // from RaceSlotMove
  } else {
    a = -double(p.coastDecel);  // from RaceSlotMove (no throttle)
  }
  a -= double(p.coastDecel) * 2.0 * in.brake;  // PLACEHOLDER brake strength
  s.speed = std::clamp(s.speed + a * dt, 0.0, top);

  s.yaw += double(in.steer) * cfg.maxYawRate * dt * (0.35 + 0.65 * std::min(1.0, s.speed / (0.3 * top) + 0.2));
  double nx = s.x + std::sin(s.yaw) * s.speed * dt;
  double nz = s.z + std::cos(s.yaw) * s.speed * dt;

  double floorY;
  if (scene.floorHeight(nx, nz, s.y + cfg.hoverHeight * 6, 0, &floorY)) {
    s.x = nx;
    s.z = nz;
    double target = floorY + cfg.hoverHeight;
    s.y += (target - s.y) * std::min(1.0, 12.0 * dt);  // simple critically-damped-ish follow
  } else {
    // No ground ahead: the edge of the track acts as a wall (PLACEHOLDER, not original collision).
    s.speed *= 0.35;
  }
}

}  // namespace slip
