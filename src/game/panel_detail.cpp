#include "game/panel_detail.hpp"

namespace slip {

namespace {
// virtual addresses of the template / line tables and the distance gate in the original executable (CONFIRMED by
// disassembling the type handlers 0x3FD2C, 0x3FEA6, 0x3FF2C, 0x3FFF0, 0x40074, 0x40523)
struct TypeInfo { int type; uint32_t tmpl, lines; int gate; };
constexpr TypeInfo kTypes[] = {
    {2, 0x3FDF2, 0x3FD90, 0x17D400}, {3, 0x3FFB0, 0x3FF0C, 0x17D400}, {4, 0x3FFB0, 0x3FF90, 0x17D400},
    {5, 0x400F8, 0x40054, 0x17D400}, {6, 0x400F8, 0x400D8, 0x17D400}, {7, 0x405E2, 0x40580, 0},
};
}  // namespace

std::array<PanelDetail, 32> loadPanelDetails(const GameData& data) {
  std::array<PanelDetail, 32> out{};
  auto exe = data.read("SLIPSTRM.EXE");
  if (!exe) return out;
  auto off = [](uint32_t va) { return size_t(0x4D854) + size_t(va - 0x10000); };
  auto u16 = [&](size_t o) -> int { return o + 2 <= exe->size() ? int((*exe)[o] | ((*exe)[o + 1] << 8)) : -1; };
  for (const TypeInfo& ti : kTypes) {
    PanelDetail d;
    const size_t t = off(ti.tmpl), l = off(ti.lines);
    const int nBase = u16(t), nPts = u16(t + 2), nLines = u16(l);
    if (nBase < 3 || nBase > 4 || nPts < nBase || nPts > 64 || nLines < 1 || nLines > 64) continue;  // not the expected build
    d.nBase = nBase;
    d.gate = ti.gate;
    d.mid.assign(size_t(nPts), {0xFFFF, 0xFFFF});
    // entries are 4 bytes starting after the 4-byte header; the first nBase (really: the first four) are placeholders
    bool ok = true;
    for (int k = 0; k < nPts; ++k) {
      const int a = u16(t + 4 + 4 * size_t(k)), b = u16(t + 6 + 4 * size_t(k));
      if (a < 0 || b < 0) { ok = false; break; }
      d.mid[size_t(k)] = {uint16_t(a), uint16_t(b)};
    }
    for (int k = 0; k < nLines && ok; ++k) {
      const int a = u16(l + 2 + 6 * size_t(k)), b = u16(l + 4 + 6 * size_t(k)), f = u16(l + 6 + 6 * size_t(k));
      if (a < 0 || b < 0 || f < 0 || a >= nPts || b >= nPts) { ok = false; break; }
      d.lines.push_back({uint16_t(a), uint16_t(b), f != 0});
    }
    d.valid = ok;
    out[size_t(ti.type)] = std::move(d);
  }
  // floor types (handlers 0x40AB8..0x4128A): template, three sub-polygons (two border polygons in the SDYellow colour and
  // the lane polygon in the material colour) and a line list, all at 80 % ramp; far beyond 0x29B300 the polygon is flat
  struct Floor { int type; uint32_t tmpl; int n[3]; uint32_t list[3]; uint32_t lines; };
  constexpr Floor kFloors[] = {
      {0x86, 0x40C18, {4, 4, 4}, {0x40BA4, 0x40BAC, 0x40BB4}, 0x40BBC}, {0x87, 0x40DFE, {4, 3, 4}, {0x40DC2, 0x40DCA, 0x40DD0}, 0x40DD8},
      {0x88, 0x40DFE, {4, 3, 4}, {0x40F6C, 0x40F74, 0x40F7A}, 0x40F84}, {0x89, 0x410DE, {4, 3, 4}, {0x410A0, 0x410A8, 0x410AE}, 0x410B8},
      {0x8A, 0x410DE, {4, 3, 4}, {0x4124C, 0x41254, 0x4125A}, 0x41264}, {0x8B, 0x41356, {4, 4, 4}, {0x4133E, 0x41346, 0x4134E}, 0},
  };
  for (const Floor& f : kFloors) {
    PanelDetail d;
    const size_t t = off(f.tmpl);
    const int nBase = u16(t), nPts = u16(t + 2);
    if (nBase < 3 || nBase > 4 || nPts < nBase || nPts > 64) continue;
    d.nBase = nBase;
    d.gate = 0x29B300;
    d.mid.assign(size_t(nPts), {0xFFFF, 0xFFFF});
    bool ok = true;
    for (int k = 0; k < nPts; ++k) {
      const int a = u16(t + 4 + 4 * size_t(k)), b = u16(t + 6 + 4 * size_t(k));
      if (a < 0 || b < 0) { ok = false; break; }
      d.mid[size_t(k)] = {uint16_t(a), uint16_t(b)};
    }
    for (int j = 0; j < 3 && ok; ++j) {
      PanelDetail::Poly p;
      p.yellow = j < 2;
      for (int k = 0; k < f.n[j]; ++k) {
        const int v = u16(off(f.list[j]) + 2 * size_t(k));
        if (v < 0 || v >= nPts) { ok = false; break; }
        p.idx.push_back(uint16_t(v));
      }
      d.polys.push_back(std::move(p));
    }
    if (f.lines && ok) {
      const size_t l = off(f.lines);
      const int nLines = u16(l);
      if (nLines < 1 || nLines > 64) ok = false;
      for (int k = 0; k < nLines && ok; ++k) {
        const int a = u16(l + 2 + 6 * size_t(k)), b = u16(l + 4 + 6 * size_t(k)), fl = u16(l + 6 + 6 * size_t(k));
        if (a < 0 || b < 0 || fl < 0 || a >= nPts || b >= nPts) { ok = false; break; }
        d.lines.push_back({uint16_t(a), uint16_t(b), fl != 0});
      }
    }
    d.kind = PanelKind::Floor;
    d.valid = ok;
    out[size_t(panelIndex(f.type))] = std::move(d);
  }
  // cage / wire-grid types (handlers 0x40138..0x4175A): template + 2-point line list (4-byte entries), 80 % ramp colour,
  // gate 0x129DA00; types 0x80-0x82 use the SDCage material, the others the polygon's own
  struct Cage { int type; uint32_t tmpl, lines; bool sd; };
  constexpr Cage kCages[] = {
      {0x80, 0x4019A, 0x4017C, true},  {0x81, 0x4047A, 0x40464, true},  {0x82, 0x404FA, 0x404E4, true},  {0x84, 0x4028A, 0x4027C, false},
      {0x85, 0x402F2, 0x402E4, false}, {0x8C, 0x4036A, 0x4034C, false}, {0x8D, 0x4021E, 0x40208, false}, {0x8E, 0x403F6, 0x403D8, false},
      {0x91, 0x416E6, 0x416D4, false}, {0x92, 0x41746, 0x4173C, false}, {0x93, 0x417A2, 0x41798, false},
  };
  for (const Cage& c : kCages) {
    PanelDetail d;
    const size_t t = off(c.tmpl), l = off(c.lines);
    const int nBase = u16(t), nPts = u16(t + 2), nLines = u16(l);
    if (nBase < 3 || nBase > 4 || nPts < nBase || nPts > 64 || nLines < 1 || nLines > 64) continue;
    d.nBase = nBase;
    d.cage = true;
    d.kind = PanelKind::Cage;
    d.sdCage = c.sd;
    d.gate = 0x129DA00;
    d.mid.assign(size_t(nPts), {0xFFFF, 0xFFFF});
    bool ok = true;
    for (int k = 0; k < nPts; ++k) {
      const int a = u16(t + 4 + 4 * size_t(k)), b = u16(t + 6 + 4 * size_t(k));
      if (a < 0 || b < 0) { ok = false; break; }
      d.mid[size_t(k)] = {uint16_t(a), uint16_t(b)};
    }
    for (int k = 0; k < nLines && ok; ++k) {
      const int a = u16(l + 2 + 4 * size_t(k)), b = u16(l + 4 + 4 * size_t(k));
      if (a < 0 || b < 0) { ok = false; break; }
      if (a >= nPts || b >= nPts) continue;  // 0xFFFF entries terminate/skip in the original list
      d.lines.push_back({uint16_t(a), uint16_t(b), false});
    }
    d.valid = ok;
    out[size_t(panelIndex(c.type))] = std::move(d);
  }
  // animated lights and the road-line floor (handlers 0x3F5BE, 0x40865, 0x3FA5C, 0x4147E)
  auto readTemplate = [&](PanelDetail& d, uint32_t va) {
    const size_t t = off(va);
    const int nBase = u16(t), nPts = u16(t + 2);
    if (nBase < 3 || nBase > 16 || nPts < nBase || nPts > 200) return false;
    d.nBase = nBase;
    d.mid.assign(size_t(nPts), {0xFFFF, 0xFFFF});
    for (int k = 0; k < nPts; ++k) {
      const int a = u16(t + 4 + 4 * size_t(k)), b = u16(t + 6 + 4 * size_t(k));
      if (a < 0 || b < 0) return false;
      d.mid[size_t(k)] = {uint16_t(a), uint16_t(b)};
    }
    return true;
  };
  auto readList = [&](uint32_t va, int n, bool counted) {  // point-index list, optionally preceded by its count
    std::vector<uint16_t> v;
    size_t o = off(va);
    if (counted) { n = u16(o); o += 2; }
    if (n < 3 || n > 16) return v;
    for (int i = 0; i < n; ++i) { const int x = u16(o + 2 * size_t(i)); if (x < 0) return std::vector<uint16_t>{}; v.push_back(uint16_t(x)); }
    return v;
  };
  auto ptrAt = [&](uint32_t table, int i) -> uint32_t {  // relocated pointer stored as object-relative offset
    const size_t o = off(table) + 4 * size_t(i);
    if (o + 4 > exe->size()) return 0;
    return uint32_t((*exe)[o] | ((*exe)[o + 1] << 8) | ((*exe)[o + 2] << 16) | (uint32_t((*exe)[o + 3]) << 24)) + 0x10000u;
  };
  {  // type 0x90: road floor with white centre lines
    PanelDetail d;
    d.kind = PanelKind::RoadFloor;
    d.gate = 0x1656C0;
    bool ok = readTemplate(d, 0x415AC);
    const uint32_t lane[2] = {0x4159C, 0x415A4}, line[3] = {0x41584, 0x4158C, 0x41594};
    for (int j = 0; j < 2 && ok; ++j) { auto v = readList(lane[j], 4, false); ok = !v.empty(); d.polys.push_back({v, j == 1}); }  // yellow=true marks the darker lane
    for (int j = 0; j < 3 && ok; ++j) { auto v = readList(line[j], 4, false); ok = !v.empty(); d.roadLine.push_back({v, false}); }
    d.valid = ok;
    out[size_t(panelIndex(0x90))] = std::move(d);
  }
  {  // type 0x8F: floor light strip, chase of 8 quad pairs
    PanelDetail d;
    d.kind = PanelKind::ChaseFloor;
    d.gate = 0x1A0FE0;
    bool ok = readTemplate(d, 0x3FBE0);
    for (int k = 0; k < 8 && ok; ++k) {
      auto a = readList(ptrAt(0x3FB00, 2 * k), 0, true), b = readList(ptrAt(0x3FB00, 2 * k + 1), 0, true);
      ok = !a.empty() && !b.empty();
      d.lampsA.push_back(a);
      d.lampsB.push_back(b);
    }
    d.valid = ok;
    out[size_t(panelIndex(0x8F))] = std::move(d);
  }
  {  // type 1: orange chase lights (4 pairs, 8 more at the highest detail)
    PanelDetail d;
    d.kind = PanelKind::ChaseOrange;
    d.gate = 0x20C380;
    bool ok = readTemplate(d, 0x3F810);
    for (int k = 0; k < 4 && ok; ++k) {
      auto a = readList(ptrAt(0x3F6C0, 2 * k), 0, true), b = readList(ptrAt(0x3F6C0, 2 * k + 1), 0, true);
      ok = !a.empty() && !b.empty();
      d.lampsA.push_back(a);
      d.lampsB.push_back(b);
    }
    for (int k = 0; k < 8 && ok; ++k) {
      auto a = readList(ptrAt(0x3F6E0, 2 * k), 0, true), b = readList(ptrAt(0x3F6E0, 2 * k + 1), 0, true);
      ok = !a.empty() && !b.empty();
      d.lampsA2.push_back(a);
      d.lampsB2.push_back(b);
    }
    d.valid = ok;
    out[size_t(panelIndex(1))] = std::move(d);
  }
  {  // type 0x1E: refuel pad, 8 pulsing polygons over the flat pad
    PanelDetail d;
    d.kind = PanelKind::Refuel;
    d.gate = 0x20C380;
    bool ok = readTemplate(d, 0x4097C);
    for (int k = 0; k < 8 && ok; ++k) { auto v = readList(0x4092Cu + 10u * uint32_t(k), 0, true); ok = !v.empty(); d.lampsA.push_back(v); }
    d.valid = ok;
    out[size_t(panelIndex(0x1E))] = std::move(d);
  }
  return out;
}

}  // namespace slip
