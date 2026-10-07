#include "game/weapons.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>

#include "game/ship_collide.hpp"
#include "original_formats/formats.hpp"

namespace slip {

namespace {
constexpr double kTau = 6.283185307179586;
constexpr size_t kExeBase = 0x4D854 - 0x10000;  // file offset = kExeBase + virtual address
constexpr double kBeamSpeed = 0x77240;          // beam head speed (0x5C4C6 / 0x5C786)
constexpr double kMissileAccel = 0x22E98, kMissileCap = 0x9D1AC;  // 0x5D26F.. servers
constexpr double kHomingRate = 0x4000 / 65536.0 * kTau;            // rad/s: 0x212F8 is fed 0x4000 * dt (65536 = full turn)
constexpr double kScramblerHomeRange = 0x2FA80;                    // 0x5D3C9: farther than this it flies straight
constexpr double kLockRange = 0xEE480, kLockMinZ = 0x988;          // 0x51076..0x5107B
constexpr double kPickupHalf = 0x2620;                             // pickup collision cube (0x42B4F)

uint32_t rd32(const Bytes& e, size_t va) {
  const size_t o = kExeBase + va;
  if (o + 4 > e.size()) return 0;
  return uint32_t(e[o]) | (uint32_t(e[o + 1]) << 8) | (uint32_t(e[o + 2]) << 16) | (uint32_t(e[o + 3]) << 24);
}
int32_t rds32(const Bytes& e, size_t va) { return int32_t(rd32(e, va)); }

inline double dot3(const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline double len3(const double* a) { return std::sqrt(dot3(a, a)); }
inline void world(const ShipState& s, const double* r, double* out) {  // model point -> world
  for (int k = 0; k < 3; ++k) out[k] = (&s.x)[k] + s.m[k] * r[0] + s.m[3 + k] * r[1] + s.m[6 + k] * r[2];
}
inline void toLocal(const ShipState& s, const double* d, double* l) {  // world delta -> ship frame (right, up, forward)
  l[0] = dot3(d, s.m); l[1] = dot3(d, s.m + 3); l[2] = dot3(d, s.m + 6);
}

// Segment a->b against the ship's collision box grown by `r`. Returns the entry fraction or -1.
double segmentVsShip(const ShipState& s, const double* a, const double* b, double r) {
  if (!s.hasBox) return -1;
  const double da[3] = {a[0] - s.x, a[1] - s.y, a[2] - s.z}, db[3] = {b[0] - s.x, b[1] - s.y, b[2] - s.z};
  double la[3], lb[3];
  toLocal(s, da, la);
  toLocal(s, db, lb);
  double t0 = 0, t1 = 1;
  for (int k = 0; k < 3; ++k) {
    const double lo = s.boxLo[k] - r, hi = s.boxHi[k] + r, d = lb[k] - la[k];
    if (std::fabs(d) < 1e-9) {
      if (la[k] < lo || la[k] > hi) return -1;
      continue;
    }
    double u0 = (lo - la[k]) / d, u1 = (hi - la[k]) / d;
    if (u0 > u1) std::swap(u0, u1);
    t0 = std::max(t0, u0);
    t1 = std::min(t1, u1);
    if (t0 > t1) return -1;
  }
  return t0;
}

bool pointNearShip(const ShipState& s, const double* p, double r) {
  const double d[3] = {p[0] - s.x, p[1] - s.y, p[2] - s.z};
  double l[3];
  toLocal(s, d, l);
  for (int k = 0; k < 3; ++k)
    if (l[k] < s.boxLo[k] - r || l[k] > s.boxHi[k] + r) return false;
  return true;
}

void setForward(double* m, const double* f) {  // sub_26F43 role: new heading, keep a sane up axis
  double fw[3] = {f[0], f[1], f[2]};
  const double l = len3(fw);
  if (l < 1e-9) return;
  for (double& x : fw) x /= l;
  double up[3] = {m[3], m[4], m[5]};
  double d = dot3(up, fw);
  for (int k = 0; k < 3; ++k) up[k] -= d * fw[k];
  double ul = len3(up);
  if (ul < 1e-6) { up[0] = 0; up[1] = 1; up[2] = 0; d = dot3(up, fw); for (int k = 0; k < 3; ++k) up[k] -= d * fw[k]; ul = len3(up); }
  if (ul < 1e-6) { up[0] = 1; up[1] = 0; up[2] = 0; d = dot3(up, fw); for (int k = 0; k < 3; ++k) up[k] -= d * fw[k]; ul = len3(up); }
  for (double& x : up) x /= ul;
  const double right[3] = {up[1] * fw[2] - up[2] * fw[1], up[2] * fw[0] - up[0] * fw[2], up[0] * fw[1] - up[1] * fw[0]};
  for (int k = 0; k < 3; ++k) { m[k] = right[k]; m[3 + k] = up[k]; m[6 + k] = fw[k]; }
}

// Turn the heading towards `want` (unit) by at most `maxAng` radians (0x212F8: axis-angle rotation of the orientation).
void turnToward(double* m, const double* want, double maxAng) {
  const double* f = m + 6;
  const double c = std::clamp(dot3(f, want), -1.0, 1.0);
  const double ang = std::acos(c);
  if (ang < 1e-9) return;
  if (ang <= maxAng) { setForward(m, want); return; }
  double axis[3] = {f[1] * want[2] - f[2] * want[1], f[2] * want[0] - f[0] * want[2], f[0] * want[1] - f[1] * want[0]};
  const double al = len3(axis);
  if (al < 1e-9) return;
  for (double& x : axis) x /= al;
  const double cs = std::cos(maxAng), sn = std::sin(maxAng);
  for (int row = 0; row < 3; ++row) {
    double* v = m + row * 3;
    const double kx[3] = {axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]};
    const double kd = dot3(axis, v);
    for (int k = 0; k < 3; ++k) v[k] = v[k] * cs + kx[k] * sn + axis[k] * kd * (1 - cs);
  }
  setForward(m, m + 6);
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------- tables
WeaponTable defaultWeaponTable() {
  WeaponTable t;
  static const char* kNames[kWeaponCount] = {"Blaster", "Disrupter", "Frag", "Super Frag", "Seeker", "Super Seeker", "Ambler", "Scrambler", "Hyper Neuro", "Smoker", "Bomber", "Mini Mines"};
  static const int kPack[kWeaponCount] = {-1, 8, 3, 3, 3, 4, 3, 3, 3, 3, 3, 8};
  static const int kCone[kWeaponCount] = {0x145, 0x145, 0x145, 0x145, 0x145, 0x145, 0x145, 0x145, 0x145, 0, 0x145, 0};
  static const double kDmg[kWeaponCount][2] = {{1, 1}, {1, 1}, {2, 15}, {4, 25}, {15, 2}, {25, 4}, {1, 1}, {25, 25}, {1, 1}, {1, 1}, {1, 1}, {10, 10}};
  for (int i = 0; i < kWeaponCount; ++i) {  // fallback values (identical to the table of the original game)
    t.w[size_t(i)].name = kNames[i];
    t.w[size_t(i)].pack = kPack[i];
    t.w[size_t(i)].recharge = i == 0 ? 0x800 / 16384.0 : 1.0;
    t.w[size_t(i)].cost = i == 0 ? 0xC00 / 16384.0 : 1.0;
    t.w[size_t(i)].cone = kCone[i];
    t.w[size_t(i)].damageA = kDmg[i][0];
    t.w[size_t(i)].damageB = kDmg[i][1];
  }
  const double kBoostGain[5] = {0.1, 0.15, 0.2, 0.25, 0.3}, kBoostBurn[5] = {0.25, 0.1875, 0.15625, 0.125, 0.0625};
  static const char* kBoostNames[5] = {"Delphine Injection", "Corolis Dynamic", "Dual Derwent", "Cleric Quinn", "Tech Tech 301"};
  for (int i = 0; i < 5; ++i) { t.boosters[size_t(i)].name = kBoostNames[i]; t.boosters[size_t(i)].gain = kBoostGain[i]; t.boosters[size_t(i)].burn = kBoostBurn[i]; }
  return t;
}

WeaponTable loadWeaponTable(const GameData& data) {
  WeaponTable t = defaultWeaponTable();
  auto exe = data.read("SLIPSTRM.EXE");
  if (!exe || exe->size() < kExeBase + 0x5D612 + 12 * 0x38) return t;
  const size_t tbl = kExeBase + 0x5D612;
  if (std::memcmp(exe->data() + tbl, "Blaster", 7) != 0) return t;
  for (int i = 0; i < kWeaponCount; ++i) {
    const size_t va = 0x5D612 + size_t(i) * 0x38;
    WeaponDef& w = t.w[size_t(i)];
    w.name.assign(reinterpret_cast<const char*>(exe->data() + kExeBase + va), strnlen(reinterpret_cast<const char*>(exe->data() + kExeBase + va), 16));
    for (int k = 0; k < 3; ++k) w.price[k] = rds32(*exe, va + 0x10 + size_t(k) * 4);
    w.pack = rds32(*exe, va + 0x1C);
    w.recharge = rds32(*exe, va + 0x20) / 16384.0;
    w.cost = rds32(*exe, va + 0x24) / 16384.0;
    w.cone = rds32(*exe, va + 0x2C);
    w.damageA = rds32(*exe, va + 0x30) / 65536.0;
    w.damageB = rds32(*exe, va + 0x34) / 65536.0;
  }
  if (exe->size() >= kExeBase + 0x5BD44 + 5 * 0x24 && std::memcmp(exe->data() + kExeBase + 0x5BD44, "Delphine", 8) == 0) {
    for (int i = 0; i < 5; ++i) {
      const size_t va = 0x5BD44 + size_t(i) * 0x24;
      BoosterDef& b = t.boosters[size_t(i)];
      b.name.assign(reinterpret_cast<const char*>(exe->data() + kExeBase + va), strnlen(reinterpret_cast<const char*>(exe->data() + kExeBase + va), 24));
      b.price = rds32(*exe, va + 0x18);
      b.gain = rds32(*exe, va + 0x1C) / 16384.0 - 1.0;
      b.burn = rds32(*exe, va + 0x20) / 16384.0;
    }
  }
  t.fromExecutable = true;
  return t;
}

std::array<ShipRefPoints, 10> loadShipRefPoints(const GameData& data) {
  std::array<ShipRefPoints, 10> out{};
  for (int i = 0; i < 10; ++i) {
    auto b = data.read("RACER" + std::to_string(i) + ".ART");
    if (!b) continue;
    auto art = parseArt(*b);
    if (!art || art->nodes.empty()) continue;
    for (const ArtRefPoint& r : art->nodes[0].refPoints) {
      double* dst = r.tag == "lasl" ? out[size_t(i)].lasl : r.tag == "lasr" ? out[size_t(i)].lasr : r.tag == "weap" ? out[size_t(i)].weap
                  : r.tag == "head" ? out[size_t(i)].head : r.tag == "smok" ? out[size_t(i)].smok : nullptr;
      if (dst) { dst[0] = r.pos.x; dst[1] = r.pos.y; dst[2] = r.pos.z; }
    }
  }
  return out;
}

std::vector<PickupSpot> loadPickupSpots(const GameData& data, int track) {
  std::vector<PickupSpot> out;
  auto exe = data.read("SLIPSTRM.EXE");
  if (!exe || track < 1 || track > 10 || exe->size() < kExeBase + 0x5502C + 44) return out;
  const uint32_t ptr = rd32(*exe, 0x5502C + size_t(track) * 4);
  if (ptr == 0) return out;
  const size_t va = size_t(ptr) + 0x10000;  // relocated pointer tables hold object-relative offsets
  const uint32_t n = rd32(*exe, va);
  if (n == 0 || n > 64 || kExeBase + va + 4 + n * 16 > exe->size()) return out;
  for (uint32_t i = 0; i < n; ++i) {
    const size_t e = va + 4 + size_t(i) * 16;
    out.push_back({{double(rds32(*exe, e)), double(rds32(*exe, e + 4)), double(rds32(*exe, e + 8))}, rds32(*exe, e + 12)});
  }
  return out;
}

Loadout aiLoadout(int shipIndex) {
  static const int kWeaponByClass[10] = {4, 4, 7, 5, 2, 2, 3, 1, 8, 2};  // table 0x58675
  Loadout l;
  l.weaponA = kWeaponByClass[std::clamp(shipIndex, 0, 9)];
  l.ammoA = 6;  // table 0x5869D
  l.booster = 0;
  return l;
}

Loadout defaultPlayerLoadout() {
  Loadout l;
  l.weaponA = kSeeker; l.ammoA = 9;
  l.weaponB = kScrambler; l.ammoB = 9;
  l.booster = 0;
  return l;
}

bool parseLoadoutOption(const std::string& text, Loadout* out, std::string* error) {
  Loadout l;
  l.booster = -1;
  std::stringstream ss(text);
  std::string tok;
  int slot = 0;
  auto lower = [](std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; };
  auto squash = [&](std::string s) { std::string r; for (char c : lower(s)) if (std::isalnum(static_cast<unsigned char>(c))) r += c; return r; };
  while (std::getline(ss, tok, ',')) {
    if (tok.empty()) continue;
    std::string name = tok;
    int amount = -1;
    const size_t colon = tok.find(':');
    if (colon != std::string::npos) { name = tok.substr(0, colon); amount = std::atoi(tok.c_str() + colon + 1); }
    const std::string key = squash(name);
    if (key == "none") continue;
    if (key == "booster") { l.booster = std::clamp(amount < 0 ? 0 : amount, 0, 4); continue; }
    if (key == "fast") { l.fastRecharge = true; continue; }
    if (key == "wide") { l.wideLock = true; continue; }
    static const char* kKeys[kWeaponCount] = {"blaster", "disrupter", "frag", "superfrag", "seeker", "superseeker", "ambler", "scrambler", "hyperneuro", "smoker", "bomber", "minimines"};
    int id = -1;
    for (int i = 1; i < kWeaponCount; ++i) if (key == kKeys[i] || (key == "mines" && i == kMiniMines) || (key == "disruptor" && i == kDisrupter)) id = i;
    if (id < 0) { if (error) *error = "unknown weapon '" + name + "'"; return false; }
    if (amount < 0) amount = 9;
    if (slot == 0) { l.weaponA = id; l.ammoA = amount; }
    else if (slot == 1) { l.weaponB = id; l.ammoB = amount; }
    else { if (error) *error = "at most two weapons can be carried"; return false; }
    ++slot;
  }
  *out = l;
  return true;
}

int cues::launchCue(int weapon) {
  switch (weapon) {  // 0x5C684 / 0x5C944 / 0x5CC01 / 0x5CD0E / 0x5CE1B / 0x5CF28 / 0x5D035 / 0x5D142 / 0x5D5CA
    case kDisrupter: return 0x36;
    case kFrag: return 0x37;
    case kSuperFrag: return 0x38;
    case kSeeker: return 0x39;
    case kSuperSeeker: return 0x3A;
    case kAmbler: return 0x3B;
    case kScrambler: return 0x3C;
    case kBomber: return 0x3E;
    case kMiniMines: return 1;
    default: return -1;
  }
}

// ------------------------------------------------------------------------------------------------------------- the world
void CombatWorld::init(const WeaponTable& table, const std::array<ShipRefPoints, 10>& refs, int track, const std::vector<PickupSpot>& spots, unsigned seed) {
  table_ = &table;
  refs_ = refs;
  track_ = track;
  rng_.s = seed ? seed : 1u;
  projectiles.clear();
  explosions.clear();
  events.clear();
  pickups.clear();
  for (auto& c : combat) c = CombatState{};
  for (int k = 0; k < kWeaponCount; ++k) radius_[size_t(k)] = 1500;
  static const int kRandomTypes[5] = {0, 1, 3, 2, 5};  // list at 0x42A1C
  for (const PickupSpot& s : spots) {
    Pickup p;
    for (int k = 0; k < 3; ++k) p.pos[k] = s.pos[k];
    p.type = s.type >= 0 ? s.type : kRandomTypes[rng_.next() % 5];
    pickups.push_back(p);
  }
}

void CombatWorld::setLoadout(int ship, const Loadout& l) {
  CombatState& c = combat[size_t(ship)];
  c.load = l;
  c.selected = 0;
  c.boosterFuel = l.booster >= 0 ? 1.0 : 0.0;  // 0x50610: [+0x32] = 0x4000 when the record owns a booster
}

void CombatWorld::setProjectileRadius(int kind, double r) { if (kind >= 0 && kind < kWeaponCount) radius_[size_t(kind)] = r; }

void CombatWorld::emitFx(int id, int ship, const double* pos) {
  CombatEvent e;
  e.kind = CombatEvent::Fx; e.id = id; e.ship = ship;
  for (int k = 0; k < 3; ++k) e.pos[k] = pos[k];
  events.push_back(e);
}

void CombatWorld::emitCue(const CombatContext&, int ship, int cue) {
  CombatEvent e;
  e.kind = CombatEvent::Cue; e.id = cue; e.ship = ship;
  events.push_back(e);
}

void CombatWorld::addExplosion(const double* pos, int kind) {
  Explosion x;
  for (int k = 0; k < 3; ++k) x.pos[k] = pos[k];
  x.kind = kind;
  x.life = kind == 1 ? 4.0 : 0.5;
  explosions.push_back(x);
}

void CombatWorld::step(const CombatContext& ctx, double dt) {
  if (!table_) return;
  for (int i = 0; i < int(ctx.ships.size()) && i < 10; ++i)
    if (ctx.ships[size_t(i)]) shipLogic(ctx, i, dt);
  for (Projectile& p : projectiles) if (p.alive) stepProjectile(ctx, p, dt);
  projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.alive; }), projectiles.end());
  stepPickups(ctx);
  for (Explosion& x : explosions) x.age += dt;
  explosions.erase(std::remove_if(explosions.begin(), explosions.end(), [](const Explosion& x) { return x.age >= x.life; }), explosions.end());
}

void CombatWorld::shipLogic(const CombatContext& ctx, int i, double dt) {
  ShipState& s = *ctx.ships[size_t(i)];
  CombatState& c = combat[size_t(i)];
  const auto& tb = *table_;
  s.boosterGain = c.load.booster >= 0 ? tb.boosters[size_t(c.load.booster)].gain : 0.0;
  if (s.wrecked) { c.fireHeld = false; c.lockTarget = -1; return; }  // the debris handler replaces the ship handler
  c.cooldown = std::max(0.0, c.cooldown - dt);
  c.lockout = std::max(0.0, c.lockout - dt);

  // Pit lane (0x50E97..0x50EF1): inside the refuel piece the damage melts away (25 points/s) and the booster fuel refills.
  if (ctx.scene && ctx.scene->track_data.refuelPiece >= 0 && size_t(ctx.scene->track_data.refuelPiece) < ctx.scene->pieceBoxes.size()) {
    const float p[3] = {float(s.x - ctx.scene->origin[0]), float(s.y - ctx.scene->origin[1]), float(s.z - ctx.scene->origin[2])};
    if (ctx.scene->pieceContains(size_t(ctx.scene->track_data.refuelPiece), p, 512.0f)) {
      s.damageA = std::max(0.0, s.damageA - 25.0 * dt);
      s.damageB = std::max(0.0, s.damageB - 25.0 * dt);
      c.boosterFuel = std::min(1.0, c.boosterFuel + dt);
    }
  }
  // energy pools (0x50EF6..0x50FC4)
  const int ids[3] = {kBlaster, c.load.weaponA, c.load.weaponB};
  for (int p = 0; p < 3; ++p) {
    if (c.energy[p] >= 1.0 || ids[p] < 0) continue;
    c.energy[p] = std::min(1.0, c.energy[p] + tb.w[size_t(ids[p])].recharge * (c.load.fastRecharge ? 2.0 : 1.0) * dt);
  }
  // booster burn (0x510CB..0x5110E)
  if (s.boosterOn) {
    const double burn = c.load.booster >= 0 ? tb.boosters[size_t(c.load.booster)].burn : 1.0;
    c.boosterFuel -= burn * dt;
    if (c.boosterFuel <= 0) { c.boosterFuel = 0; s.boosterOn = false; }
  }
  updateLock(ctx, i);

  bool wantFire = false, wantCycle = false;
  if (ctx.human.size() > size_t(i) && ctx.human[size_t(i)]) {
    if (ctx.controls.size() > size_t(i)) { wantFire = ctx.controls[size_t(i)].fire; wantCycle = ctx.controls[size_t(i)].cycle; }
  } else {
    wantFire = c.fireHeld;  // AI: the decision of the last tick stays active until the next one
    aiDecide(ctx, i, dt, &wantFire, &wantCycle);
  }
  c.firePrev = c.fireHeld;
  c.fireHeld = wantFire;
  if (wantCycle) cycleWeapon(i);
  if (c.fireHeld) fire(ctx, i);
}

void CombatWorld::cycleWeapon(int i) {  // 0x51248
  CombatState& c = combat[size_t(i)];
  auto hasA = [&] { return c.load.weaponA >= 0; };
  auto hasB = [&] { return c.load.weaponB >= 0; };
  auto booster = [&] { return c.load.booster >= 0 ? 3 : 0; };
  switch (c.selected) {
    case 0: c.selected = hasA() ? 1 : hasB() ? 2 : booster(); break;
    case 1: c.selected = hasB() ? 2 : booster(); break;
    case 2: c.selected = booster(); break;
    default: c.selected = 0; break;
  }
}

void CombatWorld::updateLock(const CombatContext& ctx, int i) {  // 0x50FC4..0x510C7 and the cone test 0x140BF
  CombatState& c = combat[size_t(i)];
  c.lockTarget = -1;
  const bool human = ctx.human.size() > size_t(i) && ctx.human[size_t(i)];
  if (human && c.selected != 0 && c.lockout > 0) return;  // 0x50FDA: no lock during the first 15 s
  int id = -1;
  if (c.selected == 0) id = kBlaster; else if (c.selected == 1) id = c.load.weaponA; else if (c.selected == 2) id = c.load.weaponB;
  if (id < 0) return;
  int cone = table_->w[size_t(id)].cone;
  if (cone == 0) return;
  if (c.load.wideLock) cone *= 2;
  const ShipState& me = *ctx.ships[size_t(i)];
  const double sn = std::sin(cone / 65536.0 * kTau), cs = std::cos(cone / 65536.0 * kTau);
  double origin[3];
  world(me, refs_[size_t(i)].head, origin);
  double best = 1e300;
  for (int j = 0; j < int(ctx.ships.size()) && j < 10; ++j) {
    if (j == i || !ctx.ships[size_t(j)] || ctx.ships[size_t(j)]->wrecked) continue;
    const ShipState& o = *ctx.ships[size_t(j)];
    const double d[3] = {o.x - origin[0], o.y - origin[1], o.z - origin[2]};
    double l[3];
    toLocal(me, d, l);
    const double ext = o.extent;
    if (l[2] + ext < kLockMinZ) continue;
    if (l[2] * sn + l[0] * cs + ext < 0 || l[2] * sn - l[0] * cs + ext < 0) continue;
    if (l[2] * sn + l[1] * cs + ext < 0 || l[2] * sn - l[1] * cs + ext < 0) continue;
    const double dist = len3(d);
    if (dist > kLockRange || dist >= best) continue;
    best = dist;
    c.lockTarget = j;
  }
}

void CombatWorld::aiDecide(const CombatContext& ctx, int i, double dt, bool* fireOut, bool* cycleOut) {  // RaceAIControl tail 0x5154F..0x515D0
  CombatState& c = combat[size_t(i)];
  c.aiClock += dt;
  *cycleOut = false;
  if (c.aiClock < 1.0 / 30.0) return;  // the original decides once per frame (about 30 per second)
  c.aiClock = 0;
  bool fire = false, cycle = false;
  const bool done = ctx.finished.size() > size_t(i) && ctx.finished[size_t(i)];
  if (!done) {
    if ((rng_.next() & 0xFFFF) <= 0x2000) cycle = true;               // 0x5155E: 1/8 per decision
    else if (c.selected != 3 && c.lockTarget >= 0) fire = true;       // 0x51570..0x5157E
  }
  if (c.lockTarget >= 0 && c.lockTarget == ctx.humanShip && ctx.humanShip >= 0 && ctx.shipClass.size() > size_t(i))
    emitCue(ctx, i, cues::aiTargetsHuman(ctx.shipClass[size_t(i)]));  // 0x51588: the pilot taunts the player it has locked
  *fireOut = fire;
  *cycleOut = cycle;
}

void CombatWorld::fire(const CombatContext& ctx, int i) {  // RaceSlot draw handler 0x50C5E..0x50D99
  CombatState& c = combat[size_t(i)];
  const auto& tb = *table_;
  ShipState& s = *ctx.ships[size_t(i)];
  const bool human = i == ctx.humanShip;
  if (c.selected == 0) {
    if (c.cooldown > 0) return;
    const WeaponDef& w = tb.w[kBlaster];
    if (c.energy[0] + 1e-9 < w.cost) return;
    c.energy[0] -= w.cost;
    c.cooldown = c.load.fastRecharge ? 0.3 : 0.5;  // 0x1F4 ms / 0x12C ms
    launch(ctx, i, kBlaster);
    return;
  }
  if (c.selected == 3) {  // booster switch: edge triggered (0x50CBF: [+0x3D] = previous state)
    if (c.firePrev) return;
    if (c.boosterFuel <= 0) return;
    s.boosterOn = !s.boosterOn;
    if (s.boosterOn) { const double p[3] = {s.x, s.y, s.z}; emitFx(12, i, p); }
    return;
  }
  int& weapon = c.selected == 1 ? c.load.weaponA : c.load.weaponB;
  int& ammo = c.selected == 1 ? c.load.ammoA : c.load.ammoB;
  double& energy = c.energy[c.selected];
  if (weapon < 0) return;
  const WeaponDef& w = tb.w[size_t(weapon)];
  if (energy + 1e-9 < w.cost) return;
  if (ammo == 0) return;
  const int fired = weapon;
  if (ammo > 0 && --ammo == 0) {  // last round: the weapon is gone and the selection moves on (0x50D1A..0x50D3A)
    weapon = -1;
    cycleWeapon(i);
    if (human) emitCue(ctx, i, cues::outOfAmmo);
  }
  energy -= w.cost;
  launch(ctx, i, fired);
}

void CombatWorld::launch(const CombatContext& ctx, int i, int weapon) {  // launchers 0x5C34D..0x5D5D5
  CombatState& c = combat[size_t(i)];
  ShipState& s = *ctx.ships[size_t(i)];
  const bool human = i == ctx.humanShip;
  const double ownerPos[3] = {s.x, s.y, s.z};
  ++c.shotsFired;
  auto spawn = [&](int kind, const double* pos) -> Projectile& {
    projectiles.emplace_back();
    Projectile& p = projectiles.back();
    p.kind = kind; p.owner = i; p.target = c.lockTarget;
    for (int k = 0; k < 3; ++k) { p.pos[k] = pos[k]; p.prev[k] = pos[k]; }
    for (int k = 0; k < 9; ++k) p.m[k] = s.m[k];
    p.radius = radius_[size_t(kind)];
    return p;
  };
  if (human && cues::launchCue(weapon) >= 0) emitCue(ctx, i, cues::launchCue(weapon));
  switch (weapon) {
    case kBlaster:
    case kDisrupter: {
      const double* refsB[2] = {refs_[size_t(i)].lasl, refs_[size_t(i)].lasr};
      for (const double* r : refsB) {
        double pos[3];
        world(s, r, pos);
        Projectile& p = spawn(weapon, pos);
        p.life = 5.0;  // [+4] = 0x1388 ms
        p.speed = kBeamSpeed;
        if (p.target >= 0 && ctx.ships[size_t(p.target)]) {  // 0x5C457..0x5C476: aimed at the locked ship at once
          const ShipState& t = *ctx.ships[size_t(p.target)];
          const double d[3] = {t.x - pos[0], t.y - pos[1], t.z - pos[2]};
          setForward(p.m, d);
        }
      }
      emitFx(4, i, ownerPos);
      break;
    }
    case kSmoker: {
      double pos[3];
      world(s, refs_[size_t(i)].smok, pos);
      addExplosion(pos, 1);
      emitFx(5, i, ownerPos);
      break;
    }
    case kMiniMines: {
      static const double kOff[4][2] = {{4880, 4880}, {-4880, -4880}, {-4880, 4880}, {4880, -4880}};  // table 0x5D5D6
      for (const auto& o : kOff) {
        const double r[3] = {o[0], o[1], -9760};
        double pos[3];
        world(s, r, pos);
        Projectile& p = spawn(kMiniMines, pos);
        p.target = -1;
        p.life = 10.0;  // [+4] = 0x2710 ms
        p.speed = 0;
      }
      emitFx(7, i, ownerPos);
      break;
    }
    default: {  // homing missiles
      double pos[3];
      world(s, refs_[size_t(i)].weap, pos);
      Projectile& p = spawn(weapon, pos);
      p.life = -1;
      const double add = (weapon == kAmbler || weapon == kHyperNeuro) ? 0x45D30 : weapon == kScrambler ? 0x1174C : kMissileAccel;
      p.speed = s.speed + add;
      emitFx(5, i, ownerPos);
      break;
    }
  }
}

void CombatWorld::stepProjectile(const CombatContext& ctx, Projectile& p, double dt) {
  p.age += dt;
  if (p.life >= 0) {
    p.life -= dt;
    if (p.life < 0) { p.alive = false; return; }
  }
  if (p.age > 90) { p.alive = false; return; }  // safety net, the original has no limit for missiles
  for (int k = 0; k < 3; ++k) p.prev[k] = p.pos[k];
  const bool beam = p.kind == kBlaster || p.kind == kDisrupter;
  if (p.kind == kMiniMines) {
    for (int j = 0; j < int(ctx.ships.size()) && j < 10; ++j) {
      if (!ctx.ships[size_t(j)] || (j == p.owner && p.age < 1.0)) continue;
      const ShipState& s = *ctx.ships[size_t(j)];
      if (s.wrecked || !s.hasBox || !pointNearShip(s, p.pos, p.radius)) continue;
      hitShip(ctx, p, j);
      addExplosion(p.pos, 0);
      p.alive = false;
      return;
    }
    return;
  }
  // heading
  const ShipState* tgt = (p.target >= 0 && p.target < int(ctx.ships.size())) ? ctx.ships[size_t(p.target)] : nullptr;
  if (beam) {
    if (tgt) {  // 0x5C8C8..0x5C90E: while the locked ship is within ~36 degrees of the heading the beam is re-aimed at it
      const double d[3] = {tgt->x - p.pos[0], tgt->y - p.pos[1], tgt->z - p.pos[2]};
      const double l = len3(d);
      if (l > 1 && dot3(d, p.m + 6) / l > 0x3400 / 16384.0) setForward(p.m, d);
    }
  } else {
    p.speed = std::min(kMissileCap, p.speed + kMissileAccel * dt);  // 0x5D26F..0x5D29D
    if (tgt) {
      double d[3] = {tgt->x - p.pos[0], tgt->y - p.pos[1], tgt->z - p.pos[2]};
      const double l = len3(d);
      const bool home = p.kind != kScrambler || l <= kScramblerHomeRange;  // 0x5D3C9: the scrambler only homes when close
      if (home && l > 1) {
        for (double& x : d) x /= l;
        turnToward(p.m, d, kHomingRate * dt);
      }
    }
  }
  const double* f = p.m + 6;
  const double step = p.speed * dt;
  double np[3] = {p.pos[0] + f[0] * step, p.pos[1] + f[1] * step, p.pos[2] + f[2] * step};
  // ships (message 0x106 / the ray test 0x139AD)
  double bestT = 2.0;
  int bestShip = -1;
  for (int j = 0; j < int(ctx.ships.size()) && j < 10; ++j) {
    if (!ctx.ships[size_t(j)] || j == p.owner) continue;  // INFERRED: projectiles never hit their owner's ship
    const ShipState& s = *ctx.ships[size_t(j)];
    const double t = segmentVsShip(s, p.pos, np, beam ? 0.0 : p.radius);
    if (t >= 0 && t < bestT) { bestT = t; bestShip = j; }
  }
  // track (wall message 0x107; beams only stop when they hit a ship)
  double wallT = 2.0;
  if (!beam && step > 0) {
    const double lo[3] = {-p.radius, -p.radius, -p.radius}, hi[3] = {p.radius, p.radius, p.radius};
    const double I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (ctx.scene) {
      ShipHit h = sweepShipBox(*ctx.scene, p.pos, I, lo, hi, f, step);
      if (h.hit) wallT = h.dist / step;
    }
  }
  if (!beam) {
    for (const ShipState* o : ctx.obstacles) {
      const double t = segmentVsShip(*o, p.pos, np, p.radius);
      if (t >= 0 && t < wallT) wallT = t;
    }
  }
  if (bestShip >= 0 && bestT <= wallT) {
    for (int k = 0; k < 3; ++k) p.pos[k] += (np[k] - p.pos[k]) * std::min(bestT, 1.0);
    if (beam) beamHit(ctx, p, bestShip);
    else { hitShip(ctx, p, bestShip); addExplosion(p.pos, 0); }
    ++combat[size_t(p.owner)].hitsDealt;
    p.alive = false;
    return;
  }
  if (wallT <= 1.0) {
    for (int k = 0; k < 3; ++k) p.pos[k] += (np[k] - p.pos[k]) * wallT;
    addExplosion(p.pos, 0);
    emitFx(9, p.owner, p.pos);
    p.alive = false;
    return;
  }
  for (int k = 0; k < 3; ++k) p.pos[k] = np[k];
}

void CombatWorld::hitShip(const CombatContext& ctx, const Projectile& p, int victim) {  // victim side of message 0x106 (0x5082F..0x50A57)
  ShipState& v = *ctx.ships[size_t(victim)];
  const WeaponDef& w = table_->w[size_t(p.kind)];
  int fx = 9;
  switch (p.kind) {
    case kAmbler: fx = 16; v.halfCapTime = 10.0; break;      // [+0x28] = 0x2710: speed cap halved
    case kBomber: fx = 13; v.forceThrottleTime = 4.0; break; // [+0x2A] = 0xFA0: throttle jammed on
    case kHyperNeuro: fx = 15; v.hyperTime = 10.0; break;    // [+0x2C] = 0x2710: hypersensitive steering
    case kScrambler: fx = 14; break;
    case kMiniMines: if (victim == ctx.humanShip) emitCue(ctx, victim, cues::mineHit); break;
    default: break;
  }
  const double vp[3] = {v.x, v.y, v.z};
  emitFx(fx, victim, vp);
  if (p.owner == ctx.humanShip && ctx.humanShip >= 0 && ctx.shipClass.size() > size_t(victim)) emitCue(ctx, victim, cues::hitByHuman(ctx.shipClass[size_t(victim)]));
  shipDamage(v, w.damageA, w.damageB);
}

void CombatWorld::beamHit(const CombatContext& ctx, const Projectile& p, int victim) {  // message 0x202 (0x50651..0x506FA)
  ShipState& v = *ctx.ships[size_t(victim)];
  const bool human = ctx.human.size() > size_t(victim) && ctx.human[size_t(victim)];
  if (human) { v.boostTime = 0; v.slowTime = 0.25; }  // 0x50660: the boost is lost, steering jolts for 0xFA ms
  const double vp[3] = {v.x, v.y, v.z};
  if (p.kind == kDisrupter) {
    v.reverseTime = 5.0;  // [+0x26] = 0x1388: steering and pitch reversed
    emitFx(11, victim, vp);
    return;
  }
  const WeaponDef& w = table_->w[kBlaster];
  const double mul = difficulty == 2 ? 4.0 : 2.0;  // 0x5C0F9: the table value is shifted left by 1 (by 2 at the hardest level)
  shipDamage(v, w.damageA * mul, w.damageB * mul);
  emitFx(10, victim, vp);
  if (victim == ctx.humanShip) emitCue(ctx, victim, cues::underFire);
}

void CombatWorld::stepPickups(const CombatContext& ctx) {  // bonus object (0x42BD5): touching it consumes it
  for (Pickup& pk : pickups) {
    if (!pk.alive) continue;
    for (int j = 0; j < int(ctx.ships.size()) && j < 10; ++j) {
      if (!ctx.ships[size_t(j)]) continue;
      const ShipState& s = *ctx.ships[size_t(j)];
      if (s.wrecked || !s.hasBox || !pointNearShip(s, pk.pos, kPickupHalf)) continue;
      pk.alive = false;
      applyPickup(ctx, j, pk.type);
      break;
    }
  }
  pickups.erase(std::remove_if(pickups.begin(), pickups.end(), [](const Pickup& p) { return !p.alive; }), pickups.end());
}

void CombatWorld::applyPickup(const CombatContext& ctx, int ship, int type) {  // ship message 0x106 with a bonus object (0x50724..0x5082B)
  ShipState& s = *ctx.ships[size_t(ship)];
  CombatState& c = combat[size_t(ship)];
  const double p[3] = {s.x, s.y, s.z};
  switch (type) {
    case 0: s.damageA = 0; emitFx(6, ship, p); break;                          // engine repaired
    case 1: s.damageB = 0; emitFx(6, ship, p); break;                          // steering repaired
    case 2: c.boosterFuel = 1.0; emitFx(6, ship, p); break;                    // booster tank refilled
    case 3: s.reverseTime = 5.0; emitFx(11, ship, p); break;                   // reversed controls for 5 s
    case 4: c.credits += 50; emitFx(6, ship, p); break;                        // +50 credits
    case 5: s.boosterFreeTime = 5.0; emitFx(12, ship, p); break;               // free booster for 5 s
    default: emitFx(6, ship, p); break;
  }
}

}  // namespace slip
