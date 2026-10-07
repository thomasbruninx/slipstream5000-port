// Application logic of the viewer/driver demo. SDL-free so it can also render headlessly.
#pragma once
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "game/scene.hpp"
#include "game/ship_params.hpp"
#include "audio/audio_system.hpp"
#include "game/doors.hpp"
#include "game/ship_ai.hpp"
#include "game/ship_sim.hpp"
#include "game/weapons.hpp"
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
  bool countdown = true;   // 5 s start sequence with announcer and held ships
  int laps = 3;            // race length for the finish (result music, HUD)
  AudioConfig audio;       // --soundfont, --no-audio, --music-volume ...
  std::string music;       // --music NAME.HMP (default: one of the race songs, as the original picks at random)
  bool noMusic = false;
  std::string weapons = "default";  // --weapons SPEC: the player's loadout ("default" = the original's cheat loadout, "none" = blaster only)
  bool pickups = true;              // --no-pickups
  bool aiWeapons = true;            // --no-ai-weapons
  bool voices = true;               // --no-voices: pilot / announcer lines
  bool cockpit = true;              // first person camera at the ship's 'head' reference point (--chase starts in the chase view)
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
  void toggleAllScenery() { showAllScenery_ = !showAllScenery_; }
  void toggleAI() { aiEnabled_ = !aiEnabled_; }
  void toggleMusic() { audio_.toggleMusic(); }
  void toggleSfx() { audio_.toggleSfx(); }
  AudioSystem& audio() { return audio_; }
  void cycleWeapon() { cyclePending_ = true; }
  void toggleCamera() { cockpit_ = !cockpit_; }
  void toggleAssist() { simCfg_.assist = !simCfg_.assist; }
  void togglePainter() { painter_ = !painter_; }
  void toggleVisibility() { useVisMask_ = !useVisMask_; }
  void toggleCulling() { cullOverride_ = renderer_.cullBackfaces ? 0 : 1; }
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
  Doors doors_;
  AiTables aiTables_;
  std::array<AiState, 10> ai_{};
  RaceInfo race_;
  AudioSystem audio_;
  int engineVoice_ = 0;
  // race flow: 5 s countdown (announcer at 5, ENGSTART at 3, second announcement and engines at 1, then GO), laps, finish
  double countdown_ = 0;
  int countdownStage_ = 0, lapsDone_ = 0, finishRank_ = 0;
  bool finished_ = false;
  int ambientForPlayer() const;
  void playTrackMusic();
  void drainSounds(const double listener[3]);
  // combat: weapons, pickups, voice cues (docs/simulation.md "Weapons and pickups")
  WeaponTable weaponTable_;
  std::array<ShipRefPoints, 10> refPoints_{};
  CombatWorld combat_;
  Loadout playerLoadout_;
  bool cyclePending_ = false;
  bool cockpit_ = true;
  int lastLap_ = 0;
  double resultCueTimer_ = -1;
  std::array<Sprite, 6> bonusSprites_, explSprites_;
  std::array<Sprite, 4> fireSprites_;
  void setupCombat();
  void stepCombat(double step, const InputState& in, bool held);
  void drawCombatOverlay();
  std::string combatLine() const;
  bool aiEnabled_ = std::getenv("SLIP_NOAI") == nullptr;
  double simAccum_ = 0;
  ShipInput lastDriveInput_;

  std::vector<std::string> shapes_, sprites_;
  int shapeIdx_ = 0, spriteIdx_ = 0;
  std::string status_;
  bool showAllScenery_ = std::getenv("SLIP_ALLSCENERY") != nullptr;  // ignore the [0x33EEC] far-scenery cull (F7)
  int cullOverride_ = -1;  // -1 automatic, 0/1 forced by Tab
  double animSeconds_ = std::getenv("SLIP_TIME") ? std::atof(std::getenv("SLIP_TIME")) : 0.0;  // animation clock for the flashing lights
  bool painter_ = !std::getenv("SLIP_NOPAINTER");  // original back-to-front BSP/painter order (F6 toggles)
  void renderTrackPainter(const MeshTransform& xf);
  void buildShadowCasters();
  bool useVisMask_ = !std::getenv("SLIP_NOVIS");  // original per-record visibility classes (F5 toggles)
  double orbitDist_ = 10000, orbitYaw_ = 0.6, orbitPitch_ = 0.25;
  double modelCenter_[3] = {0, 0, 0};
  Sprite sprite_;
  Palette spritePal_;
  double fpsAvg_ = 0;
  std::array<ShipState, 10> grid_;  // static start-grid ships
};

}  // namespace slip
