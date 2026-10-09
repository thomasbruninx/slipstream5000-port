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
  // The options of the original's configuration screens (CFG words 0x492E2..0x492FE, frontend_config.cpp). The ones the port cannot show (weapons monitor,
  // clouds, shading, textures, window size) are stored only; the rear monitor works (viewer_app.cpp drawRearMonitor).
  bool rearMonitor = true, weaponsMonitor = true, clouds = true, texturesCoarse = false, windowReduced = false, shadows = true;
  bool damage = true;               // [0x492EE] "Damage": off = the human's craft takes no damage (RaceSlotDamage 0x52035)
  bool sfxOn = true, speech = true, musicOn = true;
  int engine = 2;                   // engine sounds: 0 off, 1 quiet, 2 normal (CFG word 0x492E2)
  int shading = 2;                  // 0 none, 1 Gouraud, 2 specular
  KeyMap keys;                      // the race controls (Controls page)
};

class PauseMenu {
 public:
  enum class Action { None, Resume, QuitRace, ExitGame };
  enum class Key { Up, Down, Left, Right, Select, Back };
  void load(const GameData& data);
  void open() { page_ = Page::Main; sel_ = 0; open_ = true; }
  bool isOpen() const { return open_; }
  // Returns what the front end must do; edits `s` for the configuration pages (the caller applies the changes).
  Action key(Key k, GameSettings* s);
  void draw(const HudCanvas& c, const HudAssets& a, const GameSettings& s) const;

 private:
  enum class Page { Main, Config, Sound, General, Difficulty, Detail, Controls };
  std::vector<std::string> items(const GameSettings& s) const;
  std::string title() const;
  std::string optMain_[4] = {"Continue Race", "Configuration", "Quit Race", "Exit game"};
  std::string optConfig_[6] = {"General", "Controls", "Detail", "Continue", "Difficulty", "Sound"};
  std::string titleConfig_ = "Configuration";
  Page page_ = Page::Main;
  int sel_ = 0;
  bool open_ = false;
};

}  // namespace slip
