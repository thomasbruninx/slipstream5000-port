#include "game/drones.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
constexpr double kForgetDistance = 3.5e6; // own: drones this far from the human are removed
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
  d.node = t.nodes[size_t(n)].next;
  if (d.node < 0) return;
  const Vec3i& q = t.nodes[size_t(d.node)].pos;
  double f[3] = {double(q.x) - p.x, double(q.y) - p.y, double(q.z) - p.z};
  const double l = std::max(1.0, len3(f));
  for (int k = 0; k < 3; ++k) d.dir[k] = f[k] / l;
  ++counter_;
  d.phase = double(counter_) * 1.7;
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
  // flight along the path nodes
  for (size_t i = drones.size(); i-- > 0;) {
    Drone& d = drones[i];
    bool gone = d.node < 0 || size_t(d.node) >= t.nodes.size();
    if (!gone) {
      const Vec3i& q = t.nodes[size_t(d.node)].pos;
      double to[3] = {double(q.x) - d.pos[0], double(q.y) - d.pos[1], double(q.z) - d.pos[2]};
      double dist = len3(to);
      if (dist < kReach) {
        d.node = t.nodes[size_t(d.node)].next;
        gone = d.node < 0;
      } else {
        for (double& x : to) x /= dist;
        const double k = std::min(1.0, 2.5 * dt);  // turns smoothly towards the next node
        for (int j = 0; j < 3; ++j) d.dir[j] += (to[j] - d.dir[j]) * k;
        const double l = std::max(1e-6, len3(d.dir));
        for (double& x : d.dir) x /= l;
        // speed (0x4A648..0x4A69D): 0xAE8F8 (capped at 0x2BA3E) while the node is far; within 0x3B920 of it the curvature of the next 0x5F500 units sets it:
        // 0x1F6BC + 0x2F21A * (0x4000 - sum of (0x8000 - straight)) / 0x4000, never above 0x2BA3E
        double v = 0xae8f8;
        if (dist <= 0x3b920) {
          double acc = 0, sumLen = 0, last[3] = {d.pos[0], d.pos[1], d.pos[2]};
          int n = d.node;
          for (int guard = 0; guard < 200 && n >= 0; ++guard) {
            const Vec3i& np = t.nodes[size_t(n)].pos;
            const double e[3] = {np.x - last[0], np.y - last[1], np.z - last[2]};
            double a[3] = {std::fabs(e[0]), std::fabs(e[1]), std::fabs(e[2])};
            std::sort(a, a + 3);
            sumLen += a[2] + (a[1] + a[0]) * 0.25;
            last[0] = np.x; last[1] = np.y; last[2] = np.z;
            acc += 0x8000 - t.nodes[size_t(n)].straight;
            if (sumLen >= 0x5f500) break;
            n = t.nodes[size_t(n)].next;
          }
          const double g = std::max(0.0, 0x4000 - acc);
          if (g != 0x3000) v = std::floor(0x2f21a * g / 16384.0) + 0x1f6bc;
        }
        v = std::min(v, kMaxSpeed);
        for (int j = 0; j < 3; ++j) d.pos[j] += d.dir[j] * v * dt;
        d.phase += dt * 0.9;
      }
    }
    const double away[3] = {d.pos[0] - humanPos[0], d.pos[1] - humanPos[1], d.pos[2] - humanPos[2]};
    if (gone || len3(away) > kForgetDistance) { drones.erase(drones.begin() + long(i)); if (i < targets.size()) targets.erase(targets.begin() + long(i)); }
  }
  targets.resize(drones.size());
  for (size_t i = 0; i < drones.size(); ++i) {
    for (int k = 0; k < 3; ++k) targets[i].pos[k] = drones[i].pos[k];
    targets[i].radius = radius;
    targets[i].alive = true;
    targets[i].hit = 0;
  }
}

}  // namespace slip
