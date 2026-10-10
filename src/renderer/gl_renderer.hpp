// OpenGL back end of the renderer (slip::Renderer). The shared front end (renderer.cpp) decides what is visible and turns the scene into triangles / lines; this class draws them on
// the GPU into an offscreen framebuffer at the internal resolution (hidden SDL window, OpenGL 3.3 core) and delivers the picture into the CPU framebuffer at the end of every 3D pass,
// so the HUD, the menus and the presentation work exactly as with the software renderer.
//
// Look: the same palette, textures, lighting law and painter's order as the software renderer (see docs/renderers.md for the mapping). All shaders are GLSL 330 and can be replaced
// by files (scene.vert, scene.frag, sky.frag, overlay.frag, post_<name>.frag in <user folder>/shaders), plus a post-processing pass selected with --shader NAME.
#pragma once
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "renderer/gl_loader.hpp"
#include "renderer/renderer.hpp"

#include <SDL3/SDL_video.h>

namespace slip {

struct GlOptions {
  int msaa = 0;             // multisample anti-aliasing: 0 / 2 / 4 / 8 samples (clamped to what the driver offers)
  std::string filter = "nearest";  // texture filtering: nearest (the original's look) | bilinear | smooth (bilinear + distance filter)
  bool fx = false;                // effects shaders: water, soft particles, spark streaks
  bool ao = true, bloom = true;   // with lighting on: ambient occlusion and bloom
  std::string lighting = "off";  // real-time lighting: off (the original's flat look) | lights (ambient, sun, tunnel lamps, dynamic lights) | shadows (also sun shadows)
  std::string postShader;  // "" / "none" or the name of a post-processing shader (built in: crt, smooth, sharpen; or post_<name>.frag in the shader folder)
};

class GlRenderer : public Renderer {
 public:
  GlRenderer();
  ~GlRenderer() override;
  bool init(const GlOptions& opt, std::string* error);  // creates the context and the programs; false = unusable (the caller falls back to the software renderer)
  const char* name() const override { return "opengl"; }
  void resize(int w, int h) override;
  void finishScene() override;
  void setScene(const Scene* scene) override;
  void drawSky(const Scene& scene, double seconds) override;
  void drawSpriteWorld(const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent) override;
  void drawLineWorld(const double a[3], const double b[3], uint32_t color) override;
  void drawStarWorld(const double w[3], double radius, double angle, uint32_t color) override;
  void drawRectScreen(int x0, int y0, int x1, int y1, uint32_t color) override;
  void drawFxSprite(FxKind kind, const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent, float life01, float seed) override;
  void drawSparkWorld(const double w[3], const double vel[3], double radius, double angle, uint32_t color, float life01, int kind = 0) override;
  bool hasEffects() const override { return fxOn_; }
  const std::string& glInfo() const { return info_; }

 protected:
  void rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, int flatIndexNy, uint8_t layer, int forceIdx = -1) override;
  void drawLine3D(P3 a, P3 b, uint32_t color) override;
  void shadowBegin(const std::vector<VV>& receiver, int receiverId) override;
  void shadowEnd() override;
  void onBeginFrame(uint32_t sky, uint32_t ground) override;

 private:
  struct SceneVertex {  // camera space position, texture coordinate, flat colour, atlas rectangle (width 0 = flat), depth scale
    float x, y, z, u, v;
    uint8_t r, g, b, a;
    float rx, ry, rw, rh;
    float ds;
    float nx, ny, nz;   // camera-space normal (0 = unlit)
    float flags;        // bit 0 inside a tunnel, bit 1 emits light
    float spec;         // specular strength
  };
  struct OverlayVertex {  // pixel position (y down), depth value of the overlay primitive, texture coordinate, colour
    float x, y, d, u, v;
    uint8_t r, g, b, a;
  };
  struct Program {
    gl::GLuint id = 0;
    gl::GLint loc[48] = {};
  };
  bool buildProgram(Program* p, const std::string& vs, const std::string& fs, const char* label, std::string* error);
  std::string shaderSource(const char* file, const char* fallback) const;
  void createTargets();
  void destroyTargets();
  void uploadAtlas(const Scene& scene);
  gl::GLuint spriteTexture(const Sprite& spr, const Palette& pal, int transparent);
  void openBatch();                 // scissor / item bookkeeping before a primitive is added to the scene batch
  void flush();                     // draw the scene batch
  void ensureDepthCopy();
  void drawOverlay(gl::GLenum mode, const std::vector<OverlayVertex>& v, gl::GLuint tex, bool depthTest, bool fullScissor = false);
  void overlayLine(float x0, float y0, float d0, float x1, float y1, float d1, uint32_t color, bool depthTest);
  void emitSceneLine(const P3& a, const P3& b, uint32_t color, float ds);
  void emitTriangle(const SceneVertex& a, const SceneVertex& b, const SceneVertex& c);
  void setScissor(int x0, int y0, int x1, int y1);  // inclusive screen rectangle (y down)
  float depthScale(uint8_t layer, int forceIdx) const;

  gl::Api gl_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext ctx_ = nullptr;
  bool ok_ = false;
  std::string info_;
  GlOptions opt_;
  // programs
  Program fxProg_, scene_, sky_, overlay_, post_, restore_, shadowProg_, ssao_, bloomX_, blur_, comp_;
  gl::GLuint resultFbo_ = 0;
  bool fxOn_ = false;
  void drawFx(int mode, gl::GLuint tex, const std::vector<OverlayVertex>& v, bool additive, float life, float seed, float soft);
  bool hdr_ = false, aoOn_ = false, bloomOn_ = false;
  gl::GLuint bloomFbo_[2] = {0, 0}, bloomTex_[2] = {0, 0};
  void runPost(int x0, int y0, int x1, int y1);
  void fullscreenPass(Program& p, gl::GLuint fbo, int w, int h, int sx0, int sy0, int sx1, int sy1);
  bool havePost_ = false;
  // targets
  gl::GLuint msFbo_ = 0, msColorRb_ = 0, msDepthIdRb_ = 0, msDepthRb_ = 0;  // multisampled render target (MSAA); fbo_ is then the resolve target
  int samples_ = 0;
  int filterMode_ = 0;
  gl::GLuint renderFbo() const { return msFbo_ ? msFbo_ : fbo_; }
  gl::GLuint fbo_ = 0, colorTex_ = 0, depthTex_ = 0, depthRb_ = 0, copyFbo_ = 0, copyTex_ = 0, postFbo_ = 0, postTex_ = 0;
  gl::GLuint vao_ = 0, vbo_ = 0, ovao_ = 0, ovbo_ = 0, emptyVao_ = 0;
  int tw_ = 0, th_ = 0;
  // atlas of the material textures of `atlasScene_`
  struct AtlasEntry { gl::GLuint tex = 0; std::vector<std::array<float, 4>> rects; float w = 1, h = 1; };
  float atlasW_ = 1, atlasH_ = 1;
  std::unordered_map<uint64_t, AtlasEntry> atlases_;  // by content (textures + palette) so that scenes drawn alternately (track, ship cards) do not re-upload
  gl::GLuint atlas_ = 0;
  const Scene* atlasScene_ = nullptr;
  std::vector<std::array<float, 4>> rects_;
  std::unordered_map<std::string, gl::GLuint> sprites_;
  // scene batch
  bool guardDone_ = false;
  std::vector<SceneVertex> tris_;
  std::vector<SceneVertex> lines_;   // thin decal lines: drawn without writing the depth copy used by overlays and ambient occlusion (their flat quads would shade the floor)
  bool batchOpen_ = false;
  int batchScissor_[4] = {0, 0, 0, 0};
  int batchItem_ = -2, clearedItem_ = -2;
  uint32_t itemFirstPoly_ = 0;
  mutable uint32_t seq_ = 0, itemFirstSeq_ = 0;   // draw order within an item: every primitive gets a hair nearer depth than the one before (coplanar lane lines over lanes over floors)
  bool depthCopyValid_ = false;
  bool globalDepth_ = true;   // the depth buffer holds the depth of everything drawn (false after painter items cleared it per item)
  void restoreGlobalDepth();
  bool sceneActive_ = false;
  // shadow clipping
  bool shadowMode_ = false;
  std::vector<std::array<float, 3>> shadowPlanes_;
  // real-time lighting
  void prepareLighting(const Scene& scene);   // once per frame: pick the lights, set up the sun and its shadow map
  void renderShadowMap(const Scene& scene);
  void uploadStaticCasters(const Scene& scene);
  bool lightsReady_ = false, shadowOn_ = false;
  int lightN_ = 0;
  static constexpr int kMaxLights = 64;
  float lightPos_[kMaxLights * 3] = {}, lightCol_[kMaxLights * 3] = {}, lightRad_[kMaxLights] = {};
  float sunCam_[3] = {0, 1, 0}, upCam_[3] = {0, 1, 0};
  float shX_[6] = {}, shY_[6] = {}, shZ_[6] = {}, shOff_[6] = {}, shParams_[6] = {1, 1, 1, 1, 1, 1};
  gl::GLuint shadowFbo_[2] = {0, 0}, shadowTex_[2] = {0, 0}, shadowSize_[2] = {0, 0};
  gl::GLuint shadowVao_ = 0, shadowVbo_ = 0, shadowDynVbo_ = 0, shadowDynVao_ = 0;
  size_t staticCasterVerts_ = 0;
  const Scene* casterScene_ = nullptr;
  float uProj_[4] = {1, 0, 1, 0};
  float nearD_ = 6.0f;
};

}  // namespace slip
