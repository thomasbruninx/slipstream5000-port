// Particle effects of the original's "RaceBang" system (RaceBangInstall 0x4FD0D, effect table 0x4F14C, effect engine 0x274A0..0x27F60, animated sprite slots 0x1E774):
//  * smoke emitters: an emitter (life in ms, optionally following a ship / a missile) lays a smoke puff every `period` ms; a puff is an animated sprite that flickers
//    through its first frame list (SMKGRY / SMKBLK, random frame every few ms) while it grows from `s0` to `s1` (tGrow ms) and holds that size (tHold ms), then fades through the
//    second list (SMKGRYF / SMKBLKF, in order) for tFade ms while it moves on to `sEnd`. Puffs can rise (explosion smoke accelerates up to a maximum speed).
//  * "bangs" (0x4F639): after a craft is destroyed small fireballs flare up at random places around it every 200 ms for 2 s.
//  * fireballs (0x4F61B -> 0x1E774): EXPL frames flicker for 75 % of the life while the sprite grows from a quarter to its full size, then EXPLF fades for the last 25 %.
//  * debris pieces (0x4F7DE / 0x4F81F / 0x4F9EC): up to 4 pieces of the destroyed craft (R<n>FRG.. / DRFRG..) fly off in random directions at 10725..28600 units/s, tumble,
//    fall (gravity 15696 units/s^2, terminal speed 28600) and vanish after 10 s or when they touch the track.
// CONFIRMED: the effect table, the puff / fireball / debris rules above, the call sites (docs/weapons.md "Particle effects"). INFERRED: the unit of the sprite size (the sprite
// is a square of side 2 x size, 0x19ACC), the way the lateral jitter of the smoker is applied. The fields D[3..5] of the effect records are the grow / hold / fade times (0x27A52..0x27AEC).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace slip {

enum class PartFam { SmkGry = 0, SmkBlk, Expl, Fire };
constexpr int kPartFrames[4][2] = {{6, 9}, {6, 9}, {6, 6}, {4, 0}};      // frames of the first / second list per family
constexpr double kPartFramePeriod[4] = {0.030, 0.050, 0.100, 0.010};    // seconds between random frame changes (list header word 1)

struct EffectDesc {  // the records at 0x4F208, 0x4F15C, 0x4F188, 0x4F1B4: eleven dwords {size0, size1, sizeEnd, tGrow, tHold, tFade, listA, listB, flameList, period, jitter}
  PartFam fam;
  double s0, s1, sEnd;           // D[0], D[1], D[2]: the half size at the start, after growing and at the end of the fade
  double tGrow, tHold, tFade;    // D[3], D[4], D[5] in ms: growing (0x27A9A), holding (0x27AEC), fading (0x27A52)
  double period;                 // D[9]: ms between puffs
  double jitter;                 // D[10]: lateral jitter of the spawn position
  bool flame;                    // D[8]: a Fire sprite at the emitter
};
const EffectDesc& effectDesc(int type);  // 0 missile trail, 1 black smoke, 2 (unused in the original), 3 smoke screen

struct Puff {
  double pos[3] = {0, 0, 0};
  double age = 0;                 // seconds
  double s0 = 0, s1 = 0, sEnd = 0;          // half sizes (world units)
  double tGrow = 0, tHold = 0, tFade = 0;   // seconds
  double rise = 0, riseMax = 0;
  PartFam fam = PartFam::SmkGry;
  int frame = 0;
  double frameTimer = 0;
  bool alive = true;
  double size() const;            // half side in world units
  bool fading() const { return age >= tGrow + tHold; }
  double total() const { return tGrow + tHold + tFade; }
};

struct Emitter {
  int id = 0;
  int type = 0;
  double pos[3] = {0, 0, 0}, right[3] = {1, 0, 0}, up[3] = {0, 1, 0};
  double life = 0, timer = 0;     // seconds
  double riseMax = 0;
  bool attached = true;           // follows its owner (the game moves it each step)
  bool alive = true;
};

struct Fireball {
  double pos[3] = {0, 0, 0};
  double age = 0, total = 3.0, size = 9760;
  int frame = 0;
  double frameTimer = 0;
  bool alive = true;
  double currentSize() const;
  bool fading() const { return age >= total * 0.75; }
};

struct Spark {  // 0x27F72: a star of two crossing lines (or a single pixel when it is tiny), flying on a parabola
  double pos[3] = {0, 0, 0}, vel[3] = {0, 0, 0};
  double age = 0, life = 1.6;   // seconds
  double size = 488;            // half length of the lines in world units
  double angle = 0, rate = 0;   // turns, turns / s
  int kind = 0;                 // 0 wall spark (SPARK ramp), 1 water droplet (SPLASH ramp), 2 chip in the colour of the surface hit
  int material = -1;            // Scene::materials index whose ramp gives the colour (-1: the viewer picks by kind)
  bool alive = true;
};

struct Bang {  // RaceBangSlotControl 0x4F639 (installed by 0x4F414 when a craft is destroyed)
  int id = 0;
  double pos[3] = {0, 0, 0};
  double life = 2.0, timer = 0, spread = 0xb70;
  bool alive = true;
};

struct DebrisPiece {
  bool dead = false;              // a piece of a destroyed craft (R<n>FRG00..03) rather than of a hit one (FRG50..53)
  double pos[3] = {0, 0, 0}, vel[3] = {0, 0, 0};
  double ang[3] = {0, 0, 0}, rate[3] = {0, 0, 0};  // turns, turns / s
  int set = 0;                    // 0..9 = ship R<n>FRG pieces, 10 = drone
  int piece = 0;                  // 0..3
  double age = 0;
  bool alive = true;
};

class ParticleSystem {
 public:
  void reset(uint32_t seed);
  int addEmitter(int type, const double pos[3], double lifeSeconds, double riseMax = 0);
  Emitter* emitter(int id);
  void detach(int id);                                       // the owner is gone: the emitter stays where it is until its life ends
  void fireball(const double pos[3], double size = 9760, double total = 3.0);
  void debris(const double pos[3], int count, int set);      // count is limited to 4; pieces start at `pos`
  void debrisPiece(const double pos[3], int set, int piece, bool dead = false); // one piece (shape R<set>FRG5<piece>, FRG0<piece> when `dead`) at pos
  int addBang(const double pos[3]);   // a craft was destroyed: fireballs flare up around pos for 2 s
  Bang* bang(int id);
  // A ship hit a wall (message 0x107, RaceBang 0x4FD93 + 0x4FF2E): 32 sparks (6 water droplets) in a cone around the surface normal n plus 6 chips in the colour of the surface.
  void scrape(const double pos[3], const double n[3], double shipSpeed, bool water, int surfaceMaterial);
  void step(double dt);
  double rand01() { return frand(); }
  unsigned irand(unsigned n) { return rnd() % n; }

  std::vector<Puff> puffs;
  std::vector<Emitter> emitters;
  std::vector<Fireball> fireballs;
  std::vector<DebrisPiece> pieces;
  std::vector<Spark> sparks;
  std::vector<Bang> bangs;
  std::function<bool(const double* from, const double* to)> blocked;  // true when the segment touches the track (debris vanish)

 private:
  uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  double frand() { return double(rnd() & 0xFFFF) / 65536.0; }
  void spawnPuff(Emitter& e);
  uint32_t rng_ = 1;
  int nextId_ = 1;
};

}  // namespace slip
