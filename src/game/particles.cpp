#include "game/particles.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

namespace {
// 0x4F208 (trail: grey smoke + fire), 0x4F15C (black smoke), 0x4F188 (black smoke, unused), 0x4F1B4 (grey smoke screen)
const EffectDesc kEffects[4] = {
    {PartFam::SmkGry, 0x1e8, 0x3d0, 0x3d0, 0x64, 0x190, 0xc8, 0xc8, 0, true},
    {PartFam::SmkBlk, 0x1e8, 0xf40, 0x1128, 0xfa, 0x5dc, 0x12c, 0xfa, 0, false},
    {PartFam::SmkBlk, 0x5b8, 0x7a0, 0x7a0, 0x190, 0x4b0, 0x12c, 0x64, 0, false},
    {PartFam::SmkGry, 0x988, 0x1e80, 0x2620, 0x1f4, 0x7d0, 0x3e8, 0x190, 0x16e0, false},
};
constexpr double kGravity = 0x3d50;  // 15696 units/s^2; terminal fall speed 0x6FB8 = 28600
}  // namespace

const EffectDesc& effectDesc(int type) { return kEffects[std::clamp(type, 0, 3)]; }

double Puff::size() const {  // 0x27A9A: grows from s0 to s1; 0x27A52: after the hold it moves on to sEnd while the second list plays
  if (age < tGrow) return s0 + (s1 - s0) * (tGrow > 0 ? age / tGrow : 1.0);
  if (age < tGrow + tHold) return s1;
  return s1 + (sEnd - s1) * (tFade > 0 ? std::min(1.0, (age - tGrow - tHold) / tFade) : 1.0);
}

double Fireball::currentSize() const { return size * 0.25 + size * 0.75 * std::clamp(age / total, 0.0, 1.0); }

void ParticleSystem::reset(uint32_t seed) {
  puffs.clear(); emitters.clear(); fireballs.clear(); pieces.clear(); sparks.clear(); bangs.clear();
  rng_ = seed ? seed : 1;
  nextId_ = 1;
}

int ParticleSystem::addEmitter(int type, const double pos[3], double lifeSeconds, double riseMax) {
  Emitter e;
  e.id = nextId_++;
  e.type = std::clamp(type, 0, 3);
  for (int k = 0; k < 3; ++k) e.pos[k] = pos[k];
  e.life = lifeSeconds;
  e.timer = 0;
  e.riseMax = riseMax;
  emitters.push_back(e);
  return e.id;
}

Emitter* ParticleSystem::emitter(int id) {
  for (Emitter& e : emitters) if (e.id == id && e.alive) return &e;
  return nullptr;
}

void ParticleSystem::detach(int id) { if (Emitter* e = emitter(id)) e->attached = false; }

void ParticleSystem::fireball(const double pos[3], double size, double total) {
  Fireball f;
  for (int k = 0; k < 3; ++k) f.pos[k] = pos[k];
  f.size = size;
  f.total = total;
  f.frame = int(rnd() % 6);
  fireballs.push_back(f);
}

void ParticleSystem::debris(const double pos[3], int count, int set) {
  count = std::min(count, 4);  // 0x4F804: cmp ax, 4
  for (int i = 0; i < count; ++i) debrisPiece(pos, set, int(rnd() % 4));
}

void ParticleSystem::debrisPiece(const double pos[3], int set, int piece, bool dead) {
  {
    DebrisPiece d;
    d.dead = dead;
    for (int k = 0; k < 3; ++k) d.pos[k] = pos[k];
    d.set = set;
    d.piece = piece;
    // direction: the unit vector from the craft to the spawn point plus a jitter of up to +-2.0 per axis dominates, so the pieces fly in random directions
    double v[3];
    double l = 0;
    do {
      for (double& c : v) c = frand() * 2.0 - 1.0;
      l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    } while (l < 1e-3 || l > 1.0);
    const double speed = 0x29e5 + frand() * 0x45d3;  // 0x4F91B..0x4F925: 10725 .. 28600
    for (int k = 0; k < 3; ++k) d.vel[k] = v[k] / l * speed;
    for (int k = 0; k < 3; ++k) d.rate[k] = (0x3000 + frand() * 0x1000) / 65536.0 * (rnd() & 1 ? 1.0 : -1.0);  // 0x4F96C..0x4F99C (words 0x3000..0x3FFF per second)
    pieces.push_back(d);
  }
}

void ParticleSystem::scrape(const double pos[3], const double n[3], double shipSpeed, bool water, int surfaceMaterial) {
  const double kTurn = 6.283185307179586;
  // an orthonormal frame around the surface normal
  const double ref[3] = {std::fabs(n[0]) < 0.9 ? 1.0 : 0.0, std::fabs(n[0]) < 0.9 ? 0.0 : 1.0, 0.0};
  double u[3] = {n[1] * ref[2] - n[2] * ref[1], n[2] * ref[0] - n[0] * ref[2], n[0] * ref[1] - n[1] * ref[0]};
  const double ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
  for (double& c : u) c /= ul > 1e-9 ? ul : 1.0;
  const double v[3] = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};
  double origin[3];
  for (int k = 0; k < 3; ++k) origin[k] = pos[k] + n[k] * 0x1e8;  // 0x4FDF0: 488 units off the wall
  const double speed = std::max(0.0, shipSpeed) + 0x45d3;      // 0x4FE4D: the ship's speed plus 17875
  struct Batch { int count; int kind; double size; int material; };
  const Batch batches[2] = {{water ? 6 : 32, water ? 1 : 0, water ? 1.0 : 976.0, -1}, {6, 2, 244.0, surfaceMaterial}};  // 0x4FEBE / 0x4FEDF, 0x5003F
  for (const Batch& b : batches)
    for (int i = 0; i < b.count; ++i) {
      Spark s;
      s.kind = b.kind;
      s.material = b.material;
      s.life = (1500.0 + double(rnd() >> 2 & 0x3FFF) * 1500.0 / 65536.0) * 0.001;  // 0x28038: 0x5DC + 0..375 ms
      s.size = b.size * (0x2000 + double(rnd() & 0x1FFF)) / 0x4000;               // 0x28050: 0.5 .. 1.0 of the base size
      s.angle = frand();
      s.rate = (0x2000 + double(rnd() & 0x1FFF)) / 65536.0;                       // 0x2807F: turns per second
      const double a = (frand() * 2 - 1) * (0xa00 / 65536.0) * kTurn, c = (frand() * 2 - 1) * (0xa00 / 65536.0) * kTurn;  // 0x280B5 / 0x280CA: +-14 degrees about two axes
      for (int k = 0; k < 3; ++k) {
        s.pos[k] = origin[k];
        s.vel[k] = (n[k] * std::cos(a) * std::cos(c) + u[k] * std::sin(a) + v[k] * std::cos(a) * std::sin(c)) * speed;
      }
      sparks.push_back(s);
    }
}

int ParticleSystem::addBang(const double pos[3]) {
  Bang b;
  b.id = nextId_++;
  for (int k = 0; k < 3; ++k) b.pos[k] = pos[k];
  bangs.push_back(b);
  return b.id;
}

Bang* ParticleSystem::bang(int id) {
  for (Bang& b : bangs) if (b.id == id && b.alive) return &b;
  return nullptr;
}

void ParticleSystem::spawnPuff(Emitter& e) {
  const EffectDesc& d = effectDesc(e.type);
  Puff p;
  for (int k = 0; k < 3; ++k) p.pos[k] = e.pos[k];
  if (d.jitter > 0) {  // 0x27ED7 / 0x27EF3: lateral offset up to +-D[10] along the emitter's x and y
    const double jx = frand() * d.jitter * (rnd() & 2 ? -1 : 1), jy = frand() * d.jitter * (rnd() & 2 ? -1 : 1);
    for (int k = 0; k < 3; ++k) p.pos[k] += e.right[k] * jx + e.up[k] * jy;
  }
  p.fam = d.fam;
  p.s0 = d.s0 * (1.0 + (double((int(rnd() & 0xFFFF) ^ 0x8000) - 0x8000) / 32.0) / 16384.0);   // 0x27CCB: +-1/16
  p.s1 = d.s1 * (1.0 + (double((int(rnd() & 0xFFFF) ^ 0x8000) - 0x8000) / 16.0) / 16384.0);   // 0x27CAA: +-1/8
  p.sEnd = d.sEnd;
  p.tGrow = d.tGrow * 0.001; p.tHold = d.tHold * 0.001; p.tFade = d.tFade * 0.001;
  p.riseMax = e.riseMax;
  p.frame = int(rnd() % unsigned(kPartFrames[int(d.fam)][0]));
  puffs.push_back(p);
}

void ParticleSystem::step(double dt) {
  for (Emitter& e : emitters) {
    if (!e.alive) continue;
    e.life -= dt;
    if (e.life < 0) { e.alive = false; continue; }
    e.timer -= dt;
    if (e.timer < 0) {
      e.timer = effectDesc(e.type).period * 0.001;
      spawnPuff(e);
    }
  }
  emitters.erase(std::remove_if(emitters.begin(), emitters.end(), [](const Emitter& e) { return !e.alive; }), emitters.end());

  for (Puff& p : puffs) {
    p.age += dt;
    if (p.age >= p.total()) { p.alive = false; continue; }
    if (!p.fading()) {  // first list: a random frame every few milliseconds
      p.frameTimer += dt;
      const int n = kPartFrames[int(p.fam)][0];
      if (p.frameTimer >= kPartFramePeriod[int(p.fam)]) {
        p.frameTimer = 0;
        int f = int(rnd() % unsigned(n));
        if (f == p.frame) f = (f + 1) % n;
        p.frame = f;
      }
    } else {  // second list: in order over the fade time
      const int n = kPartFrames[int(p.fam)][1];
      p.frame = std::min(n - 1, int(n * (p.tFade > 0 ? (p.age - p.tGrow - p.tHold) / p.tFade : 1.0)));
    }
    if (p.riseMax > 0) {  // 0x27BC5: the speed grows by riseMax per second up to riseMax
      p.rise = std::min(p.riseMax, p.rise + p.riseMax * dt);
      p.pos[1] += p.rise * dt;
    }
  }
  puffs.erase(std::remove_if(puffs.begin(), puffs.end(), [](const Puff& p) { return !p.alive; }), puffs.end());

  for (Fireball& f : fireballs) {
    f.age += dt;
    if (f.age >= f.total) { f.alive = false; continue; }
    if (!f.fading()) {  // EXPL: a random frame every 100 ms
      f.frameTimer += dt;
      if (f.frameTimer >= kPartFramePeriod[int(PartFam::Expl)]) {
        f.frameTimer = 0;
        int n = int(rnd() % 6);
        if (n == f.frame) n = (n + 1) % 6;
        f.frame = n;
      }
    } else {  // EXPLF in order
      const double q = f.total * 0.25;
      f.frame = std::min(5, int(6 * (f.age - f.total * 0.75) / q));
    }
  }
  fireballs.erase(std::remove_if(fireballs.begin(), fireballs.end(), [](const Fireball& f) { return !f.alive; }), fireballs.end());

  for (Bang& b : bangs) {  // every 200 ms a fireball (size 0x16E0, 0x190 ms) at a random offset of up to +-0xB70 on each axis (0x4F6B0..0x4F715)
    b.life -= dt;
    if (b.life < 0) { b.alive = false; continue; }
    b.timer -= dt;
    if (b.timer < 0) {
      b.timer = 0.2;
      double p[3];
      for (int k = 0; k < 3; ++k) p[k] = b.pos[k] + (frand() * 2 - 1) * b.spread;
      fireball(p, 0x16e0, 0.4);
    }
  }
  bangs.erase(std::remove_if(bangs.begin(), bangs.end(), [](const Bang& b) { return !b.alive; }), bangs.end());

  for (Spark& s : sparks) {  // 0x50080: gravity, a step, vanishing when the track is touched
    s.age += dt;
    if (s.age > s.life) { s.alive = false; continue; }
    s.angle += s.rate * dt;
    s.vel[1] = std::max(s.vel[1] - kGravity * dt, -double(0x6fb8));
    double np[3];
    for (int k = 0; k < 3; ++k) np[k] = s.pos[k] + s.vel[k] * dt;
    if (blocked && blocked(s.pos, np)) { s.alive = false; continue; }
    for (int k = 0; k < 3; ++k) s.pos[k] = np[k];
  }
  sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return !s.alive; }), sparks.end());

  for (DebrisPiece& d : pieces) {
    d.age += dt;
    if (d.age > 10.0) { d.alive = false; continue; }  // 0x4FA36: cmp ax, 0x2710
    d.vel[1] = std::max(d.vel[1] - kGravity * dt, -double(0x6fb8));  // 0x4FA4D..0x4FA6F: -0x3D50 / s, floor 0xFFFF9048
    double np[3];
    for (int k = 0; k < 3; ++k) { np[k] = d.pos[k] + d.vel[k] * dt; d.ang[k] += d.rate[k] * dt; }
    if (blocked && blocked(d.pos, np)) { d.alive = false; continue; }  // 0x4FB02: TrackSlotCheckStatic
    for (int k = 0; k < 3; ++k) d.pos[k] = np[k];
  }
  pieces.erase(std::remove_if(pieces.begin(), pieces.end(), [](const DebrisPiece& d) { return !d.alive; }), pieces.end());
}

}  // namespace slip
