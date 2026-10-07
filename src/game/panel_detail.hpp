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

enum class PanelKind { Lines, Floor, Cage, RoadFloor, ChaseFloor, ChaseOrange, Refuel };

struct PanelDetail {
  PanelKind kind = PanelKind::Lines;
  bool valid = false;
  int nBase = 0;                                   // polygon vertex count the template is made for
  std::vector<std::array<uint16_t, 2>> mid;        // point k (k >= nBase) = midpoint of points mid[k][0], mid[k][1]
  struct Line { uint16_t a, b; bool darker; };
  std::vector<Line> lines;
  // Floor types (flag high byte 0x86..0x8B): the polygon is NOT drawn as such; these sub-polygons are drawn instead
  // (colour: yellow = SDYellow ramp at 80 %, lane = the polygon's own material ramp at 80 %).
  struct Poly { std::vector<uint16_t> idx; bool yellow; };
  std::vector<Poly> polys;
  // animated light types: pairs of quads (point-index lists) switched by the chase phase / pulse colour
  std::vector<std::vector<uint16_t>> lampsA, lampsB;  // Refuel: lampsA = the 8 pulsing polygons; Chase*: pairs (a_i, b_i) in lampsA/B
  std::vector<std::vector<uint16_t>> lampsA2, lampsB2;  // ChaseOrange extra set drawn at the highest detail
  std::vector<Poly> roadLine;                           // RoadFloor: the three SDRoadLine polygons
  bool cage = false;                               // wire-grid type: only lines are drawn, in the 80 % ramp colour
  bool sdCage = false;                             // ... of the SDCage material instead of the polygon's own
  int gate = 0;                                    // draw only if the nearest vertex is at most this far (0 = no limit)
};

// Index: panelIndex(type) = type for 2..7, 8 + (type - 0x86) for the floor types 0x86..0x8B; other entries invalid.
inline int panelIndex(int type) {
  if (type >= 0x86 && type <= 0x8B) return 8 + (type - 0x86);
  if (type >= 2 && type <= 7) return type;
  switch (type) {  // cage / wire-grid types and light types
    case 0x01: return 24; case 0x1E: return 25; case 0x8F: return 26; case 0x90: return 27;
    case 0x80: return 14; case 0x81: return 15; case 0x82: return 16; case 0x84: return 17; case 0x85: return 18;
    case 0x8C: return 19; case 0x8D: return 20; case 0x8E: return 21; case 0x91: return 22; case 0x92: return 23;
    default: return 0;
  }
}
std::array<PanelDetail, 28> loadPanelDetails(const GameData& data);

}  // namespace slip
