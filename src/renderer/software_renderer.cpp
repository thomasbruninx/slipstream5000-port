#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "renderer/software_renderer.hpp"

namespace slip {

namespace {
inline uint32_t shade(uint32_t rgb, float f) {
  uint32_t r = uint32_t(float((rgb >> 16) & 255) * f), g = uint32_t(float((rgb >> 8) & 255) * f), b = uint32_t(float(rgb & 255) * f);
  return 0xff000000u | (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
}
}  // namespace

void SoftwareRenderer::resize(int w, int h) {
  w_ = w;
  h_ = h;
  color_.assign(size_t(w) * size_t(h), 0xff000000u);
  depth_.assign(size_t(w) * size_t(h), 0.0f);
  backdrop_.assign(size_t(w) * size_t(h), 0);
  itemBuf_.assign(size_t(w) * size_t(h), -1);
  polyId_.assign(size_t(w) * size_t(h), 0);
  recvBuf_.assign(size_t(w) * size_t(h), 0);
}

void SoftwareRenderer::onBeginFrame(uint32_t sky, uint32_t ground) {
  // two-colour backdrop split at the horizon: a pixel is sky when its view ray points above the horizontal plane
  if (vp_.x0 > 0 || vp_.y0 > 0 || vp_.x1 < w_ - 1 || vp_.y1 < h_ - 1) std::fill(color_.begin(), color_.end(), 0xff000000u);
  for (int y = vp_.y0; y <= vp_.y1; ++y) {
    const float a = fwd_[1] * focal_ + up_[1] * (cy_ - (float(y) + 0.5f)), b = right_[1];
    uint32_t* row = &color_[size_t(y) * size_t(w_)];
    if (skyRamp.empty()) {
      for (int x = vp_.x0; x <= vp_.x1; ++x) row[x] = (a + b * (float(x) + 0.5f - cx_)) > 0 ? sky : ground;
    } else {
      // sky gradient: the ramp runs from the zenith colour to the horizon colour with the elevation of the view ray (INFERRED: the original shades the sky in bands through 0x1D242)
      const float dy = cy_ - (float(y) + 0.5f), n = float(skyRamp.size() - 1);
      for (int x = vp_.x0; x <= vp_.x1; ++x) {
        const float dx = float(x) + 0.5f - cx_, ry = a + b * dx;
        if (ry <= 0) { row[x] = ground; continue; }
        const float s = ry / std::sqrt(focal_ * focal_ + dx * dx + dy * dy);
        row[x] = skyRamp[size_t(std::lround(n * (1.0f - std::min(s / skyBand, 1.0f))))];
      }
    }
  }
  std::fill(depth_.begin(), depth_.end(), 0.0f);
  std::fill(backdrop_.begin(), backdrop_.end(), uint8_t(0));
  std::fill(itemBuf_.begin(), itemBuf_.end(), -1);
  std::fill(polyId_.begin(), polyId_.end(), 0u);
  polyCounter_ = 0;
  std::fill(recvBuf_.begin(), recvBuf_.end(), 0);
}

void SoftwareRenderer::drawSky(const Scene& scene, double seconds) {
  if (scene.skySprites.empty()) return;
  const float scale = float(vp_.x1 - vp_.x0 + 1) / 320.0f;
  const double turn = 6.283185307179586 / 65536.0;
  for (int layer = 0; layer < 3; ++layer) {
    const double drift = (layer == 2 ? scene.skySpeed : layer == 0 ? scene.skySpeed / 8.0 : 0.0) * seconds * turn;
    for (const SkySprite& k : scene.skySprites) {
      if (k.layer != layer || k.img.w <= 0) continue;
      const double az = k.az - drift, ce = std::cos(k.el);
      const double d[3] = {std::sin(az) * ce, std::sin(k.el), std::cos(az) * ce};
      const float z = float(d[0] * fwd_[0] + d[1] * fwd_[1] + d[2] * fwd_[2]);
      if (z <= 0.05f) continue;
      const float x = float(d[0] * right_[0] + d[1] * right_[1] + d[2] * right_[2]), y = float(d[0] * up_[0] + d[1] * up_[1] + d[2] * up_[2]);
      const float sx = cx_ + x / z * focal_, sy = cy_ - y / z * focal_;
      const float pw = float(k.img.w) * scale * k.scale, ph = float(k.img.h) * scale * k.scale;
      // the point is the bottom centre of the picture (INFERRED from the corner setup of 0x1A421: x from -w/2 to w/2, y from -h to 0)
      // the picture stands upright in the world: it turns with the camera's roll (the original draws a rotated quad when the corners are not axis aligned, 0x1A4B0)
      const float rn = std::sqrt(right_[1] * right_[1] + up_[1] * up_[1]);
      const float ux = rn > 1e-4f ? right_[1] / rn : 0.0f, uy = rn > 1e-4f ? -up_[1] / rn : -1.0f;  // world up on screen (y down)
      const float rx = -uy, ry = ux;                                                                 // world right on screen
      const float rad = std::hypot(pw * 0.5f, ph) + 1.0f;
      const int x0 = int(std::floor(sx - rad)), x1 = int(std::ceil(sx + rad)), y0 = int(std::floor(sy - rad)), y1 = int(std::ceil(sy + rad));
      for (int py = std::max(y0, vp_.y0); py < std::min(y1, vp_.y1 + 1); ++py) {
        for (int px = std::max(x0, vp_.x0); px < std::min(x1, vp_.x1 + 1); ++px) {
          const float dx = float(px) + 0.5f - sx, dy = float(py) + 0.5f - sy;
          const float lu = dx * rx + dy * ry, lh = dx * ux + dy * uy;
          if (lu < -pw * 0.5f || lu >= pw * 0.5f || lh < 0 || lh >= ph) continue;
          const int u = std::clamp(int((lu + pw * 0.5f) / pw * float(k.img.w)), 0, k.img.w - 1);
          const int v = std::clamp(int((1.0f - lh / ph) * float(k.img.h)), 0, k.img.h - 1);
          const uint8_t idx = k.img.pixels[size_t(v) * size_t(k.img.w) + size_t(u)];
          if (int(idx) == k.transparent) continue;
          color_[size_t(py) * size_t(w_) + size_t(px)] = 0xff000000u | scene.palette.rgba[idx];
        }
      }
    }
  }
}

void SoftwareRenderer::rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, int upLight, uint8_t layer, int forceIdx) {
  const Texture* tex = (forceIdx < 0 && useTexture && mat && mat->texture >= 0) ? &scene.textures[size_t(mat->texture)] : nullptr;
  uint32_t flat = 0xff808080u;
  {
    // Flat polygon colour = ramp position from the original lighting law (see flatIndex).
    if (forceIdx >= 0) flat = shade(scene.palette.rgba[size_t(std::clamp(forceIdx, 0, 255))], 1.0f);
    else if (mat) flat = shade(scene.palette.rgba[size_t(std::clamp(flatIndex(mat, upLight), 0, 255))], 1.0f);
    else flat = shade(scene.palette.rgba[0], light);
  }
  struct SP { double x, y, iw, uw, vw; };
  auto raster = [&](const float sx[3], const float sy[3], const float iw[3], const float uw[3], const float vw[3]) {
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (std::fabs(area) < 1e-4f) return;
    stats_.trisRastered++;
    int minx = std::max(sx0_, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
    int maxx = std::min(sx1_, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
    int miny = std::max(sy0_, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
    int maxy = std::min(sy1_, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
    if (minx > maxx || miny > maxy) return;
    float inv = 1.0f / area;
    for (int y = miny; y <= maxy; ++y) {
      float py = float(y) + 0.5f;
      for (int x = minx; x <= maxx; ++x) {
        float px = float(x) + 0.5f;
        float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
        float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
        float w2 = 1.0f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        float z = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
        size_t o = size_t(y) * size_t(w_) + size_t(x);
        if (shadowRecv_) {
          if (recvBuf_[o] == shadowRecv_) color_[o] = flat;
          continue;
        }
        // Layering of scenery against road (see docs/research-log.md): scenery must be clearly nearer than road to
        // cover it (near-ties and slight intrusions of building volumes into tunnel walls resolve to the road);
        // backdrop scenery never covers road. Among equal layers a later-drawn near-tie wins (coplanar decals).
        const float cur = depth_[o];
        // lamps / lane colours are painted over the polygon they belong to whatever its (bent) surface does, as the original has no depth test between them
        const bool overOwnBase = forceIdx >= 0 && polyId_[o] == curPoly_;
        if (overOwnBase) {
        } else if (curItem_ >= 0) {
          // inside one item (a piece) the original draws polygons in list order with no depth test: decals (yellowbars over Light Wall A) lie within ~0.2 % of the wall they sit on, which a strict
          // test turns into half-drawn signs; a later polygon wins unless it is clearly behind
          if (itemBuf_[o] == curItem_ && z < cur * (1.0f - 1e-2f)) continue;
        } else {
          static const bool pureDepth = getenv("SLIP_PUREDEPTH") != nullptr;
          const uint8_t curLayer = backdrop_[o];
          if (pureDepth) { if (z < cur) continue; } else
          if (cur > 0) {
            if (layer == 0) {
              if (curLayer == 0) { if (z < cur * (1.0f - 2e-5f)) continue; }
              else if (curLayer == 2) { /* road always covers backdrop */ }
              else if (z < cur / 1.3f) continue;  // scenery clearly nearer than this road fragment
            } else {
              if (curLayer == 0) { if (layer == 2 || z < cur * 1.3f) continue; }
              else if (z < cur * (1.0f - 2e-5f)) continue;
            }
          }
        }
        uint32_t col = flat;
        if (tex) {
          float u = (w0 * uw[0] + w1 * uw[1] + w2 * uw[2]) / z;
          float vv = (w0 * vw[0] + w1 * vw[1] + w2 * vw[2]) / z;
          u -= std::floor(u);
          vv -= std::floor(vv);
          int tx = std::min(tex->w - 1, int(u * float(tex->w)));
          int ty = std::min(tex->h - 1, int(vv * float(tex->h)));
          uint8_t pi = tex->index[size_t(ty) * size_t(tex->w) + size_t(tx)];
          if (int(pi) == tex->transparent) continue;  // transparent colour from the SPR header (+8)
          col = shade(scene.palette.rgba[pi], 1.0f);  // textured polygons are not lit in the original (0x19E0D -> 0x1C753 passes no shade)
        }
        { static const char* pk = getenv("SLIP_PICK"); static int pkx = -1, pky = -1; static bool init = false;
          if (pk && !init) { sscanf(pk, "%d,%d", &pkx, &pky); init = true; }
          if (pk && x == pkx && y == pky) fprintf(stderr, "pick %d,%d mat=%s layer=%d iz=%g tex=%d piece=%d poly=%zu pflags=%04x\n", x, y, mat ? mat->name.c_str() : "-", int(layer), double(z), tex ? 1 : 0, dbgPiece_, dbgPoly_, unsigned(dbgFlags_)); }
        depth_[o] = z;
        backdrop_[o] = layer;
        itemBuf_[o] = curItem_;
        if (forceIdx < 0) polyId_[o] = curPoly_;
        recvBuf_[o] = recvId_;
        color_[o] = col;
      }
    }
  };
  // screen space: x / y / 1/z / u/z / v/z are affine, so a triangle that reaches far outside the window (a wall right in front of the camera, 1/z huge) is clipped to the
  // window first; float edge functions on coordinates of 1e5 and more lose their precision and the polygon would flicker or vanish
  std::vector<SP> poly;
  {
    const VV* v[3] = {&a, &b, &c};
    for (int i = 0; i < 3; ++i) {
      const double invz = 1.0 / double(v[i]->z);
      poly.push_back({double(cx_) + double(v[i]->x) * invz * focal_, double(cy_) - double(v[i]->y) * invz * focal_, invz, double(v[i]->u) * invz, double(v[i]->v) * invz});
    }
  }
  const double gx0 = double(sx0_) - 1, gx1 = double(sx1_) + 2, gy0 = double(sy0_) - 1, gy1 = double(sy1_) + 2;
  auto clipAxis = [&](std::vector<SP>& in, int axis, double bound, bool keepGreater) {
    std::vector<SP> out;
    for (size_t i = 0; i < in.size(); ++i) {
      const SP& p0 = in[i];
      const SP& p1 = in[(i + 1) % in.size()];
      const double d0 = (axis == 0 ? p0.x : p0.y) - bound, d1 = (axis == 0 ? p1.x : p1.y) - bound;
      const bool in0 = keepGreater ? d0 >= 0 : d0 <= 0, in1 = keepGreater ? d1 >= 0 : d1 <= 0;
      if (in0) out.push_back(p0);
      if (in0 != in1) {
        const double t = d0 / (d0 - d1);
        out.push_back({p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t, p0.iw + (p1.iw - p0.iw) * t, p0.uw + (p1.uw - p0.uw) * t, p0.vw + (p1.vw - p0.vw) * t});
      }
    }
    in.swap(out);
  };
  clipAxis(poly, 0, gx0, true);
  if (poly.size() >= 3) clipAxis(poly, 0, gx1, false);
  if (poly.size() >= 3) clipAxis(poly, 1, gy0, true);
  if (poly.size() >= 3) clipAxis(poly, 1, gy1, false);
  for (size_t k = 1; k + 1 < poly.size(); ++k) {
    const SP* t3[3] = {&poly[0], &poly[k], &poly[k + 1]};
    float sx[3], sy[3], iw[3], uw[3], vw[3];
    for (int i = 0; i < 3; ++i) { sx[i] = float(t3[i]->x); sy[i] = float(t3[i]->y); iw[i] = float(t3[i]->iw); uw[i] = float(t3[i]->uw); vw[i] = float(t3[i]->vw); }
    raster(sx, sy, iw, uw, vw);
  }
}

void SoftwareRenderer::drawLine3D(P3 a, P3 b, uint32_t col) {
  const float n = cam_.nearPlane;
  if (a.z < n && b.z < n) return;
  if (a.z < n) { float t = (n - a.z) / (b.z - a.z); a = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, n}; }
  else if (b.z < n) { float t = (n - b.z) / (a.z - b.z); b = {b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t, n}; }
  const float x0 = cx_ + a.x / a.z * focal_, y0 = cy_ - a.y / a.z * focal_;
  const float x1 = cx_ + b.x / b.z * focal_, y1 = cy_ - b.y / b.z * focal_;
  float iz0 = 1.0f / a.z, iz1 = 1.0f / b.z;
  // clip to the scissor rectangle (Liang-Barsky) so very long lines keep one sample per pixel
  float cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1, ta = 0.0f, tb = 1.0f;
  {
    const float dx = x1 - x0, dy = y1 - y0;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0 - float(sx0_), float(sx1_ + 1) - x0, y0 - float(sy0_), float(sy1_ + 1) - y0};
    for (int k = 0; k < 4; ++k) {
      if (p[k] == 0) { if (q[k] < 0) return; continue; }
      const float r = q[k] / p[k];
      if (p[k] < 0) ta = std::max(ta, r); else tb = std::min(tb, r);
      if (ta > tb) return;
    }
    cx0 = x0 + dx * ta; cy0 = y0 + dy * ta; cx1 = x0 + dx * tb; cy1 = y0 + dy * tb;
    const float z0 = iz0, z1 = iz1;
    iz0 = z0 + (z1 - z0) * ta; iz1 = z0 + (z1 - z0) * tb;
  }
  const int steps = std::min(int(std::max(std::fabs(cx1 - cx0), std::fabs(cy1 - cy0))) + 1, 8192);
  for (int i = 0; i <= steps; ++i) {
    const float t = float(i) / float(steps);
    const int x = int(std::floor(cx0 + (cx1 - cx0) * t)), y = int(std::floor(cy0 + (cy1 - cy0) * t));
    const float z = iz0 + (iz1 - iz0) * t;
    // the original draws 1-pixel lines at 320x200: keep the same proportion at our resolution
    const int th = std::max(1, int(std::lround(float(w_) / 320.0f)));
    for (int dy = 0; dy < th; ++dy)
      for (int dx = 0; dx < th; ++dx) {
        const int px = x + dx - th / 2, py = y + dy - th / 2;
        if (px < sx0_ || px > sx1_ || py < sy0_ || py > sy1_) continue;
        const size_t o = size_t(py) * size_t(w_) + size_t(px);
        if (z < depth_[o] * (1.0f - 2e-2f) && (curItem_ < 0 || itemBuf_[o] == curItem_)) continue;
        color_[o] = col;
        depth_[o] = std::max(depth_[o], z);
        itemBuf_[o] = curItem_;
      }
  }
}

void SoftwareRenderer::drawSpriteWorld(const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent) {
  float sx, sy, z;
  if (spr.w <= 0 || spr.h <= 0 || !projectToScreen(world, &sx, &sy, &z)) return;
  const float pw = float(worldWidth) / z * focal_;  // projected width in pixels
  if (pw < 1.0f) return;
  const float ph = pw * float(spr.h) / float(spr.w);
  const int x0 = int(std::floor(sx - pw * 0.5f)), x1 = int(std::ceil(sx + pw * 0.5f));
  const int y0 = int(std::floor(sy - ph * 0.5f)), y1 = int(std::ceil(sy + ph * 0.5f));
  const float iz = 1.0f / z;
  for (int y = std::max(y0, sy0_); y <= std::min(y1 - 1, sy1_); ++y) {
    const int v = std::clamp(int((float(y) + 0.5f - (sy - ph * 0.5f)) / ph * float(spr.h)), 0, spr.h - 1);
    for (int x = std::max(x0, sx0_); x <= std::min(x1 - 1, sx1_); ++x) {
      const int u = std::clamp(int((float(x) + 0.5f - (sx - pw * 0.5f)) / pw * float(spr.w)), 0, spr.w - 1);
      const uint8_t idx = spr.pixels[size_t(v) * size_t(spr.w) + size_t(u)];
      if (int(idx) == transparent) continue;
      const size_t o = size_t(y) * size_t(w_) + size_t(x);
      if (iz < depth_[o] * (1.0f - 2e-2f)) continue;
      color_[o] = 0xff000000u | pal.rgba[idx];
    }
  }
}

void SoftwareRenderer::drawLineWorld(const double a[3], const double b[3], uint32_t color) {
  auto cs = [&](const double* p) {
    const float dx = float(p[0] - cam_.pos[0]), dy = float(p[1] - cam_.pos[1]), dz = float(p[2] - cam_.pos[2]);
    return P3{dx * right_[0] + dy * right_[1] + dz * right_[2], dx * up_[0] + dy * up_[1] + dz * up_[2], dx * fwd_[0] + dy * fwd_[1] + dz * fwd_[2]};
  };
  const int saved = curItem_;
  curItem_ = -1;
  drawLine3D(cs(a), cs(b), color);
  curItem_ = saved;
}

void SoftwareRenderer::drawStarWorld(const double w[3], double radius, double angle, uint32_t color) {
  float sx, sy, z;
  if (!projectToScreen(w, &sx, &sy, &z)) return;
  const float dx = float(w[0] - cam_.pos[0]), dy = float(w[1] - cam_.pos[1]), dz = float(w[2] - cam_.pos[2]);
  const P3 c{dx * right_[0] + dy * right_[1] + dz * right_[2], dx * up_[0] + dy * up_[1] + dz * up_[2], dx * fwd_[0] + dy * fwd_[1] + dz * fwd_[2]};
  const int saved = curItem_;
  curItem_ = -1;
  if (radius / z * focal_ <= 2.0) {
    drawLine3D(c, P3{c.x + 0.8f * z / focal_, c.y, c.z}, color);
  } else {
    const float r = float(radius), ca = float(std::cos(angle * 6.283185307179586)), sa = float(std::sin(angle * 6.283185307179586));
    drawLine3D(P3{c.x - r * ca, c.y - r * sa, c.z}, P3{c.x + r * ca, c.y + r * sa, c.z}, color);
    drawLine3D(P3{c.x + r * sa, c.y - r * ca, c.z}, P3{c.x - r * sa, c.y + r * ca, c.z}, color);
  }
  curItem_ = saved;
}

void SoftwareRenderer::drawRectScreen(int x0, int y0, int x1, int y1, uint32_t color) {
  auto put = [&](int x, int y) { if (x >= 0 && x < w_ && y >= 0 && y < h_) color_[size_t(y) * size_t(w_) + size_t(x)] = color; };
  for (int x = x0; x <= x1; ++x) { put(x, y0); put(x, y1); }
  for (int y = y0; y <= y1; ++y) { put(x0, y); put(x1, y); }
}

}  // namespace slip
