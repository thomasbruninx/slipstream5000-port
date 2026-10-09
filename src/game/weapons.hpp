// Weapons, pickups and the per-ship combat state, ported from the original's ship message handler (RaceSlotControl,
// 0x50480..0x51247), the weapon table (0x5D612, 12 x 0x38 bytes), the launchers (0x5C34D..0x5D5D5), the projectile
// servers (0x5C481 / 0x5C741 beams, 0x5CB03 / 0x5D24B / 0x5D33A missiles, 0x5D453 mines) and the bonus objects
// (0x42A9A / 0x42BD5, placement tables at 0x5502C). Everything numeric is read from the user's SLIPSTRM.EXE at run time.
// What is CONFIRMED / STRONGLY INFERRED / SPECULATIVE is listed in docs/simulation.md ("Weapons and pickups").
#pragma once
#include <array>
#include "game/particles.hpp"
#include <cstdint>
#include <string>
#include <vector>

#include "game/scene.hpp"
#include "game/ship_params.hpp"
#include "game/ship_sim.hpp"
#include "original_formats/game_data.hpp"

namespace slip {

enum WeaponId : int {
  kBlaster = 0, kDisrupter, kFrag, kSuperFrag, kSeeker, kSuperSeeker, kAmbler, kScrambler, kHyperNeuro, kSmoker, kBomber, kMiniMines,
  kWeaponCount
};

struct WeaponDef {  // one 0x38 byte record of the table at 0x5D612
  std::string name;
  int price[3] = {0, 0, 0};   // +0x10: shop price per difficulty level
  int pack = -1;              // +0x1C: rounds per purchase (-1 = unlimited)
  double recharge = 1.0;      // +0x20 / 0x4000: energy pool refill per second
  double cost = 1.0;          // +0x24 / 0x4000: energy a shot needs and uses (pool 0..1)
  int cone = 0;               // +0x2C: lock-on cone half angle in 1/65536 turn (0 = no lock-on)
  double damageA = 1.0;       // +0x30 / 0x10000: engine damage points (of 100)
  double damageB = 1.0;       // +0x34 / 0x10000: steering damage points
};

struct BoosterDef {  // table 0x5BD44, 0x24 bytes each (5 items)
  std::string name;
  int price = 0;
  double gain = 0.1;   // +0x1C / 0x4000 - 1: added to the speed factor while burning
  double burn = 0.25;  // +0x20 / 0x4000: fuel (0..1) used per second
};

struct WeaponTable {
  std::array<WeaponDef, kWeaponCount> w;
  std::array<BoosterDef, 5> boosters;
  bool fromExecutable = false;
};
WeaponTable defaultWeaponTable();  // the values of the original's table, used when no executable is available (tests)
WeaponTable loadWeaponTable(const GameData& data);

struct ShipRefPoints {  // ART reference points of the root node (unscaled model units)
  double lasl[3] = {-3000, -800, 0}, lasr[3] = {3000, -800, 0}, weap[3] = {0, -1500, 0}, head[3] = {0, 500, 1000}, smok[3] = {0, 0, -5000};
  double frag[4][3] = {};  // ART debris list 1: where the pieces R<n>FRG50..53 start (ship frame)
};
std::array<ShipRefPoints, 10> loadShipRefPoints(const GameData& data);

struct PickupSpot { double pos[3]; int type; };  // type -1 = random
std::vector<PickupSpot> loadPickupSpots(const GameData& data, int track);

// What the ship carries into the race (record +0x32/+0x36 weapon ids, +0x3A/+0x3E ammo, +0x42 booster item, +0x46 flags).
struct Loadout {
  int weaponA = -1, weaponB = -1;  // -1 = empty
  int ammoA = 0, ammoB = 0;        // < 0 = unlimited
  int booster = -1;                // -1 = none
  bool fastRecharge = false;       // record flag 1: energy pools and the blaster cooldown recharge twice as fast
  bool wideLock = false;           // record flag 2: lock cone doubled
};
// Default AI loadout (0x58641: weapon A by ship class 0x58675, 6 rounds, no B, booster item 0) and the cheat-mode
// loadout of the original ([0x53FF8]: Seeker + Scrambler, 9 rounds each) that the port gives the player by default.
Loadout aiLoadout(int shipIndex);
Loadout defaultPlayerLoadout();
bool parseLoadoutOption(const std::string& text, Loadout* out, std::string* error);  // "seeker:9,scrambler:9,booster:2"

struct CombatState {
  Loadout load;
  int selected = 0;                 // [+0x14]: 0 blaster, 1 weapon A, 2 weapon B, 3 booster
  double energy[3] = {1, 1, 1};     // [+0x16] blaster, [+0x18] A, [+0x1A] B: 0..1 (0x4000)
  double boosterFuel = 1.0;         // [+0x32]
  double cooldown = 0;              // [+0x30] seconds (blaster)
  double lockout = 15.0;            // [+0x4C]: the human cannot lock on for the first 15 s ([+0x4C] = 0x3A98)
  int lockTarget = -1;              // [+0x1C] ship index
  int lockDrone = 0;                // id of the drone locked on (0 = none; own addition, the drones are not lock-on targets in the original), lockDronePos its position
  double lockDronePos[3] = {0, 0, 0};
  bool fireHeld = false, firePrev = false;  // [+0x3C] / [+0x3D]
  double aiClock = 0;               // AI decision cadence
  // tactical AI (own addition, see aiDecide): how angry the pilot is (hits taken), whom it blames, the pause after a heavy shot
  double anger = 0;
  int angerTarget = -1;
  double holdoff = 0;
  int credits = 0;                  // record +4 (bonus type 4 adds 50)
  int shotsFired = 0, hitsDealt = 0;
};

struct Projectile {
  int kind = 0;          // WeaponId (beam 0/1, missiles 2..8/10, mines 11)
  int owner = -1, target = -1, targetDrone = 0;
  double pos[3] = {0, 0, 0}, prev[3] = {0, 0, 0};
  double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // rows right, up, forward (the missile's orientation)
  double speed = 0;
  double life = 0;       // seconds left (beams 5 s, mines 10 s), < 0 = no limit
  double age = 0;
  double radius = 1500;  // collision radius (half the mesh size)
  bool alive = true;
  // multiplayer: ids are unique per session (owner << 24 | counter); a copy of another peer's projectile only flies and hits ships simulated here
  uint32_t id = 0;
  bool remote = false;   // copy of a projectile launched on another peer
  bool first = false;    // first projectile of a launch (plays the launch sound on the other peers)
};

struct Pickup {
  double pos[3] = {0, 0, 0};
  int type = 0;          // 0 repair engine, 1 repair steering, 2 booster fuel, 3 reversed controls (5 s), 4 +50 credits, 5 free booster (5 s)
  bool alive = true;
  int id = 0;            // index in the placement list (the same on every peer)
  double life = -1;      // seconds left (a bonus dropped by a drone lasts 0x3A98 ms), < 0 = stays
};

// A drone as the weapons see it: beams that hit it destroy it and make it drop a bonus, other projectiles only destroy it (messages 0x202 / 0x106 of 0x4A3B2).
struct DroneTarget { int id = 0; double pos[3] = {0, 0, 0}; double radius = 3000; bool alive = true; int hit = 0; /* 0 none, 1 beam, 2 other */ double hitPos[3] = {0, 0, 0}; };

struct CombatEvent {
  enum Kind { Fx, Cue } kind = Fx;
  int id = 0;       // original FxPlay id (1..16) or voice cue index (0..84)
  int ship = -1;    // ship the event belongs to (position source / own-event test)
  double pos[3] = {0, 0, 0};
};

struct CombatControls { bool fire = false; bool cycle = false; };

struct CombatContext {
  const Scene* scene = nullptr;
  std::vector<ShipState*> ships;      // indexed by ship number 0..9
  std::vector<bool> human;            // ships steered by the player
  std::vector<bool> finished;         // ships that finished the race (the AI stops shooting)
  std::vector<int> shipClass;         // 1..10 (record +0: pilot class), used by the cue tables
  std::vector<CombatControls> controls;  // human intents for this step (cycle is an edge)
  std::vector<const ShipState*> obstacles;  // door panels (static boxes): missiles stop at them (their 0x106 handler destroys the missile)
  int humanShip = -1;                 // the listener ([0x543DD]): receives the pilot / announcer cues
  std::vector<bool> remote;           // multiplayer: ships simulated on another peer (no ship logic, never a victim here; hits arrive as events)
  double raceTime = 0;                // seconds since the start of the race (the tactical AI grows more aggressive with it)
  std::vector<DroneTarget>* drones = nullptr;  // the drones of the race (hits are written back into them)
};

class CombatWorld {
 public:
  void init(const WeaponTable& table, const std::array<ShipRefPoints, 10>& refs, int track, const std::vector<PickupSpot>& spots, unsigned seed);
  void setLoadout(int ship, const Loadout& l);
  void setProjectileRadius(int kind, double r);
  // A bonus object that appears where a drone was shot (0x42A9A): type from the table 0x42A1C (single races: repair engine / steering, reversed controls, booster fuel; the
  // championship adds +50 credits; the last entry of each table is never drawn: Random(count - 1)); it disappears after `life` seconds.
  int randomBonusType(bool championship);
  void dropPickup(const double* pos, int type, double life);
  void explodeAt(const double* pos) {
    particles.fireball(pos);
    for (int i = 0; i < 4; ++i) {
      const int k = int(particles.irand(4));
      const double p[3] = {pos[0] + droneFrag[k][0], pos[1] + droneFrag[k][1], pos[2] + droneFrag[k][2]};
      particles.debrisPiece(p, 10, k);
    }
    emitFx(9, -1, pos);
  }  // a drone blows up (0x4A4BB: fireball, 0x4A42F: 4 DRFRG pieces)
  // One simulation step (all ships already moved): recharge, lock-on, AI decisions, firing, projectiles, pickups, effects.
  void step(const CombatContext& ctx, double dt);

  const WeaponTable& table() const { return *table_; }
  bool classicAi = false;  // the original AI weapon logic (random cycling, fires everything at any lock) instead of the tactical one
  int difficulty = 1;  // [0x49F04]: 2 doubles the blaster damage again (0x5C0F9)
  std::array<CombatState, 10> combat;
  std::vector<Projectile> projectiles;
  std::vector<Pickup> pickups;
  ParticleSystem particles;  // smoke trails, explosions, debris (game/particles.hpp)
  std::vector<CombatEvent> events;  // drained by the front end
  // multiplayer: what happened here that the other peers must hear about (drained by game/netplay)
  struct HitRec { uint32_t id; int victim; double pos[3]; };
  std::vector<Projectile> launched;  // projectiles launched this step (a smoker puff is kind kSmoker, not added to `projectiles`)
  std::vector<HitRec> hitLog;        // projectiles that hit a ship simulated here
  std::vector<std::pair<int, int>> pickupLog;  // (pickup id, ship) taken by a ship simulated here
  static int hitFx(int kind);
  void spawnRemote(const Projectile& p);
  void remoteHit(const CombatContext& ctx, uint32_t id, int victim, const double* pos);
  void remotePickup(int id, int ship);

 private:
  struct Rng { uint32_t s = 1; uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } } rng_;
  void shipLogic(const CombatContext& ctx, int i, double dt);
  unsigned dropCounter_ = 0;
  double stepDt_ = 1.0 / 120.0;
  void updateLock(const CombatContext& ctx, int i);
  void provoke(int victim, int owner, double amount);
  bool tableCone(int weapon) const { return weapon >= 0 && table_->w[size_t(weapon)].cone > 0; }
  void aiDecide(const CombatContext& ctx, int i, double dt, bool* fire, bool* cycle);
  void cycleWeapon(int i);
  void fire(const CombatContext& ctx, int i);
  void launch(const CombatContext& ctx, int i, int weapon);
  void stepProjectile(const CombatContext& ctx, Projectile& p, double dt);
  void hitShip(const CombatContext& ctx, const Projectile& p, int victim);
  void beamHit(const CombatContext& ctx, const Projectile& p, int victim);
  void stepPickups(const CombatContext& ctx);
  void applyPickup(const CombatContext& ctx, int ship, int type);
  void emitFx(int id, int ship, const double* pos);
  void emitCue(const CombatContext& ctx, int ship, int cue);
  // particle effects (the call sites of RaceBang 0x4F61B / 0x4F79E / 0x4F7BC / 0x4F3A0)
  void followProjectile(int emitter, uint32_t projId);
  void followShip(int emitter, int ship, const double* localOffset);
  void trailFor(const Projectile& p);                       // missile exhaust (0x5CCC6..0x5D214)
  void smokeBurst(const double* pos, double seconds);       // black smoke that rises (0x4F7BC, 1 s wall hit / 2 s mine)
  void shipDebris(const CombatContext& ctx, int victim, int count);  // pieces start at the ART debris points of the craft
 public:
  double droneFrag[4][3] = {};  // DRONE.ART debris list 1 (set by the application)
  // A ship scraped a wall or the water (message 0x107 -> RaceBang 0x4FD93 / 0x4FF2E).
  void scrapeEffects(const double pos[3], const double n[3], double speed, bool water, int surfaceMaterial) { particles.scrape(pos, n, speed, water, surfaceMaterial); }
 private:
  void damageSmoke(const CombatContext& ctx, int ship, double damageA);  // RaceSlotDamage 0x52090: a hit of 9.0 or more leaves a smoking engine for 4 s
  struct Follow { int emitter; int kind; uint32_t proj; int ship; double off[3]; };  // kind 0 projectile, 1 ship reference point
  std::vector<Follow> follows_;
  void stepFollows(const CombatContext& ctx);
  static bool isRemote(const CombatContext& ctx, int i) { return ctx.remote.size() > size_t(i) && ctx.remote[size_t(i)]; }
  uint32_t launchCounter_ = 0;
  const WeaponTable* table_ = nullptr;
  std::array<ShipRefPoints, 10> refs_{};
  std::array<double, kWeaponCount> radius_{};
  int track_ = 1;
};

// Voice cue tables of the original (index = pilot class 1..10 -> cue number in the 85 entry list of mode 3).
namespace cues {
constexpr int outOfAmmo = 0, wonRace = 0, wonRace2 = 1, shipBreaking1 = 2, shipBreaking2 = 3;
inline int contact(int cls) { return 3 + cls; }          // 0x509BA
inline int hitByHuman(int cls) { return 13 + cls; }      // 0x50881
inline int passLine1(int cls) { return 23 + cls; }       // 0x50C03
inline int passLine2(int cls) { return 33 + cls; }       // 0x50C2B
inline int aiTargetsHuman(int cls) { return 43 + cls; }  // 0x515A4
inline int positionAnnounce(int rank) { return 64 + rank; }  // 0x5A7A4: EPS0..EPS9 for rank 1..10
inline int finishLine(int cls) { return 74 + cls; }      // 0x5A7CC
constexpr int underFire = 0x3F, mineHit = 0x40;
int launchCue(int weapon);                               // announcer line when the player fires (-1 = none)
}  // namespace cues

}  // namespace slip
