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
      t.transparent = spr->hdr8 == 0xFFFF ? -1 : int(spr->hdr8 & 0xFF);
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
      {
        // Draw3DSetMaterials: end = start + min(end-start, 0x70) - ((1 << shift) - 1)
        const int shift = m.raw[0x1E] | (m.raw[0x1F] << 8);
        int range = std::min(int(m.palEnd) - int(m.palStart), 0x70);
        int end = int(m.palStart) + range - ((1 << std::min(shift, 15)) - 1);
        sm.palEnd = uint8_t(std::clamp(end, 0, 255));
        sm.fallbackColor = m.raw[0x12];
        sm.flag15 = int(int8_t(m.raw[0x15]));
        sm.upperName = m.name;
        for (auto& ch : sm.upperName) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        sm.fixedLight = m.raw[0x16] | (m.raw[0x17] << 8);
        sm.ambientCoef = m.raw[0x18] | (m.raw[0x19] << 8);
        sm.diffuseCoef = m.raw[0x1A] | (m.raw[0x1B] << 8);
        sm.specularCoef = m.raw[0x1C] | (m.raw[0x1D] << 8);
      }
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

// The craft as DoViewCar (0x46A94) builds it for the pilot information card: RACER<n>.ART with the materials of VIEW<n>.MAT (not CARS.MAT), drawn in the
// palette of the card. Only the mesh (index 0 of shipMeshes), the materials and the textures are filled.
bool buildShipPreview(const GameData& data, int ship, Scene* out, float scale) {
  auto matBytes = data.read("VIEW" + std::to_string(ship) + ".MAT");
  auto artBytes = data.read("RACER" + std::to_string(ship) + ".ART");
  if (!matBytes || !artBytes) return false;
  auto mats = parseMaterials(*matBytes);
  auto art = parseArt(*artBytes);
  if (!mats || !art) return false;
  *out = Scene{};
  Builder b{data, *out, *mats, {}};
  b.buildMaterials();
  std::vector<Vec3i> acc(art->nodes.size());
  for (size_t n = 0; n < art->nodes.size(); ++n) {
    acc[n] = art->nodes[n].offset;
    const int par = art->nodes[n].parent;
    if (par >= 0 && size_t(par) < n) { acc[n].x += acc[size_t(par)].x; acc[n].y += acc[size_t(par)].y; acc[n].z += acc[size_t(par)].z; }
  }
  Mesh& m = out->shipMeshes[0];
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
        vs.push_back({float(sh->verts[k].x + acc[n].x) * scale, float(sh->verts[k].y + acc[n].y) * scale, float(sh->verts[k].z + acc[n].z) * scale});
      }
      if (bad || vs.size() < 3) continue;
      MeshPoly p;
      p.first = uint32_t(m.verts.size());
      p.count = uint16_t(vs.size());
      p.material = b.globalMaterial(sh->materials, poly.material);
      p.hasUV = !poly.uv.empty();
      p.normal = Builder::unitNormal(poly.nx, poly.ny, poly.nz);
      for (size_t i = 0; i < vs.size(); ++i) {
        m.verts.push_back(vs[i]);
        m.uv.push_back(p.hasUV ? poly.uv[i][0] * kFix14 : 0.0f);
        m.uv.push_back(p.hasUV ? poly.uv[i][1] * kFix14 : 0.0f);
      }
      m.polys.push_back(p);
    }
  }
  return !m.polys.empty();
}

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

  // Load-time polygon flag rewrite of TrackDrawSetup (0x3984E..0x39A6B), list A polygons only: the file's 0x80/0x08/0x40
  // bits are cleared and re-derived, bit 1 is recomputed (surface faces the light = upward), bit 3 (0x08) marks the polygons that
  // define the visible-extent window (cage / chase-light types, transparent textured slopes), bit 6 (0x40) = material "TRNC*".
  auto deriveFlags = [&](const TrackPolygon& poly) -> uint16_t {
    uint16_t f = poly.flags;
    const int gm = b.globalMaterial(t.trcMaterials, poly.material);
    const SurfaceMaterial* sm = gm >= 0 && size_t(gm) < s->materials.size() ? &s->materials[size_t(gm)] : nullptr;
    const std::string nm = sm ? sm->upperName : std::string();
    auto starts = [&](const char* pre) { return nm.rfind(pre, 0) == 0; };
    f = uint16_t(f & ~0x80);
    if (starts("WATE")) f |= 0x80;
    f = uint16_t(f & ~(0x08 | 0x40));
    const int type = f >> 8;
    const bool skip = (f & 1) || (f & 4);
    if (!skip) {
      switch (type) { case 0x80: case 0x81: case 0x82: case 0x8F: case 0x8D: case 0x91: case 0x84: case 0x85: case 0x92: case 0x93: f |= 8; break; default: break; }
    }
    f = uint16_t(f & ~2);
    bool toTrnc = true;
    if (type != 0x8F && skip) toTrnc = false;
    else if (type != 0x8F && (f & 8)) toTrnc = true;
    else if (type != 0x8F && nm.rfind("TRNCHIDD", 0) == 0) toTrnc = true;
    else {
      if (poly.ny >= 316) f |= 2;  // faces the light (cos 0x3F00 in 2.14)
      if (poly.ny <= 0x3000 && sm && sm->texture >= 0 && sm->flag15 == 0 && s->textures[size_t(sm->texture)].transparent >= 0) f |= 8;
    }
    if (toTrnc && starts("TRNC")) f |= 0x40;
    return f;
  };

  // --- track pieces ---
  s->piecePolys.assign(t.pieces.size(), {});
  for (auto& pc : t.pieces) s->pieceGroup.push_back(pc.group);
  s->groupCount = t.groupCount;
  s->portalOnly = t.portalOnly;
  for (const BspNode& n : t.bsp) s->bsp.push_back({n.axis, n.point, n.hi, n.lo, n.group});
  std::vector<std::map<uint32_t, int>> polyOf(t.pieces.size());  // per piece: TRC polygon offset -> track.polys index
  for (size_t pi = 0; pi < t.pieces.size(); ++pi) {
    const TrackPiece& pc = t.pieces[pi];
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
      s->track.polys.back().piece = int32_t(pi);
      s->track.polys.back().pflags = poly.list == 0 ? deriveFlags(poly) : poly.flags;
      if (poly.uv.empty() && panelIndex(poly.flags >> 8)) s->track.polys.back().detail = uint8_t(panelIndex(poly.flags >> 8));
      s->track.polys.back().portal = (poly.flags & 1) != 0;
      // list A: flags 0x1 (portal) / 0x4 are skipped (0x3948C: test al,5); list B: 0x1/0x4/0x10 (0x3872C: test al,0x15)
      s->track.polys.back().hidden = s->track.polys.back().portal || (poly.flags & 4) != 0 || (poly.list == 1 && (poly.flags & 0x10) != 0);
      polyOf[pi][poly.offset] = int(s->track.polys.size()) - 1;
      s->piecePolys[pi].push_back(uint32_t(s->track.polys.size() - 1));
    }
  }

  // --- scenery shape instances (orientation convention SPECULATIVE: v' = v * M, 2.14) ---
  // World-space boxes of the road pieces (walls/roof included). Tall scenery (towers) that the road passes
  // through has the part inside these boxes cut away: buildings cannot occupy the drivable corridor.
  struct Box { Vec3 lo, hi; };
  std::vector<Box> roadBoxes;
  std::map<uint32_t, int> pieceByTrd;
  for (size_t pi = 0; pi < t.pieces.size(); ++pi) { pieceByTrd[t.pieces[pi].trdOffset] = int(pi); s->entryItem[t.pieces[pi].trdOffset] = {0, pi}; }
  for (size_t pi = 0; pi < t.pieces.size(); ++pi) {
    const TrackPiece& pc = t.pieces[pi];
    const TrackRecord& rec = t.records[size_t(pc.record)];
    if (rec.verts.empty()) {
      Scene::PieceBox pb{};
      pb.empty = true;
      for (int j = 0; j < 3; ++j) pb.link[j] = pb.linkPoly[j] = -1;
      s->pieceBoxes.push_back(pb);
      continue;
    }
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
      for (int k = 0; k < 3; ++k) {
        const double o = k == 0 ? double(pc.pos.x) - s->origin[0] : k == 1 ? double(pc.pos.y) - s->origin[1] : double(pc.pos.z) - s->origin[2];
        pb.bb[2 * k] = float(o + rec.bbox[2 * k] * 64.0);
        pb.bb[2 * k + 1] = float(o + rec.bbox[2 * k + 1] * 64.0);
      }
      for (int j = 0; j < 3; ++j) {
        pb.link[j] = pb.linkPoly[j] = -1;
        auto it = pieceByTrd.find(pc.links[j].pieceOffset);
        auto pt = polyOf[pi].find(pc.links[j].portalOffset);
        if (pc.links[j].pieceOffset && it != pieceByTrd.end() && pt != polyOf[pi].end()) { pb.link[j] = it->second; pb.linkPoly[j] = pt->second; }
      }
      float lx = 1e30f, ly = 1e30f, lz = 1e30f, hx = -1e30f, hy = -1e30f, hz = -1e30f;
      for (const Vec3i& v : rec.verts) {
        float x = float(double(pc.pos.x) - s->origin[0] + v.x), y = float(double(pc.pos.y) - s->origin[1] + v.y), z = float(double(pc.pos.z) - s->origin[2] + v.z);
        lx = std::min(lx, x); ly = std::min(ly, y); lz = std::min(lz, z); hx = std::max(hx, x); hy = std::max(hy, y); hz = std::max(hz, z);
      }
      pb.lo[0] = lx; pb.lo[1] = ly; pb.lo[2] = lz; pb.hi[0] = hx; pb.hi[1] = hy; pb.hi[2] = hz;
      for (const TrackPolygon& tp : rec.polys) {
        if (tp.list != 0 || (deriveFlags(tp) & 0x40)) continue;
        auto it = polyOf[pi].find(tp.offset);
        if (it == polyOf[pi].end()) continue;
        const MeshPoly& mp = s->track.polys[size_t(it->second)];
        const Vec3& v0 = s->track.verts[mp.first];
        if (mp.normal.x == 0 && mp.normal.y == 0 && mp.normal.z == 0) continue;
        pb.planes.push_back({mp.normal.x, mp.normal.y, mp.normal.z, -(mp.normal.x * v0.x + mp.normal.y * v0.y + mp.normal.z * v0.z)});
      }
      s->pieceBoxes.push_back(pb);
    }
  }
  for (size_t i = 0; i < s->pieceBoxes.size(); ++i)
    for (int j = 0; j < 3; ++j)
      if (s->pieceBoxes[i].link[j] >= 0) { s->pieceBoxes[i].graph = true; s->pieceBoxes[size_t(s->pieceBoxes[i].link[j])].graph = true; }
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
    if (inst.billboard) {
      Billboard bb;
      bb.pos = base;
      bb.radius = float(inst.radius);
      bb.vis = inst.visMask;
      bb.group = inst.group;
      for (auto& poly : sh.polys) {
        std::vector<Vec3> vs;
        bool bad = false;
        for (uint16_t i : poly.index) {
          if (i >= sh.verts.size()) { bad = true; break; }
          vs.push_back({float(sh.verts[i].x), float(sh.verts[i].y), float(sh.verts[i].z)});
        }
        if (bad || vs.size() < 3) continue;
        addPoly(bb.mesh, vs, poly.uv, b.globalMaterial(sh.materials, poly.material), Builder::unitNormal(poly.nx, poly.ny, poly.nz));
      }
      if (!bb.mesh.polys.empty()) { s->entryItem[inst.entryOffset] = {2, s->billboards.size()}; s->billboards.push_back(std::move(bb)); }
      continue;
    }
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
      {
        // the stored normal is in model space: rotate it with the instance matrix like the vertices (v * M)
        const Vec3 n0 = Builder::unitNormal(poly.nx, poly.ny, poly.nz);
        Vec3 n{(n0.x * m[0] + n0.y * m[3] + n0.z * m[6]) * kFix14, (n0.x * m[1] + n0.y * m[4] + n0.z * m[7]) * kFix14,
               (n0.x * m[2] + n0.y * m[5] + n0.z * m[8]) * kFix14};
        sp.n = n;
      }
      for (auto& v : sp.v) {
        lo.x = std::min(lo.x, v.x); lo.y = std::min(lo.y, v.y); lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x); hi.y = std::max(hi.y, v.y); hi.z = std::max(hi.z, v.z);
      }
      sps.push_back(std::move(sp));
    }
    // Only tall volumes (towers); flat spans such as bridges legitimately pass over the road.
    const bool tall = (hi.y - lo.y) > 0.6f * std::max(hi.x - lo.x, hi.z - lo.z);
    std::vector<const Box*> cut;
    if (tall && std::getenv("SLIP_CUT"))  // legacy z-buffer workaround, unnecessary in painter mode
      for (const Box& bx : roadBoxes)
        if (bx.lo.x < hi.x && bx.hi.x > lo.x && bx.lo.y < hi.y && bx.hi.y > lo.y && bx.lo.z < hi.z && bx.hi.z > lo.z) cut.push_back(&bx);

    const int32_t instIndex = int32_t(s->track.instances.size());
    s->track.instances.push_back({inst.group, base, float(inst.radius)});
    s->instPolys.emplace_back();
    s->entryItem[inst.entryOffset] = {1, size_t(instIndex)};
    auto emit = [&](const SPoly& sp) {
      std::vector<std::array<uint16_t, 2>> uv;
      for (auto& q : sp.uv) uv.push_back({uint16_t(std::clamp(std::lround(q[0]), 0L, 65535L)), uint16_t(std::clamp(std::lround(q[1]), 0L, 65535L))});
      addPoly(s->track, sp.v, uv, sp.mat, sp.n);
      s->track.polys.back().scenery = true;
      s->track.polys.back().vis = inst.visMask;
      s->track.polys.back().instance = instIndex;
      s->instPolys.back().push_back(uint32_t(s->track.polys.size() - 1));
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

  s->panelDetails = loadPanelDetails(data);
  s->sdYellowMaterial = b.mats.find("SDYellow");
  s->sdCageMaterial = b.mats.find("SDCage");
  s->sdOrangeMaterial = b.mats.find("SDOrangeLight");
  s->sdFloorLightMaterial = b.mats.find("SDFloorLight");
  s->sdBlueMaterial = b.mats.find("SDBlueLight");
  s->sdRoadLineMaterial = b.mats.find("SDRoadLine");
  s->groupTrees = t.groupTrees;  // planes come straight from the group points (see track.cpp)

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

  // --- projectile models (names from the table at 0x5BF5E) ---
  {
    static const char* kNames[7] = {"AIRMINE.SHP", "AMBLER.SHP", "BOMBER.SHP", "FRAG.SHP", "HYPER.SHP", "SCRAMBLE.SHP", "SEEKER.SHP"};
    for (int k = 0; k < 7; ++k) {
      auto bytes = data.read(kNames[k]);
      if (!bytes) continue;
      auto sh = parseShape(*bytes);
      if (!sh) continue;
      float ext = 0;
      for (auto& poly : sh->polys) {
        std::vector<Vec3> vs;
        bool bad = false;
        for (uint16_t idx : poly.index) {
          if (idx >= sh->verts.size()) { bad = true; break; }
          const auto& v = sh->verts[idx];
          vs.push_back({float(v.x) * s->shipScale, float(v.y) * s->shipScale, float(v.z) * s->shipScale});
          ext = std::max({ext, std::fabs(float(v.x)), std::fabs(float(v.y)), std::fabs(float(v.z))});
        }
        if (bad || vs.size() < 3) continue;
        addPoly(s->weaponMeshes[size_t(k)], vs, poly.uv, b.globalMaterial(sh->materials, poly.material), Builder::unitNormal(poly.nx, poly.ny, poly.nz));
      }
      s->weaponMeshRadius[size_t(k)] = ext;
    }
  }

  {  // the drone model (DRONE.ART node: Drone.Shp, the first detail level)
    auto bytes = data.read("DRONE.SHP");
    if (bytes)
      if (auto sh = parseShape(*bytes)) {
        float ext = 0;
        for (auto& poly : sh->polys) {
          std::vector<Vec3> vs;
          bool bad = false;
          for (uint16_t idx : poly.index) {
            if (idx >= sh->verts.size()) { bad = true; break; }
            const auto& v = sh->verts[idx];
            vs.push_back({float(v.x) * s->shipScale, float(v.y) * s->shipScale, float(v.z) * s->shipScale});
            ext = std::max({ext, std::fabs(float(v.x)), std::fabs(float(v.y)), std::fabs(float(v.z))});
          }
          if (bad || vs.size() < 3) continue;
          addPoly(s->droneMesh, vs, poly.uv, b.globalMaterial(sh->materials, poly.material), Builder::unitNormal(poly.nx, poly.ny, poly.nz));
        }
        s->droneRadius = ext;
      }
  }

  s->track_data = std::move(t);
  s->buildFloorIndex();
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
bool Scene::pieceContains(size_t i, const float p[3], float slack) const {
  const PieceBox& b = pieceBoxes[i];
  if (b.empty) return false;
  for (int k = 0; k < 3; ++k)
    if (p[k] < b.bb[2 * k] || p[k] > b.bb[2 * k + 1]) return false;
  for (const auto& pl : b.planes)
    if (pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] < -slack) return false;
  return true;
}

uint16_t Scene::visMaskAt(double wx, double wy, double wz) const {
  const float p[3] = {float(wx - origin[0]), float(wy - origin[1]), float(wz - origin[2])};
  double bestVol = 1e300;
  uint16_t flags = 0xFFFF;
  for (const PieceBox& b : pieceBoxes) {
    if (b.empty || !b.graph) continue;
    const bool in = pieceContains(size_t(&b - pieceBoxes.data()), p);
    double vol = double(b.hi[0] - b.lo[0]) * double(b.hi[1] - b.lo[1]) * double(b.hi[2] - b.lo[2]);
    if (in && vol < bestVol) { bestVol = vol; flags = b.flags; }
  }
  const uint16_t m = uint16_t(flags & 0x5F);
  return m ? m : uint16_t(0xFFFF);  // no cell (or empty class set): draw everything, as the original does without a cell
}
}  // namespace slip

namespace slip {
void Scene::bspOrder(const double cam[3], std::vector<int>* groups) const {
  groups->clear();
  if (bsp.empty()) return;
  const double p[3] = {cam[0] - origin[0] + origin[0], cam[1], cam[2]};  // planes are in absolute world units
  std::vector<int> stack{0};
  // iterative far-first walk: visit the side that does not contain the camera first
  std::function<void(int, int)> walk = [&](int n, int depth) {
    if (n < 0 || depth > 64) return;
    const BspNodeS& nd = bsp[size_t(n)];
    if (nd.axis < 0) { if (nd.group >= 0) groups->push_back(nd.group); return; }
    const double plane = double(nd.point) * 1048576.0;
    if (p[nd.axis] >= plane) { walk(nd.lo, depth + 1); walk(nd.hi, depth + 1); }  // camera on the high side: low side is far
    else { walk(nd.hi, depth + 1); walk(nd.lo, depth + 1); }
  };
  walk(0, 0);
}
}  // namespace slip

namespace slip {
void Scene::groupOrder(int g, const double cam[3], std::vector<ItemRef>* out) const {
  out->clear();
  if (g < 0 || size_t(g) >= groupTrees.size() || groupTrees[size_t(g)].empty()) return;
  const auto& tree = groupTrees[size_t(g)];
  std::function<void(int, int)> emit = [&](int n, int depth) {
    if (n < 0 || size_t(n) >= tree.size() || depth > 64) return;
    const GroupTreeNode& nd = tree[size_t(n)];
    auto item = [&]() {
      if (!nd.item) return;
      auto it = entryItem.find(nd.item);
      if (it != entryItem.end()) out->push_back(it->second);
    };
    if (nd.leaf) { item(); return; }
    const double side = double(nd.n[0]) * cam[0] + double(nd.n[1]) * cam[1] + double(nd.n[2]) * cam[2] - double(nd.plane);
    if (side >= 0) { emit(nd.b, depth + 1); item(); emit(nd.a, depth + 1); }   // camera on the A side: B is far
    else { emit(nd.a, depth + 1); item(); emit(nd.b, depth + 1); }
  };
  emit(0, 0);
}
}  // namespace slip
