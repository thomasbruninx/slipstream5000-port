// The original's front end: logo, intro movie, main menu, track choice, vehicle choice ("parking lot"), pilot information and the garage.
// All art, texts and hit maps come from the user's game files (MAINMENU / TRKBT / CH_TEAM / VIEWCAR / GARAGE sprites, *.ST0 string tables,
// *.ZON hit maps, INTRO.GDV / LOGO_S.GDV); the layout of the buttons is read from the sprite headers (position words). See docs/frontend.md.
// SDL-free: the screen is a 320x200 ARGB buffer that the application scales to the window.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_system.hpp"
#include "game/globe.hpp"
#include "game/weapons.hpp"
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
};

struct RaceSetup {  // what the front end hands to the race
  int track = 1;      // 1..10
  int ship = 0;       // 0..9
  int laps = 3;
  Loadout loadout;
};

class FrontEnd {
 public:
  enum class Key { Up, Down, Left, Right, Select, Back };
  enum class Screen { Logo, Intro, Gremlin, Credits, Main, OnePlayer, Multi, Tracks, Team, ViewCar, Info, Garage, Best, Results, Notice };

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
  int takeNetRequest() { const int r = net_; net_ = 0; return r; }  // "Multiplayer" submenu: 1 host, 2 browse LAN games, 3 join by address (the application opens the matching network screen)
  // Pilot information screen (DoViewCar 0x46A94): the 3D craft turning in the middle of the card. The application renders it: ship number, turn angle
  // (radians) and the virtual 320x200 rectangle it may draw into. Returns -1 on every other screen.
  int previewShip(double* angle, int rect[4]) const;
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
  // results and best lap records
  RaceResult result_;
  struct Record { int ship = 0; int ms = 0; };
  std::vector<Record> records_[11];  // per track 1..10, fastest first (at most 5)
  int bestTrack_ = 1;
  void loadRecords();
  void saveRecords() const;
  void addRecord(int track, int ship, double seconds);
  void drawResults();
  int resultZoneAt(int x, int y) const;
  void resultChoose(int i);
  const std::string& pilotName(int ship);
  int resSel_ = 1;  // 0 Replay, 1 Continue
  std::vector<std::string> names_;
  const Palette* facePalette();
  static std::string timeText(double seconds);
};

}  // namespace slip
