// Ship dynamics ported from the original RaceSlotMove (0x51B0A) / RaceSlotHover (0x51EC4): thrust law, steer -> bank ->
// yaw orientation update on a 3x3 slot matrix, drag on the slide velocity and the speed -> velocity step were decoded and
// checked against the x86 emulator oracle (tools/re/phys_*.py, docs/simulation.md). Braking, vertical hover and wall
// handling are still placeholders until the collision module is ported.
#pragma once
#include "game/scene.hpp"
#include "game/ship_params.hpp"

namespace slip {

struct ShipState {
  double x = 0, y = 0, z = 0;  // world coordinates
  double yaw = 0;               // radians; 0 = looking along +z, positive turns towards +x
  double pitch = 0;
  double speed = 0;
  int ship = 0;
  // Orientation matrix like slot+0x48: rows = right, up, forward (world), unit scale. Identity = facing +z.
  double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  double slide[3] = {0, 0, 0};  // slide velocity (slot data +0..+8), decays with drag 4/s
  double pitchAxis = 0;         // ramped pitch axis
  double boxLo[3] = {0, 0, 0}, boxHi[3] = {0, 0, 0};  // collision box in model space (ART extents); empty = no track collision
  bool hasBox = false;
  double hitCooldown = 0;       // keeps the hit message at the original's frame cadence
  int hits = 0;                 // number of wall/floor hits so far (diagnostics)
  double steerAxis = 0;         // ramped steering axis -1..1 (keyboard behaviour of the original)
  double roll = 0;              // bank in turns (derived)
  bool matrixInit = false;      // set once m was built from yaw
};

// Object-to-world rotation (columns = right, up, forward) for the renderer, row-major.
void shipRenderMatrix(const ShipState& s, float* R);

struct ShipInput {
  float throttle = 0;  // 0..1
  float brake = 0;     // 0..1
  float steer = 0;     // -1..1 (positive = right)
  float pitch = 0;     // -1..1 (positive = nose up); ramped like steering
};

struct ShipSimConfig {
  double hoverHeight = 14000;   // world units above floor (PLACEHOLDER)
  double maxYawRate = 1.8;      // rad/s (legacy assist mode only)
  bool assist = false;          // legacy floor-following hover (no track collision, no pitch control)
};

// Fills the collision box from a ship mesh (x made symmetric like TrackSlotAdd 0x349d1).
void setShipBoxFromMesh(ShipState& s, const Mesh& mesh);

// Wall-hit message handler of the original (0x107): speed *= 0.75 and a bounce of speed along
// cos(67.5deg)*tangent-of-heading + sin(67.5deg)*normal added to the slide velocity.
void shipHitResponse(ShipState& s, const double n[3], const double heading[3]);

// Scene-independent part: orientation, speed, drag; returns the world velocity (units/s) the slot engine would integrate.
void stepShipDynamics(ShipState& s, const ShipInput& in, double dt, const ShipParams& p, double* vel);
void stepShip(ShipState& s, const ShipInput& in, double dt, const ShipParams& p, const Scene& scene, const ShipSimConfig& cfg);

}  // namespace slip
