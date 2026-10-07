// Application logic of the viewer/driver demo. SDL-free so it can also render headlessly.
#pragma once
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "game/scene.hpp"
#include "game/ship_params.hpp"
#include "game/ship_sim.hpp"
#include "input/input_state.hpp"
#include "original_formats/game_data.hpp"
#include "renderer/software_renderer.hpp"

namespace slip {

enum class AppMode { Track, Model, Sprite };

struct AppOptions {
  std::string dataDir;
  AppMode mode = AppMode::Track;
  int track = 1;
  std::string shape;
  std::string sprite;
  int width = 960, height = 540;
  bool drive = false;
  int ship = 0;
  bool haveCam = false;
  double cam[3] = {0, 0, 0};
  float camYaw = 0, camPitch = 0;
  float shipScale = 2.0f;
};

class ViewerApp {
 public:
  bool init(const AppOptions& opt, std::string* error);
  void update(double dt, const InputState& in);
  void render();
  const SoftwareRenderer& renderer() const { return renderer_; }
  std::vector<std::string> hudLines() const;

  // commands from the platform layer
  void nextItem(int delta);       // next/previous track, model or sprite
  void setMode(AppMode m);
  void toggleDrive();
  void selectShip(int i);
  void toggleVisibility() { useVisMask_ = !useVisMask_; }
  void toggleCulling() { renderer_.cullBackfaces = !renderer_.cullBackfaces; }
  AppMode mode() const { return mode_; }
  bool driving() const { return driving_; }

 private:
  bool loadTrack(int idx, std::string* err);
  void placeCameraAtStart();
  void loadModel(int idx);
  void loadSprite(int idx);
  void drawSprite();

  AppOptions opt_;
  std::unique_ptr<GameData> data_;
  std::unique_ptr<Scene> scene_;
  std::array<ShipParams, 10> params_{};
  SoftwareRenderer renderer_;
  Camera cam_;
  AppMode mode_ = AppMode::Track;
  int track_ = 1;
  bool driving_ = false;
  ShipState player_;
  ShipSimConfig simCfg_;
  double simAccum_ = 0;
  ShipInput lastDriveInput_;

  std::vector<std::string> shapes_, sprites_;
  int shapeIdx_ = 0, spriteIdx_ = 0;
  std::string status_;
  bool useVisMask_ = !std::getenv("SLIP_NOVIS");  // original per-record visibility classes (F5 toggles)
  double orbitDist_ = 10000, orbitYaw_ = 0.6, orbitPitch_ = 0.25;
  double modelCenter_[3] = {0, 0, 0};
  Sprite sprite_;
  Palette spritePal_;
  double fpsAvg_ = 0;
  std::array<ShipState, 10> grid_;  // static start-grid ships
};

}  // namespace slip
