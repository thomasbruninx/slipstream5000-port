// Compatibility renderer: a small CPU rasteriser (z-buffer, perspective-correct textures,
// flat shade-ramp colouring) sufficient to inspect original geometry/textures/placement.
// It consumes game/Scene only; a future modern renderer can replace it.
#pragma once
#include <cstdint>
#include <vector>

#include "game/scene.hpp"

namespace slip {

struct Camera {
  double pos[3] = {0, 0, 0};
  float yaw = 0;     // radians, 0 = +z, positive turns towards +x
  float pitch = 0;   // radians, positive looks up (+y)
  float fovY = 1.0f; // radians
  float nearPlane = 600.0f;
};

// world = pos + R * meshVertex (R row-major 3x3). Identity for the track.
struct MeshTransform {
  double pos[3] = {0, 0, 0};
  float R[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// A mesh that casts a shadow onto up-facing track polygons (the ships). `piece` = track piece it stands on (-1 = none).
struct ShadowCaster {
  const Mesh* mesh = nullptr;
  MeshTransform xf;
  int piece = -1;
  std::vector<Vec3> world;  // vertices relative to the scene origin, filled lazily per frame by the renderer
};

struct RenderStats {
  uint32_t polysSubmitted = 0, polysDrawn = 0, trisRastered = 0;
};

class SoftwareRenderer {
 public:
  void resize(int w, int h);
  int width() const { return w_; }
  int height() const { return h_; }
  void beginFrame(const Camera& cam, uint32_t skyColor, uint32_t groundColor);
  // `only` restricts drawing to the listed polygon indices. `item` >= 0 selects painter's mode: fragments of a
  // new item overwrite earlier items unconditionally (items are drawn far-to-near) and are depth-tested only
  // against fragments of the same item (the original has no global z-buffer).
  void drawMesh(const Scene& scene, const Mesh& mesh, const MeshTransform& xf, const std::vector<uint32_t>* only = nullptr, int item = -1);
  const uint32_t* pixels() const { return color_.data(); }  // 0xFFRRGGBB
  uint32_t* framebuffer() { return color_.data(); }
  const RenderStats& stats() const { return stats_; }
  uint16_t visMask = 0xFFFF;   // camera record class bits; 0xFFFF = draw everything (see Scene::visMaskAt)
  // Scenery projected-size cull (CONFIRMED formula at 0x1A9A4/0x37931: radius*256/z must exceed the detail
  // threshold 0x20/0x14/0x0A/5 for detail 0..3; objects at or in front of the near distance are always drawn).
  float minScenerySize = 5.0f;
  bool sceneryBigEnough(const double centerWorld[3], float radius) const;
  bool visAllows(uint16_t vis) const;
  // Portal visibility (CONFIRMED structure, 0x39C58/0x39E67/0x3A065): starting from the piece containing the
  // camera, walk through portal polygons (TRC polygon flag bit 0) that face the camera; a neighbour is visited
  // with the screen rectangle of the portal intersected with the current window. Only visited pieces are drawn,
  // each clipped to its window. Windows are rectangles here (the original clips to its own window too).
  struct WinRect { int x0 = 0, y0 = 0, x1 = -1, y1 = -1; bool vis = false; };
  bool portalCulling = true;
  void computePortalVisibility(const Scene& scene);  // call after beginFrame()
  // True unless all eight corners of the world-minus-origin box lie outside one frustum plane (the engine's
  // bounding-volume test 0x36695 / outcodes 0x198B5).
  bool boxInFrustum(const Scene& scene, const float lo[3], const float hi[3]) const;
  WinRect unionWindow() const { return union_; }   // union of all portal-reached windows ([0x33EB4..0x33EC0])
  void setSceneryWindow(const WinRect* w) { sceneryWin_ = w; }  // clip window for polygons that belong to no piece
  void setPieceWindow(size_t i, const WinRect& w) { if (i < pieceWin_.size()) pieceWin_[i] = w; }
  const std::vector<WinRect>& pieceWindows() const { return pieceWin_; }
  uint32_t animTimer = 0;   // animation clock in 2.14 seconds ([0x3F078]); the viewer advances it every frame
  // Ship shadows (CONFIRMED mechanism, see docs/research-log.md): the caster's polygons are projected along the light
  // direction (0,-1,0) onto the receiving polygon's plane and filled with the receiver's shadow colour inside it.
  std::vector<ShadowCaster> shadowCasters;
  bool shadows = true;
  bool wireframe = false;
  bool cullBackfaces = false;  // uses the stored face normals (INFERRED that the original culls too)
  float farPlane = 9.0e6f;

 private:
  struct VV {
    float x, y, z, u, v;
  };
  void rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, int flatIndexNy, uint8_t layer, int forceIdx = -1);
  WinRect union_;   // bounding rectangle of the front-facing extent-frame polygons (flag 0x8) of all reached pieces ([0x33EB4..0x33EC0])
  // Bounding rectangle of the polygon after near-plane clipping and clipping against `win` (the engine's 0x1A6F5:
  // transform, clip, bounding box). False if nothing is left.
  bool polyRect(const Scene& scene, const MeshPoly& p, const float cp[3], const WinRect& win, WinRect* out) const;
  // Flat-colour palette index of a polygon (original lighting law) and the panel-line detail drawn over it.
  int flatIndex(const SurfaceMaterial* mat, int upLight) const;
  struct P3 { float x, y, z; };
  void drawLine3D(P3 a, P3 b, uint32_t color);  // camera-space line, near-clipped, depth-tested against the current item
  // `farBranch`: 0x3F3C4 above 0xA6CC0 units projects the base vertices and takes the midpoints in screen space (2D)
  std::vector<VV> detailPoints(const PanelDetail& d, const std::vector<VV>& tv, const MeshPoly& p, bool allowScreenMid = false) const;
  void fillIdxPoly(const Scene& scene, const SurfaceMaterial* mat, const std::vector<VV>& pts, const std::vector<uint16_t>& idx, int colorIdx);
  void drawRoadFloor(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawChase(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawCageLines(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawFloorDetail(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawPanelLines(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat, int flatIdx);
  void addExtent(const Scene& scene, size_t piece, const float cp[3]);
  const WinRect* sceneryWin_ = nullptr;
  std::vector<WinRect> pieceWin_;  // empty = no portal culling this frame
  int sx0_ = 0, sy0_ = 0, sx1_ = 0, sy1_ = 0;  // raster scissor (inclusive)
  int w_ = 0, h_ = 0;
  std::vector<uint32_t> color_;
  std::vector<int32_t> recvBuf_;   // id of the receiver polygon (and sub-polygon) that last wrote the pixel, 0 = none
  int recvId_ = 0;                 // id written by rasterTri for the polygon being drawn
  int shadowRecv_ = 0;             // >0: rasterTri paints only pixels owned by this receiver, without depth test
  void drawShadowsOn(const Scene& scene, const MeshPoly& rp, int baseId, int sub, int colorIdx);
  std::vector<int32_t> itemBuf_;   // painter's mode: id of the item that last wrote the pixel
  int curItem_ = -1;
  std::vector<uint8_t> backdrop_;  // per pixel: 0 track, 1 scenery, 2 backdrop scenery
  std::vector<float> depth_;  // stores 1/z (larger = nearer)
  Camera cam_;
  float right_[3], up_[3], fwd_[3];
  float focal_ = 1;
  RenderStats stats_;
};

}  // namespace slip
