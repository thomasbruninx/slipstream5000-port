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

struct RenderStats {
  uint32_t polysSubmitted = 0, polysDrawn = 0, trisRastered = 0;
};

class SoftwareRenderer {
 public:
  void resize(int w, int h);
  int width() const { return w_; }
  int height() const { return h_; }
  void beginFrame(const Camera& cam, uint32_t skyColor, uint32_t groundColor);
  void drawMesh(const Scene& scene, const Mesh& mesh, const MeshTransform& xf);
  const uint32_t* pixels() const { return color_.data(); }  // 0xFFRRGGBB
  uint32_t* framebuffer() { return color_.data(); }
  const RenderStats& stats() const { return stats_; }
  uint16_t visMask = 0xFFFF;   // camera record class bits; 0xFFFF = draw everything (see Scene::visMaskAt)
  bool wireframe = false;
  bool cullBackfaces = false;  // uses the stored face normals (INFERRED that the original culls too)
  float farPlane = 9.0e6f;

 private:
  struct VV {
    float x, y, z, u, v;
  };
  void rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, uint8_t layer);
  int w_ = 0, h_ = 0;
  std::vector<uint32_t> color_;
  std::vector<uint8_t> backdrop_;  // per pixel: 0 track, 1 scenery, 2 backdrop scenery
  std::vector<float> depth_;  // stores 1/z (larger = nearer)
  Camera cam_;
  float right_[3], up_[3], fwd_[3];
  float focal_ = 1;
  RenderStats stats_;
};

}  // namespace slip
