// Platform-independent description of one frame of user input.
#pragma once
namespace slip {
struct InputState {
  float moveForward = 0, moveRight = 0, moveUp = 0;  // -1..1 (free camera)
  float lookDX = 0, lookDY = 0;                      // radians this frame
  bool fast = false;
  float throttle = 0, brake = 0, steer = 0, pitch = 0;          // drive mode
  bool showList = false;                                        // Tab held: multiplayer player list
  bool fire = false;                                            // weapon trigger (held)
};
}  // namespace slip
