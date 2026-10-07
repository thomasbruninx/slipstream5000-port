// Procedural panel-line detail of non-textured track polygons (CONFIRMED structure, docs/research-log.md):
// polygon flag high byte 2..7 selects a template (a base polygon plus points that are midpoints of earlier
// points, 0x3F32A/0x3F4F2) and a list of line segments drawn in the polygon's ramp colour +1 / -1 (0x3FD2C etc.).
// The tables are read at runtime from the user's own SLIPSTRM.EXE; nothing from the executable is stored here.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "original_formats/game_data.hpp"

namespace slip {

struct PanelDetail {
  bool valid = false;
  int nBase = 0;                                   // polygon vertex count the template is made for
  std::vector<std::array<uint16_t, 2>> mid;        // point k (k >= nBase) = midpoint of points mid[k][0], mid[k][1]
  struct Line { uint16_t a, b; bool darker; };
  std::vector<Line> lines;
  // Floor types (flag high byte 0x86..0x8B): the polygon is NOT drawn as such; these sub-polygons are drawn instead
  // (colour: yellow = SDYellow ramp at 80 %, lane = the polygon's own material ramp at 80 %).
  struct Poly { std::vector<uint16_t> idx; bool yellow; };
  std::vector<Poly> polys;
  int gate = 0;                                    // draw only if the nearest vertex is at most this far (0 = no limit)
};

// Index: panelIndex(type) = type for 2..7, 8 + (type - 0x86) for the floor types 0x86..0x8B; other entries invalid.
inline int panelIndex(int type) { return type >= 0x86 && type <= 0x8B ? 8 + (type - 0x86) : (type >= 2 && type <= 7 ? type : 0); }
std::array<PanelDetail, 16> loadPanelDetails(const GameData& data);

}  // namespace slip
