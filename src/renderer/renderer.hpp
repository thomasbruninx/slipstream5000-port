// Renderer front end shared by every back end (software rasteriser, OpenGL, ...): camera set up, portal visibility, polygon selection and clipping, the original's lighting law
// and procedural panel detail (lamps, lines, floors). It turns the game's Scene into a stream of primitives (triangles, lines, sprites) and hands them to the back end through the
// pure virtual functions below. A back end implements those and nothing else; see software_renderer.hpp and gl_renderer.hpp.
#pragma once
#include <cstdint>
#include <vector>

#include "game/scene.hpp"

namespace slip {

struct Camera {
  double pos[3] = {0, 0, 0};
  float yaw = 0;     // radians, 0 = +z, positive turns towards +x
  float pitch = 0;   // radians, positive looks up (+y)
  float roll = 0;    // radians, positive banks right (right wing down); the cockpit camera follows the ship's bank
  float fovY = 1.0f; // radians
  float nearPlane = 12.0f;  // [0x18144] = 0xC (RaceLoadTrack 0x594EB): the original clips at 12 units
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

class Renderer {
 public:
  virtual ~Renderer() = default;
  // ---- back end -----------------------------------------------------------------------------------------------------------------------------------------------------
  virtual const char* name() const = 0;
  virtual void resize(int w, int h) = 0;           // internal resolution; allocates the CPU framebuffer (color_) that the HUD draws into
  // Called at the end of every 3D pass (main view, rear monitor, ship preview ...): back ends that render elsewhere (GPU) deliver the result into framebuffer() here.
  virtual void finishScene() {}
  // The track's sky: drawSky() adds the cloud / hill sprites (call right after beginFrame()).
  virtual void drawSky(const Scene& scene, double seconds) = 0;
  // A new scene (track) is going to be drawn: back ends may upload textures / geometry now. Called by the application when the scene changes.
  virtual void setScene(const Scene*) {}
  // Overlay primitives for gameplay objects (pickups, explosions, beams, lock marker), drawn after the track: they are depth tested against whatever the frame already holds but take
  // part in no painter item.
  virtual void drawSpriteWorld(const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent) = 0;
  virtual void drawLineWorld(const double a[3], const double b[3], uint32_t color) = 0;
  // A spark (0x28253): two crossing lines of half length `radius` turned by `angle` (turns), facing the camera; one pixel when it projects to 2 pixels or less.
  virtual void drawStarWorld(const double w[3], double radius, double angle, uint32_t color) = 0;
  virtual void drawRectScreen(int x0, int y0, int x1, int y1, uint32_t color) = 0;
  // Particles. A back end with an effects shader draws them softer and lit; the default is the plain sprite / star. life01: 0 young .. 1 gone; seed: a per-particle random number 0..1.
  enum class FxKind { Smoke, Fire, Explosion, Mist };   // Mist: the white spray of a water splash
  virtual void drawFxSprite(FxKind, const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent, float life01, float seed) {
    (void)life01; (void)seed;
    drawSpriteWorld(spr, pal, world, worldWidth, transparent);
  }
  // A wall spark / water droplet: `vel` is its velocity in world units per second (the streak points along it).
  // kind: 0 wall spark, 1 water droplet, 2 chip of the surface.
  virtual void drawSparkWorld(const double w[3], const double vel[3], double radius, double angle, uint32_t color, float life01, int kind = 0) {
    (void)vel; (void)life01; (void)kind;
    drawStarWorld(w, radius, angle, color);
  }
  // ---- front end ----------------------------------------------------------------------------------------------------------------------------------------------------
  int width() const { return w_; }
  int height() const { return h_; }
  // 3D viewport inside the framebuffer (the original's cockpit view: x 4..315, y 0..166 of 320x200, projection centre (160, 87), 0x44812).
  // Everything outside stays black. Call before beginFrame(); reset() restores the full frame.
  struct Viewport { int x0 = 0, y0 = 0, x1 = -1, y1 = -1; float cx = -1, cy = -1; };
  void setViewport(int x0, int y0, int x1, int y1, float cx, float cy) { vp_ = {x0, y0, x1, y1, cx, cy}; }
  void resetViewport() { vp_ = Viewport{}; }
  void beginFrame(const Camera& cam, uint32_t skyColor, uint32_t groundColor);
  // The "Sky" ramp (zenith .. horizon) replaces the flat sky colour of beginFrame when set.
  std::vector<uint32_t> skyRamp;
  float skyBand = 0.375f;
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
  // class-8 records need class 8 in the camera mask only in the plain far-to-near pass (0x39A89); the portal-reached list (0x3A55C) and the scenery test (0x37931) check just the intersection,
  // so a tunnel entered from outside (class 0x0A seen from class 0x02) is drawn through its portal.
  bool class8Rule = false;
  bool hideTunnelFaces = false;  // the camera is inside a tunnel: faces of towers that pass through tunnels (MeshPoly::tunnelFace) are not drawn
  bool visAllows(uint16_t vis) const;
  // Portal visibility (CONFIRMED structure, 0x39C58/0x39E67/0x3A065): starting from the piece containing the
  // camera, walk through portal polygons (TRC polygon flag bit 0) that face the camera; a neighbour is visited
  // with the screen rectangle of the portal intersected with the current window. Only visited pieces are drawn,
  // each clipped to its window. Windows are rectangles here (the original clips to its own window too).
  struct WinRect { int x0 = 0, y0 = 0, x1 = -1, y1 = -1; bool vis = false; };
  bool portalCulling = true;
  int pieceHint = -1;  // the piece the followed ship is in (the original starts the portal walk in the ship's track slot, not at a point test of the camera); -1 = test the camera point
  void computePortalVisibility(const Scene& scene);  // call after beginFrame()
  // True unless all eight corners of the world-minus-origin box lie outside one frustum plane (the engine's
  // bounding-volume test 0x36695 / outcodes 0x198B5).
  bool boxInFrustum(const Scene& scene, const float lo[3], const float hi[3]) const;
  WinRect unionWindow() const { return union_; }   // union of all portal-reached windows ([0x33EB4..0x33EC0])
  void setSceneryWindow(const WinRect* w) { sceneryWin_ = w; }  // clip window for polygons that belong to no piece
  void setPieceWindow(size_t i, const WinRect& w) { if (i < pieceWin_.size()) pieceWin_[i] = w; }
  const std::vector<WinRect>& pieceWindows() const { return pieceWin_; }
  // Overlay primitives for gameplay objects (pickups, explosions, beams, lock marker), drawn after the track: they are depth
  // tested against whatever the frame already holds but take part in no painter item.
  bool projectToScreen(const double world[3], float* x, float* y, float* z) const;
  // Per-piece lighting (0x39427): the diffuse level and the ambient of the lighting law are multiplied by the light of the piece the
  // polygon belongs to (TRD piece +0x20, 0..1; 0x4000 = full) while that piece is drawn. Indexed like Scene::pieceBoxes; empty = 1.
  std::vector<float> pieceLight;
  uint32_t animTimer = 0;   // animation clock in 2.14 seconds ([0x3F078]); the viewer advances it every frame
  // Ship shadows (CONFIRMED mechanism, see docs/research-log.md): the caster's polygons are projected along the light
  // direction (0,-1,0) onto the receiving polygon's plane and filled with the receiver's shadow colour inside it.
  std::vector<ShadowCaster> shadowCasters;
  // Real-time lighting (a back end may implement it, the software renderer ignores it; see docs/lighting.md). The application adds the dynamic lights of a frame (missiles, explosions,
  // boosters ...) after beginFrame(); the fixed lights of the track come from Scene::lights. `lightingMode` is set by the back end: 0 off, 1 lights, 2 lights and sun shadows.
  struct PointLight { double pos[3]; float color[3]; float radius; };
  std::vector<PointLight> frameLights;  // world coordinates
  void addLight(const double pos[3], float r, float g, float b, float radius) { frameLights.push_back({{pos[0], pos[1], pos[2]}, {r, g, b}, radius}); }
  int lightingMode = 0;
  // Ripples on water (a back end with a water shader animates them): rings spreading from a point where a craft skims or hits the water. world = world coordinates, age in seconds.
  struct Ripple { double pos[3]; float age; float strength; };
  std::vector<Ripple> ripples;
  virtual bool hasEffects() const { return false; }   // the back end draws water, particles and sparks with effect shaders
  bool shadows = true;
  bool wireframe = false;
  bool cullBackfaces = false;  // uses the stored face normals (INFERRED that the original culls too)
  float farPlane = 9.0e6f;

 protected:
  struct VV {
    float x, y, z, u, v;
  };
  struct P3 { float x, y, z; };
  // ---- primitives produced by the front end (camera space: x right, y up, z forward; vertices already clipped at the near plane) ----------------------------------------------------
  // A triangle. `mat` null = flat polygon without material. useTexture: the material texture is sampled with the vertex uv (1.0 = one repeat); otherwise the polygon is flat,
  // coloured by the lighting law (`light` / `flatIndexNy` = up-facing part of the normal, 0..16384) or, when forceIdx >= 0, by that palette index. layer: 0 road, 1 scenery, 2 backdrop.
  // The current scissor is sx0_..sy1_ (inclusive), the current item curItem_ (>= 0: painter's mode), the current polygon curPoly_.
  virtual void rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, int flatIndexNy, uint8_t layer, int forceIdx = -1) = 0;
  virtual void drawLine3D(P3 a, P3 b, uint32_t color) = 0;  // camera-space line, near-clipped, depth-tested against the current item
  // The ship shadow of one receiver polygon (camera space, convex) starts / ends: triangles issued between the two calls are shadow fill and are clipped to the receiver polygon.
  virtual void shadowBegin(const std::vector<VV>& receiver, int receiverId) = 0;
  virtual void shadowEnd() = 0;
  // Frame start after the camera basis is known: fill the viewport with the sky / ground backdrop and reset the per-frame buffers.
  virtual void onBeginFrame(uint32_t sky, uint32_t ground) = 0;
  WinRect union_;   // bounding rectangle of the front-facing extent-frame polygons (flag 0x8) of all reached pieces ([0x33EB4..0x33EC0])
  // Bounding rectangle of the polygon after near-plane clipping and clipping against `win` (the engine's 0x1A6F5:
  // transform, clip, bounding box). False if nothing is left.
  bool polyRect(const Scene& scene, const MeshPoly& p, const float cp[3], const WinRect& win, WinRect* out) const;
  // Flat-colour palette index of a polygon (original lighting law) and the panel-line detail drawn over it.
  int flatIndex(const SurfaceMaterial* mat, int upLight) const;
  // `farBranch`: 0x3F3C4 above 0xA6CC0 units projects the base vertices and takes the midpoints in screen space (2D)
  std::vector<VV> detailPoints(const PanelDetail& d, const std::vector<VV>& tv, const MeshPoly& p, bool allowScreenMid = false) const;
  void fillIdxPoly(const Scene& scene, const SurfaceMaterial* mat, const std::vector<VV>& pts, const std::vector<uint16_t>& idx, int colorIdx);
  void drawRoadFloor(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawChase(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawCageLines(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawFloorDetail(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat);
  void drawPanelLines(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat, int flatIdx);
  void addExtent(const Scene& scene, size_t piece, const float cp[3]);
  float lightScale_ = 1.0f;  // piece light of the polygon being drawn
  const WinRect* sceneryWin_ = nullptr;
  Viewport vp_;
  float cx_ = 0, cy_ = 0;  // projection centre of the current frame
  WinRect fullWin() const { return WinRect{vp_.x0, vp_.y0, vp_.x1, vp_.y1, true}; }
  std::vector<WinRect> pieceWin_;  // empty = no portal culling this frame
  int sx0_ = 0, sy0_ = 0, sx1_ = 0, sy1_ = 0;  // raster scissor (inclusive)
  int w_ = 0, h_ = 0;
  std::vector<uint32_t> color_;
  int recvId_ = 0;                 // id written by rasterTri for the polygon being drawn
  void drawShadowsOn(const Scene& scene, const MeshPoly& rp, int baseId, int sub, int colorIdx);
  uint32_t curPoly_ = 0, polyCounter_ = 0;
  int dbgPiece_ = -1; size_t dbgPoly_ = 0; uint16_t dbgFlags_ = 0;  // SLIP_PICK output
  int curItem_ = -1;
  std::vector<VV> shadowPoly_;
  // properties of the polygon being drawn for back ends with real-time lighting: camera-space unit normal (stored normal, may be 0), inside a tunnel, emits light (lamps)
  float curN_[3] = {0, 1, 0};
  bool curIndoor_ = false, curEmissive_ = false;
  int fillSeq_ = 0;   // number of detail fills (lanes, lines, lamps) already drawn over the current polygon: back ends order them by it
  int dbgTagPiece_ = -1, dbgTagPoly_ = -1;
  bool curWater_ = false;  // the polygon is a water surface
  float curSpec_ = 0.1f;   // specular strength of the polygon (ships and doors are metal, water shines, walls hardly)
  Camera cam_;
  float right_[3], up_[3], fwd_[3];
  float focal_ = 1;
  RenderStats stats_;
};

}  // namespace slip
