// Ship dynamics vs. the x86 emulator oracle (tools/re/phys_run2.py: RaceSlotMove with full throttle and full right steer,
// 61 frames of dt = 262/16384 s, ship parameters of ship 1). Needs no game data.
#include <cmath>
#include <cstdio>

#include "game/doors.hpp"
#include "game/ship_sim.hpp"

using namespace slip;
static int failures = 0;
#define NEAR(v, ref, tol) do { double a_ = (v), r_ = (ref); if (std::fabs(a_ - r_) > (tol)) { std::printf("FAIL %s:%d %s = %g, oracle %g\n", __FILE__, __LINE__, #v, a_, r_); ++failures; } } while (0)

int main() {
  ShipParams p;  // defaults = ship 1
  ShipState s;
  s.steerAxis = 1;  // oracle input is already at full deflection
  double dt = 262.0 / 16384.0, v[3];
  for (int f = 0; f <= 60; ++f) stepShipDynamics(s, ShipInput{1, 0, 1}, dt, p, v);
  NEAR(s.speed, 65756, 700);
  // oracle matrix @ frame 60 (2.14): {9845,-11404,-6438, 9545,11763,-6242, 8967,0,13712}
  const double ref[9] = {9845, -11404, -6438, 9545, 11763, -6242, 8967, 0, 13712};
  for (int i = 0; i < 9; ++i) NEAR(s.m[i] * 16384.0, ref[i], 160);
  NEAR(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 62268, 300);  // slot +0x2c after RaceSlotHover
  {  // full pitch input for 31 frames (oracle: m[4..8] = 15993,-3556, 3556,15993)
    ShipState q;
    q.pitchAxis = 1;
    for (int f = 0; f <= 29; ++f) stepShipDynamics(q, ShipInput{1, 0, 0, 1}, dt, p, v);
    NEAR(q.m[7] * 16384.0, 3556, 120);
    NEAR(q.m[8] * 16384.0, 15993, 120);
  }
  {  // door state machine (0x3BF86): closed -> open at 14300 u/s, flips at the ends, never leaves [closed, open]
    Doors ds;
    Door d;
    d.s[0] = 1;
    d.closed[0] = 0; d.open[0] = 28600;  // 2 s of travel
    ds.list.push_back(d);
    ds.step(1.0);  // flips at once (starts at the closed stop), then moves towards the open stop
    NEAR(ds.list[0].pos[0], 14300, 1);
    ds.step(1.5);  // reaches the open stop after 1 s and turns back for 0.5 s
    NEAR(ds.list[0].pos[0], 28600 - 7150, 1);
    NEAR(ds.list[0].state, -1, 0);
    for (int i = 0; i < 1000; ++i) { ds.step(0.016); NEAR(std::fabs(ds.list[0].pos[0] - 14300), 14300, 14300.5); }
  }
  {  // hit response vs the 0x3BD82 oracle: floor normal, heading (0,-0.25,0.97) -> bounce (0, 15137, 6269)/16384
    ShipState q;
    q.speed = 100000;
    const double n[3] = {0, 1, 0}, h[3] = {0, -0.25 * 1.0, 0.968};
    shipHitResponse(q, n, h);
    NEAR(q.speed, 75000, 1e-6);
    NEAR(q.slide[1], 75000.0 * 15137 / 16384, 150);
    NEAR(q.slide[2], 75000.0 * 6269 / 16384, 150);
  }
  {  // ship-ship message 0x106: B (standing) is pushed along A's relative velocity by 1.5*|rel|, A bounces back, rammer loses 37.5 %
    ShipState a, b;
    a.m[0] = 1; a.m[4] = 1; a.m[8] = 1; b.m[0] = 1; b.m[4] = 1; b.m[8] = 1;
    a.speed = 100000;
    shipPairResponse(a, b, true, false);
    NEAR(b.slide[2], 1.5 * 100000 * 0.9686, 800);  // 0.9686 = hover factor at level flight
    NEAR(a.slide[2], -b.slide[2], 1e-9);
    NEAR(a.speed, 62500, 1e-6);
    NEAR(b.speed, 0, 1e-9);
  }
  {  // damage 50/50 (oracle with rec+0x2A = rec+0x2E = 50<<16): matrix fwd (6854,0,14882), speed 54246
    ShipState q;
    q.steerAxis = 1; q.damageA = 50; q.damageB = 50;
    double vq[3];
    for (int f = 0; f <= 60; ++f) stepShipDynamics(q, ShipInput{1, 0, 1}, dt, p, vq);
    NEAR(q.m[6] * 16384.0, 6854, 250);
    NEAR(q.m[8] * 16384.0, 14882, 250);
    NEAR(std::sqrt(vq[0] * vq[0] + vq[1] * vq[1] + vq[2] * vq[2]), 54246, 700);
    // immunity, wreck on a second wall hit
    ShipState w;
    shipDamage(w, 2, 1);
    NEAR(w.damageA, 2, 1e-9);
    shipDamage(w, 2, 1);
    NEAR(w.damageA, 2, 1e-9);  // immune for 3 s
    const double n[3] = {0, 1, 0}, hd[3] = {0, 0, 1};
    w.speed = 1000;
    shipHitResponse(w, n, hd);
    NEAR(w.wrecked ? 0 : 1, 1, 0);  // first hit: not wrecked
    shipHitResponse(w, n, hd);
    NEAR(w.wrecked ? 1 : 0, 1, 0);  // second within 0.4 s: wrecked
  }
  {  // box-vs-box sweep vs the original 0x14620 (tools/re/ss_emu.py): contact distance along A's 1600-unit move
    auto make = [](double x, double z, double yaw) {
      ShipState s;
      s.x = x; s.z = z;
      const double c = std::cos(yaw), sn = std::sin(yaw);
      const double m[9] = {c, 0, -sn, 0, 1, 0, sn, 0, c};
      std::copy(m, m + 9, s.m);
      s.matrixInit = true;
      const double lo[3] = {-3596, -1055, -5361}, hi[3] = {3596, 1055, 5361};
      for (int i = 0; i < 3; ++i) { s.boxLo[i] = lo[i]; s.boxHi[i] = hi[i]; }
      s.hasBox = true;
      return s;
    };
    const double pi2 = 1.5707963267948966;
    ShipState a = make(0, 0, 0), b = make(0, 11500, 0);
    const double dz[3] = {0, 0, 1600}, zero[3] = {0, 0, 0};
    NEAR(shipPairTimeOfImpact(a, b, dz, zero) * 1600, 778, 8);     // oracle 778
    ShipState a2 = make(0, 0, pi2), b2 = make(9000, 0, 0);
    const double dx[3] = {1600, 0, 0};
    NEAR(shipPairTimeOfImpact(a2, b2, dx, zero) * 1600, 43, 8);    // oracle 43
    ShipState b3 = make(0, 13000, 0);
    NEAR(shipPairTimeOfImpact(a, b3, dz, zero), -1, 0);            // out of reach
  }
  std::printf(failures ? "physics: %d failure(s)\n" : "physics ok\n", failures);
  for (int i = 0; i < 9; ++i) std::printf("%g ", s.m[i] * 16384.0);
  std::printf("\n");
  return failures ? 1 : 0;
}
