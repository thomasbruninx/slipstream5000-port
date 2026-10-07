// Runtime representation built from original assets. Independent of SDL and of the renderer:
// both the compatibility (software) renderer and a future modern renderer consume it.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "original_formats/game_data.hpp"
#include "original_formats/formats.hpp"
#include "original_formats/track.hpp"

namespace slip {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

struct Texture {
  int w = 0, h = 0;
  std::vector<uint8_t> index;  // palette indices; 0 is treated as transparent when sampled
};

struct SurfaceMaterial {
  std::string name;
  uint8_t palStart = 0, palEnd = 0;  // shade ramp for untextured surfaces
  int texture = -1;                   // index into Scene::textures, or -1
  bool invisible = false;             // "Dummy" helper surfaces (palette index 250): not drawn (INFERRED)
};

struct MeshPoly {
  uint32_t first = 0;   // index into Mesh::verts / Mesh::uv
  uint16_t count = 0;
  int32_t material = -1;  // Scene::materials index (-1 = default grey)
  bool hasUV = false;
  uint16_t vis = 0xFFFF;  // visibility class bits (0xFFFF = always); see Scene::visMaskAt
  bool hidden = false;    // flat polygon that blocks the road corridor (portal/cap): not drawn (INFERRED)
  bool scenery = false;   // belongs to a TRD scenery instance (not road geometry)
  bool backdrop = false;  // scenery whose volume contains road: track surfaces always draw over it (see research-log)
  Vec3 normal;           // unit length, +y up (derived from the stored 2.14 normal)
};

struct Mesh {
  std::vector<Vec3> verts;
  std::vector<float> uv;  // 2 floats per vertex, 1.0 = one texture repeat (stored value / 0x4000)
  std::vector<MeshPoly> polys;
  void clear() { verts.clear(); uv.clear(); polys.clear(); }
};

struct Scene {
  Palette palette;
  std::vector<Texture> textures;
  std::vector<SurfaceMaterial> materials;
  Mesh track;                         // world coordinates minus `origin`
  std::array<double, 3> origin{};     // world = origin + mesh coordinate (keeps floats small)
  std::array<Mesh, 10> shipMeshes;    // model space, scaled by shipScale, centred on the ART origin
  std::array<std::array<double, 3>, 10> startPos{};  // world coordinates of the start grid
  Track track_data;                   // raw parsed data (for tools / physics)
  float shipScale = 2.0f;             // display scale applied to ART/SHP ship models
  int trackIndex = 0;
  std::string trackName;
  size_t scenerySkipped = 0;          // scenery instances whose shape could not be loaded

  // Height of the highest up-facing track surface at (x,z) (world coordinates) that lies at or
  // below yHint+margin. Returns false if there is none.
  // Visibility mask of the track record the point lies in (CONFIRMED mechanism: DoGame3D/0x39C58 sets
  // [0x33EF0] = record[+0x16] & 0x5F; records and scenery are drawn only if their class bits intersect it).
  // Points outside every piece use the nearest piece (INFERRED); 0xFFFF if the track has no pieces.
  uint16_t visMaskAt(double wx, double wy, double wz) const;
  struct PieceBox { float lo[3], hi[3]; uint16_t flags; };
  std::vector<PieceBox> pieceBoxes;
  void hidePortalPolys();
  bool floorHeight(double x, double z, double yHint, double margin, double* y) const;
  void buildFloorIndex();

 private:
  struct FloorTri {
    float a[3], b[3], c[3];
  };
  std::vector<FloorTri> floorTris_;
  std::unordered_map<int64_t, std::vector<uint32_t>> floorGrid_;
  static constexpr double kCell = 150000.0;
};

// Builds a scene from the original data. Returns false (with *error) on failure.
bool buildScene(const GameData& data, int trackIndex, Scene* scene, std::string* error, float shipScale = 2.0f);

// Loads a single shape into a stand-alone preview scene (palette from track 1, CARS.MAT).
bool buildShapePreview(const GameData& data, const std::string& shapeName, Scene* scene, std::string* error);

}  // namespace slip
