// The textured globe of the original (GLOBE.SHP: a 128 polygon sphere whose polygons use the four materials Earth1..4 = EARTH1..4.SPR, plus FLAG.SHP for
// the track marker). Used by the credits screen (CreditsScreen 0x56766: the ball flies away to the upper right while its texture scrolls, the model is
// drawn by ShapeDraw with the texture phase 0x2C420 set by 0x2D98A) and by the track choice. Rendered with a small affine texture mapper into the
// front end's 320x200 buffer: perspective x = cx + X * F / Z with F = 256, the 3D engine's default zoom (0x18108).
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "original_formats/formats.hpp"
#include "original_formats/game_data.hpp"

namespace slip {

class Globe {
 public:
  bool load(const GameData& data);
  bool loaded() const { return !shape_.polys.empty(); }
  // rot = model -> view rotation (row major), uOffset = texture phase in 1/0x4000 repeats, shade: 0 = unshaded, 1 = light from the upper left
  // overrideTex: texture for all four materials (the credits use SPDTEST.MAT where Earth1..4 all name SOFTLOGO.SPR); null = EARTH1..4 (GLOBE.MAT)
  void draw(uint32_t* buf, int w, int h, const Palette& pal, double cx, double cy, double dist, const double rot[9], int uOffset, bool shade, const Sprite* overrideTex = nullptr) const;
  // Marker (FLAG.SHP) standing on the sphere at model direction `dir` (unit vector); drawn with the same transform
  void drawFlag(uint32_t* buf, int w, int h, const Palette& pal, double cx, double cy, double dist, const double rot[9], const double dir[3]) const;
  double radius() const { return double(shape_.radius); }

 private:
  struct Tex { int w = 0, h = 0; std::vector<uint8_t> px; };
  void drawShape(const Shape& s, const std::vector<Tex*>& texOf, uint32_t* buf, int w, int h, const Palette& pal, double cx, double cy, double dist, const double rot[9], const double origin[3],
                 int uOffset, bool shade, bool texturedFlat, uint8_t flatColor) const;
  Shape shape_, flag_;
  Tex flagTex_;
  std::array<Tex, 4> tex_;
  std::vector<Tex*> texOf_;
};

}  // namespace slip
