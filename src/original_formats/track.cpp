#include "original_formats/track.hpp"

namespace slip {

const std::array<const char*, 10>& trackBaseNames() {
  static const std::array<const char*, 10> n = {"CHICAGO", "HAWAII", "TOKYO", "NORWAY", "CAVE",
                                                "CAN",     "AMAZON", "LONDON", "EGYPT",  "NEWYORK"};
  return n;
}
const std::array<const char*, 10>& trackDisplayNames() {
  static const std::array<const char*, 10> n = {"Chicago", "Hawaii", "Tokyo",  "Norway", "France (CAVE)",
                                                "Arizona (CAN)", "Amazon", "London", "Egypt", "New York"};
  return n;
}

namespace {
struct R {
  const Bytes& b;
  bool ok = true;
  bool in(size_t o, size_t n) {
    if (o > b.size() || n > b.size() - o) { ok = false; return false; }
    return true;
  }
  uint16_t u16(size_t o) { return in(o, 2) ? uint16_t(b[o] | (b[o + 1] << 8)) : 0; }
  int16_t s16(size_t o) { return int16_t(u16(o)); }
  int32_t s32(size_t o) { return in(o, 4) ? int32_t(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (uint32_t(b[o + 3]) << 24)) : 0; }
};
std::string cstr(const Bytes& b, size_t o, size_t n) {
  std::string s;
  for (size_t i = 0; i < n && o + i < b.size() && b[o + i]; ++i) s.push_back(char(b[o + i]));
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}
}  // namespace

bool loadTrack(const GameData& data, int index, Track* out, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  if (index < 1 || index > 10) return fail("track index must be 1..10");
  Track t;
  t.index = index;
  t.name = trackBaseNames()[size_t(index - 1)];
  auto trkB = data.read(t.name + ".TRK");
  auto trcB = data.read(t.name + ".TRC");
  auto trdB = data.read(t.name + ".TRD");
  auto palB = data.read(t.name + ".PAL");
  auto matB = data.read(t.name + ".MAT");
  if (!trkB || !trcB || !trdB || !palB || !matB) return fail("missing track files for " + t.name);

  if (auto p = parsePalette(palB->data(), palB->size())) t.palette = *p; else return fail("bad PAL");
  fillDefaultTail(t.palette);
  if (auto m = parseMaterials(*matB)) t.materials = *m; else return fail("bad MAT");
  if (auto cars = data.read("CARS.MAT")) if (auto m = parseMaterials(*cars)) t.materials.append(*m);

  // ---- TRK ----
  {
    R r{*trkB};
    t.trkVersion = r.u16(2);
    if (r.u16(0) != trkB->size() || t.trkVersion != 0x2b) return fail("unexpected TRK header");
    for (int i = 0; i < 10; ++i) t.start[size_t(i)] = {r.s32(0x18 + 12 * size_t(i)), r.s32(0x1c + 12 * size_t(i)), r.s32(0x20 + 12 * size_t(i))};
    if (!r.ok) return fail("truncated TRK");
  }

  // ---- TRC ----
  std::vector<uint32_t> recOffsets;
  {
    R r{*trcB};
    if (r.u16(0) != trcB->size()) return fail("TRC size mismatch");
    size_t mt = r.u16(8), lst = r.u16(6);
    uint16_t nm = r.u16(mt);
    for (int i = 0; i < nm; ++i) {
      size_t o = mt + 2 + 18 * size_t(i);
      t.trcMaterials.push_back({cstr(*trcB, o, 16), r.u16(o + 16)});
    }
    uint16_t n = r.u16(lst);
    size_t p = lst + 2;
    for (int i = 0; i < n && r.ok; ++i) {
      TrackRecord rec;
      rec.offset = uint32_t(p);
      rec.visFlags = r.u16(p + 0x16);
      size_t sub = r.u16(p + 2);
      uint16_t nv = r.u16(sub);
      for (int k = 0; k < nv; ++k)
        rec.verts.push_back({int32_t(r.s16(sub + 2 + 6 * size_t(k))) * 64, int32_t(r.s16(sub + 4 + 6 * size_t(k))) * 64,
                             int32_t(r.s16(sub + 6 + 6 * size_t(k))) * 64});
      for (int k = 0; k < 6; ++k) rec.bbox[k] = r.s16(p + 8 + 2 * size_t(k));
      if (r.u16(p + 0x18)) rec.name = cstr(*trcB, p + 0x1a, 8);
      size_t lists[2] = {r.u16(p + 4), r.u16(p + 6)};
      for (size_t lo : lists) {
        if (!lo) continue;
        uint16_t np = r.u16(lo);
        size_t q = lo + 2;
        for (int k = 0; k < np && r.ok; ++k) {
          uint16_t info = r.u16(q);
          size_t N = info & 0x7fff;
          TrackPolygon poly;
          poly.nx = r.s16(q + 2);
          poly.ny = r.s16(q + 4);
          poly.nz = r.s16(q + 6);
          poly.material = uint16_t(r.u16(q + 10) & 0x7fff);
          for (size_t j = 0; j < N; ++j) poly.index.push_back(r.u16(q + 12 + 2 * j));
          if (info & 0x8000)
            for (size_t j = 0; j < N; ++j) poly.uv.push_back({r.u16(q + 12 + 2 * N + 4 * j), r.u16(q + 14 + 2 * N + 4 * j)});
          q += (info & 0x8000) ? 12 + 6 * N : 12 + 2 * N;
          rec.polys.push_back(std::move(poly));
        }
      }
      recOffsets.push_back(uint32_t(p));
      p += r.u16(p);
      t.records.push_back(std::move(rec));
    }
    if (!r.ok) return fail("truncated TRC");
  }

  // ---- TRD: pieces (placement of TRC records) and scenery shape instances ----
  {
    R r{*trdB};
    if (r.u16(0) != trdB->size()) return fail("TRD size mismatch");
    size_t l1 = r.u16(2);
    uint16_t ng = r.u16(l1);
    size_t p = l1 + 2;
    for (int g = 0; g < ng && r.ok; ++g) {
      size_t sz = r.u16(p);
      size_t pc = r.u16(p + 4), sh = r.u16(p + 8);
      if (pc) {
        uint16_t c = r.u16(pc);
        for (int k = 0; k < c; ++k) {
          size_t e = pc + 2 + 0x22 * size_t(k);
          uint16_t recOff = r.u16(e + 2);
          int ri = -1;
          for (size_t i = 0; i < recOffsets.size(); ++i)
            if (recOffsets[i] == recOff) { ri = int(i); break; }
          if (ri < 0) continue;
          t.pieces.push_back({ri, {r.s32(e + 0x12), r.s32(e + 0x16), r.s32(e + 0x1a)}, g});
        }
      }
      if (sh) {
        uint16_t c = r.u16(sh);
        for (int k = 0; k < c; ++k) {
          size_t e = sh + 2 + 0x46 * size_t(k);
          SceneryInstance s;
          s.shape = cstr(*trdB, e, 12);
          s.pos = {r.s32(e + 0x10), r.s32(e + 0x14), r.s32(e + 0x18)};
          s.visMask = r.u16(e + 0x36);
          for (int m = 0; m < 9; ++m) s.matrix[m] = r.s16(e + 0x24 + 2 * size_t(m));
          if (!s.shape.empty()) t.scenery.push_back(s);
        }
      }
      p += sz;
    }
    if (!r.ok) return fail("truncated TRD");
  }
  *out = std::move(t);
  return true;
}

}  // namespace slip
