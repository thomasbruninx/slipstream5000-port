// Weapons, pickups and status effects (synthetic ships, no game data needed). Numbers come from the original's tables.
#include <cmath>
#include <cstdio>

#include "game/weapons.hpp"

using namespace slip;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
#define NEAR(v, ref, tol) do { double a_ = (v), r_ = (ref); if (std::fabs(a_ - r_) > (tol)) { std::printf("FAIL %s:%d %s = %g, expected %g\n", __FILE__, __LINE__, #v, a_, r_); ++failures; } } while (0)

static ShipState makeShip(int idx, double x, double y, double z) {
  ShipState s;
  s.ship = idx;
  s.x = x; s.y = y; s.z = z;
  s.matrixInit = true;
  const double lo[3] = {-3000, -880, -4472}, hi[3] = {3000, 880, 4472};
  for (int i = 0; i < 3; ++i) { s.boxLo[i] = lo[i]; s.boxHi[i] = hi[i]; }
  s.hasBox = true;
  s.extent = 5500;
  return s;
}

struct Rig {
  WeaponTable table = defaultWeaponTable();
  std::array<ShipRefPoints, 10> refs{};
  CombatWorld w;
  ShipState a = makeShip(0, 0, 0, 0), b = makeShip(1, 0, 0, 200000);
  CombatContext ctx;
  Rig() {
    w.init(table, refs, 1, {}, 7);
    ctx.ships = {&a, &b};
    ctx.human = {true, false};
    ctx.finished = {false, false};
    ctx.shipClass = {1, 2};
    ctx.controls = {CombatControls{}, CombatControls{}};
    ctx.humanShip = 0;
  }
  void run(double seconds, double dt = 1.0 / 120) {
    for (double t = 0; t < seconds; t += dt) w.step(ctx, dt);
  }
};

static void particleTests() {
  ParticleSystem ps;
  ps.reset(5);
  const double pos[3] = {0, 0, 0};
  ps.addEmitter(1, pos, 1.0, 0x6fb8);  // 1 s of black smoke: a puff every 250 ms
  for (int i = 0; i < 60; ++i) ps.step(1.0 / 60);
  CHECK(ps.puffs.size() >= 4 && ps.puffs.size() <= 5);
  CHECK(ps.emitters.empty());
  for (const Puff& p : ps.puffs) CHECK(p.pos[1] > 0 && p.size() >= 400 && p.size() <= 4500);  // they rise and grow (488 -> 3904 in 250 ms, then 4392)
  for (int i = 0; i < 60 * 3; ++i) ps.step(1.0 / 60);
  CHECK(ps.puffs.empty());  // 250 ms growth + 1.5 s hold + 300 ms fade at most
  ps.fireball(pos);
  ps.step(2.0);
  CHECK(ps.fireballs.size() == 1 && !ps.fireballs[0].fading());
  ps.step(0.4);
  CHECK(ps.fireballs.size() == 1 && ps.fireballs[0].fading());
  ps.step(1.0);
  CHECK(ps.fireballs.empty());
  ps.debris(pos, 9, 3);
  CHECK(ps.pieces.size() == 4);  // at most four pieces
  for (const DebrisPiece& d : ps.pieces) {
    const double sp = std::sqrt(d.vel[0] * d.vel[0] + d.vel[1] * d.vel[1] + d.vel[2] * d.vel[2]);
    CHECK(sp >= 10725 - 1 && sp <= 28600 + 1);
  }
  ps.step(11.0);
  CHECK(ps.pieces.empty());  // gone after 10 s
  const double n[3] = {0, 1, 0};
  ps.scrape(pos, n, 100000, false, 7);
  CHECK(ps.sparks.size() == 38);  // 32 sparks + 6 chips of the surface colour
  for (const Spark& s : ps.sparks) CHECK(s.vel[1] > 0 && s.life >= 1.5 && s.life <= 1.88);
  ps.scrape(pos, n, 100000, true, 7);
  CHECK(ps.sparks.size() == 38 + 12);  // 6 water droplets + 6 chips
  ps.step(2.0);
  CHECK(ps.sparks.empty());
}

int main() {
  particleTests();
  {  // loadout parsing and AI defaults (tables 0x58675 / 0x5869D)
    Loadout l;
    std::string err;
    CHECK(parseLoadoutOption("super seeker:5,mines:8,booster:2,fast", &l, &err));
    CHECK(l.weaponA == kSuperSeeker && l.ammoA == 5 && l.weaponB == kMiniMines && l.ammoB == 8 && l.booster == 2 && l.fastRecharge);
    CHECK(!parseLoadoutOption("nuke", &l, &err));
    CHECK(aiLoadout(0).weaponA == kSeeker && aiLoadout(2).weaponA == kScrambler && aiLoadout(7).weaponA == kDisrupter && aiLoadout(0).ammoA == 6);
  }
  {  // blaster: energy 0.1875 per shot (0xC00/0x4000), 0.5 s cooldown, refill 0.125/s (0x800/0x4000), two beams per shot
    Rig r;
    r.ctx.controls[0].fire = true;
    r.w.step(r.ctx, 1.0 / 120);
    NEAR(r.w.combat[0].energy[0], 1.0 - 0xC00 / 16384.0, 1e-9);
    CHECK(r.w.projectiles.size() == 2);
    r.run(0.2);
    NEAR(r.w.combat[0].energy[0], 1.0 - 0xC00 / 16384.0 + 0.125 * 0.2, 0.01);  // cooldown still running: one shot only
    r.run(0.4);
    CHECK(r.w.combat[0].shotsFired == 2);
  }
  {  // a beam that hits: 2 damage points on each counter (table +0x30/+0x34 = 0x10000, shifted left by 1), 3 s immunity afterwards
    Rig r;
    r.ctx.controls[0].fire = true;
    r.run(0.1);
    r.ctx.controls[0].fire = false;
    r.run(1.0);
    NEAR(r.b.damageA, 2.0, 1e-9);  // table 1.0 << 1
    NEAR(r.b.damageB, 2.0, 1e-9);
    CHECK(r.b.invuln > 2.0);
  }
  {  // a drone between the ships: the blaster beam hits it first (0x202) and it drops a bonus; a missile only destroys it (0x106); bonus types never include the last table entry
    Rig r;
    std::vector<DroneTarget> drones(1);
    drones[0].id = 1;  // the blaster locks on the nearer drone and the beam is re-aimed at it
    drones[0].pos[2] = 100000;
    drones[0].radius = 3000;
    r.ctx.drones = &drones;
    r.ctx.controls[0].fire = true;
    r.run(0.5);
    CHECK(drones[0].hit == 1);
    for (int i = 0; i < 40; ++i) { const int t = r.w.randomBonusType(false); CHECK(t >= 0 && t <= 3); const int c = r.w.randomBonusType(true); CHECK(c >= 0 && c <= 4); }
    const double at[3] = {0, 0, 100000};
    r.w.dropPickup(at, 2, 15.0);
    CHECK(r.w.pickups.size() == 1);
    r.run(15.5);
    CHECK(r.w.pickups.empty());  // the bonus lasts 15 s
  }
  {  // tactical AI (own addition): calm at the start of the race, the heavy weapon stays unused; later in the race (or after being hit) it is fired
    Rig r;
    ShipState c = makeShip(2, 0, 0, 200000);
    r.b.z = 0; r.b.x = 0; r.a.z = -300000;  // ship 1 (AI) in the middle, ship 2 right ahead of it, the human far behind
    r.ctx.ships = {&r.a, &r.b, &c};
    r.ctx.human = {true, false, false};
    r.ctx.finished = {false, false, false};
    r.ctx.shipClass = {1, 2, 3};
    r.ctx.controls = {CombatControls{}, CombatControls{}, CombatControls{}};
    Loadout l; l.weaponA = kSeeker; l.ammoA = 5;
    r.w.setLoadout(1, l);
    r.ctx.raceTime = 0;
    for (int t = 0; t < 120 * 10; ++t) { r.w.step(r.ctx, 1.0 / 120); r.ctx.raceTime += 1.0 / 120; }
    CHECK(r.w.combat[1].load.ammoA == 5);  // 10 s into the race: nothing heavy
    r.ctx.raceTime = 300;
    r.run(20);
    CHECK(r.w.combat[1].load.ammoA < 5);
  }
  {  // lock-on: Seeker locks a ship straight ahead inside the 0x145 cone, not one far to the side; the human cannot lock for 15 s
    Rig r;
    Loadout l; l.weaponA = kSeeker; l.ammoA = 3;
    r.w.setLoadout(0, l);
    r.w.combat[0].selected = 1;
    r.w.combat[0].lockout = 0;
    r.w.step(r.ctx, 1.0 / 120);
    CHECK(r.w.combat[0].lockTarget == 1);
    r.b.x = 60000;
    r.w.step(r.ctx, 1.0 / 120);
    CHECK(r.w.combat[0].lockTarget == -1);
    r.b.x = 0;
    r.w.combat[0].lockout = 5;
    r.w.step(r.ctx, 1.0 / 120);
    CHECK(r.w.combat[0].lockTarget == -1);
  }
  {  // Seeker: launched at owner speed + 0x22E98, homes in, 15/2 damage; ammo runs out and the weapon disappears (cue 0)
    Rig r;
    Loadout l; l.weaponA = kSeeker; l.ammoA = 1;
    r.w.setLoadout(0, l);
    r.w.combat[0].selected = 1;
    r.w.combat[0].lockout = 0;
    r.a.speed = 100000;
    r.b.x = 4000;  // slightly off the line: the missile has to turn
    r.ctx.controls[0].fire = true;
    r.w.step(r.ctx, 1.0 / 120);
    CHECK(r.w.projectiles.size() == 1);
    NEAR(r.w.projectiles[0].speed, 100000 + 0x22E98 + 0x22E98 / 120.0, 1.0);
    CHECK(r.w.projectiles[0].target == 1);
    CHECK(r.w.combat[0].load.weaponA == -1 && r.w.combat[0].selected == 0);
    bool cue0 = false;
    for (auto& e : r.w.events) cue0 = cue0 || (e.kind == CombatEvent::Cue && e.id == cues::outOfAmmo);
    CHECK(cue0);
    r.ctx.controls[0].fire = false;
    r.run(0.5);
    CHECK(r.w.particles.emitters.size() == 1 && !r.w.particles.puffs.empty());  // the missile leaves a smoke trail (0x4F79E)
    r.run(2.5);
    NEAR(r.b.damageA, 15.0, 1e-9);
    NEAR(r.b.damageB, 2.0, 1e-9);
    CHECK(r.w.projectiles.empty());
    bool fx9 = false;
    for (auto& e : r.w.events) fx9 = fx9 || (e.kind == CombatEvent::Fx && e.id == 9);
    CHECK(fx9);
  }
  {  // Ambler: speed cap halved for 10 s; Hyper Neuro: x16 steering for 10 s; Bomber: throttle jammed for 4 s
    Rig r;
    Projectile p;
    p.kind = kAmbler; p.owner = 0; p.life = -1;
    r.w.projectiles.push_back(p);
    // direct contact test through the public path: place the projectile inside ship b
    r.w.projectiles[0].pos[2] = 200000; r.w.projectiles[0].m[8] = 1; r.w.projectiles[0].speed = 1000;
    r.run(0.05);
    NEAR(r.b.halfCapTime, 10.0 - 0.05, 0.1);
    ShipParams sp;
    ShipState q = makeShip(2, 0, 0, 0);
    q.halfCapTime = 10;
    double v[3];
    for (int f = 0; f < 480; ++f) stepShipDynamics(q, ShipInput{1, 0, 0}, 1.0 / 60, sp, v);
    NEAR(q.speed, sp.topSpeed * 0.5, 1);
    ShipState jam = makeShip(3, 0, 0, 0);
    jam.forceThrottleTime = 4;
    for (int f = 0; f < 60; ++f) stepShipDynamics(jam, ShipInput{0, 0, 0}, 1.0 / 60, sp, v);
    CHECK(jam.speed > 10000);  // accelerates without any input
    ShipState rev = makeShip(4, 0, 0, 0), norm = makeShip(5, 0, 0, 0);
    rev.reverseTime = 5;
    rev.speed = norm.speed = 100000;
    for (int f = 0; f < 30; ++f) {
      stepShipDynamics(rev, ShipInput{1, 0, 1}, 1.0 / 60, sp, v);
      stepShipDynamics(norm, ShipInput{1, 0, 1}, 1.0 / 60, sp, v);
    }
    CHECK(rev.m[6] * norm.m[6] < 0 || std::fabs(rev.m[6] + norm.m[6]) < 1e-6);  // heading turns the other way
    ShipState hyp = makeShip(6, 0, 0, 0);
    hyp.hyperTime = 10;
    hyp.speed = 100000;
    stepShipDynamics(hyp, ShipInput{1, 0, 0.1f}, 1.0 / 60, sp, v);
    NEAR(hyp.steerAxis, 0.1 * 4.0 / 60, 1e-9);  // the ramped axis is stored plain
    ShipState norm2 = makeShip(7, 0, 0, 0);
    norm2.speed = 100000;
    stepShipDynamics(norm2, ShipInput{1, 0, 0.1f}, 1.0 / 60, sp, v);
    CHECK(std::fabs(hyp.m[6]) > std::fabs(norm2.m[6]) * 5);
  }
  {  // pickups: touching one consumes it and applies its effect
    Rig r;
    r.a.damageA = 40; r.a.damageB = 30;
    Pickup p0; p0.type = 0; p0.pos[2] = 0;
    Pickup p3; p3.type = 3; p3.pos[0] = 1000;
    Pickup far; far.type = 1; far.pos[2] = 90000;
    r.w.pickups = {p0, p3, far};
    r.run(0.01);
    NEAR(r.a.damageA, 0, 1e-9);
    NEAR(r.a.damageB, 30, 1e-9);
    NEAR(r.a.reverseTime, 5.0, 0.05);
    CHECK(r.w.pickups.size() == 1 && r.w.pickups[0].type == 1);
  }
  {  // booster: switch on with fire while selected, burns 0.25 fuel/s (item 0), stops when the tank is empty
    Rig r;
    Loadout l; l.booster = 0;
    r.w.setLoadout(0, l);
    r.w.combat[0].selected = 3;
    r.ctx.controls[0].fire = true;
    r.w.step(r.ctx, 1.0 / 120);
    CHECK(r.a.boosterOn);
    r.ctx.controls[0].fire = false;
    r.run(2.0);
    NEAR(r.w.combat[0].boosterFuel, 1.0 - 0.25 * 2.0, 0.02);
    r.run(3.0);
    CHECK(!r.a.boosterOn && r.w.combat[0].boosterFuel == 0);
  }
  {  // mines: four of them, 10 s, hurt whoever flies into them (10/10)
    Rig r;
    Loadout l; l.weaponA = kMiniMines; l.ammoA = 2;
    r.w.setLoadout(0, l);
    r.w.combat[0].selected = 1;
    r.ctx.controls[0].fire = true;
    r.w.step(r.ctx, 1.0 / 120);
    r.ctx.controls[0].fire = false;
    CHECK(r.w.projectiles.size() == 4);
    r.b.z = r.a.z - 9760; r.b.x = 4880; r.b.y = 4880;  // fly b into one of them
    r.run(0.05);
    NEAR(r.b.damageA, 10.0, 1e-9);
    CHECK(r.w.projectiles.size() == 3);
    r.run(11.0);
    CHECK(r.w.projectiles.empty());
  }
  {  // Disrupter: no damage, steering reversed for 5 s
    Rig r;
    Loadout l; l.weaponA = kDisrupter; l.ammoA = 2;
    r.w.setLoadout(0, l);
    r.w.combat[0].selected = 1;
    r.w.combat[0].lockout = 0;
    r.ctx.controls[0].fire = true;
    r.run(0.05);
    r.ctx.controls[0].fire = false;
    r.run(1.0);
    NEAR(r.b.damageA, 0, 1e-9);
    CHECK(r.b.reverseTime > 3.5);
  }
  {  // multiplayer: a ship simulated by another peer is no victim here, launches / hits are logged for the relay, remote copies fly and die by id
    Rig r;
    r.ctx.remote = {false, true};  // ship 1 belongs to another peer
    r.w.combat[0].energy[0] = 1;
    r.ctx.controls[0].fire = true;
    r.run(0.05);
    CHECK(r.w.launched.size() == 2 && r.w.launched[0].first && !r.w.launched[1].first && r.w.launched[0].id != r.w.launched[1].id);
    r.ctx.controls[0].fire = false;
    r.run(3.0);
    CHECK(r.b.damageA == 0 && r.b.damageB == 0 && r.w.hitLog.empty());  // flew through the remote ship: its owner resolves the hit
    CHECK(!r.w.projectiles.empty());
    const uint32_t id = r.w.launched[0].id;
    const double hp[3] = {0, 0, 200000};
    r.w.remoteHit(r.ctx, id, 1, hp);
    bool gone = true;
    for (const Projectile& p : r.w.projectiles) if (p.id == id && p.alive) gone = false;
    CHECK(gone && r.w.combat[0].hitsDealt == 1);
    // a copy of a projectile launched elsewhere hits the local ship and is reported
    Projectile p;
    p.kind = kBlaster; p.owner = 1; p.id = (1u << 24) | 5; p.pos[0] = 0; p.pos[1] = 0; p.pos[2] = 30000; p.m[6] = 0; p.m[7] = 0; p.m[8] = -1; p.speed = 400000; p.life = 5; p.first = true;
    r.w.projectiles.clear(); r.w.hitLog.clear();
    r.w.spawnRemote(p);
    r.run(0.5);
    CHECK(r.w.hitLog.size() == 1 && r.w.hitLog[0].victim == 0 && r.a.damageA > 0);
  }
  std::printf(failures ? "weapons: %d failure(s)\n" : "weapons ok\n", failures);
  return failures ? 1 : 0;
}
