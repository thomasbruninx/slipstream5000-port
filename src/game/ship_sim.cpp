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
  if (s.wrecked) {  // wreck: slot speed along the flight direction
    const double sp = s.wreckLanded ? 57344.0 : s.wreckSpeed;  // [+0xF0] = 0xE000 on the ground
    for (int i = 0; i < 3; ++i) v[i] = s.wreckHead[i] * sp;
    return;
  }
  const double* m = s.m;
  const double fy = std::clamp(m[7], -0.25, 0.25);
  const double factor = -fy / 8.0 + (0x200 - std::fabs(m[1]) * 0x4000 / 32.0) / 16384.0 + (0x1000 - 40.0 * s.damageA) / 16384.0 + 0x2c00 / 16384.0;  // RaceSlotHover: 0x1000 - 0x28*[rec+0x2A]>>16
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
  // per-frame timers (RaceSlotControl 0x104: ship data +0x12 and +0x24 count down in ms)
  s.recentHit = std::max(0.0, s.recentHit - dt);
  s.invuln = std::max(0.0, s.invuln - dt);
  s.boostTime = std::max(0.0, s.boostTime - dt);
  s.slowTime = std::max(0.0, s.slowTime - dt);
  s.reverseTime = std::max(0.0, s.reverseTime - dt);
  s.halfCapTime = std::max(0.0, s.halfCapTime - dt);
  s.forceThrottleTime = std::max(0.0, s.forceThrottleTime - dt);
  s.hyperTime = std::max(0.0, s.hyperTime - dt);
  s.boosterFreeTime = std::max(0.0, s.boosterFreeTime - dt);
  if (s.wrecked) {  // dead-ship handler 0x51293 -> 0x3E9F5 (message 0x104, 0x3EB28)
    s.wreckTime += dt;
    s.steerAxis = s.pitchAxis = 0;
    // heading steers towards the aim point at 2/s (0x3EB5D..0x3EB7F)
    if (s.wreckAimValid) {
      double d[3] = {s.wreckAim[0] - s.x, s.wreckAim[1] - s.y, s.wreckAim[2] - s.z};
      const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (l > 1) {
        const double k = std::min(1.0, 2.0 * dt);
        for (int i = 0; i < 3; ++i) s.wreckHead[i] += (d[i] / l - s.wreckHead[i]) * k;
        const double hl = std::sqrt(s.wreckHead[0] * s.wreckHead[0] + s.wreckHead[1] * s.wreckHead[1] + s.wreckHead[2] * s.wreckHead[2]);
        if (hl > 0) for (double& x : s.wreckHead) x /= hl;
      }
    }
    if (!s.wreckLanded) {
      // tumble: spin = 0xE000 units/s (0.875 turn/s) of yaw, twice that of pitch, random directions (0x3EBC2..0x3EC95)
      const double spin = 57344.0 / 65536.0 * dt;
      rot(s.m, s.wreckSpinPitch * 2 * spin, kPitchPairs, -1, 1);
      rot(s.m, s.wreckSpinYaw * spin, kYawPairs, 1, -1);
      orthonormalize(s.m);
      s.wreckTimer -= dt;
      if (s.wreckTimer <= 0) s.wreckLanded = true;  // mode |= 2 (0x3ECB8)
    } else {
      // landed: the orientation settles onto the travel direction (0x3ED5C..0x3EDE4, pitch limited to +-0x3000)
      double f[3] = {s.wreckHead[0], std::clamp(s.wreckHead[1], -0.7071, 0.7071), s.wreckHead[2]};
      const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
      if (fl > 1e-6) {
        const double k = std::min(1.0, 4.0 * dt);
        for (int i = 0; i < 3; ++i) s.m[6 + i] += (f[i] / fl - s.m[6 + i]) * k;
        orthonormalize(s.m);
      }
    }
    shipVelocity(s, vel);
    return;
  }
  // ---- speed (RaceSlotMove 0x51BB6..0x51D66) ----
  double top = double(p.topSpeed);
  double fac = s.speedFactor + (s.slowTime > 0 ? -0.25 : s.boostTime > 0 ? 0.5 : 0.0);  // 0x51CA6..0x51CC2
  fac += s.startBonus;
  if (s.boosterFreeTime > 0 || s.boosterOn) fac += s.boosterGain;  // 0x51CDC..0x51CFD: booster item factor - 1
  const bool forced = s.forceThrottleTime > 0;  // 0x51B1D: control flag bit 0 (throttle) forced on
  double a;
  // An analog throttle (a trigger; the original only knew on / off) also sets the speed it settles at: top speed x the pull. Above that speed the ship coasts down.
  const bool partial = !forced && in.throttle > 0.01f && in.throttle < 0.999f && s.speed > top * fac * double(in.throttle);
  if (partial) {
    a = -double(p.coastDecel);
  } else if (in.throttle > 0.01f || forced) {
    double ratio = std::clamp(s.speed / top, 0.0, 1.0);
    a = (double(p.thrustAtRest) - double(p.thrustAtRest - p.thrustAtTop) * ratio) * (forced ? 1.0 : in.throttle);
  } else {
    a = -double(p.coastDecel);
  }
  a -= double(p.coastDecel) * 2.0 * in.brake;  // PLACEHOLDER brake strength
  a *= fac;  // 0x51D0D: thrust * factor
  s.speed = std::clamp(s.speed + a * dt, 0.0, top * fac * (s.halfCapTime > 0 ? 0.5 : 1.0));  // speed cap f3 * factor (0x51D36), halved by +0x28 (0x51D55)

  // ---- orientation (0x51D72..0x51E59) ----
  // Keyboard steering ramp (sub_59A05): the axis moves 4.0/s towards the pressed side (reversing zeroes it first)
  // and back to centre at the same rate when released.
  if (in.direct) {
    s.steerAxis = std::clamp(double(in.steer), -1.0, 1.0);
    s.pitchAxis = std::clamp(double(in.pitch), -1.0, 1.0);
  } else {
    double want = std::clamp(double(in.steer), -1.0, 1.0), step = 4.0 * dt;
    if (std::fabs(want) < 0.01) {
      s.steerAxis = s.steerAxis > 0 ? std::max(0.0, s.steerAxis - step) : std::min(0.0, s.steerAxis + step);
    } else {
      if (s.steerAxis * want < 0) s.steerAxis = 0;
      s.steerAxis = std::clamp(s.steerAxis + (want > 0 ? step : -step) * std::fabs(want), -1.0, 1.0);
    }
  }
  // Status effects on the control axes (0x51B10..0x51B75): reversed (+0x26), then hypersensitive (+0x2C or the 0.25 s jolt
  // +0x40 = slowTime): axis * 16 clamped to +-1. The pitch axis gets the same treatment below.
  auto axisFx = [&](double a) {
    if (s.reverseTime > 0) a = -a;
    if (s.hyperTime > 0 || s.slowTime > 0) a = std::clamp(a * 16.0, -1.0, 1.0);
    return a;
  };
  const double steer16 = axisFx(s.steerAxis) * 0x4000;
  double* m = s.m;
  double roll = std::atan2(-m[1], m[4]) / kTau;  // Atan2Matrix on the matrix snapshot
  double target = std::clamp(steer16 / 2, -double(0x7f00), double(0x7f00) - 0.0) / 65536.0;
  double rollDelta = (target - roll) * dt * 4.0;  // (target-roll)*dt(2.14)>>12
  const double dtq = std::floor(dt * 16384.0);  // GetFrameDeltaSecs: 2.14 seconds
  // hi16(dtq * input) for the steering input and for the bank term -m[1] (snapshot, 2.14)
  double yawTerm = (std::floor(dtq * steer16 / 65536.0) + std::floor(dtq * std::round(-m[1] * 16384.0) / 65536.0)) / 65536.0;
  if (!in.direct) {
    double want = std::clamp(double(in.pitch), -1.0, 1.0), step = 4.0 * dt;
    if (std::fabs(want) < 0.01) s.pitchAxis = s.pitchAxis > 0 ? std::max(0.0, s.pitchAxis - step) : std::min(0.0, s.pitchAxis + step);
    else {
      if (s.pitchAxis * want < 0) s.pitchAxis = 0;
      s.pitchAxis = std::clamp(s.pitchAxis + (want > 0 ? step : -step) * std::fabs(want), -1.0, 1.0);
    }
  }
  // DX input +0x4000 = nose up (emulator: forward.y = m[7] rises, tests/physics_tests.cpp)
  double pitchTerm = std::floor(dtq * (axisFx(s.pitchAxis) * 0x4000) / 65536.0) / 65536.0;
  // steering damage: gains lose 0x51*[rec+0x2E]>>16 (0x51 = 81 per damage point), RaceSlotMove 0x51E0D
  const double dmgGain = 81.0 * s.damageB / 16384.0;
  double yawGain = double(p.f5) / 16384.0 - dmgGain;
  double pitchGain = double(p.f4) / 16384.0 - dmgGain;
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
void shipDamage(ShipState& s, double a, double b) {  // RaceSlotDamage 0x52035 (units: 1.0 = 0x10000)
  if (s.invuln > 0) { a = b = 0; }
  else if (a != 0 || b != 0) s.invuln = 3.0;  // [slot+0x24] = 0xBB8 ms
  const double na = std::max(0.0, s.damageA + a), nb = std::max(0.0, s.damageB + b);
  if (na > 100.0 || nb > 100.0) { ++s.sfxOverDamage; return; }  // over the limit: only effects, nothing is stored (0x520F8..0x52189)
  s.damageA = na;
  s.damageB = nb;
}

void shipHitResponse(ShipState& s, const double n[3], const double heading[3]) {
  if (s.wrecked) {  // wall hit while wrecked (0x3EA19)
    if (s.wreckLanded) { s.wreckRecover = true; return; }
    const double hn = heading[0] * n[0] + heading[1] * n[1] + heading[2] * n[2];
    if (std::fabs(hn) > 0x100 / 16384.0) {  // not grazing: speed/2 (>= 0x1174C), bounce 22.5 deg off the surface (0x3EAF8)
      s.wreckSpeed = std::max(71500.0, s.wreckSpeed * 0.5);
      const double ang = 0x1000 / 65536.0 * kTau;
      double tg[3] = {heading[0] - n[0] * hn, heading[1] - n[1] * hn, heading[2] - n[2] * hn};
      const double tl = std::sqrt(tg[0] * tg[0] + tg[1] * tg[1] + tg[2] * tg[2]);
      for (int i = 0; i < 3; ++i) s.wreckHead[i] = tl < 1e-4 ? n[i] : std::cos(ang) * tg[i] / tl + std::sin(ang) * n[i];
    }
    return;
  }
  if (s.recentHit > 0) {  // second wall hit within 0x190 ms: the ship is wrecked (0x50AE0..0x50B30, 0x3E8F2)
    s.speed *= 0.75;
    s.savedSpeed = s.speed;
    s.wrecked = true;
    ++s.sfxWreck;
    s.wreckTime = 0;
    s.wreckLanded = false;
    s.wreckRecover = false;
    s.wreckTimer = 1.0;
    s.wreckSpeed = std::max(71500.0, s.speed * 0.5);
    for (int i = 0; i < 3; ++i) s.wreckHead[i] = heading[i];
    s.wreckAimValid = false;
    s.wreckNode = -1;
    s.wreckSpinYaw = ((s.hits * 7 + int(s.x)) & 1) ? 1 : -1;
    s.wreckSpinPitch = ((s.hits * 5 + int(s.z)) & 1) ? 1 : -1;
    s.slide[0] = s.slide[1] = s.slide[2] = 0;
    s.recentHit = 0;
    return;
  }
  s.recentHit = 0.4;
  {  // 0x50A82..0x50A9B: slot speed above 0x22E98 -> effect 2 (hard), else 3
    double vv[3];
    shipVelocity(s, vv);
    if (s.hitWater) ++s.sfxWater;  // 0x50AA2..0x50AB6: material name starts with WATE -> effect 8
    else (std::sqrt(vv[0] * vv[0] + vv[1] * vv[1] + vv[2] * vv[2]) > 0x22e98 ? s.sfxWallHard : s.sfxWallLight)++;
  }
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
  shipDamage(s, 2.0, 1.0);
}

void setShipBoxFromMesh(ShipState& s, const Mesh& mesh, double unscale) {
  if (mesh.verts.empty()) return;
  double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
  for (const Vec3& v : mesh.verts) {
    const double c[3] = {v.x, v.y, v.z};
    for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], c[i]); hi[i] = std::max(hi[i], c[i]); }
  }
  const double ax = std::max(std::fabs(lo[0]), std::fabs(hi[0]));
  s.boxLo[0] = -ax; s.boxHi[0] = ax;
  for (int i = 1; i < 3; ++i) { s.boxLo[i] = lo[i]; s.boxHi[i] = hi[i]; }
  for (int i = 0; i < 3; ++i) { s.boxLo[i] *= unscale; s.boxHi[i] *= unscale; }
  double ext = 0;
  for (int k = 0; k < 8; ++k) {
    const double x = (k & 1) ? s.boxHi[0] : s.boxLo[0], y = (k & 2) ? s.boxHi[1] : s.boxLo[1], z = (k & 4) ? s.boxHi[2] : s.boxLo[2];
    ext = std::max(ext, std::sqrt(x * x + y * y + z * z));
  }
  s.extent = ext;
  s.hasBox = true;
}

void moveShip(ShipState& s, double dt, const Scene& scene, const ShipSimConfig& cfg) {
  double v[3];
  shipVelocity(s, v);
  const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * dt;
  if (s.hasBox && !cfg.assist) {
    if (len <= 0) return;
    // CollideStep (0x137A2): sweep, stop at the contact margin, send the hit message (velocity changes), then carry on
    // with the remaining time and the new velocity (up to 0x20 sub-steps in the original).
    double pos[3] = {s.x, s.y, s.z};
    const bool insideNow = shipBoxInsideTrack(scene, pos, s.m, s.boxLo, s.boxHi);
    double np[3] = {pos[0], pos[1], pos[2]};
    double tr = dt;
    s.pairCooldown = std::max(0.0, s.pairCooldown - dt);
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
      s.hitWater = h.material >= 0 && size_t(h.material) < scene.materials.size() && scene.materials[size_t(h.material)].upperName.rfind("WATE", 0) == 0;
      if (s.nWallFx < 4) {  // RaceSlotControl 0x50A64: RaceBang 0x4FD93 / 0x4FF2E at the contact point
        ShipState::WallFx& w = s.wallFx[s.nWallFx++];
        for (int i = 0; i < 3; ++i) w.n[i] = h.n[i];
        const double sup = std::fabs(h.n[0] * s.m[0] + h.n[1] * s.m[1] + h.n[2] * s.m[2]) * std::max(std::fabs(s.boxLo[0]), std::fabs(s.boxHi[0])) +
                           std::fabs(h.n[0] * s.m[3] + h.n[1] * s.m[4] + h.n[2] * s.m[5]) * std::max(std::fabs(s.boxLo[1]), std::fabs(s.boxHi[1])) +
                           std::fabs(h.n[0] * s.m[6] + h.n[1] * s.m[7] + h.n[2] * s.m[8]) * std::max(std::fabs(s.boxLo[2]), std::fabs(s.boxHi[2]));
        for (int i = 0; i < 3; ++i) w.pos[i] = np[i] - h.n[i] * sup;  // the point of the box nearest to the wall
        w.material = h.material;
        w.water = s.hitWater;
        w.speed = speedNow;
      }
      shipHitResponse(s, h.n, dir);  // message 0x107 on every contact event, as in CollideStep
    }
    if (insideNow && !shipBoxInsideTrack(scene, np, s.m, s.boxLo, s.boxHi)) {
      // 0x38C97: the static check failed at the end of the move -> bisect (15 halvings) back to the last valid position
      double lo = 0, hi = 1;
      const double d[3] = {np[0] - pos[0], np[1] - pos[1], np[2] - pos[2]};
      for (int it = 0; it < 15; ++it) {
        const double mid = (lo + hi) * 0.5;
        const double q[3] = {pos[0] + d[0] * mid, pos[1] + d[1] * mid, pos[2] + d[2] * mid};
        if (shipBoxInsideTrack(scene, q, s.m, s.boxLo, s.boxHi)) lo = mid; else hi = mid;
      }
      for (int i = 0; i < 3; ++i) np[i] = pos[i] + d[i] * lo;
      s.speed *= 0.5;  // PLACEHOLDER response: the original sends a wall-hit message for the polygon at that spot
      ++s.hits;
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

void stepShip(ShipState& s, const ShipInput& in, double dt, const ShipParams& p, const Scene& scene, const ShipSimConfig& cfg) {
  double v[3];
  stepShipDynamics(s, in, dt, p, v);
  moveShip(s, dt, scene, cfg);
}

namespace {
// Separating-axis test of two oriented boxes (model-space lo/hi, rows of M = right/up/forward in world).
bool obbOverlap(const double pa[3], const double* Ma, const double* loA, const double* hiA, const double pb[3], const double* Mb,
                const double* loB, const double* hiB) {
  double ca[3], cb[3], ha[3], hb[3];
  for (int i = 0; i < 3; ++i) {
    const double la = (loA[0] + hiA[0]) * 0.5, ua = (loA[1] + hiA[1]) * 0.5, wa = (loA[2] + hiA[2]) * 0.5;
    const double lb = (loB[0] + hiB[0]) * 0.5, ub = (loB[1] + hiB[1]) * 0.5, wb = (loB[2] + hiB[2]) * 0.5;
    ca[i] = pa[i] + Ma[i] * la + Ma[3 + i] * ua + Ma[6 + i] * wa;
    cb[i] = pb[i] + Mb[i] * lb + Mb[3 + i] * ub + Mb[6 + i] * wb;
    ha[i] = (hiA[i] - loA[i]) * 0.5;
    hb[i] = (hiB[i] - loB[i]) * 0.5;
  }
  const double d[3] = {cb[0] - ca[0], cb[1] - ca[1], cb[2] - ca[2]};
  auto test = [&](const double ax[3]) {
    const double l = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
    if (l < 1e-9) return true;  // degenerate axis: cannot separate
    double ra = 0, rb = 0, dd = 0;
    for (int k = 0; k < 3; ++k) {
      ra += ha[k] * std::fabs(Ma[3 * k] * ax[0] + Ma[3 * k + 1] * ax[1] + Ma[3 * k + 2] * ax[2]);
      rb += hb[k] * std::fabs(Mb[3 * k] * ax[0] + Mb[3 * k + 1] * ax[1] + Mb[3 * k + 2] * ax[2]);
    }
    dd = std::fabs(d[0] * ax[0] + d[1] * ax[1] + d[2] * ax[2]);
    return dd <= ra + rb;  // ra, rb, dd all scale with |ax|
  };
  for (int k = 0; k < 3; ++k) {
    if (!test(Ma + 3 * k) || !test(Mb + 3 * k)) return false;
    for (int j = 0; j < 3; ++j) {
      const double* a = Ma + 3 * k;
      const double* b = Mb + 3 * j;
      const double c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
      if (!test(c)) return false;
    }
  }
  return true;
}

// First fraction f in [0,1] at which the boxes overlap when A travels dA and B dB (-1 = never).
double firstOverlap(const ShipState& a, const ShipState& b, const double dA[3], const double dB[3]) {
  constexpr int kSteps = 256;
  auto at = [&](double f) {
    const double pa[3] = {a.x + dA[0] * f, a.y + dA[1] * f, a.z + dA[2] * f};
    const double pb[3] = {b.x + dB[0] * f, b.y + dB[1] * f, b.z + dB[2] * f};
    return obbOverlap(pa, a.m, a.boxLo, a.boxHi, pb, b.m, b.boxLo, b.boxHi);
  };
  if (at(0)) return -1;  // boxes already overlapping: the original reports no collision (oracle, tools/re/ss_emu.py)
  double prev = 0;
  for (int k = 1; k <= kSteps; ++k) {
    const double f = double(k) / kSteps;
    if (at(f)) {
      double lo = prev, hi = f;
      for (int it = 0; it < 12; ++it) { const double mid = (lo + hi) * 0.5; if (at(mid)) hi = mid; else lo = mid; }
      return hi;
    }
    prev = f;
  }
  return -1;
}
}  // namespace

bool shipBoxesOverlap(const ShipState& a, const ShipState& b) {
  const double pa[3] = {a.x, a.y, a.z}, pb[3] = {b.x, b.y, b.z};
  return obbOverlap(pa, a.m, a.boxLo, a.boxHi, pb, b.m, b.boxLo, b.boxHi);
}

double shipPairTimeOfImpact(const ShipState& a, const ShipState& b, const double dA[3], const double dB[3]) { return firstOverlap(a, b, dA, dB); }

// Slot-vs-slot collision (CollideStep -> 0x15A46 -> 0x14620 -> 0x149A1/0x14A16, message 0x106 -> RaceSlotControl 0x509E6):
// the contact normal is NOT geometric: B gets unit(vA - vB), A the opposite, and the relative speed |vA - vB| rides along.
// Each ship's handler (CONFIRMED by reading the code): slide += normal * max(1.5*rel, 0x37DC); a ship flagged as the
// rammer (`1656B`: the collision also happens with the other ship standing still) loses speed (*0.625).
// The narrow phase is a plain SAT test on the ART boxes instead of the original's contact generator (0x16454...).
void shipPairResponse(ShipState& a, ShipState& b, bool aRams, bool bRams) {
  ++a.sfxContact;
  ++b.sfxContact;
  for (ShipState* q : {&a, &b})  // 0x509A6: the pilot's contact line only plays when the high word of slide x (slot data +2) is 2
    if (((long long)(q->slide[0]) >> 16 & 0xFFFF) == 2) ++q->cueContact;
  if (a.wrecked) a.wreckRecover = true;  // message 0x106 in the debris handler: speed 0, handler restored (0x3EB19)
  if (b.wrecked) b.wreckRecover = true;
  double va[3], vb[3];
  shipVelocity(a, va);
  shipVelocity(b, vb);
  const double rel[3] = {va[0] - vb[0], va[1] - vb[1], va[2] - vb[2]};
  const double rl = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
  if (rl < 1e-6) return;
  const double mag = std::max(1.5 * rl, double(0x37dc));
  for (int i = 0; i < 3; ++i) {
    b.slide[i] += rel[i] / rl * mag;
    a.slide[i] -= rel[i] / rl * mag;
  }
  if (aRams) a.speed *= 0.625;
  if (bRams) b.speed *= 0.625;
  shipDamage(a, aRams ? 2.0 : 0.0, aRams ? 6.0 : 4.0);  // RaceSlotDamage(0 [+0x20000], 0x40000 [+0x20000]) 0x50A20..0x50A50
  shipDamage(b, bRams ? 2.0 : 0.0, bRams ? 6.0 : 4.0);
}

void resolveShipPairs(std::vector<ShipState*>& ships, const std::vector<std::array<double, 3>>& startPos, double dt, const Scene& scene,
                      const ShipSimConfig& cfg) {
  // CollideStep (0x137A2) loop: find the earliest slot-slot contact of the remaining time, advance every slot to it, send
  // the messages, then continue with the remaining time and the new velocities (0x1446B / jump back to 0x137CA).
  std::vector<std::array<double, 3>> start = startPos;
  double remaining = dt;
  for (int pass = 0; pass < 4 && remaining > 1e-9; ++pass) {
    double bestF = 2;
    size_t bi = 0, bj = 0;
    bool aRams = false, bRams = false;
    for (size_t i = 0; i < ships.size(); ++i)
      for (size_t j = i + 1; j < ships.size(); ++j) {
        ShipState& a = *ships[i];
        ShipState& b = *ships[j];
        if (!a.hasBox || !b.hasBox || a.pairCooldown > 0 || b.pairCooldown > 0) continue;
        const double dA[3] = {a.x - start[i][0], a.y - start[i][1], a.z - start[i][2]};
        const double dB[3] = {b.x - start[j][0], b.y - start[j][1], b.z - start[j][2]};
        ShipState a0 = a, b0 = b;
        a0.x = start[i][0]; a0.y = start[i][1]; a0.z = start[i][2];
        b0.x = start[j][0]; b0.y = start[j][1]; b0.z = start[j][2];
        const double f = firstOverlap(a0, b0, dA, dB);
        if (f < 0 || f >= bestF) continue;
        const double zero[3] = {0, 0, 0};
        bestF = f; bi = i; bj = j;
        aRams = firstOverlap(a0, b0, dA, zero) >= 0;  // 0x1656B: A alone reaches B
        bRams = firstOverlap(a0, b0, zero, dB) >= 0;
        if (!aRams && !bRams) aRams = bRams = true;
      }
    if (bestF > 1) break;
    // advance every ship to the contact instant
    for (size_t k = 0; k < ships.size(); ++k) {
      ShipState& s = *ships[k];
      s.x = start[k][0] + (s.x - start[k][0]) * bestF;
      s.y = start[k][1] + (s.y - start[k][1]) * bestF;
      s.z = start[k][2] + (s.z - start[k][2]) * bestF;
      start[k] = {s.x, s.y, s.z};
    }
    shipPairResponse(*ships[bi], *ships[bj], aRams, bRams);
    ships[bi]->pairCooldown = ships[bj]->pairCooldown = 1.0 / 60.0;
    ++ships[bi]->hits; ++ships[bj]->hits;
    remaining *= 1.0 - bestF;
    // carry on with the remaining time and the new velocities
    for (ShipState* s : ships)
      if (s->hasBox) moveShip(*s, remaining, scene, cfg);
  }
}

}  // namespace slip
