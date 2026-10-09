// Runtime representation built from original assets. Independent of SDL and of the renderer:
// both the compatibility (software) renderer and a future modern renderer consume it.
#pragma once
#include <map>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "original_formats/game_data.hpp"
#include "original_formats/formats.hpp"
#include "original_formats/track.hpp"
#include "game/panel_detail.hpp"

namespace slip {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

struct Texture {
  int w = 0, h = 0;
  std::vector<uint8_t> index;  // palette indices
  int transparent = -1;        // SPR header +8: the transparent colour index, -1 = opaque (CONFIRMED by correlation with the data)
};

struct SurfaceMaterial {
  std::string name;
  uint8_t palStart = 0, palEnd = 0;  // shade ramp for untextured surfaces
  int texture = -1;                   // index into Scene::textures, or -1
  // lighting coefficients from the MAT record (+0x16/+0x18/+0x1A/+0x1C -> runtime +0x1C/+0x20/+0x24/+0x28, 2.14),
  // CONFIRMED use in 0x1C4B6; ramp end already reduced by (1<<shift)-1 (0x18E80..0x18E99)
  int flag15 = 0;         // MAT +0x15 (s8) -> runtime +0x1A: non-zero materials are skipped by the transparency flag test (0x1A3D5)
  std::string upperName;  // normalised name for the load-time flag rules (TRNC*, WATE*)
  int fallbackColor = 0;  // MAT +0x12 -> runtime +0x50: colour used when ramp colour-1 would fall below the ramp
  int fixedLight = 0, ambientCoef = 0, diffuseCoef = 0, specularCoef = 0;
  bool invisible = false;             // "Dummy" helper surfaces (palette index 250): not drawn (INFERRED)
};

struct MeshPoly {
  uint32_t first = 0;   // index into Mesh::verts / Mesh::uv
  uint16_t count = 0;
  int32_t material = -1;  // Scene::materials index (-1 = default grey)
  bool hasUV = false;
  uint16_t pflags = 0;    // TRC polygon flag word (+8)
  int32_t piece = -1;     // owning track piece (road polygons), -1 for scenery
  bool portal = false;    // TRC polygon flag bit 0: opening to a neighbouring piece, never drawn (CONFIRMED)
  int32_t instance = -1;  // index into Mesh::instances (scenery), -1 = none
  uint16_t vis = 0xFFFF;  // visibility class bits (0xFFFF = always); see Scene::visMaskAt
  uint8_t detail = 0;     // panel-line template type (flag high byte 2..7) for flat polygons, else 0
  bool hidden = false;    // flat polygon that blocks the road corridor (portal/cap): not drawn (INFERRED)
  bool scenery = false;   // belongs to a TRD scenery instance (not road geometry)
  bool backdrop = false;  // scenery whose volume contains road: track surfaces always draw over it (see research-log)
  Vec3 normal;           // unit length, +y up (derived from the stored 2.14 normal)
};

struct MeshInstance {
  int group = -1;  // TRD group (BSP leaf)
  Vec3 center;    // relative to the mesh origin
  float radius = 0;
};
struct Mesh {
  std::vector<Vec3> verts;
  std::vector<float> uv;  // 2 floats per vertex, 1.0 = one texture repeat (stored value / 0x4000)
  std::vector<MeshPoly> polys;
  std::vector<MeshInstance> instances;
  void clear() { verts.clear(); uv.clear(); polys.clear(); instances.clear(); }
};

struct Billboard {  // scenery shape that always faces the camera (TRD entry +0x38 != 0); mesh in model space
  Mesh mesh;
  Vec3 pos;         // relative to Scene::origin
  float radius = 0;
  uint16_t vis = 0xFFFF;
  int group = -1;
};

struct BspNodeS {
  int axis = -1, point = 0, hi = -1, lo = -1, group = -1;
};

struct Scene {
  Palette palette;
  std::vector<Texture> textures;
  std::vector<SurfaceMaterial> materials;
  std::array<PanelDetail, 32> panelDetails;
  int sdOrangeMaterial = -1, sdFloorLightMaterial = -1, sdBlueMaterial = -1, sdRoadLineMaterial = -1;
  int sdCageMaterial = -1;
  int sdYellowMaterial = -1;  // "SDYellow" (colour of the floor border polygons)  // read from the user's executable
  std::vector<Billboard> billboards;
  Mesh track;                         // world coordinates minus `origin`
  std::array<double, 3> origin{};     // world = origin + mesh coordinate (keeps floats small)
  std::array<Mesh, 10> shipMeshes;    // model space, scaled by shipScale, centred on the ART origin
  // Projectile models: AIRMINE, AMBLER, BOMBER, FRAG, HYPER, SCRAMBLE, SEEKER (the .SHP names the launchers load at 0x5BF5E),
  // scaled like the ships. Index = Scene::weaponMeshIndex(weapon id).
  std::array<Mesh, 7> weaponMeshes;
  Mesh droneMesh;                     // DRONE.SHP: the little flying craft that appear during a race (0x4A240)
  float droneRadius = 3000;
  std::array<float, 7> weaponMeshRadius{};  // half the largest extent in unscaled model units (collision radius)
  static int weaponMeshIndex(int weapon) {  // 0x5BF34 table order
    switch (weapon) { case 11: return 0; case 6: return 1; case 10: return 2; case 2: case 3: return 3; case 8: return 4; case 7: return 5; case 4: case 5: return 6; default: return -1; }
  }
  std::array<std::array<double, 3>, 10> startPos{};  // world coordinates of the start grid
  Track track_data;                   // raw parsed data (for tools / physics)
  float shipScale = 1.0f;             // display scale applied to ART/SHP ship models (1 = the original size)
  int trackIndex = 0;
  std::string trackName;
  size_t scenerySkipped = 0;          // scenery instances whose shape could not be loaded

  // Height of the highest up-facing track surface at (x,z) (world coordinates) that lies at or
  // below yHint+margin. Returns false if there is none.
  // Visibility mask of the track record the point lies in (CONFIRMED mechanism: DoGame3D/0x39C58 sets
  // [0x33EF0] = record[+0x16] & 0x5F; records and scenery are drawn only if their class bits intersect it).
  // Points outside every piece of the portal graph give 0xFFFF (draw everything), like the original without a cell.
  uint16_t visMaskAt(double wx, double wy, double wz) const;
  struct PieceBox {
    float lo[3], hi[3];
    uint16_t flags;
    bool empty;
    bool graph = false;  // takes part in the portal graph (has links or is linked to)
    int link[3];      // neighbour piece index reached through portal polygon linkPoly (-1 = none)
    int linkPoly[3];  // index into track.polys
    float bb[6];      // record bounding box in world-minus-origin coordinates (CONFIRMED cell test, 0x38524)
    std::vector<std::array<float, 4>> planes;  // unit normal + offset: inside if n.p + d >= -256 (list-A polygons without flag 0x40)
  };
  // Cell test of 0x38524 (camera, slack 256) / 0x37FCC (slots, slack 512): bbox + inner side of every list-A polygon without flag 0x40.
  bool pieceContains(size_t i, const float p[3], float slack = 256.0f) const;
  std::vector<PieceBox> pieceBoxes;  // same order as Track::pieces
  // Painter's-order data (CONFIRMED structure, see research-log): TRD groups are the BSP leaves; each piece and
  // scenery instance belongs to one group.
  std::vector<BspNodeS> bsp;
  int groupCount = 0;
  bool portalOnly = false;  // see Track::portalOnly
  std::vector<int> pieceGroup;                      // per piece
  std::vector<std::vector<uint32_t>> piecePolys;    // per piece: indices into track.polys
  std::vector<std::vector<uint32_t>> instPolys;     // per Mesh::instances entry: indices into track.polys
  struct ItemRef { int kind; size_t idx; };  // 0 piece, 1 scenery instance (track.instances), 2 billboard
  std::vector<std::vector<GroupTreeNode>> groupTrees;
  std::map<uint32_t, ItemRef> entryItem;  // TRD entry offset -> item
  // Items of one group far-to-near for the (world-space) camera, following the group's draw-order tree.
  void groupOrder(int group, const double cam[3], std::vector<ItemRef>* out) const;
  // Groups far-to-near relative to the world-space camera position (BSP walk, 0x37601).
  void bspOrder(const double cam[3], std::vector<int>* groups) const;
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
bool buildScene(const GameData& data, int trackIndex, Scene* scene, std::string* error, float shipScale = 1.0f);
// Craft preview of the pilot information card: shipMeshes[0] with the materials of VIEW<n>.MAT.
bool buildShipPreview(const GameData& data, int ship, Scene* scene, float scale = 1.0f);

// Loads a single shape into a stand-alone preview scene (palette from track 1, CARS.MAT).
bool buildShapePreview(const GameData& data, const std::string& shapeName, Scene* scene, std::string* error);

}  // namespace slip
