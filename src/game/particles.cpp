#include "game/particles.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

namespace {
// 0x4F208 (trail: grey smoke + fire), 0x4F15C (black smoke), 0x4F188 (black smoke, unused), 0x4F1B4 (grey smoke screen)
const EffectDesc kEffects[4] = {
    {PartFam::SmkGry, 0x1e8, 0x3d0, 0x3d0, 200, 0, true},
    {PartFam::SmkBlk, 0x1e8, 0xf40, 0x1128, 250, 0, false},
    {PartFam::SmkBlk, 0x5b8, 0x7a0, 0x7a0, 100, 0, false},
    {PartFam::SmkGry, 0x988, 0x1e80, 0x2620, 400, 0x16e0, false},
};
constexpr double kGravity = 0x3d50;  // 15696 units/s^2; terminal fall speed 0x6FB8 = 28600
}  // namespace

const EffectDesc& effectDesc(int type) { return kEffects[std::clamp(type, 0, 3)]; }

double Puff::size() const {
  if (fading()) return s1;
  const double a = s1 * 0.001;
  return s0 + (s1 - s0) * (a > 0 ? age / a : 1.0);
}

double Fireball::currentSize() const { return size * 0.25 + size * 0.75 * std::clamp(age / total, 0.0, 1.0); }

void ParticleSystem::reset(uint32_t seed) {
  puffs.clear(); emitters.clear(); fireballs.clear(); pieces.clear();
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
  for (int i = 0; i < count; ++i) {
    DebrisPiece d;
    for (int k = 0; k < 3; ++k) d.pos[k] = pos[k];
    d.set = set;
    d.piece = int(rnd() % 4);
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
  p.durF = d.durF * 0.001;
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
    const double a = p.s1 * 0.001;
    if (p.age >= a + p.durF) { p.alive = false; continue; }
    if (p.age < a) {  // first list: a random frame every few milliseconds
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
      p.frame = std::min(n - 1, int(n * (p.durF > 0 ? (p.age - a) / p.durF : 1.0)));
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
