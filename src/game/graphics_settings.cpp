#include <string>

#include "game/pause_menu.hpp"

namespace slip {

namespace {
constexpr int kRes[GameSettings::kResolutionCount][2] = {{640, 360}, {960, 540}, {1280, 720}, {1600, 900}, {1920, 1080}};
}

const char* GameSettings::resolutionName(int i) {
  static const char* kNames[kResolutionCount] = {"640 x 360", "960 x 540", "1280 x 720", "1600 x 900", "1920 x 1080"};
  return kNames[i < 0 ? 0 : i >= kResolutionCount ? kResolutionCount - 1 : i];
}

void GameSettings::resolutionSize(int i, int* w, int* h) {
  i = i < 0 ? 0 : i >= kResolutionCount ? kResolutionCount - 1 : i;
  *w = kRes[i][0];
  *h = kRes[i][1];
}

std::string GameSettings::graphicsSignature() const {
  return std::to_string(renderer) + "," + std::to_string(resolution) + "," + std::to_string(filter) + "," + std::to_string(aa) + "," + std::to_string(lighting) + "," + std::to_string(int(fx)) + "," +
         std::to_string(int(ao)) + "," + std::to_string(int(bloom)) + "," + std::to_string(postShader);
}

}  // namespace slip
