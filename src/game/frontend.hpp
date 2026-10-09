// The original's front end: logo, intro movie, main menu, track choice, vehicle choice ("parking lot"), pilot information and the garage.
// All art, texts and hit maps come from the user's game files (MAINMENU / TRKBT / CH_TEAM / VIEWCAR / GARAGE sprites, *.ST0 string tables,
// *.ZON hit maps, INTRO.GDV / LOGO_S.GDV); the layout of the buttons is read from the sprite headers (position words). See docs/frontend.md.
// SDL-free: the screen is a 320x200 ARGB buffer that the application scales to the window.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_system.hpp"
#include "game/championship.hpp"
#include "game/globe.hpp"
#include "game/pause_menu.hpp"
#include "game/weapons.hpp"
#include "original_formats/ann.hpp"
#include "original_formats/formats.hpp"
#include "original_formats/game_data.hpp"
#include "original_formats/gdv.hpp"

namespace slip {

struct RaceResult {  // handed to the results screen when a race ends
  int track = 1, ship = 0;
  int place[10] = {};        // finishing place (1..10) of every ship
  double time[10] = {};      // finishing time (seconds), projected for ships that were still racing
  bool projected[10] = {};
  double bestLap = 0;        // the player's fastest lap (0 = none)
  Loadout remaining;         // what the player's craft still carries (weapons, rounds left)
  bool haveRemaining = false;
  bool noVoice = false;      // the results are shown again after a replay: no new line
};

struct RaceSetup {  // what the front end hands to the race
  int track = 1;      // 1..10
  int ship = 0;       // 0..9
  int laps = 3;
  Loadout loadout;
  std::array<int, 10> grid = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};  // start slot (0 = pole) of every ship
  bool championship = false;
};

class FrontEnd {
 public:
  enum class Key { Up, Down, Left, Right, Select, Back };
  enum class Screen { Logo, Intro, Gremlin, Credits, Main, OnePlayer, Multi, Tracks, Team, ViewCar, Info, Garage, Best, Results, ChampPos, FinalPos, Slots, Reporters, Config, Notice };

  bool init(const GameData& data, AudioSystem* audio);
  void start(bool skipMovies);
  void update(double dt);
  void draw();  // renders the current screen into pixels()
  void key(Key k);
  void mouseMove(int x, int y);  // virtual 320x200 coordinates, (-1,-1) = outside
  void click(int x, int y);
  const uint32_t* pixels() const { return buf_.data(); }
  bool wantsQuit() const { return quit_; }
  bool takeRace(RaceSetup* out) { if (!race_) return false; race_ = false; *out = setup_; return true; }
  void showResults(const RaceResult& r);
  // text entry (saved game names): the application forwards typed characters while wantsText()
  bool wantsText() const { return (screen_ == Screen::Slots && slotEntry_) || (screen_ == Screen::Best && bestEdit_ >= 0); }
  void textInput(const std::string& t);
  void backspace();
  bool championshipActive() const { return champ_.active(); }
  void debugChampLastRace() { while (champ_.active() && !champ_.lastRace()) champ_.nextRace(); }  // test hook (--front-keys L)
  // TV fly-through of the championship (PlayTrackIntro 0x57A79): when the reporters have introduced the track the application flies over it and calls introFinished().
  void setProgress(int p, bool unlockAll) { progress_ = std::clamp(p, 1, 10); unlockAll_ = unlockAll; }  // tracks open for single races
  int progress() const { return progress_; }
  bool takeProgressChanged() { const bool r = progressChanged_; progressChanged_ = false; return r; }
  void setKeyNamer(std::function<std::string(int)> f) { keyName_ = std::move(f); }  // scancode -> text for the key page
  bool wantsKey() const { return screen_ == Screen::Config && keyWait_; }
  void rawKey(int scancode);
  void setPadNamer(std::function<std::string(int)> f) { padName_ = std::move(f); }  // controller code -> text
  bool wantsPad() const { return screen_ == Screen::Config && padWait_; }
  void rawPad(int code);                                                              // the next controller button (1000 + axis for a trigger) while wantsPad()                                                         // the next key while wantsKey()
  void setSettings(GameSettings* s) { cfg_ = s; }  // the options the configuration screens edit
  bool takeConfigChanged() { const bool r = cfgChanged_; cfgChanged_ = false; return r; }
  bool takeReplay() { const bool r = replay_; replay_ = false; return r; }  // Replay was chosen on the results screen
  int takeIntroRequest() { if (!introWanted_) return 0; introWanted_ = false; return setup_.track; }
  void introFinished();
  void setFlyThrough(bool on) { flyThrough_ = on; }  // the application can show the TV fly-through
  int takeNetRequest() { const int r = net_; net_ = 0; return r; }  // "Multiplayer" submenu: 1 host, 2 browse LAN games, 3 join by address (the application opens the matching network screen)
  // Pilot information screen (DoViewCar 0x46A94): the 3D craft turning in the middle of the card. The application renders it: ship number, turn angle
  // (radians) and the virtual 320x200 rectangle it may draw into. Returns -1 on every other screen.
  int previewShip(double* angle, int rect[4]) const;
  // The 3D craft of the current screen: the pilot card's turning craft or the three record holders of the best laps screen.
  struct Preview { int ship = 0; double angle = 0; int rect[4] = {0, 0, 0, 0}; double fit = 0.80; const Palette* pal = nullptr; };  // pal: palette of the craft (null = the screen's)
  std::vector<Preview> previews();
  const Palette& palette() const { return pal_; }  // palette of the screen just drawn
  bool ready() const { return ready_; }
  Screen screen() const { return screen_; }
  void setDefaults(int track, int laps, const Loadout& l) { setup_.track = track; setup_.laps = laps; setup_.loadout = l; }
  // shop: weapon / booster prices (by difficulty level) and the money the player starts a race with
  void setEconomy(const WeaponTable* t, int difficulty, int credits) { table_ = t; difficulty_ = difficulty; startCredits_ = credits; }

 private:
  struct Btn { int x = 0, y = 0, w = 0, h = 0; std::string normal, hover, label; int id = 0; bool enabled = true; };
  const Sprite* spr(const std::string& name);
  void usePalette(const Palette& p);
  bool uiLoaded_ = false, uiOk_ = false;
  uint32_t ui_[8] = {};
  const Font* font(const std::string& name);
  std::string str(const std::string& file, const std::string& tag) const;
  void go(Screen s);
  void playSelect();  // SELECT.SMP (0x4AE8B)
  void playVoice(int ship);  // the pilot's greeting (table 0x45842)
  void buildButtons();
  void activate(int id);
  void skipIntro();
  void drawButtons(const Palette* pal);
  int brightIndex(bool brightest) const;
  void drawText(const Font& f, const std::string& s, int x, int y, int idx);
  int hit(int x, int y) const;
  int zone(const std::string& zon, int x, int y);
  void drawLogo();
  void drawIntro();
  void drawGremlin();
  void drawCredits();
  void drawMain();
  void drawTracks();
  int selectedTrack() const;  // 0-based track under the selection of the track list
  void turnGlobe(int track0, double dt);
  void drawGlobe(int track0, int cx, int cy);
  void drawTeam();
  void drawViewCar();
  void drawInfo();
  void greyscale();
  void greyRamp();
  void lighten(int x0, int y0, int x1, int y1, int percent);
  void wrapTextCentered(const Font& f, const std::string& s, int cx, int y, int w, int idx, int lineGap);
  int cardSel_ = 0;
  void drawGarage();
  void drawBest();
  void drawNotice();
  void fade(double a);
  void startMovie(const std::string& file);
  void wrapText(const Font& f, const std::string& s, int x, int y, int w, int idx, int lineGap);

  const GameData* data_ = nullptr;
  AudioSystem* audio_ = nullptr;
  std::map<std::string, std::unique_ptr<Sprite>> sprites_;
  std::map<std::string, std::unique_ptr<Font>> fonts_;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> tables_;
  std::map<std::string, std::vector<uint8_t>> zones_;
  std::vector<uint32_t> buf_ = std::vector<uint32_t>(320 * 200, 0xff000000u);
  Palette pal_;                  // palette of the current screen (the backdrop's embedded palette)
  int net_ = 0;
  bool ready_ = false, quit_ = false, race_ = false, skipMovies_ = false;
  Screen screen_ = Screen::Logo, back_ = Screen::Main;
  double t_ = 0;                 // seconds in the current screen
  std::vector<Btn> btns_;
  int sel_ = 0, mx_ = -1, my_ = -1;
  // movie
  GdvDecoder gdv_;
  std::vector<uint8_t> gdvIdx_;
  double movieClock_ = 0;
  int movieVoice_ = 0;
  bool movieOpen_ = false;
  Globe globe_;
  double trackDt_ = 0.016;
  double gYaw_ = 0, gTilt_ = 0;  // track choice: the globe turns to the selected track
  std::vector<std::string> credits_;
  size_t creditIdx_ = 0;
  double creditT_ = 0;
  int creditVoice_ = 0, voice_ = 0;
  std::shared_ptr<SoundSample> selectSnd_;
  // selections
  RaceSetup setup_;
  int viewShip_ = 0, hoverShip_ = -1, mode_ = 1;  // mode: 0 practice, 1 single race, 2 championship
  std::string notice_;
  // garage: page 0 = the four buttons (weapons, turbo, systems, start race), 1 = load / left pod / right pod / ok, 2 = weapon grid, 3 = turbo grid, 4 = systems
  const WeaponTable* table_ = nullptr;
  int difficulty_ = 1, startCredits_ = 750, garPage_ = 0, garHov_ = -1, pod_ = 0, cash_ = 0;
  double garOpen_ = 0;
  int podW_[2] = {-1, -1}, podAmmo_[2] = {0, 0}, booster_ = 0;
  bool fastRecharge_ = false, wideLock_ = false, loader_ = false;  // the three upgrades: charger, targetter, loader
  void garageEnter();
  void garageKey(Key k);
  void garageClick(int x, int y);
  void garageGo(int page, bool fromMouse);
  void garageChoose(int i, bool fromMouse);
  void garageBuildLoadout();
  int garageZoneAt(int x, int y) const;
  int weaponPrice(int id) const;
  int boosterPrice(int i) const;
  void drawGaragePanel(const Sprite& panel);
  const Sprite* cropIcon(int weapon);
  std::map<int, Sprite> icons_;
  // championship (game/championship, frontend_champ.cpp)
  Championship champ_;
  void champBeginRace();     // the next race of the calendar: track, then the garage
  void champRaceFinished();  // results -> points and prize money -> positions / final positions
  void champStart();         // the pilot card was accepted in the championship
  // configuration screens (frontend_config.cpp): the options live in the application's GameSettings (setSettings), the screens edit them directly
  enum class CfgPage { Main, General, Controls, Keys, Pad, Detail, Difficulty, Sound };
  struct CfgRow;
  GameSettings* cfg_ = nullptr;
  GameSettings cfgLocal_;
  CfgPage cfgPage_ = CfgPage::Main;
  int cfgHov_ = -1;
  bool cfgChanged_ = false;
  bool keyWait_ = false;       // a key row waits for the next key press
  Palette previewPal_[3];       // palettes of the craft shown in the Best screen
  bool padWait_ = false;       // a controller row waits for the next button
  std::function<std::string(int)> padName_;
  int keyKeep_ = -1;           // the action being bound
  std::function<std::string(int)> keyName_;
  std::vector<CfgRow> cfgRows();
  const char* cfgFile() const;
  void openConfig();
  int cfgZoneAt(int x, int y);
  void cfgActivate(int i, int dir);
  void cfgKey(Key k);
  void drawConfig();
  void frameButton(int x1, int y1, int x2, int y2, const Sprite* src, const std::string& label);
  // the reporters' scenes (sub_573B7): globe, talking face, subtitles; driven by the .ANN scripts (frontend_reporters.cpp)
  void startReporters(int phase);   // phase 0: before the TV fly-through (<X>INT.ANN), 1: after it (<X>IN1.ANN)
  void updateReporters(double dt);
  void drawReporters();
  void reportersEnd();
  void skipReporters();
  std::string text(const std::string& file, const std::string& tag);  // string table entry, loading the table on first use
  struct Reporters {
    AnnScript script;
    size_t pc = 0;
    double wait = 0, clock = 0, voiceEnd = -1;
    int voice = 0, phase = 0, track = 0;
    std::string tag, table;
    bool female = false, ready = false;
    std::vector<uint8_t> face;    // the running face program
    size_t facePc = 0;
    double faceClock = 0;
    bool faceOn = false;
    int frame[4] = {0, 0, 0, 0};  // mouth, eyes, lids, brows
  } rep_;
  bool introWanted_ = false, flyThrough_ = false, replay_ = false;
  int progress_ = 1;
  bool unlockAll_ = false, progressChanged_ = false;
  // saved games (RES_GAME screen, 0x53536 / 0x53318): six slots in ~/Library/Application Support/Slipstream/slot<N>.sav
  void openSlots(bool save);
  int slotZoneAt(int x, int y);
  void slotsKey(Key k);
  void slotsChoose(int slot);
  void slotsFinish();
  void drawSlots();
  void refreshSlots();
  static std::string slotPath(int slot);
  bool slotSave_ = false, slotEntry_ = false;
  int slotHover_ = -1, slotChosen_ = -1;
  double slotAnim_ = 0;
  std::string slotName_[6], slotText_;
  Screen slotBack_ = Screen::Main;
  // results and best lap records
  RaceResult result_;
  // Best laps (0x422EC / 0x42389 / 0x4208E): the three fastest laps of every track with the driver's craft and name; the defaults come from SLIPSTRM.CFG
  struct Record { int ship = 0; int ms = 0; std::string name; };
  std::vector<Record> records_[11];  // per track 1..10, fastest first (at most 3)
  int bestTrack_ = 1, bestEdit_ = -1, bestHov_ = -1;
  std::string bestAfterNote_;
  bool bestAfterChamp_ = false;      // after the name entry: the championship goes on with the points
  void loadRecords();
  void saveRecords() const;
  int insertRecord(int track, int ship, int ms);  // position 0..2 or -1 when the lap is not in the first three
  void finishRecordEntry();
  int bestZoneAt(int x, int y) const;
  void bestActivate(int zone);
  void drawRanking();
  int resultZoneAt(int x, int y) const;
  void resultChoose(int i);
  const std::string& pilotName(int ship);
  int resSel_ = 1;  // 0 Replay, 1 Continue
  std::vector<std::string> names_;
  const Palette* facePalette();
  static std::string timeText(double seconds);
};

}  // namespace slip
