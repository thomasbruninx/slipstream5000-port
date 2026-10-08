// In-race HUD ported from the original (HUD init 0x43B4A, frame draw 0x4429D, timer / position / speed 0x453B8, messages 0x44654,
// weapon panel 0x5C12D / 0x5BE65 / 0x522C0 / 0x5C234, damage and energy bars 0x572F2 / 0x5733B, viewport 0x44812). Everything is
// laid out on the original's 320x200 screen and scaled to the framebuffer; all art comes from the user's game files (console
// sprites CON<ship>_TN/BN, CONS_EXT, POS<n>, NSIGHT, TSIGHT, TURBO_N, fonts TIME / SPD / SMALLEST). See docs/hud.md.
#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "original_formats/formats.hpp"
#include "original_formats/game_data.hpp"

namespace slip {

// The original's 3D window (0x44812): x 4..315, y 8..166, projection centre (160, 87).
struct HudLayout {
  static constexpr int vx0 = 4, vy0 = 8, vx1 = 315, vy1 = 166, cx = 160, cy = 87;
};

struct HudAssets {
  Font time, spd, small, smallest, shade, menu;
  std::array<Sprite, 10> portrait;                // GAMEF0..9: the pilot's face while a voice line plays (52x48)
  std::array<Sprite, 10> pos;                     // POS0..9: rank digits
  Sprite nsight, tsight[2], turbo, consExt;       // crosshair, lock markers (flashing), turbo indicator, bar of the external views
  Sprite consTop[2], consBottom[2];               // CON<ship>_TN1/2, CON<ship>_BN1/2 (frame 2 = dark piece)
  Palette palette;
  bool loaded = false;
  bool load(const GameData& data, int shipIndex, const Palette& pal);
};

struct HudState {
  bool cockpit = true;          // console (TN + BN) instead of the CONS_EXT bar
  int consoleFrame = 1;         // 1, or 2 when the piece is dark (TRD piece +0x20 < 0x2000, 0x3526C)
  double speed = 0;             // slot speed, units/s
  bool kph = false;             // ':' (mph, speed / 715) or ';' (kph, speed / 444) glyph
  int rank = 1;                 // 1..10
  int lap = 0;                  // record +0x20 (0 before the start line)
  int totalLaps = 3;
  double lapTime = 0;           // [+0xE]
  double lastLapTime = -1;      // popup (4 s) after a lap
  double finalLapTimer = 0;     // "Final Lap!!" 2 s after the line is crossed on the last lap
  double gameOverTimer = 0;     // "GAME OVER" 4 s
  int countdown = 0;            // "Prepare to Race N"
  int finishedPosition = 0;     // "Finished Position N"
  double damageA = 0, damageB = 0;  // 0..100 (engine / steering); B reads 100 while controls are scrambled
  // weapon panel
  int selected = 0;             // 0 blaster, 1 A, 2 B, 3 booster
  std::string weaponName;       // name of the selected weapon
  int ammo = -1;                // < 0: unlimited (shows the energy bar instead of the READY / LOADING line)
  double energy = 1.0;          // 0..1 pool of the selected weapon
  bool canLock = false;         // selected weapon has a lock cone: crosshair
  bool booster = false;         // free boost or booster burning: TURBO indicator
  double boosterFuel = 1.0;
  bool lockVisible = false;     // lock marker on screen
  double lockX = 0, lockY = 0;  // virtual 320x200 coordinates of the target
  double time = 0;              // seconds, drives the lock marker flicker
  int portraitPilot = 0;        // 1..10: pilot speaking (0x53061), 0 none
  std::string portraitText;     // pilot's current rank, or "FINISHED" (0x58C42..0x58C6F)
  int centerX = HudLayout::cx, centerY = HudLayout::cy;  // projection centre incl. the hit shake
};

class Hud {
 public:
  // Draws everything except the 3D view (borders, console, bars, text, sights) into an ARGB framebuffer.
  void draw(uint32_t* fb, int w, int h, const HudState& st, const HudAssets& a) const;
  // Border colour index and the viewport in framebuffer pixels.
  static void viewport(int w, int h, int cx, int cy, int* x0, int* y0, int* x1, int* y1, float* pcx, float* pcy);
};

// Minimal canvas for the 320x200 virtual screen (also used by the pause menu).
struct HudCanvas {
  uint32_t* fb = nullptr;
  int w = 0, h = 0;
  const Palette* pal = nullptr;
  double sx() const { return double(w) / 320.0; }
  double sy() const { return double(h) / 200.0; }
  void fill(int x0, int y0, int x1, int y1, uint32_t argb) const;          // virtual rect, inclusive
  void fillIndex(int x0, int y0, int x1, int y1, int palIndex) const;
  void blit(const Sprite& s, int x, int y, int transparent) const;
  void blitScaled(const Sprite& s, int x, int y, int tw, int th, int transparent) const;
  void text(const Font& f, const std::string& s, int x, int y, int palIndex) const;  // x left
  void textCentered(const Font& f, const std::string& s, int x0, int x1, int y, int palIndex) const;
  void darken(int x0, int y0, int x1, int y1, int percent) const;
};

}  // namespace slip
