// Compatibility renderer: a small CPU rasteriser (z-buffer, perspective-correct textures, flat shade-ramp colouring) that follows the original's structure. Back end of the
// shared front end in renderer.hpp.
#pragma once
#include "renderer/renderer.hpp"

namespace slip {

class SoftwareRenderer : public Renderer {
 public:
  const char* name() const override { return "software"; }
  void resize(int w, int h) override;
  void drawSky(const Scene& scene, double seconds) override;
  void drawSpriteWorld(const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent) override;
  void drawLineWorld(const double a[3], const double b[3], uint32_t color) override;
  void drawStarWorld(const double w[3], double radius, double angle, uint32_t color) override;
  void drawRectScreen(int x0, int y0, int x1, int y1, uint32_t color) override;

 protected:
  void rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, int flatIndexNy, uint8_t layer, int forceIdx = -1) override;
  void drawLine3D(P3 a, P3 b, uint32_t color) override;
  void shadowBegin(const std::vector<VV>&, int receiverId) override { shadowRecv_ = receiverId; }
  void shadowEnd() override { shadowRecv_ = 0; }
  void onBeginFrame(uint32_t sky, uint32_t ground) override;

 private:
  std::vector<int32_t> recvBuf_;   // id of the receiver polygon (and sub-polygon) that last wrote the pixel, 0 = none
  int shadowRecv_ = 0;             // >0: rasterTri paints only pixels owned by this receiver, without depth test
  std::vector<uint32_t> polyId_;   // id of the polygon whose base fill last wrote the pixel (detail polygons are painted over their own base without a depth test)
  std::vector<int32_t> itemBuf_;   // painter's mode: id of the item that last wrote the pixel
  std::vector<uint8_t> backdrop_;  // per pixel: 0 track, 1 scenery, 2 backdrop scenery
  std::vector<float> depth_;       // stores 1/z (larger = nearer)
};

}  // namespace slip
