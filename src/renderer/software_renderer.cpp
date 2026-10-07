#include <cstdio>
#include <cstdlib>
#include "renderer/software_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

void SoftwareRenderer::resize(int w, int h) {
  w_ = w;
  h_ = h;
  color_.assign(size_t(w) * size_t(h), 0xff000000u);
  depth_.assign(size_t(w) * size_t(h), 0.0f);
  backdrop_.assign(size_t(w) * size_t(h), 0);
}

void SoftwareRenderer::beginFrame(const Camera& cam, uint32_t sky, uint32_t ground) {
  cam_ = cam;
  stats_ = {};
  float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw), cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
  fwd_[0] = sy * cp; fwd_[1] = sp; fwd_[2] = cy * cp;
  right_[0] = cy; right_[1] = 0; right_[2] = -sy;
  // up = forward x right
  up_[0] = fwd_[1] * right_[2] - fwd_[2] * right_[1];
  up_[1] = fwd_[2] * right_[0] - fwd_[0] * right_[2];
  up_[2] = fwd_[0] * right_[1] - fwd_[1] * right_[0];
  focal_ = (float(h_) * 0.5f) / std::tan(cam.fovY * 0.5f);
  // simple two-colour backdrop split at the horizon
  int horizon = int(h_ * 0.5f + std::tan(cam.pitch) * focal_);
  horizon = std::clamp(horizon, 0, h_);
  for (int y = 0; y < h_; ++y) {
    uint32_t c = y < horizon ? sky : ground;
    std::fill_n(&color_[size_t(y) * size_t(w_)], w_, c);
  }
  std::fill(depth_.begin(), depth_.end(), 0.0f);
  std::fill(backdrop_.begin(), backdrop_.end(), uint8_t(0));
}

void SoftwareRenderer::drawMesh(const Scene& scene, const Mesh& mesh, const MeshTransform& xf) {
  // camera-relative translation computed in double (world coordinates reach ~7e6)
  const double dx = xf.pos[0] - cam_.pos[0], dy = xf.pos[1] - cam_.pos[1], dz = xf.pos[2] - cam_.pos[2];
  // transform all vertices once
  static thread_local std::vector<VV> tv;
  tv.resize(mesh.verts.size());
  for (size_t i = 0; i < mesh.verts.size(); ++i) {
    const Vec3& v = mesh.verts[i];
    float rx = float(dx) + xf.R[0] * v.x + xf.R[1] * v.y + xf.R[2] * v.z;
    float ry = float(dy) + xf.R[3] * v.x + xf.R[4] * v.y + xf.R[5] * v.z;
    float rz = float(dz) + xf.R[6] * v.x + xf.R[7] * v.y + xf.R[8] * v.z;
    tv[i] = {rx * right_[0] + ry * right_[1] + rz * right_[2], rx * up_[0] + ry * up_[1] + rz * up_[2],
             rx * fwd_[0] + ry * fwd_[1] + rz * fwd_[2], mesh.uv[2 * i], mesh.uv[2 * i + 1]};
  }
  static const float L[3] = {0.35f, 0.85f, 0.40f};
  const float nearZ = cam_.nearPlane;
  std::vector<VV> poly, clipped;
  for (const MeshPoly& p : mesh.polys) {
    stats_.polysSubmitted++;
    // trivial rejects
    bool allNear = true, allFar = true, allLeft = true, allRight = true, allUp = true, allDown = true;
    for (uint16_t k = 0; k < p.count; ++k) {
      const VV& v = tv[p.first + k];
      allNear &= v.z < nearZ;
      allFar &= v.z > farPlane;
      allLeft &= v.x < -v.z * (float(w_) * 0.5f / focal_);
      allRight &= v.x > v.z * (float(w_) * 0.5f / focal_);
      allUp &= v.y > v.z * (float(h_) * 0.5f / focal_);
      allDown &= v.y < -v.z * (float(h_) * 0.5f / focal_);
    }
    if (allNear || allFar || allLeft || allRight || allUp || allDown) continue;

    if (p.hidden) continue;
    if (p.vis != 0xFFFF && visMask != 0xFFFF) {
      if ((p.vis & visMask) == 0) continue;
      if (!(visMask & 8) && (p.vis & 8)) continue;  // class 8 only visible from class-8 records (0x39A89)
    }
    if (p.material >= 0 && size_t(p.material) < scene.materials.size() && scene.materials[size_t(p.material)].invisible) continue;
    // shading
    float nx = xf.R[0] * p.normal.x + xf.R[1] * p.normal.y + xf.R[2] * p.normal.z;
    float ny = xf.R[3] * p.normal.x + xf.R[4] * p.normal.y + xf.R[5] * p.normal.z;
    float nz = xf.R[6] * p.normal.x + xf.R[7] * p.normal.y + xf.R[8] * p.normal.z;
    if (cullBackfaces && (p.normal.x != 0 || p.normal.y != 0 || p.normal.z != 0)) {
      const VV& v0 = tv[p.first];
      float nvx = nx * right_[0] + ny * right_[1] + nz * right_[2];
      float nvy = nx * up_[0] + ny * up_[1] + nz * up_[2];
      float nvz = nx * fwd_[0] + ny * fwd_[1] + nz * fwd_[2];
      if (nvx * v0.x + nvy * v0.y + nvz * v0.z > 0) continue;  // facing away from the camera
    }
    float ndl = std::fabs(nx * L[0] + ny * L[1] + nz * L[2]) / 1.0f;
    float light = std::clamp(0.35f + 0.65f * ndl, 0.0f, 1.0f);
    const SurfaceMaterial* mat = (p.material >= 0 && size_t(p.material) < scene.materials.size()) ? &scene.materials[size_t(p.material)] : nullptr;

    // near-plane clip (Sutherland-Hodgman, keeps uv)
    poly.assign(tv.begin() + p.first, tv.begin() + p.first + p.count);
    clipped.clear();
    for (size_t i = 0; i < poly.size(); ++i) {
      const VV& a = poly[i];
      const VV& b = poly[(i + 1) % poly.size()];
      bool ain = a.z >= nearZ, bin = b.z >= nearZ;
      if (ain) clipped.push_back(a);
      if (ain != bin) {
        float t = (nearZ - a.z) / (b.z - a.z);
        clipped.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, nearZ, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t});
      }
    }
    if (clipped.size() < 3) continue;
    stats_.polysDrawn++;
    for (size_t k = 1; k + 1 < clipped.size(); ++k)
      rasterTri(scene, clipped[0], clipped[k], clipped[k + 1], mat, p.hasUV, light, uint8_t(p.backdrop ? 2 : p.scenery ? 1 : 0));
  }
}

namespace {
inline uint32_t shade(uint32_t rgb, float f) {
  uint32_t r = uint32_t(float((rgb >> 16) & 255) * f), g = uint32_t(float((rgb >> 8) & 255) * f), b = uint32_t(float(rgb & 255) * f);
  return 0xff000000u | (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
}
}  // namespace

void SoftwareRenderer::rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, uint8_t layer) {
  float sx[3], sy[3], iw[3], uw[3], vw[3];
  const VV* v[3] = {&a, &b, &c};
  for (int i = 0; i < 3; ++i) {
    float invz = 1.0f / v[i]->z;
    sx[i] = float(w_) * 0.5f + v[i]->x * invz * focal_;
    sy[i] = float(h_) * 0.5f - v[i]->y * invz * focal_;
    iw[i] = invz;
    uw[i] = v[i]->u * invz;
    vw[i] = v[i]->v * invz;
  }
  float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
  if (std::fabs(area) < 1e-4f) return;
  stats_.trisRastered++;
  int minx = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
  int maxx = std::min(w_ - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
  int miny = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
  int maxy = std::min(h_ - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
  if (minx > maxx || miny > maxy) return;
  const Texture* tex = (useTexture && mat && mat->texture >= 0) ? &scene.textures[size_t(mat->texture)] : nullptr;
  uint32_t flat = 0xff808080u;
  {
    int lo = mat ? mat->palStart : 0, hi = mat ? mat->palEnd : 0;
    int idx = lo + int(std::lround(float(hi - lo) * light));
    flat = shade(scene.palette.rgba[size_t(std::clamp(idx, 0, 255))], mat ? 1.0f : light);
  }
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
      // Layering of scenery against road (see docs/research-log.md): scenery must be clearly nearer than road to
      // cover it (near-ties and slight intrusions of building volumes into tunnel walls resolve to the road);
      // backdrop scenery never covers road. Among equal layers a later-drawn near-tie wins (coplanar decals).
      const float cur = depth_[o];
      const uint8_t curLayer = backdrop_[o];
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
      uint32_t col = flat;
      if (tex) {
        float u = (w0 * uw[0] + w1 * uw[1] + w2 * uw[2]) / z;
        float vv = (w0 * vw[0] + w1 * vw[1] + w2 * vw[2]) / z;
        u -= std::floor(u);
        vv -= std::floor(vv);
        int tx = std::min(tex->w - 1, int(u * float(tex->w)));
        int ty = std::min(tex->h - 1, int(vv * float(tex->h)));
        uint8_t pi = tex->index[size_t(ty) * size_t(tex->w) + size_t(tx)];
        if (pi == 0) continue;  // index 0 = transparent (INFERRED)
        col = shade(scene.palette.rgba[pi], 0.45f + 0.55f * light);
      }
      depth_[o] = z;
      backdrop_[o] = layer;
      color_[o] = col;
    }
  }
}

}  // namespace slip
