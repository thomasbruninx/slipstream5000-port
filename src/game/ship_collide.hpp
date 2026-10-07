// Ship-vs-track collision. The geometry test is ported from the original track collision sweep
// (TrackSlot collision callback 0x38C97 -> 0x39054 -> 0x3CAEA, see docs/simulation.md):
//  * every corner of the ship's collision box (min/max of the ART model, x symmetric) is swept along the movement;
//  * the polygons tested are the list-A polygons of the piece(s) containing the corner and of their portal neighbours,
//    skipping portals and file-flag-0x40 polygons; a polygon only counts when the movement runs against its normal
//    (cos >= 0x10/0x4000) and the corner is in front of the plane;
//  * the plane is hit when the corner comes within 0x1E8 units; the hit point must lie inside the polygon (0x36CEC).
// The collision *response* (what the original does after the hit, 0x1394E/0x38E84/CollideStep) is not fully decoded
// yet; shipCollideResponse() is a PLACEHOLDER documented as such.
#pragma once
#include "game/scene.hpp"

namespace slip {

constexpr double kShipCollideMargin = 0x1E8;  // CONFIRMED: sub 0x1E8 in 0x3CBED

struct ShipHit {
  bool hit = false;
  double dist = 0;     // distance the box may travel before the nearest polygon is reached (>= 0)
  double n[3] = {0, 1, 0};
  uint16_t polyFlags = 0;
  int piece = -1;
  int material = -1;  // Scene::materials index of the polygon that was hit
};

// pos in world coordinates, M = orientation rows (right, up, forward), lo/hi = collision box in model space.
ShipHit sweepShipBox(const Scene& scene, const double pos[3], const double M[9], const double lo[3], const double hi[3], const double dir[3], double len);

// True when all eight corners lie inside some track piece (the 0x391CC consistency check, simplified).
bool shipBoxInsideTrack(const Scene& scene, const double pos[3], const double M[9], const double lo[3], const double hi[3]);

}  // namespace slip
