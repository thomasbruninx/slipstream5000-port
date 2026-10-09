// The port's configuration file (the original keeps the same options in SLIPSTRM.CFG): ~/Library/Application Support/Slipstream/config.txt, "key value" lines.
#pragma once
#include <string>

#include "game/pause_menu.hpp"

namespace slip {

struct SavedConfig {
  GameSettings settings;
  int progress = 1;  // tracks unlocked for single races / practice (CFG word 0x493E6): the first `progress` tracks of the calendar order
  bool present = false;
};

std::string configPath();
SavedConfig loadConfig();
bool saveConfig(const SavedConfig& c);

}  // namespace slip
