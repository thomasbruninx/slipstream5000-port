// Pause menu. The entries and texts are the original's (PAUSED.ST0 "Continue Race / Configuration / Quit Race / Exit To Dos",
// CONFIG.ST0 "General / Controls / Detail / Continue / Difficulty / Sound", shown with MENUFONT.FNT); the layout is the port's own
// (the original's menu zone engine is not ported). DoGame3D 0x58D5E..0x58F9C handles the pause state [0x592DA]: while paused the race
// clock stands still and the menu result decides: 1 continue, 2 configuration, 3 quit the race (0x591CE), 4 exit (0x55DEE).
#pragma once
#include <string>
#include <vector>

#include "game/hud.hpp"
#include "game/keymap.hpp"

namespace slip {

// Persistent in ~/Library/Application Support/Slipstream/config.txt (settings_file.cpp).
struct GameSettings {
  float music = 0.8f, sfx = 1.0f;   // volumes 0..1
  int difficulty = 1;               // [0x492EA]: 0..2
  bool kph = false;                 // speed unit of the HUD
  bool trackMap = true;             // General page "Track map" (config word [0x492FE]): the map overlay of the race screen (M)
  int detail = 3;                   // 0..3: scenery size cull thresholds 32 / 20 / 10 / 5 (0x350C7); 3 = highest
  // The options of the original's configuration screens (CFG words 0x492E2..0x492FE, frontend_config.cpp). The ones the port cannot show (clouds, shading,
  // textures, window size) are stored only; the rear and weapons monitors work (viewer_app.cpp drawRearMonitor).
  bool rearMonitor = true, weaponsMonitor = true, clouds = true, texturesCoarse = false, windowReduced = false, shadows = true;
  bool damage = true;               // [0x492EE] "Damage": off = the human's craft takes no damage (RaceSlotDamage 0x52035)
  bool sfxOn = true, speech = true, musicOn = true;
  int engine = 2;                   // engine sounds: 0 off, 1 quiet, 2 normal (CFG word 0x492E2)
  int shading = 2;                  // 0 none, 1 Gouraud, 2 specular
  // Graphics (the port's own, Detail / Effects pages). Everything but `fullscreen` is read at start-up: the menus tell the player that a restart is needed (graphicsChanged()).
  int renderer = 0;                 // 0 software, 1 OpenGL
  int resolution = 1;               // index into kResolutions: the internal render size
  bool fullscreen = false;          // applied at once
  int filter = 0;                   // OpenGL: 0 nearest, 1 bilinear, 2 smooth
  int aa = 0;                       // OpenGL: index into kAaSamples: 0 off, 2x, 4x, 8x
  int lighting = 0;                 // OpenGL: 0 off, 1 lights, 2 lights + sun shadows
  bool fx = false;                  // OpenGL: effects shaders (water, smoke, fire, sparks)
  bool ao = true, bloom = true;     // OpenGL with lighting: ambient occlusion, bloom
  int postShader = 0;               // OpenGL: 0 none, 1 crt, 2 smooth, 3 sharpen, 4 fxaa
  std::string gfxLaunch;            // graphicsSignature() of what the running game started with (not saved)
  static constexpr int kResolutionCount = 5;
  static const char* resolutionName(int i);          // "960 x 540"
  static void resolutionSize(int i, int* w, int* h);
  static int aaSamples(int i) { return i <= 0 ? 0 : i == 1 ? 2 : i == 2 ? 4 : 8; }
  static const char* distanceName(int i) { return i <= 0 ? "Short" : i == 1 ? "Medium" : i == 2 ? "Long" : "Maximum"; }   // the `detail` field: draw distance of scenery, lights and shadows
  static float distanceScale(int i) { return i <= 0 ? 0.6f : i == 1 ? 1.0f : i == 2 ? 2.0f : 3.5f; }
  static const char* rendererName(int i) { return i == 1 ? "OpenGL" : "Software"; }
  static const char* filterName(int i) { return i == 1 ? "Bilinear" : i == 2 ? "Smooth" : "Nearest"; }
  static const char* aaName(int i) { return i == 1 ? "2x" : i == 2 ? "4x" : i == 3 ? "8x" : "Off"; }
  static const char* lightingName(int i) { return i == 1 ? "Lights" : i == 2 ? "Lights and shadows" : "Off"; }
  static const char* postName(int i) { return i == 1 ? "CRT" : i == 2 ? "Smooth" : i == 3 ? "Sharpen" : i == 4 ? "FXAA" : "Off"; }
  static const char* postShaderId(int i) { return i == 1 ? "crt" : i == 2 ? "smooth" : i == 3 ? "sharpen" : i == 4 ? "fxaa" : ""; }
  std::string graphicsSignature() const;               // the settings that need a restart (everything but fullscreen)
  bool graphicsChanged() const { return !gfxLaunch.empty() && graphicsSignature() != gfxLaunch; }
  KeyMap keys;                      // the race controls (Controls page)
};

class PauseMenu {
 public:
  enum class Action { None, Resume, QuitRace, ExitGame, ResetPlayer };
  enum class Key { Up, Down, Left, Right, Select, Back };
  void load(const GameData& data);
  void open() { page_ = Page::Main; sel_ = 0; open_ = true; }
  bool isOpen() const { return open_; }
  // Returns what the front end must do; edits `s` for the configuration pages (the caller applies the changes).
  Action key(Key k, GameSettings* s);
  void draw(const HudCanvas& c, const HudAssets& a, const GameSettings& s) const;
  // Mouse: the row under a point of the 320x200 screen (-1 = none); hovering selects it.
  int itemAt(int x, int y, const GameSettings& s) const;
  void setSel(int i) { sel_ = i; }
  int sel() const { return sel_; }

 private:
  enum class Page { Main, Config, Sound, General, Difficulty, Detail, Effects, Controls };
  std::vector<std::string> items(const GameSettings& s) const;
  std::string title() const;
  std::string optMain_[4] = {"Continue Race", "Configuration", "Quit Race", "Exit game"};
  std::string optReset_ = "Reset Player";  // a port addition (not in PAUSED.ST0)
  std::string optConfig_[6] = {"General", "Controls", "Detail", "Continue", "Difficulty", "Sound"};
  std::string titleConfig_ = "Configuration";
  Page page_ = Page::Main;
  int sel_ = 0;
  bool open_ = false;
};

}  // namespace slip
