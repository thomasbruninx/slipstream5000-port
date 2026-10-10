#include "game/settings_file.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "original_formats/user_dir.hpp"

namespace slip {

std::string configPath() {
  return userDataDir() + "/config.txt";
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
    else if (key == "renderer") s.renderer = std::clamp(i, 0, 1);
    else if (key == "resolution") s.resolution = std::clamp(i, 0, GameSettings::kResolutionCount - 1);
    else if (key == "fullscreen") s.fullscreen = i != 0;
    else if (key == "filter") s.filter = std::clamp(i, 0, 2);
    else if (key == "aa") s.aa = std::clamp(i, 0, 3);
    else if (key == "lighting") s.lighting = std::clamp(i, 0, 2);
    else if (key == "fx") s.fx = i != 0;
    else if (key == "ao") s.ao = i != 0;
    else if (key == "bloom") s.bloom = i != 0;
    else if (key == "postShader") s.postShader = std::clamp(i, 0, 4);
    else if (key == "progress") c.progress = std::clamp(i, 1, 10);
    else if (key == "reverseAccel") s.keys.reverseAccel = i != 0;
    else if (key == "padInvertPitch") s.keys.padInvertPitch = i != 0;
    else if (key.compare(0, 3, "pad") == 0 && key.size() == 4 && key[3] >= '0' && key[3] < char('0' + kPadActions)) s.keys.pad[size_t(key[3] - '0')] = std::clamp(i, 0, 2000);
    else if (key.compare(0, 3, "key") == 0 && key.size() == 4 && key[3] >= '0' && key[3] < char('0' + kKeyActions)) s.keys.sc[size_t(key[3] - '0')] = std::clamp(i, 0, 511);
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
    << "\nmusicOn " << int(s.musicOn) << "\nengine " << s.engine << "\nshading " << s.shading << "\nprogress " << c.progress << "\nreverseAccel " << int(s.keys.reverseAccel) << "\n";
  f << "renderer " << s.renderer << "\nresolution " << s.resolution << "\nfullscreen " << int(s.fullscreen) << "\nfilter " << s.filter << "\naa " << s.aa << "\nlighting " << s.lighting
    << "\nfx " << int(s.fx) << "\nao " << int(s.ao) << "\nbloom " << int(s.bloom) << "\npostShader " << s.postShader << "\n";
  f << "padInvertPitch " << int(s.keys.padInvertPitch) << "\n";
  for (int k = 0; k < kPadActions; ++k) f << "pad" << k << " " << s.keys.pad[size_t(k)] << "\n";
  for (int k = 0; k < kKeyActions; ++k) f << "key" << k << " " << s.keys.sc[size_t(k)] << "\n";
  return bool(f);
}

}  // namespace slip
