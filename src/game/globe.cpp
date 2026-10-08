#include "game/globe.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

bool Globe::load(const GameData& data) {
  auto sb = data.read("GLOBE.SHP");
  if (!sb) return false;
  auto sh = parseShape(*sb);
  if (!sh) return false;
  shape_ = std::move(*sh);
  for (int i = 0; i < 4; ++i)
    if (auto b = data.read("EARTH" + std::to_string(i + 1) + ".SPR"))
      if (auto sp = parseSprite(*b)) { tex_[size_t(i)].w = sp->w; tex_[size_t(i)].h = sp->h; tex_[size_t(i)].px = sp->pixels; }
  texOf_.clear();
  for (const MaterialRef& m : shape_.materials) {
    int n = 0;
    if (m.name.size() >= 5) n = m.name.back() - '0';
    texOf_.push_back(n >= 1 && n <= 4 ? &tex_[size_t(n - 1)] : nullptr);
  }
  if (auto fb = data.read("FLAG.SHP")) if (auto fs = parseShape(*fb)) flag_ = std::move(*fs);
  if (auto eb = data.read("EARTFLAG.SPR")) if (auto es = parseSprite(*eb)) { flagTex_.w = es->w; flagTex_.h = es->h; flagTex_.px = es->pixels; }
  return true;
}

namespace {
struct P { double x, y, z; };
}  // namespace

void Globe::drawShape(const Shape& s, const std::vector<Tex*>& texOf, uint32_t* buf, int w, int h, const Palette& pal, double cx, double cy, double dist, const double rot[9], const double origin[3],
                      int uOffset, bool shade, bool texturedFlat, uint8_t flatColor) const {
  const double F = 256.0;
  std::vector<P> v(s.verts.size());
  for (size_t i = 0; i < s.verts.size(); ++i) {
    const double x = double(s.verts[i].x) - origin[0], y = double(s.verts[i].y) - origin[1], z = double(s.verts[i].z) - origin[2];
    v[i] = {rot[0] * x + rot[1] * y + rot[2] * z, rot[3] * x + rot[4] * y + rot[5] * z, rot[6] * x + rot[7] * y + rot[8] * z + dist};
  }
  for (const Polygon& p : s.polys) {
    if (p.index.size() < 3) continue;
    // backface test on the transformed vertices (the view looks along +z, y up)
    const P &a = v[p.index[0]], &b = v[p.index[1]], &c = v[p.index[2]];
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (nx * a.x + ny * a.y + nz * a.z >= 0) continue;
    const Tex* tex = nullptr;
    {
      const int mi = int(p.material) - 1;
      if (p.uv.size() == p.index.size() && mi >= 0 && size_t(mi) < texOf.size()) tex = texOf[size_t(mi)];
      if (tex && tex->px.empty()) tex = nullptr;
      if (!tex && !texturedFlat) continue;
    }
    const bool flat = tex == nullptr;
    double light = 1.0;
    if (shade) {
      const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
      light = std::clamp(0.6 + 0.4 * std::max(0.0, (-nx * 0.35 + ny * 0.5 - nz * 0.8) / std::max(l, 1e-9)), 0.0, 1.0);
    }
    for (size_t t = 1; t + 1 < p.index.size(); ++t) {  // triangle fan
      const size_t ids[3] = {0, t, t + 1};
      double sx[3], sy[3], u[3] = {0, 0, 0}, vv[3] = {0, 0, 0};
      bool ok = true;
      for (int k = 0; k < 3; ++k) {
        const P& q = v[p.index[ids[k]]];
        if (q.z < 16) { ok = false; break; }
        sx[k] = cx + q.x * F / q.z;
        sy[k] = cy - q.y * F / q.z;
        if (!flat) { u[k] = p.uv[ids[k]][0]; vv[k] = p.uv[ids[k]][1]; }
      }
      if (!ok) continue;
      const double den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2]);
      if (std::fabs(den) < 1e-9) continue;
      const int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]})))), x1 = std::min(w - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
      const int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]})))), y1 = std::min(h - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const double px = x + 0.5, py = y + 0.5;
          const double l0 = ((sy[1] - sy[2]) * (px - sx[2]) + (sx[2] - sx[1]) * (py - sy[2])) / den;
          const double l1 = ((sy[2] - sy[0]) * (px - sx[2]) + (sx[0] - sx[2]) * (py - sy[2])) / den;
          const double l2 = 1.0 - l0 - l1;
          if (l0 < -0.001 || l1 < -0.001 || l2 < -0.001) continue;
          uint8_t idx = flatColor;
          if (!flat) {
            const double uu = l0 * u[0] + l1 * u[1] + l2 * u[2] + uOffset, tv = l0 * vv[0] + l1 * vv[1] + l2 * vv[2];
            int tx = int(std::floor(uu * tex->w / 16384.0)) % tex->w, ty = int(std::floor(tv * tex->h / 16384.0)) % tex->h;
            if (tx < 0) tx += tex->w;
            if (ty < 0) ty += tex->h;
            idx = tex->px[size_t(ty) * size_t(tex->w) + size_t(tx)];
          }
          uint32_t col = 0xff000000u | (pal.rgba[idx] & 0xffffffu);
          if (shade && light < 1.0) {
            const uint32_t r = uint32_t(double((col >> 16) & 255) * light), g = uint32_t(double((col >> 8) & 255) * light), b = uint32_t(double(col & 255) * light);
            col = 0xff000000u | (r << 16) | (g << 8) | b;
          }
          buf[size_t(y) * size_t(w) + size_t(x)] = col;
        }
    }
  }
}

void Globe::draw(uint32_t* buf, int w, int h, const Palette& pal, double cx, double cy, double dist, const double rot[9], int uOffset, bool shade, const Sprite* overrideTex) const {
  const double o[3] = {0, 0, 0};
  Tex own;
  std::vector<Tex*> over;
  if (overrideTex) { own.w = overrideTex->w; own.h = overrideTex->h; own.px = overrideTex->pixels; over.assign(texOf_.size(), &own); }
  drawShape(shape_, overrideTex ? over : texOf_, buf, w, h, pal, cx, cy, dist, rot, o, uOffset, shade, false, 0);
}

void Globe::drawFlag(uint32_t* buf, int w, int h, const Palette& pal, double cx, double cy, double dist, const double rot[9], const double dir[3]) const {
  if (flag_.polys.empty()) return;
  // place the flag on the surface: rotate it (model +y up) to `dir`, then translate by radius * dir
  const double R = double(shape_.radius);
  double up[3] = {dir[0], dir[1], dir[2]};
  // the cloth faces the viewer: the model-space direction towards the camera, made perpendicular to the pole
  double cam[3] = {-rot[6], -rot[7], -rot[8]};
  const double d = cam[0] * up[0] + cam[1] * up[1] + cam[2] * up[2];
  double fwd[3] = {cam[0] - d * up[0], cam[1] - d * up[1], cam[2] - d * up[2]};
  double l = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
  if (l < 1e-6) { fwd[0] = 1; fwd[1] = 0; fwd[2] = 0; l = 1; }
  for (double& x : fwd) x /= l;
  double right[3] = {up[1] * fwd[2] - up[2] * fwd[1], up[2] * fwd[0] - up[0] * fwd[2], up[0] * fwd[1] - up[1] * fwd[0]};
  // transform flag vertices by [right up fwd] and place them: build a combined matrix for drawShape by pre-transforming a copy of the shape
  Shape s = flag_;
  for (Vec3i& p : s.verts) {
    const double x = p.x, y = p.y, z = p.z;
    p.x = int32_t(std::lround(right[0] * x + up[0] * y + fwd[0] * z + dir[0] * R));
    p.y = int32_t(std::lround(right[1] * x + up[1] * y + fwd[1] * z + dir[1] * R));
    p.z = int32_t(std::lround(right[2] * x + up[2] * y + fwd[2] * z + dir[2] * R));
  }
  const double o[3] = {0, 0, 0};
  std::vector<Tex*> texOf(6, nullptr);
  texOf[5] = &const_cast<Tex&>(flagTex_);  // material id 6 = the flag cloth (EARTFLAG.SPR), id 5 = the pole (flat colour from the ramp 225..231)
  drawShape(s, texOf, buf, w, h, pal, cx, cy, dist, rot, o, 0, true, true, 228);
}

}  // namespace slip
