#include <cctype>
#include <cmath>
#include <map>
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
  if (auto exe = data.read("SLIPSTRM.EXE")) {  // static UI palette (VideoSetPalette at 0x557C7, data 0x54304): entries 248..255 = black x3, grey, green, red, yellow, white
    constexpr size_t o = 0x4D854 + (0x54304 - 0x10000);
    if (exe->size() > o + 4 + 24 && (*exe)[o] == 248 && (*exe)[o + 1] == 0 && (*exe)[o + 2] == 8 && (*exe)[o + 3] == 0) {
      for (int i = 0; i < 8; ++i) {
        auto up = [](uint32_t v) { return (v << 2) | (v >> 4); };
        const uint32_t r = up((*exe)[o + 4 + size_t(i) * 3]), g = up((*exe)[o + 5 + size_t(i) * 3]), b = up((*exe)[o + 6 + size_t(i) * 3]);
        t.palette.rgba[size_t(248 + i)] = (r << 16) | (g << 8) | b;
      }
    }
  }
  if (auto m = parseMaterials(*matB)) t.materials = *m; else return fail("bad MAT");
  if (auto cars = data.read("CARS.MAT")) if (auto m = parseMaterials(*cars)) t.materials.append(*m);

  // ---- TRK ----
  {
    R r{*trkB};
    t.trkVersion = r.u16(2);
    if (r.u16(0) != trkB->size() || t.trkVersion != 0x2b) return fail("unexpected TRK header");
    t.portalOnly = r.s16(0x9E) != 0;
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
      int listNo = -1;
      for (size_t lo : lists) {
        ++listNo;
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
          poly.flags = r.u16(q + 8);
          poly.offset = uint32_t(q);
          poly.list = uint8_t(listNo);
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

  std::vector<uint32_t> groupOffsets;
  std::vector<uint32_t> pieceNodeOff;
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
          TrackPiece tp;
          tp.trdOffset = uint32_t(e);
          for (int j = 0; j < 3; ++j) tp.links[j] = {r.u16(e + 4 + 4 * size_t(j)), r.u16(e + 6 + 4 * size_t(j))};
          tp.record = ri;
          tp.pos = {r.s32(e + 0x12), r.s32(e + 0x16), r.s32(e + 0x1a)};
          tp.group = g;
          tp.light = r.u16(e + 0x20);
          pieceNodeOff.push_back(r.u16(e + 0x1e));
          t.pieces.push_back(tp);
        }
      }
      if (sh) {
        uint16_t c = r.u16(sh);
        for (int k = 0; k < c; ++k) {
          size_t e = sh + 2 + 0x46 * size_t(k);
          SceneryInstance s;
          s.shape = cstr(*trdB, e, 12);
          s.pos = {r.s32(e + 0x10), r.s32(e + 0x14), r.s32(e + 0x18)};
          s.group = g;
          s.entryOffset = uint32_t(e);
          s.visMask = r.u16(e + 0x36);
          s.radius = uint32_t(r.s32(e + 0x1C));
          s.billboard = r.u16(e + 0x38) != 0;
          for (int m = 0; m < 9; ++m) s.matrix[m] = r.s16(e + 0x24 + 2 * size_t(m));
          if (!s.shape.empty()) t.scenery.push_back(s);
        }
      }
      groupOffsets.push_back(uint32_t(p));
      {
        std::vector<GroupTreeNode> tree;
        size_t tb = r.u16(p + 6);
        if (tb) {
          size_t nn = r.u16(tb);
          for (size_t k = 0; k < nn && r.ok; ++k) {
            size_t o = tb + 2 + 24 * k;
            GroupTreeNode nd;
            uint32_t ca = uint32_t(r.s32(o)), cb = uint32_t(r.s32(o + 4));
            nd.a = ca ? int((ca - 2) / 24) : -1;
            nd.b = cb ? int((cb - 2) / 24) : -1;
            nd.item = uint32_t(r.s32(o + 8));
            nd.type = r.u16(o + 0xC);
            nd.leaf = r.s16(o + 0x10) == -1;
            nd.point = nd.leaf ? -1 : int(r.u16(o + 0x10));
            float len = 0;
            for (int j = 0; j < 3; ++j) { nd.n[j] = float(r.s16(o + 0x12 + 2 * size_t(j))); len += nd.n[j] * nd.n[j]; }
            len = std::sqrt(len);
            if (len > 0) for (int j = 0; j < 3; ++j) nd.n[j] /= len;
            tree.push_back(nd);
          }
          // plane through group point `point`: points are u16 x,y,z (<<6) relative to the group origin (+0xA,+0xE,+0x12)
          size_t pl = r.u16(p + 2);
          if (pl) {
            const double org[3] = {double(r.s32(p + 0xA)), double(r.s32(p + 0xE)), double(r.s32(p + 0x12))};
            for (auto& nd : tree) {
              if (nd.leaf || nd.point < 0) continue;
              size_t q = pl + 2 + 8 * size_t(nd.point);
              double P[3];
              for (int j = 0; j < 3; ++j) P[j] = org[j] + double(r.u16(q + 2 * size_t(j))) * 64.0;
              nd.plane = float(nd.n[0] * P[0] + nd.n[1] * P[1] + nd.n[2] * P[2]);
            }
          }
        }
        t.groupTrees.push_back(std::move(tree));
      }
      p += sz;
    }
    t.groupCount = int(ng);
    if (!r.ok) return fail("truncated TRD");
  }
  // ---- TRD path nodes ----
  {
    R r{*trdB};
    size_t lo = r.u16(8);
    size_t n = lo ? r.u16(lo) : 0;
    std::map<uint32_t, int> at;
    for (size_t i = 0; i < n; ++i) at[uint32_t(lo + 2 + 0x32 * i)] = int(i);
    for (size_t i = 0; i < n && r.ok; ++i) {
      size_t o = lo + 2 + 0x32 * i;
      TrackNode nd;
      nd.offset = uint32_t(o);
      auto idx = [&](uint32_t off) { auto it = at.find(off); return it == at.end() ? -1 : it->second; };
      nd.next = idx(r.u16(o));
      nd.prev = idx(r.u16(o + 2));
      nd.alt = idx(r.u16(o + 4));
      nd.straight = r.u16(o + 8);
      nd.merge = r.u16(o + 6);
      nd.pos = {r.s32(o + 0xc), r.s32(o + 0x10), r.s32(o + 0x14)};
      nd.width = r.s32(o + 0x18);
      nd.remain = r.s32(o + 0x24);
      t.nodes.push_back(nd);
    }
    if (!r.ok) t.nodes.clear();
    for (size_t i = 0; i < t.pieces.size(); ++i) {
      if (t.pieces[i].trdOffset == r.u16(4)) t.lapPieceA = int(i);
      if (t.pieces[i].trdOffset == r.u16(6)) t.lapPieceB = int(i);
    }
    for (size_t i = 0; i < t.pieces.size() && i < pieceNodeOff.size(); ++i) {
      auto it = at.find(pieceNodeOff[i]);
      t.pieces[i].node = it == at.end() ? -1 : it->second;
    }
  }
  // InitRefuel (0x3D568): the piece with a polygon whose material is "REFUEL 3"; nodes with an alternative route that
  // reaches that piece's node before a merge node get the pit flag.
  {
    for (size_t i = 0; i < t.pieces.size() && t.refuelPiece < 0; ++i)
      for (const TrackPolygon& poly : t.records[size_t(t.pieces[i].record)].polys) {
        if (poly.list != 0 || poly.material >= t.trcMaterials.size()) continue;
        std::string nm = t.trcMaterials[poly.material].name;
        for (char& ch : nm) ch = char(std::toupper((unsigned char)ch));
        if (nm.rfind("REFUEL 3", 0) == 0) { t.refuelPiece = int(i); break; }
      }
    if (t.refuelPiece >= 0 && t.pieces[size_t(t.refuelPiece)].node >= 0) {
      const int target = t.pieces[size_t(t.refuelPiece)].node;
      for (size_t i = 0; i < t.nodes.size(); ++i) {
        const TrackNode& nd = t.nodes[i];
        if (nd.alt < 0) continue;
        int w = nd.alt;
        for (int guard = 0; guard < 1000 && w >= 0; ++guard) {
          if (t.nodes[size_t(w)].merge) break;
          if (w == target) { t.nodes[i].pit = true; break; }
          if (w == int(i)) break;
          w = t.nodes[size_t(w)].next;
        }
      }
    }
  }
  // ---- TRK BSP (needs TRD group offsets) ----
  {
    R r{*trkB};
    size_t ct = r.u16(0xC);
    size_t n = ct ? r.u16(ct) : 0;
    std::map<uint32_t, int> nodeAt;
    for (size_t i = 0; i < n; ++i) nodeAt[uint32_t(ct + 2 + 24 * i)] = int(i);
    t.bsp.resize(n);
    for (size_t i = 0; i < n && r.ok; ++i) {
      size_t o = ct + 2 + 24 * i;
      uint16_t kind = r.u16(o);
      BspNode& nd = t.bsp[i];
      if (kind == 0xFFFF) {
        uint16_t trdOff = r.u16(o + 6);
        for (size_t g = 0; g < groupOffsets.size(); ++g)
          if (groupOffsets[g] == trdOff) nd.group = int(g);
        continue;
      }
      if (kind < 22) { nd.axis = 0; nd.point = kind / 2 + 1; }
      else if (kind < 28) { nd.axis = 1; nd.point = (kind - 22) / 2 + 1; }
      else { nd.axis = 2; nd.point = (kind - 28) / 2 + 1; }
      auto hi = nodeAt.find(r.u16(o + 2)), lo = nodeAt.find(r.u16(o + 4));
      nd.hi = hi == nodeAt.end() ? -1 : hi->second;
      nd.lo = lo == nodeAt.end() ? -1 : lo->second;
    }
    if (!r.ok) t.bsp.clear();
  }
  *out = std::move(t);
  return true;
}

}  // namespace slip
