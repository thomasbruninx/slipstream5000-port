#include "game/drones.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "game/ship_collide.hpp"

namespace slip {

namespace {
constexpr int kMaxDrones = 6;             // 0x4A29F
constexpr double kFirstDelay = 1.0;       // [0x4A22C] = 0x3E8
constexpr double kPeriod = 10.0;          // 0x2710
constexpr double kRetry = 1.0;
constexpr int kAhead = 10;                // 0x36282 is called with ECX = 10
constexpr double kMaxSpeed = 0x2ba3e;     // 0x4A691
constexpr double kReach = 0x11df0;        // 0x4A529: the path node counts as reached
constexpr double kBonusLife = 15.0;       // 0x3A98 ms
constexpr double kYawGain = 4.0, kPitchGain = 3.5;  // 0x4A79C..0x4A7FC: the heading follows the error with 4.0 / s (yaw) and 3.5 / s (pitch)
double len3(const double* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
}  // namespace

void DroneWorld::reset() {
  drones.clear();
  targets.clear();
  blasts.clear();
  timer_ = kFirstDelay;
  counter_ = 0;
}

void DroneWorld::spawn(const Scene& scene, int humanNode) {
  const Track& t = scene.track_data;
  if (humanNode < 0 || t.nodes.empty()) return;
  int n = humanNode;
  static const int ahead = std::getenv("SLIP_DRONE_AHEAD") ? std::atoi(std::getenv("SLIP_DRONE_AHEAD")) : kAhead;  // test hook
  for (int i = 0; i < ahead; ++i) {
    n = t.nodes[size_t(n)].next;
    if (n < 0) return;
  }
  Drone d;
  const Vec3i& p = t.nodes[size_t(n)].pos;
  d.pos[0] = p.x; d.pos[1] = p.y; d.pos[2] = p.z;
  d.prev = n;
  d.node = t.nodes[size_t(n)].next;
  if (d.node < 0) return;
  const Vec3i& q = t.nodes[size_t(d.node)].pos;
  double f[3] = {double(q.x) - p.x, double(q.y) - p.y, double(q.z) - p.z};
  const double l = std::max(1.0, len3(f));
  for (int k = 0; k < 3; ++k) d.dir[k] = f[k] / l;
  ++counter_;
  d.phase = double(counter_) * 1.7;
  d.id = int(counter_);
  drones.push_back(d);
}

void DroneWorld::step(const Scene& scene, double dt, const double humanPos[3], int humanNode, const std::vector<const ShipState*>& ships, CombatWorld* combat, bool championship) {
  blasts.clear();
  const Track& t = scene.track_data;
  // hits written by the weapons since the last step, collisions with ships
  for (size_t i = 0; i < drones.size() && i < targets.size(); ++i) {
    DroneTarget& tg = targets[i];
    if (!tg.alive) continue;
    int hit = tg.hit;
    if (!hit)
      for (const ShipState* s : ships) {
        if (!s || !s->hasBox || s->wrecked) continue;
        const double d[3] = {drones[i].pos[0] - s->x, drones[i].pos[1] - s->y, drones[i].pos[2] - s->z};
        if (len3(d) < s->extent * 0.9 + radius) { hit = 2; break; }  // message 0x106: run into by a ship
      }
    if (!hit) continue;
    tg.alive = false;
    blasts.push_back({{drones[i].pos[0], drones[i].pos[1], drones[i].pos[2]}});
    if (hit == 1 && combat) combat->dropPickup(drones[i].pos, combat->randomBonusType(championship), kBonusLife);  // 0x202: the bonus appears
  }
  for (size_t i = drones.size(); i-- > 0;)
    if (i < targets.size() && !targets[i].alive) { drones.erase(drones.begin() + long(i)); targets.erase(targets.begin() + long(i)); }
  // spawning (0x4A291): the timer runs down, then a drone appears when fewer than six exist
  timer_ -= dt;
  if (timer_ < 0) {
    if (int(drones.size()) < kMaxDrones) {
      const size_t before = drones.size();
      spawn(scene, humanNode);
      timer_ = drones.size() > before ? kPeriod : kRetry;
    } else timer_ = kRetry;
  }
  // Flight (0x4A4CF, CONFIRMED structure): the drone chases a point of the path segment A -> B that lies 0x11DF0 units ahead of it; B advances when the drone is within 0x11DF0 of it.
  // The heading turns towards the chase point at 4.0 / s (yaw) and 3.5 / s (pitch) and the drone moves along its heading at the speed rule below. Nothing removes a drone but a hit
  // (ship, weapon, wall) or the end of the path: there is no distance rule (the port's own 3.5 million unit rule was dropped).
  for (size_t i = drones.size(); i-- > 0;) {
    Drone& d = drones[i];
    bool gone = d.node < 0 || size_t(d.node) >= t.nodes.size();
    bool wall = false;
    if (!gone) {
      auto at = [&](int n, double* o) { const Vec3i& q = t.nodes[size_t(n)].pos; o[0] = q.x; o[1] = q.y; o[2] = q.z; };
      double B[3];
      at(d.node, B);
      double to[3] = {B[0] - d.pos[0], B[1] - d.pos[1], B[2] - d.pos[2]};
      double dist = len3(to);
      if (dist < kReach) {  // 0x4A529: the node is reached, the next segment starts
        d.prev = d.node;
        d.node = t.nodes[size_t(d.node)].next;
        gone = d.node < 0 || size_t(d.node) >= t.nodes.size();
        if (!gone) { at(d.node, B); for (int k = 0; k < 3; ++k) to[k] = B[k] - d.pos[k]; }
      }
      if (!gone) {
        double A[3];
        if (d.prev >= 0) at(d.prev, A); else { A[0] = d.pos[0]; A[1] = d.pos[1]; A[2] = d.pos[2]; }
        double u[3] = {A[0] - B[0], A[1] - B[1], A[2] - B[2]};
        const double segLen = len3(u);
        if (segLen > 1e-6) for (double& x : u) x /= segLen;
        double ahead = dist - kReach;                 // 0x4A5A2
        if (ahead < 0) ahead += segLen;
        const double carrot[3] = {B[0] + u[0] * ahead, B[1] + u[1] * ahead, B[2] + u[2] * ahead};
        double want[3] = {carrot[0] - d.pos[0], carrot[1] - d.pos[1], carrot[2] - d.pos[2]};
        const double wl = len3(want);
        if (wl > 1e-6) {
          for (double& x : want) x /= wl;
          const double tau = 6.283185307179586;
          const double yaw = std::atan2(d.dir[0], d.dir[2]), pitch = std::asin(std::clamp(d.dir[1], -1.0, 1.0));
          double ey = std::atan2(want[0], want[2]) - yaw;
          while (ey > tau / 2) ey -= tau;
          while (ey < -tau / 2) ey += tau;
          const double ep = std::asin(std::clamp(want[1], -1.0, 1.0)) - pitch;
          const double ny = yaw + std::clamp(ey, -tau / 4, tau / 4) * std::min(1.0, kYawGain * dt);
          const double np = std::clamp(pitch + std::clamp(ep, -tau / 4, tau / 4) * std::min(1.0, kPitchGain * dt), -1.4, 1.4);
          d.dir[0] = std::sin(ny) * std::cos(np); d.dir[1] = std::sin(np); d.dir[2] = std::cos(ny) * std::cos(np);
        }
        // speed (0x4A648..0x4A69D): 0xAE8F8 (capped at 0x2BA3E) while the node is far; within 0x3B920 of it the curvature of the next 0x5F500 units sets it:
        // 0x1F6BC + 0x2F21A * (0x4000 - sum of (0x8000 - straight)) / 0x4000, never above 0x2BA3E
        double v = 0xae8f8;
        if (dist <= 0x3b920) {
          double acc = 0, sumLen = 0, last[3] = {d.pos[0], d.pos[1], d.pos[2]};
          int n = d.node;
          for (int guard = 0; guard < 200 && n >= 0; ++guard) {
            const Vec3i& np2 = t.nodes[size_t(n)].pos;
            const double e[3] = {np2.x - last[0], np2.y - last[1], np2.z - last[2]};
            double a[3] = {std::fabs(e[0]), std::fabs(e[1]), std::fabs(e[2])};
            std::sort(a, a + 3);
            sumLen += a[2] + (a[1] + a[0]) * 0.25;
            last[0] = np2.x; last[1] = np2.y; last[2] = np2.z;
            acc += 0x8000 - t.nodes[size_t(n)].straight;
            if (sumLen >= 0x5f500) break;
            n = t.nodes[size_t(n)].next;
          }
          const double g = std::max(0.0, 0x4000 - acc);
          if (g != 0x3000) v = std::floor(0x2f21a * g / 16384.0) + 0x1f6bc;
        }
        v = std::min(v, kMaxSpeed);
        const double len = v * dt;
        // the drone is a slot of the collision system (TrackSlotAdd 0x41): the track stops it (message 0x107)
        const double I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, lo[3] = {-radius, -radius, -radius}, hi[3] = {radius, radius, radius};
        const ShipHit h = sweepShipBox(scene, d.pos, I, lo, hi, d.dir, len);
        if (h.hit && h.dist < len) wall = true;
        else for (int j = 0; j < 3; ++j) d.pos[j] += d.dir[j] * len;
        d.phase += dt * 0.9;
      }
    }
    if (gone || wall) {
      if (wall) blasts.push_back({{d.pos[0], d.pos[1], d.pos[2]}, true});
      drones.erase(drones.begin() + long(i));
      if (i < targets.size()) targets.erase(targets.begin() + long(i));
    }
  }
  targets.resize(drones.size());
  for (size_t i = 0; i < drones.size(); ++i) {
    for (int k = 0; k < 3; ++k) targets[i].pos[k] = drones[i].pos[k];
    targets[i].radius = radius;
    targets[i].id = drones[i].id;
    targets[i].alive = true;
    targets[i].hit = 0;
  }
}

}  // namespace slip
