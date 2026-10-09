// The race controls (the original's Controls page, CFG words 0x492BC..0x492C8). The values are SDL scancodes (stored in config.txt); the front end is SDL free and only
// shows the names it is given. Defaults = the shipped SLIPSTRM.CFG / READ.ME: cursor up = nose down, cursor down = nose up, cursor left / right, Space = accelerate,
// Alt (Option) = fire, Ctrl = select weapon. The cameras are F1 cockpit, F2 chase, F3 rear, F4 TV, F5 free (keypad Ins / Del / PgUp / PgDn / + / - move the free camera)
// and are not remappable, like in the original. Brake is the port's own extra action (the original has none; unbound by default).
#pragma once
#include <array>

namespace slip {

enum class KeyAction { Up = 0, Down, Left, Right, Select, Fire, Accel, Brake, Count };
constexpr int kKeyActions = int(KeyAction::Count);

struct KeyMap {
  // scancodes of SDL3: UP 82, DOWN 81, LEFT 80, RIGHT 79, SPACE 44, LALT 226, LCTRL 224; 0 = unbound
  std::array<int, kKeyActions> sc = {82, 81, 80, 79, 224, 226, 44, 0};
  bool reverseAccel = false;  // CONTROLS.ST0 REVA "Reverse Accelerator": the craft accelerates while the key is NOT held
  static const char* label(KeyAction a) {  // the order of CONTROLS.ST0 DEF0..DEF6 (+ brake)
    static const char* n[kKeyActions] = {"Up", "Down", "Left", "Right", "Select", "Fire", "Accel", "Brake"};
    return n[int(a)];
  }
};

}  // namespace slip
