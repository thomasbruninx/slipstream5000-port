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
#include "game/ship_sim.hpp"

namespace slip {

struct Door {
  int piece = -1;      // piece holding the Dummy quad (entities are drawn with their piece)
  double c[3] = {0, 0, 0};       // quad centre (world)
  double s[3] = {0, 0, 0};       // slide direction
  double closed[3] = {0, 0, 0}, open[3] = {0, 0, 0};
  double pos[3] = {0, 0, 0};     // current panel centre
  double speed = 0x37dc;         // u/s; reset to 0x37DC on every flip, AI ships raise it to 0x53CA while they are on the door's piece (0x35564)
  int state = -1;                // -1 closing (towards `closed`), 0 opening (towards `open`)
  // Collision cube of the door slot (TrackInitDoors 0x3C813: the panel rectangle, thickness +-0x7A0 along the normal; INFERRED mapping):
  double u[3] = {1, 0, 0}, w[3] = {0, 1, 0}, n[3] = {0, 0, 1};  // panel axes (v0->v1, v0->v3) and normal
  double ha = 0, hb = 0;                                         // half extents along u and w
  double prev[3] = {0, 0, 0};                                    // position before the last step (contact solver)
  bool touched = false;                                          // set by the step when a ship overlaps / touches it
  Mesh mesh;                     // panel relative to its centre (two-sided DOORS quad)
};

struct Doors {
  std::vector<Door> list;
  void build(const Scene& scene);
  void step(double dt);  // original 0x3BF86 update at dt seconds, without ships (viewer)
  // With ships (door slot update 0x104): a door whose cube overlaps a ship at the start of the step opens at 0x37DC; a closing
  // door that would end up inside a ship stays where it is and reopens (0x3C17B).
  void step(double dt, const std::vector<ShipState*>& ships);
  // Door as a static-box slot for the contact solver: box = the cube, velocity = slide direction * speed (in `slide`).
  ShipState proxy(size_t i) const;
  // Message 0x106 from a ship (0x3BFDB): the door opens at 0x6FB8 u/s.
  void touch(size_t i);
};

}  // namespace slip
