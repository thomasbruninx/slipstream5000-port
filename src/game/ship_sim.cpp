#include "game/ship_sim.hpp"
#include "game/ship_collide.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

namespace {
constexpr double kTau = 6.283185307179586;

void orthonormalize(double* m) {  // sub_23003: re-normalise forward, make right orthogonal, up = forward x right
  auto norm = [](double* v) {
    double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0) for (int i = 0; i < 3; ++i) v[i] /= l;
  };
  double* r = m;
  double* u = m + 3;
  double* f = m + 6;
  norm(f);
  double d = r[0] * f[0] + r[1] * f[1] + r[2] * f[2];
  for (int i = 0; i < 3; ++i) r[i] -= d * f[i];
  norm(r);
  u[0] = f[1] * r[2] - f[2] * r[1];
  u[1] = f[2] * r[0] - f[0] * r[2];
  u[2] = f[0] * r[1] - f[1] * r[0];
}

// The four axis rotations of sub_2708F, angles in turns. Pairs are (a,b) element indices into m.
void rot(double* m, double turns, const int (*pairs)[2], double sgnA, double sgnB) {
  double c = std::cos(turns * kTau), s = std::sin(turns * kTau);
  for (int k = 0; k < 3; ++k) {
    double a = m[pairs[k][0]], b = m[pairs[k][1]];
    m[pairs[k][0]] = a * c + sgnA * b * s;
    m[pairs[k][1]] = b * c + sgnB * a * s;
  }
}
const int kPitchPairs[3][2] = {{3, 6}, {4, 7}, {5, 8}};  // sub_22B40: rows up/forward
const int kRollPairs[3][2] = {{0, 3}, {1, 4}, {2, 5}};   // sub_22D61: rows right/up
const int kYawPairs[3][2] = {{0, 2}, {3, 5}, {6, 8}};    // sub_22EB1: x/z components of every row
}  // namespace

void shipRenderMatrix(const ShipState& s, float* R) {
  if (!s.matrixInit) {  // static ships (start grid): build from yaw
    float c = float(std::cos(s.yaw)), sn = float(std::sin(s.yaw));
    const float y[9] = {c, 0, sn, 0, 1, 0, -sn, 0, c};
    std::copy(y, y + 9, R);
    return;
  }
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) R[i * 3 + j] = float(s.m[j * 3 + i]);
}

// RaceSlotHover 0x51EC4: speed -> world velocity (forward * speed * factor + slide velocity).
static void shipVelocity(const ShipState& s, double* v) {
  const double* m = s.m;
  const double fy = std::clamp(m[7], -0.25, 0.25);
  const double factor = -fy / 8.0 + (0x200 - std::fabs(m[1]) * 0x4000 / 32.0) / 16384.0 + 0x1000 / 16384.0 + 0x2c00 / 16384.0;
  const double eff = s.speed * factor;
  for (int i = 0; i < 3; ++i) v[i] = m[6 + i] * eff + s.slide[i];
}

void stepShipDynamics(ShipState& s, const ShipInput& in, double dt, const ShipParams& p, double* vel) {
  if (!s.matrixInit) {
    double c = std::cos(s.yaw), sn = std::sin(s.yaw);
    const double m[9] = {c, 0, -sn, 0, 1, 0, sn, 0, c};
    std::copy(m, m + 9, s.m);
    s.matrixInit = true;
  }
  // ---- speed (RaceSlotMove 0x51BB6..0x51D66) ----
  double top = double(p.topSpeed);
  double a;
  if (in.throttle > 0.01f) {
    double ratio = std::clamp(s.speed / top, 0.0, 1.0);
    a = (double(p.thrustAtRest) - double(p.thrustAtRest - p.thrustAtTop) * ratio) * in.throttle;
  } else {
    a = -double(p.coastDecel);
  }
  a -= double(p.coastDecel) * 2.0 * in.brake;  // PLACEHOLDER brake strength
  s.speed = std::clamp(s.speed + a * dt, 0.0, top);

  // ---- orientation (0x51D72..0x51E59) ----
  // Keyboard steering ramp (sub_59A05): the axis moves 4.0/s towards the pressed side (reversing zeroes it first)
  // and back to centre at the same rate when released.
  {
    double want = std::clamp(double(in.steer), -1.0, 1.0), step = 4.0 * dt;
    if (std::fabs(want) < 0.01) {
      s.steerAxis = s.steerAxis > 0 ? std::max(0.0, s.steerAxis - step) : std::min(0.0, s.steerAxis + step);
    } else {
      if (s.steerAxis * want < 0) s.steerAxis = 0;
      s.steerAxis = std::clamp(s.steerAxis + (want > 0 ? step : -step) * std::fabs(want), -1.0, 1.0);
    }
  }
  const double steer16 = s.steerAxis * 0x4000;
  double* m = s.m;
  double roll = std::atan2(-m[1], m[4]) / kTau;  // Atan2Matrix on the matrix snapshot
  double target = std::clamp(steer16 / 2, -double(0x7f00), double(0x7f00) - 0.0) / 65536.0;
  double rollDelta = (target - roll) * dt * 4.0;  // (target-roll)*dt(2.14)>>12
  const double dtq = std::floor(dt * 16384.0);  // GetFrameDeltaSecs: 2.14 seconds
  // hi16(dtq * input) for the steering input and for the bank term -m[1] (snapshot, 2.14)
  double yawTerm = (std::floor(dtq * steer16 / 65536.0) + std::floor(dtq * std::round(-m[1] * 16384.0) / 65536.0)) / 65536.0;
  {
    double want = std::clamp(double(in.pitch), -1.0, 1.0), step = 4.0 * dt;
    if (std::fabs(want) < 0.01) s.pitchAxis = s.pitchAxis > 0 ? std::max(0.0, s.pitchAxis - step) : std::min(0.0, s.pitchAxis + step);
    else {
      if (s.pitchAxis * want < 0) s.pitchAxis = 0;
      s.pitchAxis = std::clamp(s.pitchAxis + (want > 0 ? step : -step) * std::fabs(want), -1.0, 1.0);
    }
  }
  // DX input +0x4000 = nose up (emulator: forward.y = m[7] rises, tests/physics_tests.cpp)
  double pitchTerm = std::floor(dtq * (s.pitchAxis * 0x4000) / 65536.0) / 65536.0;
  double yawGain = double(p.f5) / 16384.0;
  double pitchGain = double(p.f4) / 16384.0;
  double ang_pitch = pitchTerm * pitchGain;
  if (m[7] > 0.8125 && ang_pitch >= 0) ang_pitch = 0;
  if (m[7] < -0.8125 && ang_pitch < 0) ang_pitch = 0;
  if (ang_pitch != 0) rot(m, ang_pitch, kPitchPairs, -1, 1);
  if (rollDelta != 0) rot(m, rollDelta, kRollPairs, -1, 1);
  if (yawTerm * yawGain != 0) rot(m, yawTerm * yawGain, kYawPairs, 1, -1);
  orthonormalize(m);

  // ---- drag on the slide velocity (0x51E5E) ----
  for (int i = 0; i < 3; ++i) s.slide[i] -= s.slide[i] * 4.0 * dt;

  shipVelocity(s, vel);
  const double vx = vel[0], vy = vel[1], vz = vel[2];
  (void)vx; (void)vy; (void)vz;
  s.yaw = std::atan2(m[6], m[8]);
  s.roll = std::atan2(-m[1], m[4]) / kTau;
  s.pitch = std::asin(std::clamp(m[7], -1.0, 1.0));
}

// Ship wall-hit message 0x107 handler (RaceSlotControl 0x50A64..0x50B77, CONFIRMED by reading the code; the bounce
// direction CONFIRMED against the emulator: 0x3BD82 = cos(0x3000)*heading_tangent + sin(0x3000)*normal):
//   speed *= 0.75;  slide += unit(...) * speed;  then RaceSlotHover recomputes the velocity.
// Not ported: effects/sound, RaceSlotDamage(0x20000, 0x10000) and the "second hit while [+0x12] != 0" explosion branch.
void shipHitResponse(ShipState& s, const double n[3], const double heading[3]) {
  const double ang = 0x3000 / 65536.0 * kTau;
  const double hn = heading[0] * n[0] + heading[1] * n[1] + heading[2] * n[2];
  double tg[3] = {heading[0] - n[0] * hn, heading[1] - n[1] * hn, heading[2] - n[2] * hn};
  const double tl = std::sqrt(tg[0] * tg[0] + tg[1] * tg[1] + tg[2] * tg[2]);
  double u[3];
  if (tl < 1e-4) {
    for (int i = 0; i < 3; ++i) u[i] = n[i];  // heading parallel to the normal (0x3BDE3)
  } else {
    for (int i = 0; i < 3; ++i) u[i] = std::cos(ang) * tg[i] / tl + std::sin(ang) * n[i];
  }
  s.speed *= 0.75;
  for (int i = 0; i < 3; ++i) s.slide[i] += u[i] * s.speed;
}

void setShipBoxFromMesh(ShipState& s, const Mesh& mesh) {
  if (mesh.verts.empty()) return;
  double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
  for (const Vec3& v : mesh.verts) {
    const double c[3] = {v.x, v.y, v.z};
    for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], c[i]); hi[i] = std::max(hi[i], c[i]); }
  }
  const double ax = std::max(std::fabs(lo[0]), std::fabs(hi[0]));
  s.boxLo[0] = -ax; s.boxHi[0] = ax;
  for (int i = 1; i < 3; ++i) { s.boxLo[i] = lo[i]; s.boxHi[i] = hi[i]; }
  s.hasBox = true;
}

void stepShip(ShipState& s, const ShipInput& in, double dt, const ShipParams& p, const Scene& scene, const ShipSimConfig& cfg) {
  double v[3];
  stepShipDynamics(s, in, dt, p, v);
  const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * dt;
  if (s.hasBox && !cfg.assist) {
    if (len <= 0) return;
    // CollideStep (0x137A2): sweep, stop at the contact margin, send the hit message (velocity changes), then carry on
    // with the remaining time and the new velocity (up to 0x20 sub-steps in the original).
    double pos[3] = {s.x, s.y, s.z};
    const bool insideNow = shipBoxInsideTrack(scene, pos, s.m, s.boxLo, s.boxHi);
    double np[3] = {pos[0], pos[1], pos[2]};
    double tr = dt;
    s.hitCooldown = std::max(0.0, s.hitCooldown - dt);
    for (int iter = 0; iter < 6 && tr > 1e-9; ++iter) {
      double vv[3];
      shipVelocity(s, vv);
      const double speedNow = std::sqrt(vv[0] * vv[0] + vv[1] * vv[1] + vv[2] * vv[2]);
      const double l = speedNow * tr;
      if (l < 1e-6) break;
      const double dir[3] = {vv[0] / speedNow, vv[1] / speedNow, vv[2] / speedNow};
      ShipHit h = sweepShipBox(scene, np, s.m, s.boxLo, s.boxHi, dir, l);
      for (int i = 0; i < 3; ++i) np[i] += dir[i] * h.dist;
      if (!h.hit) break;
      ++s.hits;
      tr *= 1.0 - h.dist / l;
      // one message per original frame (~60/s): the original sends it once per CollideStep, not per sub-step
      if (s.hitCooldown <= 0) {
        shipHitResponse(s, h.n, dir);
        s.hitCooldown = 1.0 / 60.0;
      } else {
        tr = 0;  // blocked: remaining motion is lost this step
      }
    }
    if (insideNow && !shipBoxInsideTrack(scene, np, s.m, s.boxLo, s.boxHi)) {
      s.speed *= 0.5;  // PLACEHOLDER: the box would leave the piece graph, stay put
      ++s.hits;
      return;
    }
    s.x = np[0]; s.y = np[1]; s.z = np[2];
    return;
  }
  double nx = s.x + v[0] * dt;
  double nz = s.z + v[2] * dt;
  double floorY;
  if (scene.floorHeight(nx, nz, s.y + cfg.hoverHeight * 6, 0, &floorY)) {
    s.x = nx;
    s.z = nz;
    double target_y = floorY + cfg.hoverHeight;
    s.y += (target_y - s.y) * std::min(1.0, 12.0 * dt);  // PLACEHOLDER until collision/hover module is ported
  } else {
    // No ground ahead: the edge of the track acts as a wall (PLACEHOLDER, not original collision).
    s.speed *= 0.35;
  }
}

}  // namespace slip
