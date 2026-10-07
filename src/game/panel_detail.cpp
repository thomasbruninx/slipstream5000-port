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

std::array<PanelDetail, 16> loadPanelDetails(const GameData& data) {
  std::array<PanelDetail, 16> out{};
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
    d.valid = ok;
    out[size_t(panelIndex(f.type))] = std::move(d);
  }
  return out;
}

}  // namespace slip
