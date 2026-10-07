// Ship dynamics vs. the x86 emulator oracle (tools/re/phys_run2.py: RaceSlotMove with full throttle and full right steer,
// 61 frames of dt = 262/16384 s, ship parameters of ship 1). Needs no game data.
#include <cmath>
#include <cstdio>

#include "game/doors.hpp"
#include "game/ship_ai.hpp"
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
  {  // wreck (0x3E8F2/0x3E9F5): flies along the heading at max(speed*0.75/2, 0x1174C), tumbles, lands after 1 s without an aim point
    ShipState w;
    const double n[3] = {0, 0, -1}, hd[3] = {0, 0, 1};
    w.speed = 400000;
    w.recentHit = 0.3;
    shipHitResponse(w, n, hd);
    NEAR(w.wrecked ? 1 : 0, 1, 0);
    NEAR(w.savedSpeed, 300000, 1e-6);
    NEAR(w.wreckSpeed, 150000, 1e-6);
    double vw[3];
    for (int f = 0; f < 70; ++f) stepShipDynamics(w, ShipInput{}, 1.0 / 60, p, vw);  // 1.17 s: landed
    NEAR(w.wreckLanded ? 1 : 0, 1, 0);
    NEAR(std::sqrt(vw[0] * vw[0] + vw[1] * vw[1] + vw[2] * vw[2]), 57344, 1);  // landed speed [+0xF0] = 0xE000
    // factor: tier table row (oracle: AI factor multiplies thrust and cap)
    ShipState q;
    q.speedFactor = 0.5;
    double vq[3];
    for (int f = 0; f < 600; ++f) stepShipDynamics(q, ShipInput{1, 0, 0}, 1.0 / 60, p, vq);
    NEAR(q.speed, p.topSpeed * 0.5, 1);
  }
  {  // lap line (0x5A56A): B -> A counts, the first crossing only starts the race, a backwards crossing is cancelled out
    AiState a;
    const int A = 5, B = 4;
    NEAR(int(lapCrossing(a, 3, 4, A, B, 2, 0)), int(LapEvent::None), 0);
    NEAR(int(lapCrossing(a, B, A, A, B, 2, 0)), int(LapEvent::Started), 0);
    NEAR(a.laps, 1, 0);
    a.lapTime = 50;
    NEAR(int(lapCrossing(a, A, B, A, B, 2, 0)), int(LapEvent::None), 0);   // backwards over the line
    NEAR(a.back ? 1 : 0, 1, 0);
    NEAR(int(lapCrossing(a, B, A, A, B, 2, 0)), int(LapEvent::None), 0);   // forward again: not counted
    NEAR(a.laps, 1, 0);
    NEAR(int(lapCrossing(a, B, A, A, B, 2, 0)), int(LapEvent::Lap), 0);
    NEAR(a.laps, 2, 0);
    NEAR(a.bestLap, 50, 1e-9);
    a.lapTime = 40;
    NEAR(int(lapCrossing(a, B, A, A, B, 2, 3)), int(LapEvent::Finished), 0);  // 2 laps done; 3 ships were in before: rank 4
    NEAR(a.finishRank, 4, 0);
    NEAR(a.bestLap, 40, 1e-9);
    NEAR(int(lapCrossing(a, B, A, A, B, 2, 4)), int(LapEvent::None), 0);   // a finished ship is not counted again
  }
  {  // ranks: laps first, then the distance still to go; finished ships keep their finishing rank
    AiState s[4];
    s[0].laps = 2; s[1].laps = 3; s[2].laps = 2; s[3].laps = 3; s[3].finished = true; s[3].finishRank = 1;
    s[2].back = true;
    std::vector<AiState*> v = {&s[0], &s[1], &s[2], &s[3]};
    assignRanks(v, {100, 500, 10, 0});
    NEAR(s[3].rank, 1, 0); NEAR(s[1].rank, 2, 0); NEAR(s[0].rank, 3, 0); NEAR(s[2].rank, 4, 0);  // s2: 2 laps but crossed back -> 1
  }
  {  // door vs ship (0x3C00E / 0x3C17B / 0x3BFDB): a closing door never moves into a ship, it reopens; a touch opens it at 0x6FB8
    Doors ds;
    Door d;
    d.u[0] = 1; d.w[1] = 1; d.n[2] = 1;  // panel in the xy plane, normal z
    d.ha = 10000; d.hb = 10000;
    d.s[1] = 1;                           // slides along +y
    d.closed[1] = 0; d.open[1] = 30000;
    d.pos[1] = 0;
    d.state = 0;                          // opening
    ds.list.push_back(d);
    ShipState ship;
    ship.matrixInit = true; ship.hasBox = true;
    for (int i = 0; i < 3; ++i) { ship.boxLo[i] = -1000; ship.boxHi[i] = 1000; }
    ship.x = 0; ship.y = 20000; ship.z = 0;   // parked where the panel will be when it closes
    std::vector<ShipState*> ships = {&ship};
    ds.list[0].pos[1] = 30000;                // fully open
    ds.list[0].state = -1;                    // closing
    ds.step(1.0, ships);                      // would sweep through the ship: stops short, reopens
    NEAR(ds.list[0].state, 0, 0);
    NEAR(ds.list[0].pos[1] > 30000 - 15000 ? 1 : 0, 1, 0);  // never travelled through the ship
    ds.list[0].state = -1; ds.list[0].speed = 0x37dc; ds.list[0].pos[1] = 30000;
    ship.y = 90000;                           // away: the door closes freely
    ds.step(1.0, ships);
    NEAR(ds.list[0].pos[1], 30000 - 0x37dc, 1);
    ds.touch(0);
    NEAR(ds.list[0].state, 0, 0);
    NEAR(ds.list[0].speed, 0x6fb8, 0);
    ShipState px = ds.proxy(0);
    NEAR(px.slide[1], 0x6fb8, 1e-9);          // the panel moves along +s while opening
    NEAR(px.boxHi[2], 0x7a0, 0);
  }
  NEAR(startBonusForRank(1), 0.75, 1e-9);
  NEAR(startBonusForRank(10), 256 / 16384.0, 1e-9);
  std::printf(failures ? "physics: %d failure(s)\n" : "physics ok\n", failures);
  for (int i = 0; i < 9; ++i) std::printf("%g ", s.m[i] * 16384.0);
  std::printf("\n");
  return failures ? 1 : 0;
}
