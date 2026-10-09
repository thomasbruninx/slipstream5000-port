#include "game/settings_file.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace slip {

std::string configPath() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/Library/Application Support/Slipstream/config.txt";
}

SavedConfig loadConfig() {
  SavedConfig c;
  std::ifstream f(configPath());
  if (!f) return c;
  c.present = true;
  GameSettings& s = c.settings;
  std::string key;
  double v;
  while (f >> key >> v) {
    const int i = int(v);
    if (key == "music") s.music = float(std::clamp(v, 0.0, 1.0));
    else if (key == "sfx") s.sfx = float(std::clamp(v, 0.0, 1.0));
    else if (key == "difficulty") s.difficulty = std::clamp(i, 0, 2);
    else if (key == "kph") s.kph = i != 0;
    else if (key == "trackMap") s.trackMap = i != 0;
    else if (key == "detail") s.detail = std::clamp(i, 0, 3);
    else if (key == "rearMonitor") s.rearMonitor = i != 0;
    else if (key == "weaponsMonitor") s.weaponsMonitor = i != 0;
    else if (key == "clouds") s.clouds = i != 0;
    else if (key == "texturesCoarse") s.texturesCoarse = i != 0;
    else if (key == "windowReduced") s.windowReduced = i != 0;
    else if (key == "shadows") s.shadows = i != 0;
    else if (key == "damage") s.damage = i != 0;
    else if (key == "sfxOn") s.sfxOn = i != 0;
    else if (key == "speech") s.speech = i != 0;
    else if (key == "musicOn") s.musicOn = i != 0;
    else if (key == "engine") s.engine = std::clamp(i, 0, 2);
    else if (key == "shading") s.shading = std::clamp(i, 0, 2);
    else if (key == "progress") c.progress = std::clamp(i, 1, 10);
  }
  return c;
}

bool saveConfig(const SavedConfig& c) {
  std::filesystem::create_directories(std::filesystem::path(configPath()).parent_path());
  std::ofstream f(configPath());
  if (!f) return false;
  const GameSettings& s = c.settings;
  f << "music " << s.music << "\nsfx " << s.sfx << "\ndifficulty " << s.difficulty << "\nkph " << int(s.kph) << "\ntrackMap " << int(s.trackMap) << "\ndetail " << s.detail
    << "\nrearMonitor " << int(s.rearMonitor) << "\nweaponsMonitor " << int(s.weaponsMonitor) << "\nclouds " << int(s.clouds) << "\ntexturesCoarse " << int(s.texturesCoarse)
    << "\nwindowReduced " << int(s.windowReduced) << "\nshadows " << int(s.shadows) << "\ndamage " << int(s.damage) << "\nsfxOn " << int(s.sfxOn) << "\nspeech " << int(s.speech)
    << "\nmusicOn " << int(s.musicOn) << "\nengine " << s.engine << "\nshading " << s.shading << "\nprogress " << c.progress << "\n";
  return bool(f);
}

}  // namespace slip
