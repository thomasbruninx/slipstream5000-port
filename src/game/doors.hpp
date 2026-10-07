// Sliding track doors. Decoded from FindDoors (0x3C324), TrackInitDoors (0x3C75C/0x3C813) and the door slot server
// 0x3BF86 (see docs/simulation.md). Every list-A polygon with file flag 0x20 (a "Dummy" portal quad) is a door:
//  * slide direction s = normalised (v3 - v0), snapped to straight up/down when |y| >= 0.75, else its horizontal part;
//  * centre C = (v0 + v2) / 2, extents a = |v0 - v1| / 2, b = |v0 - v3| / 2;
//  * open position C + s * (2b - b/16), closed position = the midpoint of C and that (record +0x24 / +0x30);
//  * the panel (a textured DOORS quad the size of the Dummy quad) starts at the closed position and runs a constant
//    14300 u/s (0x37DC) state machine: state 0 moves along +s to the open position, state -1 along -s to the closed one;
//    reaching an end flips the state (no pause). When the way is blocked while closing (collision cube +-0x7A0) it
//    reopens; slot collisions are not ported, so that branch is not simulated.
#pragma once
#include <vector>

#include "game/scene.hpp"

namespace slip {

struct Door {
  int piece = -1;      // piece holding the Dummy quad (entities are drawn with their piece)
  double c[3] = {0, 0, 0};       // quad centre (world)
  double s[3] = {0, 0, 0};       // slide direction
  double closed[3] = {0, 0, 0}, open[3] = {0, 0, 0};
  double pos[3] = {0, 0, 0};     // current panel centre
  int state = -1;                // -1 closing (towards `closed`), 0 opening (towards `open`)
  Mesh mesh;                     // panel relative to its centre (two-sided DOORS quad)
};

struct Doors {
  std::vector<Door> list;
  void build(const Scene& scene);
  void step(double dt);  // original 0x3BF86 update at dt seconds
};

}  // namespace slip
