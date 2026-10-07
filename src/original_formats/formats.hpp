// Parsers for original Slipstream 5000 data formats. These only decode bytes into plain
// structures; interpretation (scale, orientation, lighting) happens in game/ and renderer/.
// Each format is documented in docs/formats/*.md including confidence levels.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "original_formats/game_data.hpp"

namespace slip {

struct Vec3i {
  int32_t x = 0, y = 0, z = 0;
};

// ---- .PAL / trailing palettes (CONFIRMED) ------------------------------------------------
struct Palette {
  std::array<uint32_t, 256> rgba{};  // 0xAABBGGRR-agnostic: stored as 0x00RRGGBB
  int first = 0, count = 0;
};
std::optional<Palette> parsePalette(const uint8_t* p, size_t n);
// Fills indices not covered by the file with a placeholder (SPECULATIVE: real values come
// from the engine's default palette, not from the .PAL file).
void fillDefaultTail(Palette& pal);

// ---- .SPR (CONFIRMED structure; header words +4/+6/+8 UNKNOWN) -------------------------------
struct Sprite {
  int w = 0, h = 0;
  uint16_t hdr4 = 0, hdr6 = 0, hdr8 = 0;
  std::vector<uint8_t> pixels;  // palette indices, row-major
  std::optional<Palette> palette;  // embedded trailing palette (backgrounds, portraits)
};
std::optional<Sprite> parseSprite(const Bytes& b);

// ---- .MAT (CONFIRMED structure) ---------------------------------------------------------------
struct Material {
  std::string name;     // as stored (trimmed)
  uint8_t palStart = 0, palEnd = 0;  // shade ramp in the 256-colour palette
  std::string pattern;  // sprite file pattern ("chiclwa*.spr") or empty
  uint8_t raw[46]{};
};
struct MaterialSet {
  std::vector<Material> mats;
  int find(const std::string& name) const;  // case-insensitive, -1 if absent
  void append(const MaterialSet& o) { mats.insert(mats.end(), o.mats.begin(), o.mats.end()); }
};
std::optional<MaterialSet> parseMaterials(const Bytes& b);

// ---- polygons shared by .SHP (and similar in .TRC) ----------------------------------------------
struct Polygon {
  std::vector<uint16_t> index;
  int16_t nx = 0, ny = 0, nz = 0;  // face normal, 2.14
  uint16_t material = 0;           // local material id (see owner's material table)
  uint16_t flags = 0;
  std::vector<std::array<int16_t, 3>> vnormals;   // optional per-vertex normals (2.14)
  std::vector<std::array<uint16_t, 2>> uv;        // optional (0x4000 = 1.0)
};
struct MaterialRef {
  std::string name;
  uint16_t id = 0;
};

// ---- .SHP (CONFIRMED structure) --------------------------------------------------------------------
struct Shape {
  int vertexShift = 0;  // stored vertices are multiplied by 2^shift (CONFIRMED by engine code)
  uint16_t flags = 0;
  int32_t radius = 0;
  int32_t bbox[6]{};  // xmin,xmax,ymin,ymax,zmin,zmax (not shifted)
  std::vector<Vec3i> verts;  // already multiplied by 2^vertexShift
  std::vector<Polygon> polys;
  std::vector<MaterialRef> materials;
  bool hasSortTree = false;
};
std::optional<Shape> parseShape(const Bytes& b);

// ---- .ART (CONFIRMED structure) --------------------------------------------------------------------------
struct ArtRefPoint {
  std::string tag;  // e.g. "main","weap"
  Vec3i pos;
};
struct ArtNode {
  std::string tag;
  Vec3i offset;                         // relative to parent
  std::vector<std::string> shapes;      // up to 8 body/damage variants (first = intact body)
  std::vector<std::string> shadowShapes;
  std::vector<ArtRefPoint> refPoints;
  int parent = -1;
};
struct Art {
  Vec3i minExt, maxExt;
  std::vector<ArtNode> nodes;  // nodes[0] is the root ("main")
};
std::optional<Art> parseArt(const Bytes& b);

// ---- MATHS.BIN ---------------------------------------------------------------------------------------------
struct MathsTables {
  std::vector<uint16_t> sin, asin, atan;
};
std::optional<MathsTables> parseMaths(const Bytes& b);

}  // namespace slip
