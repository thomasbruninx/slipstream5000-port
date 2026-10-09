// The race controls (the original's Controls page, CFG words 0x492BC..0x492C8). The values are SDL scancodes (stored in config.txt); the front end is SDL free and only
// shows the names it is given. Defaults = the shipped SLIPSTRM.CFG / READ.ME: cursor up = nose down, cursor down = nose up, cursor left / right, Space = accelerate,
// Alt (Option) = fire, Ctrl = select weapon. The cameras are F1 cockpit, F2 chase, F3 rear, F4 TV, F5 free (keypad Ins / Del / PgUp / PgDn / + / - move the free camera)
// and are not remappable, like in the original. Brake is the port's own extra action (the original has none; unbound by default).
#pragma once
#include <array>

namespace slip {

enum class KeyAction { Up = 0, Down, Left, Right, Select, Fire, Accel, Brake, Count };
constexpr int kKeyActions = int(KeyAction::Count);

// Controller (SDL3 gamepad API, any USB / Bluetooth pad that SDL knows - its mapping database plus an optional gamecontrollerdb.txt): the race actions bound to a button
// (SDL_GamepadButton value) or, for 1000 + axis, a trigger (4 = left, 5 = right). Steering and pitch are the left stick (and the D-pad); the right stick works the free camera.
enum class PadAction { Accel = 0, Brake, Fire, Select, Camera, Pause, Count };
constexpr int kPadActions = int(PadAction::Count);

struct KeyMap {
  // scancodes of SDL3: UP 82, DOWN 81, LEFT 80, RIGHT 79, SPACE 44, LALT 226, LCTRL 224; 0 = unbound
  std::array<int, kKeyActions> sc = {82, 81, 80, 79, 224, 226, 44, 0};
  // defaults: right trigger accelerates, left trigger brakes, West (X / square) fires, East (B / circle) selects the weapon, North (Y / triangle) changes the camera, Start pauses
  std::array<int, kPadActions> pad = {1005, 1004, 2, 1, 3, 6};
  bool padInvertPitch = false;  // stick up = nose down like the original's "up = down"; on = flight-simulator style
  bool reverseAccel = false;  // CONTROLS.ST0 REVA "Reverse Accelerator": the craft accelerates while the key is NOT held
  static const char* label(KeyAction a) {  // the order of CONTROLS.ST0 DEF0..DEF6 (+ brake)
    static const char* n[kKeyActions] = {"Up", "Down", "Left", "Right", "Select", "Fire", "Accel", "Brake"};
    return n[int(a)];
  }
};

}  // namespace slip
