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

namespace {
// Per-track sky layers: the hooks 0x4329E (Canyon), 0x4345A (Hawaii), 0x435D4 (London), 0x437A7 (Norway), 0x4372E (Egypt), 0x4394A (Amazon), 0x43AAA (New York)
// resolve sprite wildcard patterns to resource ids (ResFindIDsAll 0x24B98: the first `count` matches, in order, one id array for all patterns) and call 0x1298E with
// a layout table: u16 count + 6-byte entries (id index, azimuth, elevation) for the high layer, again for the low layer, and one more list (the hills) at the
// second address. All of it is read from the user's executable (CONFIRMED structure; the placement convention is INFERRED, see docs/research-log.md).
struct SkyDef {
  int track;
  const char* patterns[3];
  int counts[3];
  bool sun;            // New York: NYSUNS.SPR is id 0 of the array
  uint32_t layoutVa, hillsVa;
  int speed;
};
constexpr SkyDef kSkyDefs[] = {
    {2, {"hawcld*A.spr", "hawcld*B.spr", "hawcld*C.spr"}, {2, 7, 8}, false, 0x433E8, 0x43458, 0x80},
    {4, {"norhill*.spr", nullptr, nullptr}, {2, 0, 0}, false, 0x43778, 0x4377C, 1},
    {6, {"cancld*A.spr", "cancld*B.spr", "cancld*C.spr"}, {2, 2, 2}, false, 0x43274, 0x4329C, 0x10},
    {7, {"Amacld*A.spr", "Amacld*B.spr", "Amacld*C.spr"}, {4, 4, 3}, false, 0x4387E, 0x43948, 0x60},
    {8, {"loncld*A.spr", "loncld*B.spr", "loncld*C.spr"}, {2, 9, 6}, false, 0x43568, 0x435D2, 0x10},
    {9, {"eghill*.spr", nullptr, nullptr}, {3, 0, 0}, false, 0x43704, 0x43708, 1},
    {10, {"NYcl**.spr", nullptr, nullptr}, {14, 0, 0}, true, 0x43A4A, 0x43AA8, 0},
};

namespace {
struct Box { Vec3 lo, hi; };
}  // namespace

// Lighting of a track for renderers with real-time lighting. WHERE lights and sun belong (INFERRED, documented in docs/lighting.md): roofed pieces are tunnels and get a lamp at the ceiling of the
// path node plus the lamp panels / floor lights / pads that the track data defines; every other piece is open air and lit by the sun and the sky. Night tracks (Tokyo, New York: dark sky
// ramp) have no sun and a low ambient level, so the lamps and signs do the work.
void buildLighting(const Track& t, Scene& s, const std::vector<Box>& roadBoxes) {
  auto rgbf = [&](int palIdx, float* o) {
    const uint32_t c = s.palette.rgba[size_t(std::clamp(palIdx, 0, 255))];
    o[0] = float((c >> 16) & 255) / 255.0f; o[1] = float((c >> 8) & 255) / 255.0f; o[2] = float(c & 255) / 255.0f;
  };
  LightingEnv& e = s.env;
  struct Env { float elev, azim, sun, amb; };  // sun elevation / azimuth in degrees, sun strength, sky ambient strength
  static const Env kEnv[11] = {{}, {58, 40, 0.95f, 0.50f}, {70, 120, 1.05f, 0.55f}, {40, 200, 0.80f, 0.22f}, {30, 250, 0.45f, 0.75f}, {0, 0, 0.0f, 0.30f}, {50, 300, 1.00f, 0.42f},
                               {65, 80, 0.70f, 0.45f}, {35, 160, 0.40f, 0.65f}, {75, 20, 1.10f, 0.50f}, {0, 0, 0.0f, 0.18f}};
  const int ti = std::clamp(s.trackIndex, 1, 10);
  const Env& v = kEnv[ti];
  const float er = v.elev * 3.14159265f / 180.0f, ar = v.azim * 3.14159265f / 180.0f;
  e.sunDir[0] = std::cos(er) * std::sin(ar); e.sunDir[1] = std::sin(er); e.sunDir[2] = std::cos(er) * std::cos(ar);
  e.night = ti == 3 || ti == 10;
  e.waterGround = ti == 2 || ti == 10;
  for (int k = 0; k < 3; ++k) {
    e.sunColor[k] = v.sun * (k == 2 ? 0.88f : k == 1 ? 0.97f : 1.0f);
    e.skyAmbient[k] = v.amb * (k == 2 ? 1.12f : k == 1 ? 1.0f : 0.92f);
    e.groundAmbient[k] = 0.45f * v.amb * (k == 2 ? 0.8f : 1.0f);
  }
  if (e.night) { for (int k = 0; k < 3; ++k) { e.skyAmbient[k] = k == 2 ? 0.30f : 0.20f; e.groundAmbient[k] = 0.10f; } }
  e.indoorAmbient = ti == 5 ? 0.34f : 0.26f;
  {  // exposure: an average surface (half of the sun, mean of sky and ground ambient) is shown with the brightness the original gives it
    const float amb = 0.5f * (e.skyAmbient[1] + e.groundAmbient[1]);
    const float lref = amb + e.sunColor[1] * 0.5f * std::max(e.sunDir[1], 0.2f);
    e.exposure = (e.night ? 0.85f : 1.0f) / std::max(lref, 0.15f);
    e.indoorLevel = e.night ? 0.7f : ti == 5 ? 0.8f : 0.92f;
  }
  // one lamp per tunnel piece, on the ceiling of the path node
  for (size_t pi = 0; pi < t.pieces.size() && pi < s.pieceBoxes.size() && pi < roadBoxes.size(); ++pi) {
    const auto& pb = s.pieceBoxes[pi];
    const TrackPiece& pc = t.pieces[pi];
    if (!pb.roofed || pb.empty || pc.node < 0 || size_t(pc.node) >= t.nodes.size()) continue;
    const Box& bx = roadBoxes[pi];
    const Vec3i& np = t.nodes[size_t(pc.node)].pos;
    StaticLight l{};
    l.pos[0] = float(double(np.x) - s.origin[0]); l.pos[2] = float(double(np.z) - s.origin[2]);
    l.pos[1] = bx.hi.y - 0.18f * (bx.hi.y - bx.lo.y);
    l.color[0] = 0.5f; l.color[1] = 0.43f; l.color[2] = 0.33f;
    l.radius = 1.0f * std::max({bx.hi.x - bx.lo.x, bx.hi.z - bx.lo.z, 0.8f * (bx.hi.y - bx.lo.y)}) + 90000.0f;
    l.kind = 0;
    s.lights.push_back(l);
  }
  // lamp panels, floor lights and pads of the track data
  for (const MeshPoly& p : s.track.polys) {
    if (p.scenery || !p.detail || p.hidden || p.count < 3) continue;
    const PanelDetail& d = s.panelDetails[p.detail];
    int kind = 0, mat = -1;
    if (d.kind == PanelKind::ChaseOrange) { kind = 1; mat = s.sdOrangeMaterial; }
    else if (d.kind == PanelKind::ChaseFloor) { kind = 2; mat = s.sdFloorLightMaterial; }
    else if (d.kind == PanelKind::Refuel) { kind = 3; mat = s.sdBlueMaterial; }
    if (!kind || mat < 0) continue;
    StaticLight l{};
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (uint16_t k = 0; k < p.count; ++k) {
      const Vec3& q = s.track.verts[p.first + k];
      l.pos[0] += q.x / float(p.count); l.pos[1] += q.y / float(p.count); l.pos[2] += q.z / float(p.count);
      lo[0] = std::min(lo[0], q.x); hi[0] = std::max(hi[0], q.x); lo[1] = std::min(lo[1], q.y); hi[1] = std::max(hi[1], q.y); lo[2] = std::min(lo[2], q.z); hi[2] = std::max(hi[2], q.z);
    }
    // a little in front of the panel
    l.pos[0] += p.normal.x * 6000.0f; l.pos[1] += p.normal.y * 6000.0f; l.pos[2] += p.normal.z * 6000.0f;
    rgbf(s.materials[size_t(mat)].palEnd, l.color);
    const float ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    l.radius = kind == 1 ? 1.3f * ext + 40000.0f : kind == 2 ? 0.9f * ext + 24000.0f : 1.2f * ext + 30000.0f;
    l.kind = kind;
    for (float& c : l.color) c *= kind == 1 ? 0.35f : kind == 2 ? 0.25f : 0.5f;
    s.lights.push_back(l);
  }
}

void loadSky(const GameData& data, int trackIndex, Scene& s) {
  if (s.skyMaterial >= 0) {
    const SurfaceMaterial& m = s.materials[size_t(s.skyMaterial)];
    for (int i = m.palStart; i <= m.palEnd; ++i) s.skyRamp.push_back(0xff000000u | (s.palette.rgba[size_t(i)] & 0xffffffu));
  }
  if (s.groundMaterial >= 0) s.groundColor = 0xff000000u | (s.palette.rgba[size_t(s.materials[size_t(s.groundMaterial)].palStart)] & 0xffffffu);
  const SkyDef* def = nullptr;
  for (const SkyDef& d : kSkyDefs) if (d.track == trackIndex) def = &d;
  auto exe = data.read("SLIPSTRM.EXE");
  if (!def || !exe) return;
  auto off = [](uint32_t va) { return size_t(0x4D854) + size_t(va - 0x10000); };
  auto s16 = [&](size_t o) -> int { return o + 2 <= exe->size() ? int(int16_t((*exe)[o] | ((*exe)[o + 1] << 8))) : 0; };
  std::vector<Sprite> ids;
  auto load = [&](const std::string& name) {
    Sprite sp;
    if (auto b = data.read(name)) if (auto p = parseSprite(*b)) sp = std::move(*p);
    ids.push_back(std::move(sp));
  };
  if (def->sun) load("NYSUNS.SPR");
  for (int k = 0; k < 3; ++k) {
    if (!def->patterns[k]) continue;
    std::string pat = def->patterns[k];
    for (char& c : pat) c = char(std::toupper(static_cast<unsigned char>(c)));
    const auto names = data.glob(pat);
    for (int i = 0; i < def->counts[k]; ++i) { if (size_t(i) < names.size()) load(names[size_t(i)]); else ids.emplace_back(); }
  }
  auto list = [&](size_t& o, int layer) {
    const int n = s16(o);
    o += 2;
    for (int i = 0; i < n && n < 64; ++i, o += 6) {
      const int id = s16(o);
      if (id < 0 || size_t(id) >= ids.size() || ids[size_t(id)].w <= 0) continue;
      SkySprite k;
      k.img = ids[size_t(id)];
      // no transparent colour (Canyon's clouds): they are painted on the sky's zenith colour, which is taken as transparent; they are drawn at 70 % of the size so that neighbours
      // (45..67 degrees apart) do not overlap (INFERRED: opaque pictures cut each other off, full size overlaps)
      k.transparent = k.img.hdr8 != 0xFFFF ? int(k.img.hdr8 & 0xFF) : s.skyMaterial >= 0 ? int(s.materials[size_t(s.skyMaterial)].palStart) : -1;
      if (k.img.hdr8 == 0xFFFF) k.scale = 0.7f;
      k.az = double(s16(o + 2)) * 3.14159265358979323846 / 32768.0;
      k.el = double(s16(o + 4)) * 3.14159265358979323846 / 32768.0;
      k.layer = layer;
      s.skySprites.push_back(std::move(k));
    }
  };
  size_t o = off(def->layoutVa);
  list(o, 2);  // list in 0x12950: drawn last, [0x12958] = fast angle
  list(o, 0);  // list in 0x12952: drawn first, [0x1295C] = angle at 1/8 of the speed
  size_t h = off(def->hillsVa);
  list(h, 1);
  s.skySpeed = def->speed;
}
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
  s->startDir = t.startDir;
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
  std::vector<Box> roadBoxes;
  std::vector<char> roadRoofed;  // parallel to roadBoxes
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
      bool roofed = false;  // the path node lies under a visible ceiling polygon of the piece: the piece is a tunnel (hidden roofs only hide the sky; undersides of bridges next to the road do not count)
      double roofArea = 0, minx = 1e30, maxx = -1e30, minz = 1e30, maxz = -1e30;
      for (const Vec3i& v : rec.verts) { minx = std::min<double>(minx, v.x); maxx = std::max<double>(maxx, v.x); minz = std::min<double>(minz, v.z); maxz = std::max<double>(maxz, v.z); }
      for (const auto& poly : rec.polys) {  // visible ceiling polygons: their area seen from above against the piece footprint
        if (Builder::unitNormal(poly.nx, poly.ny, poly.nz).y > -0.5f || (poly.flags & 0x5) != 0 || (poly.list == 1 && (poly.flags & 0x10))) continue;
        double a2 = 0;
        const size_t n = poly.index.size();
        for (size_t a = 0, b = n - 1; a < n; b = a++) {
          if (poly.index[a] >= rec.verts.size() || poly.index[b] >= rec.verts.size()) { a2 = 0; break; }
          const Vec3i &va = rec.verts[poly.index[a]], &vb = rec.verts[poly.index[b]];
          a2 += double(vb.x) * double(va.z) - double(va.x) * double(vb.z);
        }
        roofArea += std::fabs(a2) * 0.5;
      }
      if (maxx > minx && maxz > minz && roofArea > 0.3 * (maxx - minx) * (maxz - minz)) roofed = true;
      if (!roofed && pc.node >= 0 && size_t(pc.node) < t.nodes.size()) {
        const Vec3i& np = t.nodes[size_t(pc.node)].pos;
        const double px = double(np.x) - double(pc.pos.x), pz = double(np.z) - double(pc.pos.z);
        for (const auto& poly : rec.polys) {
          if (Builder::unitNormal(poly.nx, poly.ny, poly.nz).y > -0.5f || (poly.flags & 0x5) != 0 || (poly.list == 1 && (poly.flags & 0x10))) continue;
          bool inside = false;
          const size_t n = poly.index.size();
          for (size_t a = 0, b = n - 1; a < n; b = a++) {
            if (poly.index[a] >= rec.verts.size() || poly.index[b] >= rec.verts.size()) { inside = false; break; }
            const Vec3i &va = rec.verts[poly.index[a]], &vb = rec.verts[poly.index[b]];
            if (((double(va.z) > pz) != (double(vb.z) > pz)) && px < (double(vb.x) - double(va.x)) * (pz - double(va.z)) / (double(vb.z) - double(va.z)) + double(va.x)) inside = !inside;
          }
          if (inside) { roofed = true; break; }
        }
      }
      roadRoofed.push_back(roofed ? 1 : 0);
    }
    {
      Scene::PieceBox pb{};
      pb.flags = rec.visFlags;
      pb.roofed = roadRoofed.back() != 0;
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
        pb.rayPolys.push_back(uint32_t(it->second));
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
    bool curTunnelFace = false;
    auto emit = [&](const SPoly& sp) {
      std::vector<std::array<uint16_t, 2>> uv;
      for (auto& q : sp.uv) uv.push_back({uint16_t(std::clamp(std::lround(q[0]), 0L, 65535L)), uint16_t(std::clamp(std::lround(q[1]), 0L, 65535L))});
      addPoly(s->track, sp.v, uv, sp.mat, sp.n);
      s->track.polys.back().scenery = true;
      s->track.polys.back().vis = inst.visMask;
      s->track.polys.back().instance = instIndex;
      s->instPolys.back().push_back(uint32_t(s->track.polys.size() - 1));
      s->track.polys.back().backdrop = !cut.empty();
      s->track.polys.back().tunnelFace = curTunnelFace;
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
    // A face of a tall building that passes through the road (a tunnel under a tower: Chicago's chitwk box sits on the last tunnel) is not drawn: the original's walls of the tunnel mouth
    // and the portal walk hide it, but in the port the bare quad hung in front of the tunnel exit; flagged here, skipped by the renderer while the camera is in a tunnel (INFERRED)
    auto crossesRoad = [&](const SPoly& sp) {
      Vec3 plo{1e30f, 1e30f, 1e30f}, phi{-1e30f, -1e30f, -1e30f};
      for (const Vec3& v : sp.v) {
        plo = {std::min(plo.x, v.x), std::min(plo.y, v.y), std::min(plo.z, v.z)};
        phi = {std::max(phi.x, v.x), std::max(phi.y, v.y), std::max(phi.z, v.z)};
      }
      const Vec3& n = sp.n;
      const float d0 = n.x * sp.v[0].x + n.y * sp.v[0].y + n.z * sp.v[0].z;
      for (size_t bi2 = 0; bi2 < roadBoxes.size(); ++bi2) {
        if (!roadRoofed[bi2]) continue;
        const Box& bx = roadBoxes[bi2];
        const float sx = 0.03f * (bx.hi.x - bx.lo.x), sy = 0.03f * (bx.hi.y - bx.lo.y), sz = 0.03f * (bx.hi.z - bx.lo.z);
        const Vec3 lo{bx.lo.x + sx, bx.lo.y + sy, bx.lo.z + sz}, hi{bx.hi.x - sx, bx.hi.y - sy, bx.hi.z - sz};
        if (!(lo.x < phi.x && hi.x > plo.x && lo.y < phi.y && hi.y > plo.y && lo.z < phi.z && hi.z > plo.z)) continue;
        float dmin = 1e30f, dmax = -1e30f;
        for (int k = 0; k < 8; ++k) {
          const float d = n.x * ((k & 1) ? hi.x : lo.x) + n.y * ((k & 2) ? hi.y : lo.y) + n.z * ((k & 4) ? hi.z : lo.z) - d0;
          dmin = std::min(dmin, d); dmax = std::max(dmax, d);
        }
        if (dmin < 0 && dmax > 0) return true;
      }
      return false;
    };
    for (auto& sp : sps) {
      curTunnelFace = tall && cut.empty() && crossesRoad(sp);
      subtract(sp, 0);
    }
  }

  s->panelDetails = loadPanelDetails(data);
  s->sdYellowMaterial = b.mats.find("SDYellow");
  s->sdCageMaterial = b.mats.find("SDCage");
  s->sparkMaterial = b.mats.find("Spark");
  s->splashMaterial = b.mats.find("Splash");
  s->sdOrangeMaterial = b.mats.find("SDOrangeLight");
  s->sdFloorLightMaterial = b.mats.find("SDFloorLight");
  s->sdBlueMaterial = b.mats.find("SDBlueLight");
  s->sdRoadLineMaterial = b.mats.find("SDRoadLine");
  s->skyMaterial = b.mats.find("Sky");
  s->groundMaterial = b.mats.find("Ground");
  {  // ebp of the sky draw hooks (0x43321.. 0x43B0A), one per track
    static constexpr int kBand[10] = {0x600, 0x1800, 0x600, 0xC00, 0x600, 0x600, 0x1200, 0x1200, 0x600, 0x1200};
    if (trackIndex >= 1 && trackIndex <= 10) s->skyBand = float(kBand[trackIndex - 1]) / 16384.0f;
  }
  loadSky(data, trackIndex, *s);
  buildLighting(t, *s, roadBoxes);
  if (getenv("SLIP_SKYLOG")) fprintf(stderr, "sky sprites %zu speed %g\n", s->skySprites.size(), s->skySpeed);
  if (getenv("SLIP_SKYLOG")) for (int k : {s->skyMaterial, s->groundMaterial}) if (k >= 0) { const auto& m = s->materials[size_t(k)]; fprintf(stderr, "mat %s ramp %d..%d fallback %d:", m.name.c_str(), m.palStart, m.palEnd, m.fallbackColor); for (int i = m.palStart; i <= m.palEnd; ++i) fprintf(stderr, " %06x", s->palette.rgba[size_t(i)] & 0xffffff); fprintf(stderr, "\n"); }
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

  {  // debris pieces of the ships and the drone (0x4F81F picks one of four at random)
    auto addShape = [&](const std::string& name, Mesh* m) {
      auto bytes = data.read(name);
      if (!bytes) return;
      auto sh = parseShape(*bytes);
      if (!sh) return;
      for (auto& poly : sh->polys) {
        std::vector<Vec3> vs;
        bool bad = false;
        for (uint16_t idx : poly.index) {
          if (idx >= sh->verts.size()) { bad = true; break; }
          const auto& v = sh->verts[idx];
          vs.push_back({float(v.x) * s->shipScale, float(v.y) * s->shipScale, float(v.z) * s->shipScale});
        }
        if (bad || vs.size() < 3) continue;
        addPoly(*m, vs, poly.uv, b.globalMaterial(sh->materials, poly.material), Builder::unitNormal(poly.nx, poly.ny, poly.nz));
      }
    };
    auto points = [&](const std::string& art, std::array<std::array<double, 3>, 4>* out) {
      if (auto ab = data.read(art))
        if (auto a = parseArt(*ab))
          if (!a->nodes.empty())
            for (int k = 0; k < 4; ++k) (*out)[size_t(k)] = {double(a->nodes[0].debris[1][size_t(k)].pos.x), double(a->nodes[0].debris[1][size_t(k)].pos.y), double(a->nodes[0].debris[1][size_t(k)].pos.z)};
    };
    points("DRONE.ART", &s->droneFragPoints);
    for (int r = 0; r < 10; ++r) points("RACER" + std::to_string(r) + ".ART", &s->fragPoints[size_t(r)]);
    for (int k = 0; k < 4; ++k) {
      char nm[32];
      std::snprintf(nm, sizeof nm, "DRFRG%02d.SHP", 50 + k);
      addShape(nm, &s->droneFragMeshes[size_t(k)]);
      for (int r = 0; r < 10; ++r) {
        std::snprintf(nm, sizeof nm, "R%dFRG%02d.SHP", r, 50 + k);
        addShape(nm, &s->fragMeshes[size_t(r)][size_t(k)]);
        std::snprintf(nm, sizeof nm, "R%dFRG%02d.SHP", r, k);
        addShape(nm, &s->deadFragMeshes[size_t(r)][size_t(k)]);
      }
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

int Scene::pieceAt(const float p[3]) const {
  int best = -1;
  double bestVol = 1e300;
  for (size_t i = 0; i < pieceBoxes.size(); ++i) {
    const PieceBox& b = pieceBoxes[i];
    if (b.empty || !pieceContains(i, p)) continue;
    const double vol = double(b.hi[0] - b.lo[0]) * double(b.hi[1] - b.lo[1]) * double(b.hi[2] - b.lo[2]);
    if (vol < bestVol) { bestVol = vol; best = int(i); }
  }
  return best;
}

bool Scene::rayBlocked(const double A0[3], const double B0[3]) const {
  double A[3] = {A0[0] - origin[0], A0[1] - origin[1], A0[2] - origin[2]};
  const double B[3] = {B0[0] - origin[0], B0[1] - origin[1], B0[2] - origin[2]};
  const float fa[3] = {float(A[0]), float(A[1]), float(A[2])}, fb[3] = {float(B[0]), float(B[1]), float(B[2])};
  const int endPiece = pieceAt(fb);
  int cur = pieceAt(fa);
  if (cur < 0 || endPiece < 0 || cur == endPiece) return false;  // 0x35674..0x356AB: no piece at an end, or the same piece: free
  auto unit = [](const double* a, const double* b, double* d) {
    d[0] = b[0] - a[0]; d[1] = b[1] - a[1]; d[2] = b[2] - a[2];
    const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l < 1e-9) return false;
    for (int k = 0; k < 3; ++k) d[k] /= l;
    return true;
  };
  double dir[3];
  if (!unit(A, B, dir)) return false;
  for (int hop = 0; hop < 64; ++hop) {
    const PieceBox& pb = pieceBoxes[size_t(cur)];
    int hit = -1;
    double X[3] = {0, 0, 0};
    for (uint32_t idx : pb.rayPolys) {  // 0x35742: every list-A polygon without flag 0x40 that faces the ray
      const MeshPoly& mp = track.polys[idx];
      const Vec3& n = mp.normal;
      const double dot = n.x * dir[0] + n.y * dir[1] + n.z * dir[2];
      if (dot >= -16.0 / 16384.0) continue;  // 0x35790..0x3579D: the ray must come against the polygon, by at least 0x10 (2.14)
      const Vec3& v0 = track.verts[mp.first];
      const double s = n.x * (A[0] - v0.x) + n.y * (A[1] - v0.y) + n.z * (A[2] - v0.z);
      if (s < 0) continue;
      const double t = s / -dot;
      const double P[3] = {A[0] + dir[0] * t, A[1] + dir[1] * t, A[2] + dir[2] * t};
      bool inside = true, pos = false, neg = false;  // 0x36CEC: the point lies inside the polygon
      for (uint16_t k = 0; k < mp.count && inside; ++k) {
        const Vec3& a = track.verts[mp.first + k];
        const Vec3& b = track.verts[mp.first + (k + 1) % mp.count];
        const double e[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, q[3] = {P[0] - a.x, P[1] - a.y, P[2] - a.z};
        const double c = (e[1] * q[2] - e[2] * q[1]) * n.x + (e[2] * q[0] - e[0] * q[2]) * n.y + (e[0] * q[1] - e[1] * q[0]) * n.z;
        const double tol = 1e-4 * (e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        if (c > tol) pos = true;
        if (c < -tol) neg = true;
        inside = !(pos && neg);
      }
      if (!inside) continue;
      hit = int(idx);
      for (int k = 0; k < 3; ++k) X[k] = P[k];
      break;
    }
    if (hit < 0) return true;                 // 0x35A19: the ray leaves the piece nowhere
    if (!track.polys[size_t(hit)].portal) return true;  // a solid polygon
    int next = -1;
    for (int j = 0; j < 3; ++j) if (pb.linkPoly[j] == hit) next = pb.link[j];  // 0x35939..0x35985: the neighbour behind this portal
    if (next < 0) return true;
    if (next == endPiece) return false;       // 0x3598C
    double nd[3];
    for (int k = 0; k < 3; ++k) A[k] = X[k];
    if (!unit(A, B, nd)) return false;
    if (nd[0] * dir[0] + nd[1] * dir[1] + nd[2] * dir[2] < 0) return true;  // 0x359D8: the direction turned round
    for (int k = 0; k < 3; ++k) dir[k] = nd[k];
    cur = next;
  }
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
