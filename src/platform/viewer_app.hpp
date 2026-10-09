// Application logic of the viewer/driver demo. SDL-free so it can also render headlessly.
#pragma once
#include <cstdlib>
#include <functional>
#include <memory>
#include <array>
#include <string>
#include <vector>

#include "game/scene.hpp"
#include "game/ship_params.hpp"
#include "audio/audio_system.hpp"
#include "game/doors.hpp"
#include "game/drones.hpp"
#include "original_formats/ann.hpp"
#include "game/ship_ai.hpp"
#include "game/ship_sim.hpp"
#include "game/weapons.hpp"
#include "game/frontend.hpp"
#include "game/hud.hpp"
#include "game/netplay.hpp"
#include "game/pause_menu.hpp"
#include "input/input_state.hpp"
#include "net/discovery.hpp"
#include "net/session.hpp"
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
  bool unlockAll = false;   // --unlock-all: every track can be chosen in single races from the start (the original unlocks them one by one)
  bool startBonus = false;  // the original's 15 s start-phase speed bonus by rank (0x50252, up to +75 %); off by default, it makes the leader far too fast
  float shipScale = 1.0f;  // --ship-scale: 1 = the original size
  bool countdown = true;   // 5 s start sequence with announcer and held ships
  int laps = 3;            // race length for the finish (result music, HUD)
  AudioConfig audio;       // --soundfont, --no-audio, --music-volume ...
  std::string music;       // --music NAME.HMP (default: one of the race songs, as the original picks at random)
  bool noMusic = false;
  std::string weapons = "default";  // --weapons SPEC: the player's loadout ("default" = the original's cheat loadout, "none" = blaster only)
  bool pickups = true;              // --no-pickups
  bool aiWeapons = true;            // --no-ai-weapons
  int difficulty = -1;              // --difficulty 0..2 (default: SLIPSTRM.CFG, normally 1)
  bool voices = true;               // --no-voices: pilot / announcer lines
  int view = 0;                     // 0 cockpit, 1 cockpit with the own ship drawn (--ship-view), 2 chase (--chase); V cycles
  // multiplayer (docs/multiplayer.md)
  std::string netRole;              // "host" (--host) or "join" (--join); empty: single player
  std::string netHost = "127.0.0.1";  // --join HOST[:PORT]
  int netPort = 51500;              // --port (host: TCP listen port; join: the host's port)
  std::string netName;              // --name (default: the user name)
  int netPlayers = 0;               // --players N: the host starts the race as soon as N humans are in the lobby (0 = start from the menu)
  bool front = false;               // start in the front end (logo, intro, main menu ...) instead of the track viewer
  bool skipIntro = false;           // --skip-intro: straight to the main menu
  bool netHeadless = false;         // --net-run: no window; joiners ready up by themselves, the process ends after the race
};

class ViewerApp {
 public:
  bool init(const AppOptions& opt, std::string* error);
  void update(double dt, const InputState& in);
  void render();  // 3D view, HUD, and the multiplayer menu on top
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
  void toggleCamera() { view_ = (view_ + 1) % 4; }
  // F1 cockpit, F2 chase (pressed again: the far chase), F3 rear, F4 TV camera, F5 free camera (docs/controls in docs/frontend.md)
  void setView(int v) { view_ = std::clamp(v, 0, 5); }
  int view() const { return view_; }
  const KeyMap& keymap() const { return settings_.keys; }
  // pause menu (Esc while driving): the race stands still, the menu is the original's PAUSED / CONFIG entries
  // Esc / Enter after the player has finished (the craft keeps flying on autopilot) or the race is over: the results screen. Returns true when it opened.
  bool tryShowResults();
  bool introActive() const { return introMode_; }
  bool replayActive() const { return replayMode_; }
  void frontDebugEndRace() { if (driving_ && front_ && !frontActive_) showResultsScreen(); }
  void stopReplay() { endReplay(); }
  void endFlyThrough(bool skipped);  // Esc / Enter / click, or the script ended: back to the front end
  void frontDebugResults();
  void frontDebugLast() { if (front_) front_->debugChampLastRace(); }
  bool pausable() const { return mode_ == AppMode::Track && driving_; }
  bool paused() const { return pause_.isOpen(); }
  void openPause();
  void menuKey(PauseMenu::Key k);
  bool wantsQuit() const { return quit_; }
  // multiplayer: lobby UI (Multiplayer entry, Host / Join / browse / lobby screens) and the race session
  bool netActive() const { return session_ != nullptr; }
  bool netRacing() const { return netplay_ != nullptr; }
  bool netUiOpen() const { return netUi_ != NetUi::None; }
  bool frontWantsKey() const { return front_ && frontActive_ && front_->wantsKey(); }
  void frontRawKey(int sc) { if (front_) front_->rawKey(sc); }
  void setKeyNamer(std::function<std::string(int)> f) { if (front_) front_->setKeyNamer(std::move(f)); }
  bool frontWantsPad() const { return front_ && frontActive_ && front_->wantsPad(); }
  void frontRawPad(int code) { if (front_) front_->rawPad(code); }
  void setPadNamer(std::function<std::string(int)> f) { if (front_) front_->setPadNamer(std::move(f)); }
  bool frontWantsText() const { return front_ && frontActive_ && front_->wantsText(); }
  void frontText(const std::string& t) { if (front_) front_->textInput(t); }
  void frontBackspace() { if (front_) front_->backspace(); }
  bool netWantsText() const { return netUi_ == NetUi::Address; }
  void openNetMenu();
  void netKey(PauseMenu::Key k);
  void netText(const std::string& utf8);
  void netBackspace();
  std::string netSummary() const;  // one line per ship: owner, laps, finish (used by the headless test driver)
  void toggleHud() { hudOn_ = !hudOn_; }
  // front end (menus): keys / mouse are routed here while it is active
  bool frontActive() const { return frontActive_; }
  void frontKey(PauseMenu::Key k);
  void frontMouse(double nx, double ny, bool click);  // window position 0..1
  FrontEnd* frontEnd() { return front_.get(); }
  void toggleMap() { settings_.trackMap = !settings_.trackMap; }
  bool hudActive() const { return hudOn_ && driving_ && !introMode_ && mode_ == AppMode::Track && hudAssets_.loaded; }
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
  std::unique_ptr<FrontEnd> front_;
  bool frontActive_ = false;
  void startRaceFromFront(const RaceSetup& s, bool replay = false);
  void toggleDriveIfRacing() { if (driving_) toggleDrive(); }
  RaceResult lastResult_;
  void returnToFront();
  void showResultsScreen(bool again = false);
  void placeGrid();
  // Replay (RunRaceReplay 0x5A80C / ReplayRecordStart 0x5BCDC): the original re-runs the race with the recorded controls and the same random seed. The port records the
  // controls of every 1/120 s simulation step and the combat seed, restarts the race with the same setup and feeds the recording back; Esc / the end of the recording leave it.
  struct StepRec { float throttle = 0, brake = 0, steer = 0, pitch = 0; bool fire = false, cycle = false, held = false; };
  std::vector<StepRec> replayRec_;
  size_t replayPos_ = 0;
  bool replayMode_ = false, replayEnded_ = false;
  unsigned raceSeed_ = 1;   // the combat seed of the race that is recorded / replayed
  RaceSetup lastSetup_;
  bool haveSetup_ = false;
  void startReplay();
  void endReplay();
  // TV fly-through before a championship race (PlayTrackIntro 0x57A79, viewer_intro.cpp): one ship flies the lap on the autopilot, the TV camera cuts between the
  // track's 60 camera positions and the commentators speak (the track's .ANN script, subtitles from the *PREV string table).
  bool introMode_ = false;
  AnnScript introScript_;
  size_t introPc_ = 0;
  double introWait_ = 0, introClock_ = 0, introVoiceEnd_ = -1;
  int introVoice_ = 0;
  std::string introTag_;
  std::vector<std::pair<std::string, std::string>> introText_;
  std::vector<std::array<int32_t, 3>> tvCams_;
  int tvCam_ = -1;
  double tvZoom_ = 1.0;
  double freeAz_ = 0.0, freeEl_ = 0.35, freeDist_ = 90000.0;  // the free camera (F5): orbit around the ship
  DroneWorld drones_;  // the little craft that fly ahead of the player (game/drones)
  void startFlyThrough(int track);
  void updateFlyThrough(double dt);
  void updateTvCamera();
  bool tvInPiece(const double p[3]) const;
  bool tvVisible(const std::array<int32_t, 3>& cam, const double ship[3]) const;
  void drawIntroOverlay();  // ships on the start grid by gridSlot_
  std::array<int, 10> gridSlot_ = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};  // start slot (0 = pole) of every ship
  double resultsTimer_ = 0;
  bool netFromFront_ = false;
  void renderFront();
  std::unique_ptr<Scene> previewScene_;
  int previewShip_ = -1;
  void renderShipPreview(int ship, double angle, const int rect[4], int dx, int dy, int rw, int rh, double fit = 0.80);
  std::array<std::unique_ptr<Scene>, 10> previewCache_;
  // multiplayer state
  enum class NetUi { None, Main, Browse, Address, Lobby };
  std::unique_ptr<net::Session> session_;
  std::unique_ptr<Netplay> netplay_;
  std::unique_ptr<net::IDiscovery> browse_;
  NetUi netUi_ = NetUi::None;
  int netSel_ = 0;
  std::string netAddr_, netMsg_;
  uint32_t netDataHash_ = 0, netStartLocal_ = 0;
  double netClock_ = 0, netEndTimer_ = 0;
  unsigned netSeed_ = 0;
  bool netAutoReady_ = false;
  void netInit();
  void netHostGame();
  void netJoinGame(const std::string& host, int port);
  void netLeaveSession();
  void updateNet(double dt);
  void startNetRace(const net::Start& st, uint32_t startLocal);
  void netLeaveRace();
  void drawNetUi();
  void drawPlayerList(const HudCanvas& c);
  bool showList_ = false;
  void renderFrame();
  bool shipShown(int i) const { return introMode_ ? i == player_.ship : (!netplay_ || netplay_->present(i)); }
  bool netStartWaiting() const { return netplay_ && int32_t(netStartLocal_ - session_->nowMs()) > 0; }
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
  HudAssets hudAssets_;
  Hud hud_;
  PauseMenu pause_;
  GameSettings settings_;
  bool hudOn_ = true, quit_ = false;
  double finalLapTimer_ = 0, gameOverTimer_ = 0, lapPopupTimer_ = 0, shakeTimer_ = 0, prevDamage_ = 0, lastLapShown_ = -1;
  unsigned shakeRng_ = 1;
  uint16_t lightLfsr_ = 0x5a4a;  // generator of 0x3667B (initial [0x36693]) driving the flickering pit lights
  void updatePieceLights();
  int hudShakeX_ = 0, hudShakeY_ = 0;
  void drawHud();
  void drawMap(const HudCanvas& c);
  void applySettings();
  void applyConfig();  // the configuration screens changed something: apply and save it
  bool voicesOn() const { return opt_.voices && settings_.speech; }
  int progress_ = 1;   // unlocked tracks (config file)
  int view_ = 0;  // 0 cockpit, 1 cockpit + own ship, 2 chase
  bool cockpit() const { return view_ != 2; }
  int lastLap_ = 0;
  RaceStatus raceStatus_;
  double raceClock_ = 0;  // seconds since the lights went green (the tactical AI becomes more aggressive with it)
  double startPhase_ = 15.0;  // [0x54404]: the first 15 s after GO give a rank dependent speed bonus (table 0x50252)
  bool raceOverHandled_ = false;
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
