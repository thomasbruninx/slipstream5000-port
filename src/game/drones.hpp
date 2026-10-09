// The drones (0x4A240..0x4A847, DRONE.ART / DRONE.SHP): little white-and-red craft that fly along the track in front of the player. Shot with a beam (blaster,
// disrupter) one explodes and leaves a bonus object that lasts 15 s; hit by anything else, or run into by a ship, it just explodes. CONFIRMED from the executable: the first
// drone appears after 1 s, then one every 10 s while fewer than 6 are alive (0x4A27D: timer 0x3E8 / 0x2710, limit 6), each is created ten path nodes ahead of the human
// ship (0x36282, ECX = 10), the speed never exceeds 0x2BA3E (0x4A691), the path node is advanced when the drone is within 0x11DF0 of it (0x4A529). INFERRED / own: the
// exact speed curve (the original ties it to the distance to the human and the track curvature), the removal of drones far behind, the size of the hit sphere.
#pragma once
#include <array>
#include <vector>

#include "game/scene.hpp"
#include "game/ship_sim.hpp"
#include "game/weapons.hpp"

namespace slip {

struct Drone {
  double pos[3] = {0, 0, 0};
  double dir[3] = {0, 0, 1};
  int id = 0;          // unique per race (weapons lock on by id)
  int node = -1;       // path node it flies to (B)
  int prev = -1;       // the node it came from (A, [0x4A1F4])
  double speed = 120000;
  double phase = 0;
};

class DroneWorld {
 public:
  void reset();
  // One fixed step. `humanNode` is the path node of the human ship (-1 if unknown).
  // Ships (all ships still in the race) touching a drone destroy it; the weapons set DroneTarget::hit, which is handled here: a beam hit drops a bonus into `combat`.
  void step(const Scene& scene, double dt, const double humanPos[3], int humanNode, const std::vector<const ShipState*>& ships, CombatWorld* combat, bool championship);
  std::vector<Drone> drones;
  std::vector<DroneTarget> targets;     // parallel to `drones`, handed to the weapons
  struct Blast { double pos[3]; bool wall = false; };  // wall: it flew into the track (message 0x107): a fireball only, no pieces
  std::vector<Blast> blasts;            // drones that exploded in the last step (the application plays the explosion)
  double radius = 3000;

 private:
  double timer_ = 1.0;
  unsigned counter_ = 0;
  void spawn(const Scene& scene, int humanNode);
};

}  // namespace slip
