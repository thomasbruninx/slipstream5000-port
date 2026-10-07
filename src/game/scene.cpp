#include <functional>
#include <cstdio>
#include <cstdlib>
#include "game/scene.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace slip {

namespace {

constexpr float kFix14 = 1.0f / 16384.0f;

struct Builder {
  const GameData& data;
  Scene& scene;
  MaterialSet mats;  // global material table (track MAT followed by CARS.MAT)
  std::map<std::string, int> textureCache;

  int textureFor(const std::string& pattern) {
    if (pattern.empty()) return -1;
    auto it = textureCache.find(pattern);
    if (it != textureCache.end()) return it->second;
    int idx = -1;
    auto names = data.glob(pattern);
    for (auto& n : names) {
      auto b = data.read(n);
      if (!b) continue;
      auto spr = parseSprite(*b);
      if (!spr) continue;
      Texture t;
      t.w = spr->w;
      t.h = spr->h;
      t.index = std::move(spr->pixels);
      scene.textures.push_back(std::move(t));
      idx = int(scene.textures.size()) - 1;
      break;
    }
    textureCache[pattern] = idx;
    return idx;
  }

  void buildMaterials() {
    scene.materials.clear();
    for (auto& m : mats.mats) {
      SurfaceMaterial sm;
      sm.name = m.name;
      sm.palStart = m.palStart;
      sm.palEnd = m.palEnd;
      {
        std::string ln = m.name;
        for (auto& ch : ln) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        sm.invisible = (ln == "dummy");
      }
      sm.texture = textureFor(m.pattern);
      scene.materials.push_back(std::move(sm));
    }
  }

  static Vec3 unitNormal(int16_t nx, int16_t ny, int16_t nz) {
    Vec3 n{nx * kFix14, ny * kFix14, nz * kFix14};
    float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (l > 1e-6f) n = n * (1.0f / l);
    return n;
  }

  int globalMaterial(const std::vector<MaterialRef>& table, uint16_t local) {
    for (auto& r : table)
      if (r.id == local) return mats.find(r.name);
    return -1;
  }
};

}  // namespace

bool buildScene(const GameData& data, int trackIndex, Scene* out, std::string* error, float shipScale) {
  auto s = std::make_unique<Scene>();
  s->shipScale = shipScale;
  Track t;
  if (!loadTrack(data, trackIndex, &t, error)) return false;
  s->palette = t.palette;
  s->trackIndex = trackIndex;
  s->trackName = t.name;
  Builder b{data, *s, t.materials, {}};
  b.buildMaterials();

  // Origin: first start position keeps float coordinates small and exactly representable.
  s->origin = {double(t.start[0].x), double(t.start[0].y), double(t.start[0].z)};
  for (int i = 0; i < 10; ++i) s->startPos[size_t(i)] = {double(t.start[size_t(i)].x), double(t.start[size_t(i)].y), double(t.start[size_t(i)].z)};

  auto addPoly = [&](Mesh& m, const std::vector<Vec3>& verts, const std::vector<std::array<uint16_t, 2>>& uv, int mat,
                     Vec3 normal) {
    MeshPoly p;
    p.first = uint32_t(m.verts.size());
    p.count = uint16_t(verts.size());
    p.material = mat;
    p.hasUV = !uv.empty();
    p.normal = normal;
    for (size_t i = 0; i < verts.size(); ++i) {
      m.verts.push_back(verts[i]);
      if (p.hasUV) {
        m.uv.push_back(uv[i][0] * kFix14);
        m.uv.push_back(uv[i][1] * kFix14);
      } else {
        m.uv.push_back(0);
        m.uv.push_back(0);
      }
    }
    m.polys.push_back(p);
  };

  // --- track pieces ---
  for (auto& pc : t.pieces) {
    const TrackRecord& rec = t.records[size_t(pc.record)];
    Vec3 base{float(double(pc.pos.x) - s->origin[0]), float(double(pc.pos.y) - s->origin[1]), float(double(pc.pos.z) - s->origin[2])};
    for (auto& poly : rec.polys) {
      std::vector<Vec3> vs;
      bool bad = false;
      for (uint16_t i : poly.index) {
        if (i >= rec.verts.size()) { bad = true; break; }
        const Vec3i& v = rec.verts[i];
        vs.push_back({base.x + float(v.x), base.y + float(v.y), base.z + float(v.z)});
      }
      if (bad || vs.size() < 3) continue;
      addPoly(s->track, vs, poly.uv, b.globalMaterial(t.trcMaterials, poly.material), Builder::unitNormal(poly.nx, poly.ny, poly.nz));
      s->track.polys.back().vis = rec.visFlags;
    }
  }

  // --- scenery shape instances (orientation convention SPECULATIVE: v' = v * M, 2.14) ---
  // World-space boxes of the road pieces (walls/roof included). Tall scenery (towers) that the road passes
  // through has the part inside these boxes cut away: buildings cannot occupy the drivable corridor.
  struct Box { Vec3 lo, hi; };
  std::vector<Box> roadBoxes;
  for (auto& pc : t.pieces) {
    const TrackRecord& rec = t.records[size_t(pc.record)];
    if (rec.verts.empty()) continue;
    Box bx{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (const Vec3i& v : rec.verts) {
      float x = float(double(pc.pos.x) - s->origin[0] + v.x), y = float(double(pc.pos.y) - s->origin[1] + v.y), z = float(double(pc.pos.z) - s->origin[2] + v.z);
      bx.lo = {std::min(bx.lo.x, x), std::min(bx.lo.y, y), std::min(bx.lo.z, z)};
      bx.hi = {std::max(bx.hi.x, x), std::max(bx.hi.y, y), std::max(bx.hi.z, z)};
    }
    const float e = 0.01f * std::max({bx.hi.x - bx.lo.x, bx.hi.y - bx.lo.y, bx.hi.z - bx.lo.z});
    bx.lo = bx.lo - Vec3{e, e, e};
    bx.hi = bx.hi + Vec3{e, e, e};
    roadBoxes.push_back(bx);
    {
      Scene::PieceBox pb{};
      pb.flags = rec.visFlags;
      float lx = 1e30f, ly = 1e30f, lz = 1e30f, hx = -1e30f, hy = -1e30f, hz = -1e30f;
      for (const Vec3i& v : rec.verts) {
        float x = float(double(pc.pos.x) - s->origin[0] + v.x), y = float(double(pc.pos.y) - s->origin[1] + v.y), z = float(double(pc.pos.z) - s->origin[2] + v.z);
        lx = std::min(lx, x); ly = std::min(ly, y); lz = std::min(lz, z); hx = std::max(hx, x); hy = std::max(hy, y); hz = std::max(hz, z);
      }
      pb.lo[0] = lx; pb.lo[1] = ly; pb.lo[2] = lz; pb.hi[0] = hx; pb.hi[1] = hy; pb.hi[2] = hz;
      s->pieceBoxes.push_back(pb);
    }
  }
  std::map<std::string, std::optional<Shape>> shapeCache;
  for (auto& inst : t.scenery) {
    auto it = shapeCache.find(inst.shape);
    if (it == shapeCache.end()) {
      std::optional<Shape> sh;
      if (auto bytes = data.read(inst.shape)) sh = parseShape(*bytes);
      it = shapeCache.emplace(inst.shape, std::move(sh)).first;
    }
    if (!it->second) { s->scenerySkipped++; continue; }
    const Shape& sh = *it->second;
    Vec3 base{float(double(inst.pos.x) - s->origin[0]), float(double(inst.pos.y) - s->origin[1]), float(double(inst.pos.z) - s->origin[2])};
    const int16_t* m = inst.matrix;
    struct SPoly { std::vector<Vec3> v; std::vector<std::array<float, 2>> uv; int mat; Vec3 n; };
    std::vector<SPoly> sps;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (auto& poly : sh.polys) {
      SPoly sp;
      bool bad = false;
      for (size_t k = 0; k < poly.index.size(); ++k) {
        uint16_t i = poly.index[k];
        if (i >= sh.verts.size()) { bad = true; break; }
        float x = float(sh.verts[i].x), y = float(sh.verts[i].y), z = float(sh.verts[i].z);
        sp.v.push_back({base.x + (x * m[0] + y * m[3] + z * m[6]) * kFix14, base.y + (x * m[1] + y * m[4] + z * m[7]) * kFix14,
                        base.z + (x * m[2] + y * m[5] + z * m[8]) * kFix14});
        if (!poly.uv.empty()) sp.uv.push_back({float(poly.uv[k][0]), float(poly.uv[k][1])});
      }
      if (bad || sp.v.size() < 3) continue;
      sp.mat = b.globalMaterial(sh.materials, poly.material);
      sp.n = Builder::unitNormal(poly.nx, poly.ny, poly.nz);
      for (auto& v : sp.v) {
        lo.x = std::min(lo.x, v.x); lo.y = std::min(lo.y, v.y); lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x); hi.y = std::max(hi.y, v.y); hi.z = std::max(hi.z, v.z);
      }
      sps.push_back(std::move(sp));
    }
    // Only tall volumes (towers); flat spans such as bridges legitimately pass over the road.
    const bool tall = (hi.y - lo.y) > 0.6f * std::max(hi.x - lo.x, hi.z - lo.z);
    std::vector<const Box*> cut;
    if (tall)
      for (const Box& bx : roadBoxes)
        if (bx.lo.x < hi.x && bx.hi.x > lo.x && bx.lo.y < hi.y && bx.hi.y > lo.y && bx.lo.z < hi.z && bx.hi.z > lo.z) cut.push_back(&bx);

    auto emit = [&](const SPoly& sp) {
      std::vector<std::array<uint16_t, 2>> uv;
      for (auto& q : sp.uv) uv.push_back({uint16_t(std::clamp(std::lround(q[0]), 0L, 65535L)), uint16_t(std::clamp(std::lround(q[1]), 0L, 65535L))});
      addPoly(s->track, sp.v, uv, sp.mat, sp.n);
      s->track.polys.back().scenery = true;
      s->track.polys.back().vis = inst.visMask;
      s->track.polys.back().backdrop = !cut.empty();
    };
    // Subtract a box from a convex polygon: emit the parts outside each slab in turn.
    std::function<void(SPoly, size_t)> subtract = [&](SPoly sp, size_t bi) {
      if (bi == cut.size()) { emit(sp); return; }
      const Box& bx = *cut[bi];
      SPoly cur = std::move(sp);
      for (int axis = 0; axis < 3 && cur.v.size() >= 3; ++axis)
        for (int side = 0; side < 2 && cur.v.size() >= 3; ++side) {
          auto comp = [&](const Vec3& v) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; };
          const float plane = side == 0 ? (axis == 0 ? bx.lo.x : axis == 1 ? bx.lo.y : bx.lo.z) : (axis == 0 ? bx.hi.x : axis == 1 ? bx.hi.y : bx.hi.z);
          const float sgn = side == 0 ? -1.0f : 1.0f;  // outside = sgn * (c - plane) > 0
          SPoly out = cur, in = cur;
          out.v.clear(); out.uv.clear(); in.v.clear(); in.uv.clear();
          for (size_t i = 0; i < cur.v.size(); ++i) {
            size_t j = (i + 1) % cur.v.size();
            float di = sgn * (comp(cur.v[i]) - plane), dj = sgn * (comp(cur.v[j]) - plane);
            auto push = [&](SPoly& d, size_t k) { d.v.push_back(cur.v[k]); if (!cur.uv.empty()) d.uv.push_back(cur.uv[k]); };
            if (di > 0) push(out, i); else push(in, i);
            if ((di > 0) != (dj > 0)) {
              float tt = di / (di - dj);
              Vec3 pv = cur.v[i] + (cur.v[j] - cur.v[i]) * tt;
              std::array<float, 2> pu{};
              if (!cur.uv.empty()) pu = {cur.uv[i][0] + (cur.uv[j][0] - cur.uv[i][0]) * tt, cur.uv[i][1] + (cur.uv[j][1] - cur.uv[i][1]) * tt};
              out.v.push_back(pv); in.v.push_back(pv);
              if (!cur.uv.empty()) { out.uv.push_back(pu); in.uv.push_back(pu); }
            }
          }
          if (out.v.size() >= 3) subtract(out, bi + 1);
          cur = std::move(in);
        }
      // whatever remains in `cur` is inside the box: dropped
    };
    for (auto& sp : sps) subtract(sp, 0);
  }

  // --- ship models (10 ART files): body shape of the root node plus first shape of each child ---
  for (int i = 0; i < 10; ++i) {
    auto artBytes = data.read("RACER" + std::to_string(i) + ".ART");
    if (!artBytes) continue;
    auto art = parseArt(*artBytes);
    if (!art) continue;
    // accumulate node offsets down the tree
    std::vector<Vec3i> acc(art->nodes.size());
    for (size_t n = 0; n < art->nodes.size(); ++n) {
      acc[n] = art->nodes[n].offset;
      int par = art->nodes[n].parent;
      if (par >= 0 && size_t(par) < n) {
        acc[n].x += acc[size_t(par)].x;
        acc[n].y += acc[size_t(par)].y;
        acc[n].z += acc[size_t(par)].z;
      }
    }
    for (size_t n = 0; n < art->nodes.size(); ++n) {
      const ArtNode& node = art->nodes[n];
      if (node.shapes.empty()) continue;
      auto bytes = data.read(node.shapes[0]);
      if (!bytes) continue;
      auto sh = parseShape(*bytes);
      if (!sh) continue;
      for (auto& poly : sh->polys) {
        std::vector<Vec3> vs;
        bool bad = false;
        for (uint16_t k : poly.index) {
          if (k >= sh->verts.size()) { bad = true; break; }
          vs.push_back({float(sh->verts[k].x + acc[n].x) * s->shipScale, float(sh->verts[k].y + acc[n].y) * s->shipScale,
                        float(sh->verts[k].z + acc[n].z) * s->shipScale});
        }
        if (bad || vs.size() < 3) continue;
        addPoly(s->shipMeshes[size_t(i)], vs, poly.uv, b.globalMaterial(sh->materials, poly.material), Builder::unitNormal(poly.nx, poly.ny, poly.nz));
      }
    }
  }

  s->track_data = std::move(t);
  s->buildFloorIndex();
  s->hidePortalPolys();
  *out = std::move(*s);
  return true;
}

bool buildShapePreview(const GameData& data, const std::string& shapeName, Scene* out, std::string* error) {
  Scene s;
  Track t;
  if (!loadTrack(data, 1, &t, error)) return false;
  s.palette = t.palette;
  Builder b{data, s, t.materials, {}};
  // Scenery shapes use their own track's materials; try every track MAT so names resolve.
  for (int i = 2; i <= 10; ++i) {
    Track o;
    std::string e;
    if (loadTrack(data, i, &o, &e)) {
      MaterialSet extra = o.materials;
      b.mats.append(extra);
    }
  }
  b.buildMaterials();
  auto bytes = data.read(shapeName);
  if (!bytes) { if (error) *error = "shape not found: " + shapeName; return false; }
  auto sh = parseShape(*bytes);
  if (!sh) { if (error) *error = "not a version-12 .SHP: " + shapeName; return false; }
  for (auto& poly : sh->polys) {
    std::vector<Vec3> vs;
    bool bad = false;
    for (uint16_t k : poly.index) {
      if (k >= sh->verts.size()) { bad = true; break; }
      vs.push_back({float(sh->verts[k].x), float(sh->verts[k].y), float(sh->verts[k].z)});
    }
    if (bad || vs.size() < 3) continue;
    MeshPoly p;
    p.first = uint32_t(s.track.verts.size());
    p.count = uint16_t(vs.size());
    p.material = b.globalMaterial(sh->materials, poly.material);
    p.hasUV = !poly.uv.empty();
    p.normal = Builder::unitNormal(poly.nx, poly.ny, poly.nz);
    for (size_t i = 0; i < vs.size(); ++i) {
      s.track.verts.push_back(vs[i]);
      s.track.uv.push_back(p.hasUV ? poly.uv[i][0] * kFix14 : 0);
      s.track.uv.push_back(p.hasUV ? poly.uv[i][1] * kFix14 : 0);
    }
    s.track.polys.push_back(p);
  }
  s.trackName = shapeName;
  *out = std::move(s);
  return true;
}

void Scene::buildFloorIndex() {
  floorTris_.clear();
  floorGrid_.clear();
  for (auto& p : track.polys) {
    if (p.normal.y < 0.5f) continue;  // only up-facing surfaces count as ground
    for (uint16_t k = 1; k + 1 < p.count; ++k) {
      FloorTri t;
      const Vec3 &a = track.verts[p.first], &b = track.verts[p.first + k], &c = track.verts[p.first + k + 1];
      t.a[0] = a.x; t.a[1] = a.y; t.a[2] = a.z;
      t.b[0] = b.x; t.b[1] = b.y; t.b[2] = b.z;
      t.c[0] = c.x; t.c[1] = c.y; t.c[2] = c.z;
      uint32_t id = uint32_t(floorTris_.size());
      floorTris_.push_back(t);
      float minx = std::min({a.x, b.x, c.x}), maxx = std::max({a.x, b.x, c.x});
      float minz = std::min({a.z, b.z, c.z}), maxz = std::max({a.z, b.z, c.z});
      for (int cx = int(std::floor(minx / kCell)); cx <= int(std::floor(maxx / kCell)); ++cx)
        for (int cz = int(std::floor(minz / kCell)); cz <= int(std::floor(maxz / kCell)); ++cz)
          floorGrid_[(int64_t(cx) << 32) ^ int64_t(uint32_t(cz))].push_back(id);
    }
  }
}

bool Scene::floorHeight(double wx, double wz, double yHint, double margin, double* y) const {
  double x = wx - origin[0], z = wz - origin[2], hint = yHint - origin[1];
  int cx = int(std::floor(x / kCell)), cz = int(std::floor(z / kCell));
  auto it = floorGrid_.find((int64_t(cx) << 32) ^ int64_t(uint32_t(cz)));
  if (it == floorGrid_.end()) return false;
  bool found = false;
  double best = -1e30;
  for (uint32_t id : it->second) {
    const FloorTri& t = floorTris_[id];
    double d = (t.b[2] - t.c[2]) * (t.a[0] - t.c[0]) + (t.c[0] - t.b[0]) * (t.a[2] - t.c[2]);
    if (std::fabs(d) < 1e-6) continue;
    double l1 = ((t.b[2] - t.c[2]) * (x - t.c[0]) + (t.c[0] - t.b[0]) * (z - t.c[2])) / d;
    double l2 = ((t.c[2] - t.a[2]) * (x - t.c[0]) + (t.a[0] - t.c[0]) * (z - t.c[2])) / d;
    double l3 = 1.0 - l1 - l2;
    if (l1 < -1e-4 || l2 < -1e-4 || l3 < -1e-4) continue;
    double h = l1 * t.a[1] + l2 * t.b[1] + l3 * t.c[1];
    if (h <= hint + margin && h > best) { best = h; found = true; }
  }
  if (found) *y = best + origin[1];
  return found;
}

}  // namespace slip

namespace slip {
// A flat (untextured), vertical polygon whose plane the road passes straight through (drivable floor at about
// the same height on both sides, polygon spanning from the floor upwards) cannot be a solid wall: the ships
// would have to drive through it. Such polygons are sector-boundary caps/portals ("Trench Ent" in Chicago);
// INFERRED, the original's rule for skipping them is unknown.
void Scene::hidePortalPolys() {
  for (auto& p : track.polys) {
    if (p.scenery || p.count < 3 || std::fabs(p.normal.y) > 0.35f) continue;
    if (p.material < 0 || size_t(p.material) >= materials.size()) continue;
    // Name gate: only entrance caps ("... Ent"). Other flat vertical surfaces that cross the road (start cages,
    // walls of other tracks) are real geometry as far as we can tell.
    std::string ln = materials[size_t(p.material)].name;
    while (!ln.empty() && ln.back() == ' ') ln.pop_back();
    for (auto& ch : ln) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (ln.size() < 4 || ln.compare(ln.size() - 4, 4, " ent") != 0) continue;
    float hx = p.normal.x, hz = p.normal.z, hl = std::sqrt(hx * hx + hz * hz);
    if (hl < 0.5f) continue;
    hx /= hl; hz /= hl;
    Vec3 c{0, 0, 0};
    float lo = 1e30f, hi = -1e30f;
    for (uint16_t k = 0; k < p.count; ++k) {
      const Vec3& v = track.verts[p.first + k];
      c = c + v * (1.0f / float(p.count));
      lo = std::min(lo, v.y); hi = std::max(hi, v.y);
    }
    double yf, yb;
    const double wx = double(c.x) + origin[0], wz = double(c.z) + origin[2], hint = double(lo) + origin[1];
    if (!floorHeight(wx + hx * 4000, wz + hz * 4000, hint, 25000, &yf)) continue;
    if (!floorHeight(wx - hx * 4000, wz - hz * 4000, hint, 25000, &yb)) continue;
    const double fl = std::max(yf, yb) - origin[1];
    if (std::fabs(yf - yb) < 30000 && double(lo) <= fl + 20000 && double(hi) > fl + 25000) p.hidden = true;
  }
}
}  // namespace slip

namespace slip {
uint16_t Scene::visMaskAt(double wx, double wy, double wz) const {
  if (pieceBoxes.empty()) return 0xFFFF;
  const float p[3] = {float(wx - origin[0]), float(wy - origin[1]), float(wz - origin[2])};
  double best = 1e300, bestVol = 1e300;
  uint16_t flags = 0xFFFF;
  for (const PieceBox& b : pieceBoxes) {
    double d2 = 0;
    for (int k = 0; k < 3; ++k) {
      double d = p[k] < b.lo[k] ? b.lo[k] - p[k] : p[k] > b.hi[k] ? p[k] - b.hi[k] : 0.0;
      d2 += d * d;
    }
    double vol = double(b.hi[0] - b.lo[0]) * double(b.hi[1] - b.lo[1]) * double(b.hi[2] - b.lo[2]);
    if (d2 < best || (d2 == best && vol < bestVol)) { best = d2; bestVol = vol; flags = b.flags; }
  }
  const uint16_t m = uint16_t(flags & 0x5F);
  return m ? m : uint16_t(0xFFFF);  // empty class set: draw everything (INFERRED safety)
}
}  // namespace slip
