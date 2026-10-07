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
  itemBuf_.assign(size_t(w) * size_t(h), -1);
  recvBuf_.assign(size_t(w) * size_t(h), 0);
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
  std::fill(itemBuf_.begin(), itemBuf_.end(), -1);
  std::fill(recvBuf_.begin(), recvBuf_.end(), 0);
}

bool SoftwareRenderer::visAllows(uint16_t vis) const {
  if (vis == 0xFFFF || visMask == 0xFFFF) return true;
  if ((vis & visMask) == 0) return false;
  if (!(visMask & 8) && (vis & 8)) return false;  // class 8 only visible from class-8 records (0x39A89)
  return true;
}

bool SoftwareRenderer::sceneryBigEnough(const double c[3], float radius) const {
  if (minScenerySize <= 0 || radius <= 0) return true;
  const double dx = c[0] - cam_.pos[0], dy = c[1] - cam_.pos[1], dz = c[2] - cam_.pos[2];
  const double z = dx * fwd_[0] + dy * fwd_[1] + dz * fwd_[2];
  if (z <= cam_.nearPlane) return true;
  return double(radius) * 256.0 / z > double(minScenerySize);
}

void SoftwareRenderer::drawMesh(const Scene& scene, const Mesh& mesh, const MeshTransform& xf, const std::vector<uint32_t>* only, int item) {
  curItem_ = item;
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
  std::vector<uint8_t> instOk(mesh.instances.size(), 1);
  for (size_t i = 0; i < mesh.instances.size(); ++i) {
    const double c[3] = {xf.pos[0] + mesh.instances[i].center.x, xf.pos[1] + mesh.instances[i].center.y, xf.pos[2] + mesh.instances[i].center.z};
    instOk[i] = sceneryBigEnough(c, mesh.instances[i].radius) ? 1 : 0;
  }
  static const float L[3] = {0.35f, 0.85f, 0.40f};
  const float nearZ = cam_.nearPlane;
  std::vector<VV> poly, clipped;
  const size_t npol = only ? only->size() : mesh.polys.size();
  for (size_t pi_ = 0; pi_ < npol; ++pi_) {
    const MeshPoly& p = only ? mesh.polys[(*only)[pi_]] : mesh.polys[pi_];
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
    sx0_ = 0; sy0_ = 0; sx1_ = w_ - 1; sy1_ = h_ - 1;
    if (p.piece >= 0 && !pieceWin_.empty() && size_t(p.piece) < pieceWin_.size()) {
      const WinRect& wr = pieceWin_[size_t(p.piece)];
      if (!wr.vis) continue;
      sx0_ = wr.x0; sy0_ = wr.y0; sx1_ = wr.x1; sy1_ = wr.y1;
    } else if (sceneryWin_) {
      sx0_ = sceneryWin_->x0; sy0_ = sceneryWin_->y0; sx1_ = sceneryWin_->x1; sy1_ = sceneryWin_->y1;
    }
    if (!visAllows(p.vis)) continue;
    if (p.instance >= 0 && !instOk[size_t(p.instance)]) continue;
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
    const bool detailed = p.detail && mat && !p.hasUV && scene.panelDetails[p.detail].valid;
    const size_t polyIdx = only ? size_t((*only)[pi_]) : pi_;
    const bool receiver = shadows && !shadowCasters.empty() && p.piece >= 0 && (p.pflags & 2) && !p.scenery;
    const int baseId = receiver ? int((polyIdx + 1) << 2) : 0;
    recvId_ = baseId;
    if (detailed) {
      const PanelKind kind = scene.panelDetails[p.detail].kind;
      // types with bit 7 (floors, cages, road floors, chase floors) are drawn only by their routine (0x39640 -> 0x3F2C8)
      if (kind == PanelKind::Cage) { recvId_ = 0; drawCageLines(scene, p, tv, mat); continue; }
      if (kind == PanelKind::Floor || kind == PanelKind::RoadFloor) {
        if (kind == PanelKind::Floor) drawFloorDetail(scene, p, tv, mat); else drawRoadFloor(scene, p, tv, mat);
        recvId_ = 0;
        if (receiver) {
          // border polygons use the SDYellow fallback colour, lane polygons colour 0 (0x3F53B with eax = [0x3F030] / 0)
          const int yellow = scene.sdYellowMaterial >= 0 ? scene.materials[size_t(scene.sdYellowMaterial)].fallbackColor : 0;
          drawShadowsOn(scene, p, baseId, 1, yellow);
          drawShadowsOn(scene, p, baseId, 2, 0);
        }
        continue;
      }
      if (kind == PanelKind::ChaseFloor) { recvId_ = 0; drawChase(scene, p, tv, mat); continue; }
    }
    for (size_t k = 1; k + 1 < clipped.size(); ++k)
      rasterTri(scene, clipped[0], clipped[k], clipped[k + 1], mat, p.hasUV, light, int(std::clamp(ny, 0.0f, 1.0f) * 16384.0f), uint8_t(p.backdrop ? 2 : p.scenery ? 1 : 0));
    recvId_ = 0;
    if (receiver) drawShadowsOn(scene, p, baseId, 0, mat ? mat->fallbackColor : 0);  // 0x39738: colour = material +0x50
    if (detailed) {
      const PanelKind kind = scene.panelDetails[p.detail].kind;
      if (kind == PanelKind::Lines) drawPanelLines(scene, p, tv, mat, flatIndex(mat, int(std::clamp(ny, 0.0f, 1.0f) * 16384.0f)));
      else if (kind == PanelKind::ChaseOrange || kind == PanelKind::Refuel) drawChase(scene, p, tv, mat);
    }
  }
}

namespace {
inline uint32_t shade(uint32_t rgb, float f) {
  uint32_t r = uint32_t(float((rgb >> 16) & 255) * f), g = uint32_t(float((rgb >> 8) & 255) * f), b = uint32_t(float(rgb & 255) * f);
  return 0xff000000u | (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
}
}  // namespace

void SoftwareRenderer::rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, int upLight, uint8_t layer, int forceIdx) {
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
  int minx = std::max(sx0_, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
  int maxx = std::min(sx1_, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
  int miny = std::max(sy0_, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
  int maxy = std::min(sy1_, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
  if (minx > maxx || miny > maxy) return;
  const Texture* tex = (forceIdx < 0 && useTexture && mat && mat->texture >= 0) ? &scene.textures[size_t(mat->texture)] : nullptr;
  uint32_t flat = 0xff808080u;
  {
    // Flat polygon colour = ramp position from the original lighting law (see flatIndex).
    if (forceIdx >= 0) flat = shade(scene.palette.rgba[size_t(std::clamp(forceIdx, 0, 255))], 1.0f);
    else if (mat) flat = shade(scene.palette.rgba[size_t(std::clamp(flatIndex(mat, upLight), 0, 255))], 1.0f);
    else flat = shade(scene.palette.rgba[0], light);
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
      if (shadowRecv_) {
        if (recvBuf_[o] == shadowRecv_) color_[o] = flat;
        continue;
      }
      // Layering of scenery against road (see docs/research-log.md): scenery must be clearly nearer than road to
      // cover it (near-ties and slight intrusions of building volumes into tunnel walls resolve to the road);
      // backdrop scenery never covers road. Among equal layers a later-drawn near-tie wins (coplanar decals).
      const float cur = depth_[o];
      if (curItem_ >= 0) {
        if (itemBuf_[o] == curItem_ && z < cur * (1.0f - 2e-5f)) continue;
      } else {
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
      depth_[o] = z;
      backdrop_[o] = layer;
      itemBuf_[o] = curItem_;
      recvBuf_[o] = recvId_;
      color_[o] = col;
    }
  }
}

}  // namespace slip

namespace slip {
void SoftwareRenderer::computePortalVisibility(const Scene& scene) {
  pieceWin_.clear();
  if (!portalCulling || scene.pieceBoxes.empty()) return;
  const float cp[3] = {float(cam_.pos[0] - scene.origin[0]), float(cam_.pos[1] - scene.origin[1]), float(cam_.pos[2] - scene.origin[2])};
  // Only pieces that take part in the portal graph can be the camera cell; other pieces (decor, crowd, grid
  // markers) are always drawn.
  std::vector<uint8_t> inGraph(scene.pieceBoxes.size(), 0);
  for (size_t i = 0; i < inGraph.size(); ++i) inGraph[i] = scene.pieceBoxes[i].graph ? 1 : 0;
  int start = -1;
  double bestVol = 1e300;
  for (size_t i = 0; i < scene.pieceBoxes.size(); ++i) {
    const auto& b = scene.pieceBoxes[i];
    if (b.empty || !inGraph[i]) continue;
    const bool in = scene.pieceContains(i, cp);
    double vol = double(b.hi[0] - b.lo[0]) * double(b.hi[1] - b.lo[1]) * double(b.hi[2] - b.lo[2]);
    if (in && vol < bestVol) { bestVol = vol; start = int(i); }
  }
  if (start < 0) return;  // camera outside every piece: draw everything (the original also has no cell then)
  pieceWin_.assign(scene.pieceBoxes.size(), WinRect{});
  for (size_t i = 0; i < pieceWin_.size(); ++i)
    if (!inGraph[i]) pieceWin_[i] = WinRect{0, 0, w_ - 1, h_ - 1, true};
  union_ = WinRect{w_, h_, -1, -1, false};
  struct Rec {
    const Scene& sc; SoftwareRenderer& r; const float* cp;
    void visit(int i, WinRect rect, int from, int depth) {
      WinRect& w = r.pieceWin_[size_t(i)];
      if (w.vis) {
        WinRect u{std::min(w.x0, rect.x0), std::min(w.y0, rect.y0), std::max(w.x1, rect.x1), std::max(w.y1, rect.y1), true};
        if (u.x0 == w.x0 && u.y0 == w.y0 && u.x1 == w.x1 && u.y1 == w.y1) return;
        w = u;
        rect = u;
      } else {
        w = rect;
        w.vis = true;
      }
      r.addExtent(sc, size_t(i), cp);
      if (depth > 0x300) return;
      const auto& box = sc.pieceBoxes[size_t(i)];
      for (int j = 0; j < 3; ++j) {
        if (box.link[j] < 0 || box.link[j] == from) continue;
        const MeshPoly& p = sc.track.polys[size_t(box.linkPoly[j])];
        const Vec3& v0 = sc.track.verts[p.first];
        // portal normals point back into the piece that owns them (verified on all tracks), so the camera must be on that side
        if (p.normal.x * (cp[0] - v0.x) + p.normal.y * (cp[1] - v0.y) + p.normal.z * (cp[2] - v0.z) <= 0) continue;
        WinRect pr;
        if (!r.polyRect(sc, p, cp, rect, &pr)) continue;
        visit(box.link[j], pr, i, depth + 1);
      }
    }
  } rec{scene, *this, cp};
  rec.visit(start, WinRect{0, 0, w_ - 1, h_ - 1, true}, -1, 0);
  if (!union_.vis) {
    // The code only validates the window ([0x33EB0]) through extent polygons (flag 0x08 after the load-time rewrite: cages and
    // chase-light floors, transparent slopes); tracks such as Chicago have none. We then use the union of the portal windows
    // so the far-to-near pass and the scenery still run (UNVERIFIED against the original, see docs/research-log.md).
    union_ = WinRect{w_, h_, -1, -1, false};
    for (size_t i = 0; i < pieceWin_.size(); ++i)
      if (inGraph[i] && pieceWin_[i].vis) {
        union_.x0 = std::min(union_.x0, pieceWin_[i].x0); union_.y0 = std::min(union_.y0, pieceWin_[i].y0);
        union_.x1 = std::max(union_.x1, pieceWin_[i].x1); union_.y1 = std::max(union_.y1, pieceWin_[i].y1);
        union_.vis = true;
      }
  }
}
}  // namespace slip

namespace slip {
bool SoftwareRenderer::polyRect(const Scene& scene, const MeshPoly& p, const float cp[3], const WinRect& win, WinRect* out) const {
  struct P2 { float x, y; };
  struct V3 { float x, y, z; };
  std::vector<V3> in;
  for (uint16_t k = 0; k < p.count; ++k) {
    const Vec3& v = scene.track.verts[p.first + k];
    float rx = v.x - cp[0], ry = v.y - cp[1], rz = v.z - cp[2];
    in.push_back({rx * right_[0] + ry * right_[1] + rz * right_[2], rx * up_[0] + ry * up_[1] + rz * up_[2],
                  rx * fwd_[0] + ry * fwd_[1] + rz * fwd_[2]});
  }
  std::vector<V3> cl;  // near-plane clip
  for (size_t i = 0; i < in.size(); ++i) {
    const V3& a = in[i];
    const V3& b = in[(i + 1) % in.size()];
    const bool ain = a.z >= cam_.nearPlane, bin = b.z >= cam_.nearPlane;
    if (ain) cl.push_back(a);
    if (ain != bin) {
      float t = (cam_.nearPlane - a.z) / (b.z - a.z);
      cl.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, cam_.nearPlane});
    }
  }
  if (cl.size() < 3) return false;
  std::vector<P2> poly;
  for (const V3& v : cl) poly.push_back({float(w_) * 0.5f + v.x / v.z * focal_, float(h_) * 0.5f - v.y / v.z * focal_});
  // clip against the window rectangle, one edge at a time
  auto clipEdge = [&](int axis, float bound, bool keepGreater) {
    std::vector<P2> o;
    for (size_t i = 0; i < poly.size(); ++i) {
      const P2& a = poly[i];
      const P2& b = poly[(i + 1) % poly.size()];
      const float av = axis == 0 ? a.x : a.y, bv = axis == 0 ? b.x : b.y;
      const bool ain = keepGreater ? av >= bound : av <= bound, bin = keepGreater ? bv >= bound : bv <= bound;
      if (ain) o.push_back(a);
      if (ain != bin) {
        float t = (bound - av) / (bv - av);
        o.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
      }
    }
    poly.swap(o);
  };
  clipEdge(0, float(win.x0), true);
  if (!poly.empty()) clipEdge(0, float(win.x1) + 1.0f, false);
  if (!poly.empty()) clipEdge(1, float(win.y0), true);
  if (!poly.empty()) clipEdge(1, float(win.y1) + 1.0f, false);
  if (poly.size() < 3) return false;
  float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
  for (const P2& q : poly) { mnx = std::min(mnx, q.x); mxx = std::max(mxx, q.x); mny = std::min(mny, q.y); mxy = std::max(mxy, q.y); }
  out->x0 = std::max(win.x0, int(std::floor(mnx)) - 1); out->x1 = std::min(win.x1, int(std::ceil(mxx)) + 1);
  out->y0 = std::max(win.y0, int(std::floor(mny)) - 1); out->y1 = std::min(win.y1, int(std::ceil(mxy)) + 1);
  out->vis = true;
  return out->x0 <= out->x1 && out->y0 <= out->y1;
}

int SoftwareRenderer::flatIndex(const SurfaceMaterial* mat, int upLight) const {
  // CONFIRMED lighting law (0x1CCE9, 0x1C4B6, 0x1958F, track setup 0x595DD..0x595ED): light direction (0,-1,0) in
  // world space, diffuse level 0x3333 and ambient 0x0CCC (level 1.0 / ambient 0.25 normalised to a sum of 1.0), no fog.
  const int kLevel = 0x3333, kAmbient = 0x0CCC;
  int s;
  if (mat->fixedLight) s = mat->fixedLight;
  else if (!mat->ambientCoef && !mat->diffuseCoef && !mat->specularCoef) s = kLevel + kAmbient;
  else {
    const int diffuse = (upLight * kLevel) >> 14;
    s = ((kAmbient * mat->ambientCoef) >> 14) + ((diffuse * mat->diffuseCoef) >> 14);
  }
  s = std::clamp(s, 0, 0x4000);
  return int(mat->palStart) + (((int(mat->palEnd) - int(mat->palStart)) * s) >> 14);
}

std::vector<SoftwareRenderer::VV> SoftwareRenderer::detailPoints(const PanelDetail& d, const std::vector<VV>& tv, const MeshPoly& p, bool allowScreenMid) const {
  std::vector<VV> pts(d.mid.size());
  bool screenMid = false;
  if (allowScreenMid) {
    float nearest = 1e30f;
    for (uint16_t k = 0; k < p.count; ++k) nearest = std::min(nearest, tv[p.first + k].z);
    screenMid = nearest >= 683200.0f;  // 0xA6CC0: the far branch of 0x3F3C4 interpolates in 2D
  }
  for (size_t k = 0; k < pts.size(); ++k) {
    if (int(k) < d.nBase) { pts[k] = tv[p.first + k]; continue; }
    const VV &a = pts[d.mid[k][0] % pts.size()], &b = pts[d.mid[k][1] % pts.size()];
    if (screenMid) {
      const float ax = a.x / a.z, ay = a.y / a.z, bx = b.x / b.z, by = b.y / b.z;
      const float z = (a.z + b.z) * 0.5f;
      pts[k] = {(ax + bx) * 0.5f * z, (ay + by) * 0.5f * z, z, 0, 0};
    } else {
      pts[k] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f, 0, 0};
    }
  }
  return pts;
}

void SoftwareRenderer::fillIdxPoly(const Scene& scene, const SurfaceMaterial* mat, const std::vector<VV>& pts, const std::vector<uint16_t>& idx, int colorIdx) {
  std::vector<VV> poly, cl;
  for (uint16_t i : idx) poly.push_back(pts[i % pts.size()]);
  for (size_t i = 0; i < poly.size(); ++i) {
    const VV &a = poly[i], &b = poly[(i + 1) % poly.size()];
    const bool ain = a.z >= cam_.nearPlane, bin = b.z >= cam_.nearPlane;
    if (ain) cl.push_back(a);
    if (ain != bin) {
      float t = (cam_.nearPlane - a.z) / (b.z - a.z);
      cl.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, cam_.nearPlane, 0, 0});
    }
  }
  for (size_t k = 1; k + 1 < cl.size(); ++k) rasterTri(scene, cl[0], cl[k], cl[k + 1], mat, false, 1.0f, 0, 0, colorIdx);
}

void SoftwareRenderer::drawShadowsOn(const Scene& scene, const MeshPoly& rp, int baseId, int sub, int colorIdx) {
  if (rp.normal.y < 0.0193f) return;  // only surfaces facing the light (flag bit 1: n.y >= 316/16384)
  const Vec3 v0 = scene.track.verts[rp.first];
  const Vec3 n = rp.normal;
  float lox = 1e30f, hix = -1e30f, loz = 1e30f, hiz = -1e30f;
  for (uint16_t k = 0; k < rp.count; ++k) {
    const Vec3& v = scene.track.verts[rp.first + k];
    lox = std::min(lox, v.x); hix = std::max(hix, v.x); loz = std::min(loz, v.z); hiz = std::max(hiz, v.z);
  }
  float margin = 60000.0f;  // a ship's half-extent (scaled models)
  const double camRel[3] = {cam_.pos[0] - scene.origin[0], cam_.pos[1] - scene.origin[1], cam_.pos[2] - scene.origin[2]};
  const auto& pb = scene.pieceBoxes[size_t(rp.piece)];
  for (ShadowCaster& cs : shadowCasters) {
    if (!cs.mesh || cs.piece < 0) continue;
    if (cs.piece != rp.piece && cs.piece != pb.link[0] && cs.piece != pb.link[1] && cs.piece != pb.link[2]) continue;  // 0x397B1: the piece and its three neighbours
    const float cx = float(cs.xf.pos[0] - scene.origin[0]), cz = float(cs.xf.pos[2] - scene.origin[2]);
    if (cx < lox - margin || cx > hix + margin || cz < loz - margin || cz > hiz + margin) continue;
    const Mesh& m = *cs.mesh;
    if (cs.world.size() != m.verts.size()) {
      cs.world.resize(m.verts.size());
      for (size_t i = 0; i < m.verts.size(); ++i) {
        const Vec3& v = m.verts[i];
        cs.world[i] = {float(cs.xf.pos[0] + cs.xf.R[0] * v.x + cs.xf.R[1] * v.y + cs.xf.R[2] * v.z - scene.origin[0]),
                       float(cs.xf.pos[1] + cs.xf.R[3] * v.x + cs.xf.R[4] * v.y + cs.xf.R[5] * v.z - scene.origin[1]),
                       float(cs.xf.pos[2] + cs.xf.R[6] * v.x + cs.xf.R[7] * v.y + cs.xf.R[8] * v.z - scene.origin[2])};
      }
    }
    shadowRecv_ = baseId | sub;
    // project every caster vertex once for this receiver (camera space), then fill the polygons
    thread_local std::vector<VV> cam;
    cam.resize(cs.world.size());
    for (size_t i = 0; i < cs.world.size(); ++i) {
      const Vec3& w = cs.world[i];
      const double wy = double(v0.y) - (double(n.x) * (double(w.x) - double(v0.x)) + double(n.z) * (double(w.z) - double(v0.z))) / double(n.y) + 40.0;
      const double rx = double(w.x) - camRel[0], ry = wy - camRel[1], rz = double(w.z) - camRel[2];
      cam[i] = {float(rx * right_[0] + ry * right_[1] + rz * right_[2]), float(rx * up_[0] + ry * up_[1] + rz * up_[2]),
                float(rx * fwd_[0] + ry * fwd_[1] + rz * fwd_[2]), 0, 0};
    }
    thread_local std::vector<VV> poly, cl;
    for (const MeshPoly& cp : m.polys) {
      poly.assign(cam.begin() + cp.first, cam.begin() + cp.first + cp.count);
      cl.clear();
      for (size_t i = 0; i < poly.size(); ++i) {
        const VV &a = poly[i], &b = poly[(i + 1) % poly.size()];
        const bool ain = a.z >= cam_.nearPlane, bin = b.z >= cam_.nearPlane;
        if (ain) cl.push_back(a);
        if (ain != bin) {
          const float t = (cam_.nearPlane - a.z) / (b.z - a.z);
          cl.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, cam_.nearPlane, 0, 0});
        }
      }
      for (size_t k = 1; k + 1 < cl.size(); ++k) rasterTri(scene, cl[0], cl[k], cl[k + 1], nullptr, false, 1.0f, 0, 0, colorIdx);
    }
    shadowRecv_ = 0;
  }
}

void SoftwareRenderer::drawRoadFloor(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat) {
  // type 0x90 (0x4147E): two lane polygons (80 % ramp colour and one step darker) and three SDRoadLine polygons
  const PanelDetail& d = scene.panelDetails[p.detail];
  auto ramp80 = [](const SurfaceMaterial& m) { return int(m.palStart) + (((int(m.palEnd) - int(m.palStart)) * 0x3333) >> 14); };
  const int lane = ramp80(*mat);
  float nearest = 1e30f;
  for (uint16_t k = 0; k < p.count; ++k) nearest = std::min(nearest, tv[p.first + k].z);
  if (int(p.count) != d.nBase || nearest > float(d.gate)) {
    fillIdxPoly(scene, mat, std::vector<VV>(tv.begin() + p.first, tv.begin() + p.first + p.count), [&] { std::vector<uint16_t> v; for (uint16_t k = 0; k < p.count; ++k) v.push_back(k); return v; }(), lane);
    return;
  }
  const std::vector<VV> pts = detailPoints(d, tv, p, true);
  int laneDark = lane - 1;
  if (laneDark < int(mat->palStart)) laneDark = mat->fallbackColor;
  int line = lane;
  if (scene.sdRoadLineMaterial >= 0) line = ramp80(scene.materials[size_t(scene.sdRoadLineMaterial)]);
  const int baseRecvR = recvId_;
  if (baseRecvR) recvId_ = baseRecvR | 2;
  for (const auto& pl : d.polys) fillIdxPoly(scene, mat, pts, pl.idx, pl.yellow ? laneDark : lane);
  recvId_ = baseRecvR;
  for (const auto& pl : d.roadLine) fillIdxPoly(scene, mat, pts, pl.idx, line);
}

// Animated light types. Timer [0x3F078] counts frame time in 2.14 seconds; the chase phase is ((~t) & 0x1FFF) >> 11 (four
// steps per half second, 0x3FA95), the pulse colour a triangle wave of period one second blended 0x2000..0x3000 (0x3F26E).
void SoftwareRenderer::drawChase(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat) {
  const PanelDetail& d = scene.panelDetails[p.detail];
  float nearest = 1e30f;
  for (uint16_t k = 0; k < p.count; ++k) nearest = std::min(nearest, tv[p.first + k].z);
  if (int(p.count) != d.nBase || nearest > float(d.gate)) return;
  const std::vector<VV> pts = detailPoints(d, tv, p, d.kind != PanelKind::Refuel);
  const uint32_t t = animTimer;
  const int phase = int(((~t) & 0x1FFF) >> 11);
  auto range = [&](int matIdx, int* lo, int* hi) {
    if (matIdx < 0) return false;
    *lo = scene.materials[size_t(matIdx)].palStart;
    *hi = scene.materials[size_t(matIdx)].palEnd;
    return true;
  };
  if (d.kind == PanelKind::Refuel) {
    int lo, hi;
    if (!range(scene.sdBlueMaterial, &lo, &hi)) return;
    uint32_t w = t & 0x3FFF;
    if (w > 0x2000) w ^= 0x3FFF;
    const int f = int(0x2000 + (w >> 1));
    const int idx = lo + (((hi - lo) * f) >> 14);
    for (const auto& v : d.lampsA) fillIdxPoly(scene, mat, pts, v, idx);
    return;
  }
  int lo, hi;
  if (!range(d.kind == PanelKind::ChaseOrange ? scene.sdOrangeMaterial : scene.sdFloorLightMaterial, &lo, &hi)) return;
  if (d.kind == PanelKind::ChaseFloor) {
    // the base polygon itself is not drawn; eight pairs, the pair whose (index & 3) equals the phase is lit (end colour)
    for (size_t k = 0; k < d.lampsA.size(); ++k) {
      const int idx = (int(k & 3) == phase) ? hi : lo;
      fillIdxPoly(scene, mat, pts, d.lampsA[k], idx);
      fillIdxPoly(scene, mat, pts, d.lampsB[k], idx);
    }
    return;
  }
  // ChaseOrange: four pairs always, eight more at the highest detail
  for (size_t k = 0; k < d.lampsA.size(); ++k) {
    const int idx = (int(k) == phase) ? hi : lo;
    fillIdxPoly(scene, mat, pts, d.lampsA[k], idx);
    fillIdxPoly(scene, mat, pts, d.lampsB[k], idx);
  }
  for (size_t k = 0; k < d.lampsA2.size(); ++k) {
    const int idx = (int(k & 3) == phase) ? hi : lo;
    fillIdxPoly(scene, mat, pts, d.lampsA2[k], idx);
    fillIdxPoly(scene, mat, pts, d.lampsB2[k], idx);
  }
}

void SoftwareRenderer::drawLine3D(P3 a, P3 b, uint32_t col) {
  const float n = cam_.nearPlane;
  if (a.z < n && b.z < n) return;
  if (a.z < n) { float t = (n - a.z) / (b.z - a.z); a = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, n}; }
  else if (b.z < n) { float t = (n - b.z) / (a.z - b.z); b = {b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t, n}; }
  const float x0 = float(w_) * 0.5f + a.x / a.z * focal_, y0 = float(h_) * 0.5f - a.y / a.z * focal_;
  const float x1 = float(w_) * 0.5f + b.x / b.z * focal_, y1 = float(h_) * 0.5f - b.y / b.z * focal_;
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

bool SoftwareRenderer::projectToScreen(const double w[3], float* x, float* y, float* z) const {
  const float dx = float(w[0] - cam_.pos[0]), dy = float(w[1] - cam_.pos[1]), dz = float(w[2] - cam_.pos[2]);
  const float cz = dx * fwd_[0] + dy * fwd_[1] + dz * fwd_[2];
  if (cz <= cam_.nearPlane) return false;
  const float cx = dx * right_[0] + dy * right_[1] + dz * right_[2], cy = dx * up_[0] + dy * up_[1] + dz * up_[2];
  if (x) *x = float(w_) * 0.5f + cx / cz * focal_;
  if (y) *y = float(h_) * 0.5f - cy / cz * focal_;
  if (z) *z = cz;
  return true;
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

void SoftwareRenderer::drawRectScreen(int x0, int y0, int x1, int y1, uint32_t color) {
  auto put = [&](int x, int y) { if (x >= 0 && x < w_ && y >= 0 && y < h_) color_[size_t(y) * size_t(w_) + size_t(x)] = color; };
  for (int x = x0; x <= x1; ++x) { put(x, y0); put(x, y1); }
  for (int y = y0; y <= y1; ++y) { put(x0, y); put(x1, y); }
}

void SoftwareRenderer::drawCageLines(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat) {
  const PanelDetail& d = scene.panelDetails[p.detail];
  if (!d.valid || int(p.count) != d.nBase) return;
  float nearest = 1e30f;
  for (uint16_t k = 0; k < p.count; ++k) nearest = std::min(nearest, tv[p.first + k].z);
  if (nearest > float(d.gate)) return;
  const SurfaceMaterial* cm = (d.sdCage && scene.sdCageMaterial >= 0) ? &scene.materials[size_t(scene.sdCageMaterial)] : mat;
  const int idx = int(cm->palStart) + (((int(cm->palEnd) - int(cm->palStart)) * 0x3333) >> 14);  // 0x1A1C8 at 80 %
  const uint32_t col = 0xff000000u | (scene.palette.rgba[size_t(std::clamp(idx, 0, 255))] & 0xffffffu);
  const std::vector<VV> vp = detailPoints(d, tv, p, true);
  for (const auto& ln : d.lines)
    if (ln.a < vp.size() && ln.b < vp.size()) drawLine3D({vp[ln.a].x, vp[ln.a].y, vp[ln.a].z}, {vp[ln.b].x, vp[ln.b].y, vp[ln.b].z}, col);
}

void SoftwareRenderer::drawFloorDetail(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat) {
  const PanelDetail& d = scene.panelDetails[p.detail];
  auto ramp80 = [](const SurfaceMaterial& m) { return int(m.palStart) + (((int(m.palEnd) - int(m.palStart)) * 0x3333) >> 14); };  // 0x1A1C8, fog off
  const int lane = ramp80(*mat);
  auto fan = [&](const std::vector<VV>& poly, int idx) {
    std::vector<VV> cl;
    for (size_t i = 0; i < poly.size(); ++i) {
      const VV &a = poly[i], &b = poly[(i + 1) % poly.size()];
      const bool ain = a.z >= cam_.nearPlane, bin = b.z >= cam_.nearPlane;
      if (ain) cl.push_back(a);
      if (ain != bin) {
        float t = (cam_.nearPlane - a.z) / (b.z - a.z);
        cl.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, cam_.nearPlane, 0, 0});
      }
    }
    for (size_t k = 1; k + 1 < cl.size(); ++k) rasterTri(scene, cl[0], cl[k], cl[k + 1], mat, false, 1.0f, 0, 0, idx);
  };
  const int baseRecv = recvId_;
  float nearest = 1e30f;
  for (uint16_t k = 0; k < p.count; ++k) nearest = std::min(nearest, tv[p.first + k].z);
  bool near = nearest >= cam_.nearPlane;
  if (!d.valid || int(p.count) != d.nBase || !near || nearest > float(d.gate)) {
    // far branch (0x40B84): the polygon in the 80 % lane colour
    std::vector<VV> poly(tv.begin() + p.first, tv.begin() + p.first + p.count);
    if (baseRecv) recvId_ = baseRecv | 2;
    fan(poly, lane);
    recvId_ = baseRecv;
    return;
  }
  std::vector<VV> pts(d.mid.size());
  for (size_t k = 0; k < pts.size(); ++k) {
    if (int(k) < d.nBase) pts[k] = tv[p.first + k];
    else {
      const VV &a = pts[d.mid[k][0] % pts.size()], &b = pts[d.mid[k][1] % pts.size()];
      pts[k] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f, 0, 0};
    }
  }
  int yellow = lane;
  if (scene.sdYellowMaterial >= 0) yellow = ramp80(scene.materials[size_t(scene.sdYellowMaterial)]);
  for (const auto& pl : d.polys) {
    std::vector<VV> poly;
    for (uint16_t i : pl.idx) poly.push_back(pts[i % pts.size()]);
    if (baseRecv) recvId_ = baseRecv | (pl.yellow ? 1 : 2);
    fan(poly, pl.yellow ? yellow : lane);
    recvId_ = baseRecv;
  }
  const int cPlus = std::min(lane + 1, int(mat->palEnd));
  int cMinus = lane - 1;
  if (cMinus < int(mat->palStart)) cMinus = mat->fallbackColor;
  for (const auto& ln : d.lines) {
    if (ln.a >= pts.size() || ln.b >= pts.size()) continue;
    const uint32_t col = 0xff000000u | (scene.palette.rgba[size_t(std::clamp(ln.darker ? cMinus : cPlus, 0, 255))] & 0xffffffu);
    drawLine3D({pts[ln.a].x, pts[ln.a].y, pts[ln.a].z}, {pts[ln.b].x, pts[ln.b].y, pts[ln.b].z}, col);
  }
}

void SoftwareRenderer::drawPanelLines(const Scene& scene, const MeshPoly& p, const std::vector<VV>& tv, const SurfaceMaterial* mat, int flatIdx) {
  const PanelDetail& d = scene.panelDetails[p.detail];
  if (!d.valid || int(p.count) != d.nBase || !mat) return;
  float nearest = 1e30f;
  for (uint16_t k = 0; k < p.count; ++k) nearest = std::min(nearest, tv[p.first + k].z);
  if (nearest < cam_.nearPlane || (d.gate && nearest > float(d.gate))) return;
  struct P3 { float x, y, z; };
  std::vector<P3> pts(d.mid.size());
  for (size_t k = 0; k < pts.size(); ++k) {
    if (int(k) < d.nBase) { const VV& v = tv[p.first + k]; pts[k] = {v.x, v.y, v.z}; }
    else {
      const P3 &a = pts[d.mid[k][0] % pts.size()], &b = pts[d.mid[k][1] % pts.size()];
      pts[k] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
    }
  }
  const int cPlus = flatIdx + 1;
  int cMinus = flatIdx - 1;
  if (cMinus < int(mat->palStart)) cMinus = mat->fallbackColor;  // 0x1A98C
  const uint32_t colPlus = 0xff000000u | (scene.palette.rgba[size_t(std::clamp(cPlus, 0, 255))] & 0xffffffu);
  const uint32_t colMinus = 0xff000000u | (scene.palette.rgba[size_t(std::clamp(cMinus, 0, 255))] & 0xffffffu);
  for (const auto& ln : d.lines) {
    if (ln.a >= pts.size() || ln.b >= pts.size()) continue;
    drawLine3D({pts[ln.a].x, pts[ln.a].y, pts[ln.a].z}, {pts[ln.b].x, pts[ln.b].y, pts[ln.b].z}, ln.darker ? colMinus : colPlus);
  }
}

void SoftwareRenderer::addExtent(const Scene& scene, size_t piece, const float cp[3]) {
  if (piece >= scene.piecePolys.size()) return;
  for (uint32_t pi : scene.piecePolys[piece]) {
    const MeshPoly& p = scene.track.polys[pi];
    if (!(p.pflags & 8) || p.count < 3) continue;
    const Vec3& v0 = scene.track.verts[p.first];
    if (p.normal.x * (cp[0] - v0.x) + p.normal.y * (cp[1] - v0.y) + p.normal.z * (cp[2] - v0.z) <= 0) continue;  // facing away
    WinRect r;
    if (!polyRect(scene, p, cp, WinRect{0, 0, w_ - 1, h_ - 1, true}, &r)) continue;
    union_.x0 = std::min(union_.x0, r.x0); union_.y0 = std::min(union_.y0, r.y0);
    union_.x1 = std::max(union_.x1, r.x1); union_.y1 = std::max(union_.y1, r.y1);
    union_.vis = true;
  }
}

bool SoftwareRenderer::boxInFrustum(const Scene& scene, const float lo[3], const float hi[3]) const {
  const double cp[3] = {cam_.pos[0] - scene.origin[0], cam_.pos[1] - scene.origin[1], cam_.pos[2] - scene.origin[2]};
  const double hx = double(w_) * 0.5 / double(focal_), hy = double(h_) * 0.5 / double(focal_);
  bool allBehind = true, allL = true, allR = true, allU = true, allD = true;
  for (int c = 0; c < 8; ++c) {
    const double rx = ((c & 1) ? hi[0] : lo[0]) - cp[0], ry = ((c & 2) ? hi[1] : lo[1]) - cp[1], rz = ((c & 4) ? hi[2] : lo[2]) - cp[2];
    const double x = rx * right_[0] + ry * right_[1] + rz * right_[2];
    const double y = rx * up_[0] + ry * up_[1] + rz * up_[2];
    const double z = rx * fwd_[0] + ry * fwd_[1] + rz * fwd_[2];
    allBehind &= z < cam_.nearPlane;
    allL &= x < -z * hx;
    allR &= x > z * hx;
    allU &= y > z * hy;
    allD &= y < -z * hy;
  }
  return !(allBehind || allL || allR || allU || allD);
}
}  // namespace slip
