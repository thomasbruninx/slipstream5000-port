#include "game/frontend.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "game/hud.hpp"
#include "original_formats/track.hpp"

namespace slip {

namespace {
constexpr int W = 320, H = 200;
uint32_t argb(uint32_t rgb) { return 0xff000000u | (rgb & 0xffffffu); }
std::string up(std::string s) { for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }
int transparentOf(const Sprite& s) { return s.hdr8 == 0xFFFF ? -1 : int(s.hdr8 & 0xFF); }
}  // namespace

// Where the ten tracks are on the globe (latitude, longitude); the model's front at yaw 0 looks at longitude 175 degrees east (the texture seam), a yaw
// of `a` radians turns longitude 175 + a to the front (calibrated on the rendered EARTH textures).
// Flag positions of the tracks on the globe: the executable's table 0x54FDC (longitude, latitude in degrees: Chicago -90 43, Hawaii -160 25, Tokyo 152 44, Norway 14 66,
// France 5 54, Arizona -105 39, Amazon -59 1, London 0 59, Egypt 22 33, New York -76 43; sub_5B6D6 adds 0x600 / 65536 of a turn = 3.375 degrees). CONFIRMED.
static const double kTrackLatLon[10][2] = {{43, -90 + 3.375}, {25, -160 + 3.375}, {44, 152 + 3.375}, {66, 14 + 3.375}, {54, 5 + 3.375}, {39, -105 + 3.375}, {1, -59 + 3.375}, {59, 0 + 3.375}, {33, 22 + 3.375}, {43, -76 + 3.375}};

void FrontEnd::usePalette(const Palette& p) {  // every screen's palette gets the executable's static UI colours in 248..255 (VideoSetPalette 0x557C7)
  // Sprite palettes only define a range of the 256 entries (Palette::first / count): the rest of the screen's palette stays as it was, like the VGA registers
  for (int i = p.first; i < p.first + p.count && i < 256; ++i) pal_.rgba[size_t(i)] = p.rgba[size_t(i)];
  if (!uiLoaded_) {
    uiLoaded_ = true;
    if (auto exe = data_->read("SLIPSTRM.EXE")) {
      constexpr size_t o = 0x4D854 + (0x54304 - 0x10000);
      if (exe->size() > o + 28 && (*exe)[o] == 248 && (*exe)[o + 2] == 8)
        for (int i = 0; i < 8; ++i) {
          auto up = [](uint32_t v) { return (v << 2) | (v >> 4); };
          ui_[size_t(i)] = (up((*exe)[o + 4 + size_t(i) * 3]) << 16) | (up((*exe)[o + 5 + size_t(i) * 3]) << 8) | up((*exe)[o + 6 + size_t(i) * 3]);
        }
      uiOk_ = true;
    }
  }
  if (uiOk_) for (int i = 0; i < 8; ++i) pal_.rgba[size_t(248 + i)] = ui_[size_t(i)];
}

const Sprite* FrontEnd::spr(const std::string& name) {
  auto it = sprites_.find(name);
  if (it != sprites_.end()) return it->second.get();
  std::unique_ptr<Sprite> p;
  if (auto b = data_->read(name)) if (auto s = parseSprite(*b)) p = std::make_unique<Sprite>(std::move(*s));
  const Sprite* r = p.get();
  sprites_[name] = std::move(p);
  return r;
}

const Font* FrontEnd::font(const std::string& name) {
  auto it = fonts_.find(name);
  if (it != fonts_.end()) return it->second.get();
  std::unique_ptr<Font> p;
  if (auto b = data_->read(name)) if (auto f = parseFont(*b)) p = std::make_unique<Font>(std::move(*f));
  const Font* r = p.get();
  fonts_[name] = std::move(p);
  return r;
}

std::string FrontEnd::str(const std::string& file, const std::string& tag) const {
  auto it = tables_.find(file);
  if (it == tables_.end()) return "";
  for (const auto& [t, text] : it->second) if (t == tag) return text;
  return "";
}

bool FrontEnd::init(const GameData& data, AudioSystem* audio) {
  data_ = &data;
  audio_ = audio;
  for (const char* n : {"MAINMENU.ST0", "CH_TEAM.ST0", "VIEWCAR.ST0", "GARAGE.ST0", "BESTDRV.ST0", "CHTRACK.ST0", "GENERAL.ST0", "RACERES.ST0", "CHAMPPOS.ST0", "FINALPOS.ST0", "SAVED.ST0", "CONFIG.ST0", "GENERAL.ST0", "DETAIL.ST0", "DIFF.ST0", "SOUND.ST0", "CONTROLS.ST0"})
    if (auto b = data.read(n)) tables_[n] = parseStringTable(*b);
  const Sprite* m = spr("MAINMENU.SPR");
  if (m && m->palette) usePalette(*m->palette);
  ready_ = m != nullptr;
  globe_.load(data);
  loadRecords();
  return ready_;
}

void FrontEnd::start(bool skipMovies) {
  skipMovies_ = skipMovies;
  quit_ = race_ = false;
  if (skipMovies) go(Screen::Main);
  else go(Screen::Logo);
}

void FrontEnd::playSelect() {
  if (!audio_) return;
  if (!selectSnd_)
    if (auto b = data_->read("SELECT.SMP")) if (auto sm = parseSample("SELECT.SMP", *b)) selectSnd_ = std::make_shared<SoundSample>(std::move(*sm));
  if (selectSnd_) audio_->playPcm(selectSnd_, 1.0f);
}

void FrontEnd::playVoice(int ship) {  // the pilot's greeting: EM47 / EM50 / EM49 / EF45 / EM51 / EM52 / EM48 / EF43 / EF44 / EM53 (exe table 0x45842)
  if (!audio_) return;
  if (voice_) { audio_->stopVoice(voice_); voice_ = 0; }
  auto exe = data_->read("SLIPSTRM.EXE");
  if (!exe) return;
  const size_t o = 0x4D854 + (0x45842 - 0x10000) + 4 * size_t(std::clamp(ship, 0, 9) + 1);
  if (o + 4 > exe->size()) return;
  const size_t p = size_t((*exe)[o] | ((*exe)[o + 1] << 8) | ((*exe)[o + 2] << 16)) + 0x10000;
  const size_t so = 0x4D854 + (p - 0x10000);
  std::string name;
  for (size_t i = so; i < exe->size() && (*exe)[i] && name.size() < 16; ++i) name += char((*exe)[i]);
  if (auto b = data_->read(name)) if (auto sm = parseSample(name, *b)) voice_ = audio_->playPcm(std::make_shared<SoundSample>(std::move(*sm)), 1.0f);
}

void FrontEnd::go(Screen s) {
  const Screen prev = screen_;
  if (movieVoice_ && audio_) { audio_->stopVoice(movieVoice_); movieVoice_ = 0; }
  if (creditVoice_ && audio_) { audio_->stopVoice(creditVoice_); creditVoice_ = 0; }  // REFINERY.SMP ends the moment the credits are left
  if (voice_ && audio_ && s != Screen::ViewCar) { audio_->stopVoice(voice_); voice_ = 0; }
  if (audio_ && prev == Screen::Info && s != Screen::Info) audio_->stopCue();  // leaving the information screen ends the narration
  // MainMenuDraw (0x4CF09) and the track choice (0x5AD44) play SELECT.SMP when they open and when something is chosen
  if (s == Screen::Main || s == Screen::OnePlayer || s == Screen::Multi || s == Screen::Tracks) playSelect();
  if (s == Screen::Main) champ_ = Championship{};  // leaving to the main menu ends a running championship
  screen_ = s;
  t_ = 0;
  sel_ = 0;
  btns_.clear();
  if (s == Screen::Logo) startMovie("LOGO_S.GDV");
  else if (s == Screen::Intro) startMovie("INTRO.GDV");
  else if (s == Screen::Credits) {  // CreditsScreen 0x56766: REFINERY.SMP, the globe flies away while the credits change every 2 s
    creditIdx_ = 0; creditT_ = 0;
    if (credits_.empty()) {
      if (auto exe = data_->read("SLIPSTRM.EXE")) {
        size_t o = 0x4D854 + (0x56b06 - 0x10000);
        while (o < exe->size() && (*exe)[o]) { std::string t; while (o < exe->size() && (*exe)[o]) t += char((*exe)[o++]); credits_.push_back(t); ++o; }
      }
    }
    if (audio_) { audio_->stopMusic(); if (auto b = data_->read("REFINERY.SMP")) if (auto sm = parseSample("REFINERY.SMP", *b)) creditVoice_ = audio_->playPcm(std::make_shared<SoundSample>(std::move(*sm)), 1.0f); }
  }
  else if (s == Screen::Main && audio_) { if (audio_->currentMusic() != "INTRO.HMP") audio_->playMusic("INTRO.HMP", true); }
  if (s == Screen::Garage) garageEnter();
  // music: the parking lot plays the waiting music, the pilot's card the pilot's own section of INTRO.HMP (SetMusicPart 0x55EFC, calls at 0x45D3E / 0x46174)
  if (audio_ && s == Screen::Team) audio_->setMusicPart(0);
  if (audio_ && s == Screen::ViewCar) { audio_->setMusicPart(viewShip_ + 1); if (prev != Screen::Info) playVoice(viewShip_); cardSel_ = 0; }
  if (audio_ && s == Screen::Info) { audio_->setMusicPart(viewShip_ + 1); audio_->playNarration(viewShip_); }  // DoViewCar 0x46C8E: VoiceCue(table 0x46F54[car]) narrates the text
  buildButtons();
}

void FrontEnd::startMovie(const std::string& file) {
  movieOpen_ = false;
  movieClock_ = 0;
  auto b = data_->read(file);
  if (!b || !gdv_.open(*b, nullptr)) { go(screen_ == Screen::Logo ? Screen::Intro : Screen::Main); return; }
  movieOpen_ = true;
  gdvIdx_.assign(size_t(gdv_.width()) * size_t(gdv_.height()), 0);
  if (audio_ && gdv_.hasAudio()) {  // the sound track: one pass over a second decoder, played as a single sample
    GdvDecoder a;
    if (a.open(*b, nullptr)) {
      auto s = std::make_shared<SoundSample>();
      s->name = file; s->rate = a.audioRate();
      while (a.next()) a.appendAudio(&s->pcm);
      for (size_t i = 0; i < 441 && i < s->pcm.size() / 2; ++i) { const float g = float(i) / 441.0f; s->pcm[i] *= g; s->pcm[s->pcm.size() - 1 - i] *= g; }  // 20 ms ramps: no pop when the sound starts / stops
      movieVoice_ = audio_->playPcm(s, 1.0f);
    }
  }
  if (audio_) audio_->stopMusic();
}

void FrontEnd::fade(double a) {  // a = 1 shows the screen, 0 black
  const int pct = int(std::clamp(1.0 - a, 0.0, 1.0) * 100.0);
  if (pct <= 0) return;
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.darken(0, 0, W - 1, H - 1, pct);
}

void FrontEnd::update(double dt) {
  if (!ready_) return;
  t_ += dt;
  trackDt_ = dt;
  if (screen_ == Screen::Tracks) turnGlobe(selectedTrack(), dt);  // the globe turns to the selected track
  if (screen_ == Screen::Reporters) updateReporters(dt);
  if (screen_ == Screen::Garage) garOpen_ = std::min(1.0, garOpen_ + dt / 0.4);
  if (screen_ == Screen::Slots && slotChosen_ >= 0 && !slotEntry_) { slotAnim_ += dt; if (slotAnim_ > 0.5) slotsFinish(); }  // the status panel grows (sub_5BA9C; duration INFERRED)
  if (screen_ == Screen::Logo || screen_ == Screen::Intro) {
    if (!movieOpen_) return;
    movieClock_ += dt;
    const int want = int(movieClock_ * gdv_.fps());
    bool ended = false;
    while (gdv_.frameIndex() <= want && !ended) if (!gdv_.next()) ended = true;
    if (ended || movieClock_ > gdv_.frameCount() / double(gdv_.fps()) + 0.5) go(screen_ == Screen::Logo ? Screen::Intro : Screen::Gremlin);
  } else if (screen_ == Screen::Gremlin) {
    if (t_ > 4.0) go(Screen::Credits);
  } else if (screen_ == Screen::Credits) {
    creditT_ += dt;
    if (creditT_ >= 2.0) { creditT_ -= 2.0; ++creditIdx_; }
    if (creditIdx_ >= credits_.size() && !credits_.empty()) go(Screen::Main);
  }
}

// ------------------------------------------------------------------------------------------------------------------ buttons
void FrontEnd::buildButtons() {
  btns_.clear();
  auto add = [&](const std::string& n, const std::string& h, const std::string& label, int id, bool en = true) {
    const Sprite* s = spr(n);
    if (!s) return;
    Btn b; b.x = s->hdr4; b.y = s->hdr6; b.w = s->w; b.h = s->h; b.normal = n; b.hover = h; b.label = label; b.id = id; b.enabled = en;
    btns_.push_back(b);
  };
  switch (screen_) {
    case Screen::Main:
      for (int i = 1; i <= 6; ++i) add("MAINBT_" + std::to_string(i) + ".SPR", "MAINBTH" + std::to_string(i) + ".SPR", i == 1 ? "Singleplayer" : i == 2 ? "Multiplayer" : i <= 5 ? str("MAINMENU.ST0", "OPT" + std::to_string(i)) : "Exit Game", i);  // entry 2 opens the port's network game, so it is not "Two Players"
      break;
    case Screen::Multi:
      add("MAINBT_1.SPR", "MAINBTH1.SPR", "Host Game", 1);
      add("MAINBT_2.SPR", "MAINBTH2.SPR", "Search LAN", 2);
      add("MAINBT_3.SPR", "MAINBTH3.SPR", "Direct IP", 3);
      add("MAINBT_6.SPR", "MAINBTH6.SPR", "Back", 6);
      break;
    case Screen::OnePlayer:
      for (int i = 1; i <= 3; ++i) add("MAINBT_" + std::to_string(i) + ".SPR", "MAINBTH" + std::to_string(i) + ".SPR", str("MAINMENU.ST0", "OP1" + std::to_string(i)), i);
      add("MAINBT_6.SPR", "MAINBTH6.SPR", "Back", 6);
      break;
    case Screen::Tracks:
      // The list is in the calendar order (table 0x54E10: Arizona, Chicago, Amazon, London, Norway, Egypt, France, Hawaii, Tokyo, New York); in a single race only
      // the first `progress_` tracks can be chosen, the others are dimmed (0x5B339 / 0x5AF4C). A top-four finish on the newest track unlocks the next one (0x5A85B).
      for (int j = 0; j < 10; ++j) {
        const int i = kChampTrackOrder[j] - 1;
        std::string n = str("BESTDRV.ST0", "BU" + std::to_string(i) + "1");  // "Fastest Laps - Chicago"
        if (const size_t d = n.find(" - "); d != std::string::npos) n = n.substr(d + 3);
        add("TRKBT_" + std::to_string(j) + ".SPR", "TRKBTH" + std::to_string(j) + ".SPR", n, i, j < progress_ || unlockAll_);
      }
      break;
    default: break;
  }
}

int FrontEnd::hit(int x, int y) const {
  for (size_t i = 0; i < btns_.size(); ++i)
    if (x >= btns_[i].x && x < btns_[i].x + btns_[i].w && y >= btns_[i].y && y < btns_[i].y + btns_[i].h) return int(i);
  return -1;
}

void FrontEnd::mouseMove(int x, int y) {
  mx_ = x; my_ = y;
  if (screen_ == Screen::ViewCar) {
    for (int i = 0; i < 3; ++i)
      if (const Sprite* b = spr("DRIVER" + std::to_string(viewShip_) + char('A' + i) + ".SPR"))
        if (x >= b->hdr4 && x < b->hdr4 + b->w && y >= b->hdr6 && y < b->hdr6 + b->h) cardSel_ = i;
    return;
  }
  if (screen_ == Screen::Team) {
    hoverShip_ = x < 0 ? -1 : zone("CH_TEAMZ.ZON", x, y) - 1;
    return;
  }
  if (screen_ == Screen::Garage) { garHov_ = x < 0 ? -1 : garageZoneAt(x, y); return; }
  if (screen_ == Screen::Reporters) { skipReporters(); return; }
  if (screen_ == Screen::Config) { const int z = cfgZoneAt(x, y); if (z >= 0) { cfgHov_ = z; cfgActivate(z, 1); } return; }
  if (screen_ == Screen::Slots) { if (!slotEntry_ && slotChosen_ < 0) slotHover_ = x < 0 ? -1 : slotZoneAt(x, y); return; }
  if (screen_ == Screen::Config) { cfgHov_ = x < 0 ? -1 : cfgZoneAt(x, y); return; }
  if (screen_ == Screen::Results || screen_ == Screen::ChampPos || screen_ == Screen::FinalPos) { const int z = x < 0 ? -1 : resultZoneAt(x, y); if (z >= 0) resSel_ = z; return; }
  const int h = hit(x, y);
  if (h >= 0 && btns_[size_t(h)].enabled) sel_ = h;
}

void FrontEnd::click(int x, int y) {
  mouseMove(x, y);
  if (screen_ <= Screen::Credits) { skipIntro(); return; }
  if (screen_ == Screen::ViewCar) {  // the three card buttons (DRIVER<n>A / B / C)
    for (int i = 0; i < 3; ++i)
      if (const Sprite* b = spr("DRIVER" + std::to_string(viewShip_) + char('A' + i) + ".SPR"))
        if (x >= b->hdr4 && x < b->hdr4 + b->w && y >= b->hdr6 && y < b->hdr6 + b->h) { cardSel_ = i; key(Key::Select); return; }
    return;
  }
  if (screen_ == Screen::Info) { key(Key::Select); return; }
  if (screen_ == Screen::Garage) { garageClick(x, y); return; }
  if (screen_ == Screen::Slots) { if (!slotEntry_ && slotChosen_ < 0) { const int z = slotZoneAt(x, y); if (z >= 0) { slotHover_ = z; slotsChoose(z); } } return; }
  if (screen_ == Screen::Results || screen_ == Screen::ChampPos || screen_ == Screen::FinalPos) { const int z = resultZoneAt(x, y); if (z >= 0) resultChoose(z); return; }
  if (screen_ == Screen::Team) { if (hoverShip_ >= 0 && hoverShip_ < 10) { viewShip_ = hoverShip_; go(Screen::ViewCar); } return; }
  const int h = hit(x, y);
  if (h >= 0 && btns_[size_t(h)].enabled) { sel_ = h; activate(btns_[size_t(h)].id); }
}

void FrontEnd::key(Key k) {
  if (screen_ <= Screen::Credits) {
    if (k == Key::Select || k == Key::Back) skipIntro();
    return;
  }
  const int n = int(btns_.size());
  auto move = [&](int d) { if (n == 0) return; for (int i = 0; i < n; ++i) { sel_ = (sel_ + d + n) % n; if (btns_[size_t(sel_)].enabled) break; } };
  switch (screen_) {
    case Screen::Main:
    case Screen::OnePlayer:
    case Screen::Multi:
    case Screen::Tracks:
      if (k == Key::Up) move(-1);
      else if (k == Key::Down) move(1);
      else if (k == Key::Select && n) activate(btns_[size_t(sel_)].id);
      else if (k == Key::Back) { if (screen_ == Screen::Main) quit_ = true; else if (screen_ == Screen::Tracks) { hoverShip_ = setup_.ship; go(Screen::Team); } else go(Screen::Main); }
      break;
    case Screen::Team:
      if (k == Key::Left || k == Key::Up) hoverShip_ = (hoverShip_ + 9) % 10;
      else if (k == Key::Right || k == Key::Down) hoverShip_ = (hoverShip_ + 1) % 10;
      else if (k == Key::Select) { viewShip_ = std::clamp(hoverShip_, 0, 9); go(Screen::ViewCar); }
      else if (k == Key::Back) go(Screen::OnePlayer);
      break;
    case Screen::ViewCar:  // the card: Accept / Cancel / View (CH_TEAM.ST0 OPT1..3); the picture itself is the menu
      if (k == Key::Up) cardSel_ = (cardSel_ + 2) % 3;
      else if (k == Key::Down) cardSel_ = (cardSel_ + 1) % 3;
      else if (k == Key::Select) {
        if (cardSel_ == 0) { setup_.ship = viewShip_; if (mode_ == 2) champStart(); else go(Screen::Tracks); }  // original order (0x55ADF): vehicle, track, garage, race; the championship has a calendar instead of the track choice
        else if (cardSel_ == 1) { hoverShip_ = viewShip_; go(Screen::Team); }
        else go(Screen::Info);
      } else if (k == Key::Back) { hoverShip_ = viewShip_; go(Screen::Team); }
      break;
    case Screen::Info:  // the text with the turning craft: any key or click returns to the card
      if (k == Key::Select || k == Key::Back) go(Screen::ViewCar);
      break;
    case Screen::Garage: garageKey(k); break;
    case Screen::Slots: slotsKey(k); break;
    case Screen::Reporters: if (k == Key::Select || k == Key::Back) skipReporters(); break;
    case Screen::Config: cfgKey(k); break;
    case Screen::Best:
      if (k == Key::Left) bestTrack_ = (bestTrack_ + 8) % 10 + 1;
      else if (k == Key::Right) bestTrack_ = bestTrack_ % 10 + 1;
      else if (k == Key::Select || k == Key::Back) go(Screen::Main);
      break;
    case Screen::Results:
    case Screen::ChampPos:
    case Screen::FinalPos:
      if (k == Key::Left || k == Key::Right || k == Key::Up || k == Key::Down) { if (screen_ != Screen::FinalPos) resSel_ = k == Key::Left || k == Key::Up ? 0 : 1; }
      else if (k == Key::Select) resultChoose(resSel_);
      else if (k == Key::Back) {
        if (screen_ == Screen::Results) resultChoose(1);             // Esc = Continue (0x5AB30)
        else if (screen_ == Screen::ChampPos) openSlots(true);        // 0x56460: Esc returns 1, which is the Save Game zone
        else resultChoose(0);
      }
      break;
    case Screen::Notice:
      if (k == Key::Select || k == Key::Back) go(Screen::Main);
      break;
    default: break;
  }
}

void FrontEnd::skipIntro() {  // Enter / Esc / click: the next part of the start sequence (the original's logo loops end on Enter)
  go(screen_ == Screen::Logo ? Screen::Intro : screen_ == Screen::Intro ? Screen::Gremlin : screen_ == Screen::Gremlin ? Screen::Credits : Screen::Main);
}

void FrontEnd::activate(int id) {
  if (screen_ == Screen::Main || screen_ == Screen::OnePlayer || screen_ == Screen::Multi || screen_ == Screen::Tracks) playSelect();
  if (screen_ == Screen::Multi) {
    if (id == 6) go(Screen::Main);
    else net_ = id;
  } else if (screen_ == Screen::Main) {
    if (id == 1) go(Screen::OnePlayer);
    else if (id == 5) go(Screen::Best);
    else if (id == 6) quit_ = true;
    else if (id == 2) go(Screen::Multi);  // the original's link menu (split screen / serial / modem / network) is replaced by the port's network game
    else if (id == 3) openSlots(false);
    else openConfig();
  } else if (screen_ == Screen::OnePlayer) {
    if (id == 6) go(Screen::Main);
    else { mode_ = id - 1; hoverShip_ = setup_.ship; go(Screen::Team); hoverShip_ = setup_.ship; }
  } else if (screen_ == Screen::Tracks) {
    setup_.track = id + 1;
    if (mode_ == 0) { setup_.loadout = Loadout{}; race_ = true; }  // practice has no garage
    else go(Screen::Garage);
  }
}

void FrontEnd::draw() {
  if (!ready_) return;
  switch (screen_) {
    case Screen::Logo: case Screen::Intro: drawIntro(); break;
    case Screen::Gremlin: drawGremlin(); break;
    case Screen::Credits: drawCredits(); break;
    case Screen::Main: case Screen::OnePlayer: case Screen::Multi: drawMain(); break;
    case Screen::Tracks: drawTracks(); break;
    case Screen::Team: drawTeam(); break;
    case Screen::ViewCar: drawViewCar(); break;
    case Screen::Info: drawInfo(); break;
    case Screen::Garage: drawGarage(); break;
    case Screen::Best: drawBest(); break;
    case Screen::Results: case Screen::ChampPos: case Screen::FinalPos: drawRanking(); break;
    case Screen::Slots: drawSlots(); break;
    case Screen::Reporters: drawReporters(); break;
    case Screen::Config: drawConfig(); break;
    case Screen::Notice: drawNotice(); break;
  }
}

// ------------------------------------------------------------------------------------------------------------------ zones
int FrontEnd::zone(const std::string& zon, int x, int y) {
  auto it = zones_.find(zon);
  if (it == zones_.end()) { auto b = data_->read(zon); it = zones_.emplace(zon, b ? std::vector<uint8_t>(*b) : std::vector<uint8_t>{}).first; }
  const auto& z = it->second;
  if (z.size() < 4 || y < 0 || y >= 200) return 0;
  const int n = z[0] | (z[1] << 8);
  if (y >= n) return 0;
  size_t p = size_t(z[2 + 2 * y]) | (size_t(z[3 + 2 * y]) << 8);
  while (p + 3 <= z.size()) {  // entries: zone id, end x (exclusive)
    const int id = z[p], end = z[p + 1] | (z[p + 2] << 8);
    if (x < end) return id;
    p += 3;
  }
  return 0;
}

// ------------------------------------------------------------------------------------------------------------------ drawing
void FrontEnd::drawText(const Font& f, const std::string& s, int x, int y, int idx) {
  const bool direct = idx < 0;  // colour -1: the glyph bytes are palette indices
  const uint32_t col = direct ? 0 : argb(pal_.rgba[size_t(idx & 255)]);
  int pen = x;
  for (unsigned char ch : s) {
    if (const uint8_t* bm = f.bitmap(ch))
      for (int gy = 0; gy < f.height; ++gy)
        for (int gx = 0; gx < f.cellW; ++gx) {
          const int px = pen + gx, py = y + gy;
          if (const uint8_t v = bm[gy * f.cellW + gx]; v && px >= 0 && px < W && py >= 0 && py < H) buf_[size_t(py) * W + size_t(px)] = direct ? argb(pal_.rgba[v]) : col;
        }
    pen += f.advance(ch);
  }
}

void FrontEnd::wrapText(const Font& f, const std::string& s, int x, int y, int w, int idx, int gap) {
  std::string line, word;
  auto flush = [&]() { drawText(f, line, x, y, idx); y += f.height + gap; line.clear(); };
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ' ') {
      if (!line.empty() && f.textWidth(line + " " + word) > w) flush();
      line += (line.empty() ? "" : " ") + word;
      word.clear();
    } else word += s[i];
  }
  if (!line.empty()) flush();
}

int FrontEnd::brightIndex(bool brightest) const {  // the palette entry to use for text: the brightest (or a mid-light) one
  int best = 0, bestV = -1, mid = 0, midV = 1 << 30;
  for (int i = 1; i < 256; ++i) {
    const uint32_t c = pal_.rgba[size_t(i)];
    const int v = int((c >> 16) & 255) + int((c >> 8) & 255) + int(c & 255);
    if (v > bestV) { bestV = v; best = i; }
    if (std::abs(v - 520) < midV) { midV = std::abs(v - 520); mid = i; }
  }
  return brightest ? best : mid;
}

void FrontEnd::drawButtons(const Palette* pal) {
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = pal ? pal : &pal_;
  for (size_t i = 0; i < btns_.size(); ++i) {
    const Btn& b = btns_[i];
    const bool on = int(i) == sel_ && b.enabled;
    if (const Sprite* s = spr(on ? b.hover : b.normal)) c.blit(*s, b.x, b.y, transparentOf(*s));
    if (!b.enabled && screen_ == Screen::Tracks) c.darken(b.x, b.y, b.x + b.w - 1, b.y + b.h - 1, 60);  // a locked track
    if (!b.label.empty())
      if (const Font* f = font(screen_ == Screen::Tracks ? "STARFONT.FNT" : "MENUFONT.FNT")) drawText(*f, b.label, b.x + (b.w - f->textWidth(b.label)) / 2, b.y + (b.h - f->height) / 2, -1);
  }
}

void FrontEnd::drawIntro() {
  std::fill(buf_.begin(), buf_.end(), 0xff000000u);
  if (!movieOpen_ || gdv_.frameIndex() == 0) return;
  const int w = gdv_.width(), h = gdv_.height();
  const int oy = (H - h) / 2, ox = (W - std::min(w, W)) / 2;
  const uint8_t* px = gdv_.pixels();
  for (int y = 0; y < h && y + oy < H; ++y)
    for (int x = 0; x < w && x < W; ++x) buf_[size_t(y + oy) * W + size_t(x + ox)] = gdv_.palette()[px[y * w + x]];
}

void FrontEnd::drawGremlin() {  // IntroLogos 0x565FE: the Gremlin logo picture
  const Sprite* s = spr("GREMLOGO.SPR");
  if (!s) return;
  if (s->palette) usePalette(*s->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*s, 0, 0, -1);
  fade(std::min({1.0, t_ / 0.6, (4.0 - t_) / 0.6}));
}

// CreditsScreen 0x56766: SOFTLOGO.SPR, the textured ball (GLOBE.SHP) at projection centre (260, 40) flying away from the camera (distance 0xC8 -> 0x4074
// over 0x15E steps, at least 0x898) while its texture phase advances by 0x5000 per second, and the credit texts (strings in the executable, two lines
// each, SHADE1.FNT, centred at y 180) changing every 2 s. The flight duration (7 s) and the shading are INFERRED.
void FrontEnd::drawCredits() {
  const Sprite* s = spr("SOFTLOGO.SPR");
  if (!s) return;
  if (s->palette) usePalette(*s->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*s, 0, 0, -1);
  const double flight = std::min(1.0, t_ / 7.0);
  const double dist = std::max(2200.0, 200.0 + (16500.0 - 200.0) * flight);
  const int u = int(t_ * 0x5000) & 0x3fff;
  const double rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  if (globe_.loaded()) globe_.draw(buf_.data(), W, H, pal_, 260, 40, dist, rot, u, true, s);
  if (const Font* f = font("SHADE1.FNT"); f && creditIdx_ < credits_.size()) {
    std::string line;
    int y = 180;
    for (char ch : credits_[creditIdx_] + "\r") {
      if (ch == '\r') { drawText(*f, line, 160 - f->textWidth(line) / 2, y, -1); y += f->height + 1; line.clear(); }
      else line += ch;
    }
  }
}

void FrontEnd::drawMain() {
  const Sprite* bg = spr(screen_ == Screen::Main || screen_ == Screen::OnePlayer || screen_ == Screen::Multi ? "MAINMENU.SPR" : "TITLE.SPR");
  if (!bg) return;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  drawButtons(&pal_);
}

static void mul3(const double a[9], const double b[9], double o[9]) {
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) o[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}

int FrontEnd::selectedTrack() const { return sel_ >= 0 && size_t(sel_) < btns_.size() ? std::clamp(btns_[size_t(sel_)].id, 0, 9) : 0; }

void FrontEnd::turnGlobe(int tr, double dt) {
  const double kPi = 3.14159265358979323846;
  const double lat = kTrackLatLon[tr][0] * kPi / 180, lon = (kTrackLatLon[tr][1] + 185.0) * kPi / 180;
  double dy = lon - gYaw_;
  while (dy > kPi) dy -= 2 * kPi;
  while (dy < -kPi) dy += 2 * kPi;
  const double k = std::min(1.0, dt * 4.0);
  gYaw_ += dy * k;
  gTilt_ += (-lat + 0.45 - gTilt_) * k;
}

// GLOBE.SHP with GLOBE.MAT (Earth1..4 = EARTH1..4.SPR) and the flag on the track's position.
void FrontEnd::drawGlobe(int tr, int cx, int cy) {
  if (!globe_.loaded()) return;
  const double kPi = 3.14159265358979323846;
  const double lat = kTrackLatLon[tr][0] * kPi / 180, lon = (kTrackLatLon[tr][1] + 185.0) * kPi / 180;
  const double ca = std::cos(gYaw_), sa = std::sin(gYaw_), ct = std::cos(gTilt_), st = std::sin(gTilt_);
  const double ry[9] = {ca, 0, sa, 0, 1, 0, -sa, 0, ca}, rx[9] = {1, 0, 0, 0, ct, -st, 0, st, ct};
  double rot[9];
  mul3(rx, ry, rot);
  globe_.draw(buf_.data(), W, H, pal_, cx, cy, 5900 / 0.93, rot, 0, true);
  const double dir[3] = {std::cos(lat) * std::sin(lon), std::sin(lat), -std::cos(lat) * std::cos(lon)};
  globe_.drawFlag(buf_.data(), W, H, pal_, cx, cy, 5900 / 0.93, rot, dir);
}

void FrontEnd::drawTracks() {
  const Sprite* bg = spr("STARS.SPR");
  if (bg && bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  if (bg) c.blit(*bg, 0, 0, -1);
  drawGlobe(selectedTrack(), 90, 108);
  if (const Sprite* t = spr("CH_TRACK.SPR")) {
    c.blit(*t, t->hdr4, t->hdr6, transparentOf(*t));
    if (const Font* f = font("STARFONT.FNT")) { const std::string ti = str("CHTRACK.ST0", "TITL"); drawText(*f, ti, t->hdr4 + (t->w - f->textWidth(ti)) / 2, t->hdr6 + (t->h - f->height) / 2, -1); }
  }
  drawButtons(&pal_);
}

void FrontEnd::drawTeam() {
  const Sprite* bg = spr("CH_TEAM.SPR");
  if (!bg) return;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  if (const Font* f = font("TEAMFONT.FNT")) drawText(*f, str("CH_TEAM.ST0", "TITL"), 160 - f->textWidth(str("CH_TEAM.ST0", "TITL")) / 2, 11, -1);
  if (hoverShip_ >= 0 && hoverShip_ < 10) {  // the ship under the cursor is framed (bounding box of its hit zone)
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y += 2)
      for (int x = 0; x < W; x += 2)
        if (zone("CH_TEAMZ.ZON", x, y) == hoverShip_ + 1) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 >= 0) {
      const uint32_t col = argb(pal_.rgba[size_t(brightIndex(true))]);
      for (int i = 0; i < 6; ++i) {
        for (int dx : {x0, x1}) { buf_[size_t(std::clamp(y0 + i, 0, H - 1)) * W + size_t(std::clamp(dx, 0, W - 1))] = col; buf_[size_t(std::clamp(y1 - i, 0, H - 1)) * W + size_t(std::clamp(dx, 0, W - 1))] = col; }
        for (int dy : {y0, y1}) { buf_[size_t(std::clamp(dy, 0, H - 1)) * W + size_t(std::clamp(x0 + i, 0, W - 1))] = col; buf_[size_t(std::clamp(dy, 0, H - 1)) * W + size_t(std::clamp(x1 - i, 0, W - 1))] = col; }
      }
    }
  }
}

// SpriteGreyscale (0x5B8BF): the screen behind the pilot cards is turned to grey.
void FrontEnd::greyscale() {
  for (uint32_t& p : buf_) {
    const uint32_t y = (((p >> 16) & 255) * 77 + ((p >> 8) & 255) * 150 + (p & 255) * 29) >> 8;
    p = 0xff000000u | (y << 16) | (y << 8) | y;
  }
}

// Entries 0..63 become a grey ramp (the sprites of the cards use them for the greyed background and their own light areas: the surfboard, the text).
void FrontEnd::greyRamp() {
  for (uint32_t i = 0; i < 64; ++i) { const uint32_t v = (i << 2) | (i >> 4); pal_.rgba[i] = (v << 16) | (v << 8) | v; }
}

void FrontEnd::lighten(int x0, int y0, int x1, int y1, int pct) {
  for (int y = std::max(0, y0); y <= std::min(H - 1, y1); ++y)
    for (int x = std::max(0, x0); x <= std::min(W - 1, x1); ++x) {
      uint32_t& p = buf_[size_t(y) * W + size_t(x)];
      auto up = [&](uint32_t v) { return std::min(255u, v + (255 - v) * uint32_t(pct) / 100u); };
      p = 0xff000000u | (up((p >> 16) & 255) << 16) | (up((p >> 8) & 255) << 8) | up(p & 255);
    }
}

void FrontEnd::wrapTextCentered(const Font& f, const std::string& s, int cx, int y, int w, int idx, int gap) {
  std::string line, word;
  auto flush = [&]() { drawText(f, line, cx - f.textWidth(line) / 2, y, idx); y += f.height + gap; line.clear(); };
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ' ') {
      if (!line.empty() && f.textWidth(line + " " + word) > w) flush();
      line += (line.empty() ? "" : " ") + word;
      word.clear();
    } else word += s[i];
  }
  if (!line.empty()) flush();
}

// The pilot card (0x45D5C: DRIVER<n>.SPR, 265x178 at (27, 10), with its own palette, over the greyed parking lot) with the three buttons
// DRIVER<n>A / B / C (Accept, Cancel, View: CH_TEAM.ST0 OPT1..3).
void FrontEnd::drawViewCar() {
  drawTeam();
  greyscale();
  greyRamp();
  const Sprite* s = spr("DRIVER" + std::to_string(viewShip_) + ".SPR");
  if (!s) return;
  if (s->palette) usePalette(*s->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_; c.canvasPalette = true;
  c.blit(*s, s->hdr4, s->hdr6, -1);
  const Font* f = font("SMALL.FNT");
  for (int i = 0; i < 3; ++i) {
    const Sprite* b = spr("DRIVER" + std::to_string(viewShip_) + char('A' + i) + ".SPR");
    if (!b) continue;
    c.blit(*b, b->hdr4, b->hdr6, transparentOf(*b));
    if (f) {
      const std::string label = str("CH_TEAM.ST0", "OPT" + std::to_string(i + 1));
      drawText(*f, label, b->hdr4 + (b->w - f->textWidth(label)) / 2, b->hdr6 + (b->h - f->height) / 2 + 1, brightIndex(true));
    }
    if (i == cardSel_) lighten(b->hdr4, b->hdr6, b->hdr4 + b->w - 1, b->hdr6 + b->h - 1, 35);
  }
}

// DoViewCar 0x46A94: VIEWCAR<n>.SPR (the card with the pilot's name, 265x178 at (27, 10)), the craft turning in it (rendered by the application,
// see previewShip) and the biography (VIEWCAR.ST0 CAR<n>, VIEWDESC.FNT) centred at the bottom while the pilot's voice reads it.
void FrontEnd::drawInfo() {
  drawTeam();
  greyscale();
  greyRamp();
  const Sprite* s = spr("VIEWCAR" + std::to_string(viewShip_) + ".SPR");
  if (!s) return;
  if (s->palette) usePalette(*s->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_; c.canvasPalette = true;
  c.blit(*s, s->hdr4, s->hdr6, -1);
  if (const Font* f = font("VIEWDESC.FNT")) wrapTextCentered(*f, str("VIEWCAR.ST0", "CAR" + std::to_string(viewShip_)), 160, 135, 240, -1, 1);
}

int FrontEnd::previewShip(double* angle, int rect[4]) const {
  if (screen_ != Screen::Info) return -1;
  *angle = 0.7 + t_ * 0.8;
  rect[0] = 30; rect[1] = 28; rect[2] = 290; rect[3] = 138;
  return viewShip_;
}

// ------------------------------------------------------------------------------------------------------------------ garage
// The workshop, DoGarage 0x4BAEE (docs/frontend.md): the hangar GARAGE<n>A with the craft on its crane; the menu buttons are regions of the darkened copy
// GARAGE<n>B (the hovered one shows the bright A picture) with a 1 px frame (colours 0x25 left, 0x2B top, 0x0A bottom / right, sub_5609F) and a CNFFONT
// label (GARAGE.ST0 BUT1..4 / WEP0..2 / REP3). The status panel GARBOX1 (cash, the two pods, charger / targetter / loader, turbo) grows from the point
// (217,118) when the garage opens (sub_5BA9C). Weapons -> Load... / Left Pod / Right Pod / Ok -> the grid GARBOX2 (sub_4C476, 11 weapons + Cancel); Turbo
// -> GARBOX3 (sub_4C6FE, 5 items + Cancel, a check mark GARGOT on the fitted one); Systems -> GARBOX4 (sub_4C19F, three upgrades + Exit). The weapon and turbo
// names use GARWEAP.FNT, the price of the item under the pointer is shown over it in RESULTS.FNT, green (colour 0xFC) when affordable, red (0xFD) when not.
// All zones are the executable's tables (0x4BED0, 0x4C122, 0x4BA8C, 0x4C3FE, 0x4C91E). CONFIRMED: start money 750 (0x58503), upgrade prices 300 / 500 / 700
// (0x4C46A), purchases are not refunded, the loader doubles the rounds of later weapon purchases (max 9, 0x4C070), the fitted turbo item is the pre-selected one.
namespace {
struct Zone { int x1, y1, x2, y2; };
constexpr Zone kMainZ[4] = {{13, 83, 113, 105}, {13, 111, 113, 133}, {13, 139, 113, 161}, {13, 167, 113, 189}};
constexpr Zone kPodZ[3] = {{13, 111, 113, 133}, {13, 139, 113, 161}, {13, 167, 113, 189}};
constexpr Zone kWeapZ[12] = {{24, 99, 84, 122}, {92, 99, 152, 122}, {160, 99, 220, 122}, {228, 99, 288, 122}, {24, 129, 84, 152}, {92, 129, 152, 152},
                             {160, 129, 220, 152}, {228, 129, 288, 152}, {24, 159, 84, 182}, {92, 159, 152, 182}, {160, 159, 220, 182}, {228, 159, 288, 182}};
constexpr Zone kSysZ[4] = {{24, 112, 84, 165}, {92, 112, 152, 165}, {160, 112, 220, 165}, {228, 112, 288, 165}};
constexpr Zone kTurboZ[6] = {{23, 101, 105, 138}, {113, 101, 195, 138}, {203, 101, 285, 138}, {23, 144, 105, 181}, {113, 144, 195, 181}, {203, 144, 285, 181}};
constexpr int kSysCheck[3][2] = {{54, 138}, {122, 138}, {190, 138}};                                          // table 0x4C444
constexpr int kTurboCheck[5][2] = {{64, 119}, {154, 119}, {244, 119}, {64, 162}, {154, 162}};                 // table 0x4C984
constexpr int kSysPrice[3] = {300, 500, 700};                                                                 // 0x4C46A
constexpr const char* kSysName[3] = {"Charger", "Targetter", "Loader"};                                       // 0x4B9C8
constexpr int kPanelX = 137, kPanelY = 83, kPanelW = 162, kPanelH = 107;

const Zone* garZones(int page, int* n) {
  switch (page) {
    case 0: *n = 4; return kMainZ;
    case 1: *n = 3; return kPodZ;
    case 2: *n = 12; return kWeapZ;
    case 3: *n = 6; return kTurboZ;
    default: *n = 4; return kSysZ;
  }
}
}  // namespace

int FrontEnd::garageZoneAt(int x, int y) const {
  int n;
  const Zone* z = garZones(garPage_, &n);
  for (int i = 0; i < n; ++i)
    if (x >= z[i].x1 && x <= z[i].x2 && y >= z[i].y1 && y <= z[i].y2) return i;
  return -1;
}

int FrontEnd::weaponPrice(int id) const { return table_ && id >= 1 && id <= 11 ? table_->w[size_t(id)].price[std::clamp(difficulty_, 0, 2)] : 0; }
int FrontEnd::boosterPrice(int i) const { return table_ && i >= 0 && i < 5 ? table_->boosters[size_t(i)].price : 0; }

void FrontEnd::garageEnter() {
  garPage_ = 0; garHov_ = -1; pod_ = 0; garOpen_ = 0;
  podW_[0] = podW_[1] = -1; podAmmo_[0] = podAmmo_[1] = 0;
  booster_ = 0;  // the roster record starts with turbo item 0 (the screen shows "Turbo: Delphine Injection" before anything is bought)
  fastRecharge_ = wideLock_ = loader_ = false;
  cash_ = startCredits_;
  if (champ_.active()) {  // the championship carries the money and the fitted parts from race to race
    const ChampDriver& d = champ_.driver(champ_.humanShip());
    cash_ = d.money;
    podW_[0] = d.load.weaponA; podW_[1] = d.load.weaponB;
    podAmmo_[0] = d.load.ammoA; podAmmo_[1] = d.load.ammoB;
    booster_ = d.load.booster;
    fastRecharge_ = d.load.fastRecharge; wideLock_ = d.load.wideLock; loader_ = d.loader;
  }
  if (mx_ >= 0) garHov_ = garageZoneAt(mx_, my_);
}

void FrontEnd::garageBuildLoadout() {
  Loadout l;
  l.weaponA = podW_[0]; l.weaponB = podW_[1];
  l.ammoA = podW_[0] > 0 ? podAmmo_[0] : 0;
  l.ammoB = podW_[1] > 0 ? podAmmo_[1] : 0;
  l.booster = booster_;
  l.fastRecharge = fastRecharge_; l.wideLock = wideLock_;
  setup_.loadout = l;
}

const Sprite* FrontEnd::cropIcon(int weapon) {  // the picture of a weapon: its grid cell cut out of GARBOX2 (60 x 25, shown in the pod windows of the panel)
  auto it = icons_.find(weapon);
  if (it != icons_.end()) return &it->second;
  const Sprite* box = spr("GARBOX2.SPR");
  if (!box || weapon < 1 || weapon > 11) return nullptr;
  const Zone& z = kWeapZ[weapon - 1];
  Sprite ic;
  ic.w = 60; ic.h = 25;
  ic.pixels.assign(size_t(ic.w) * size_t(ic.h), 0);
  for (int j = 0; j < ic.h; ++j)
    for (int i = 0; i < ic.w; ++i) {
      const int sx = z.x1 + i - box->hdr4, sy = z.y1 + j - box->hdr6;
      if (sx >= 0 && sx < box->w && sy >= 0 && sy < box->h) ic.pixels[size_t(j) * size_t(ic.w) + size_t(i)] = box->pixels[size_t(sy) * size_t(box->w) + size_t(sx)];
    }
  return &icons_.emplace(weapon, std::move(ic)).first->second;
}

void FrontEnd::garageGo(int page, bool fromMouse) {
  garPage_ = page;
  garHov_ = fromMouse && mx_ >= 0 ? garageZoneAt(mx_, my_) : -1;  // the pointer stays where it was; the keyboard starts without a selection
}

void FrontEnd::garageChoose(int i, bool fromMouse) {
  switch (garPage_) {
    case 0:
      if (i == 0) garageGo(1, fromMouse);
      else if (i == 1) garageGo(3, fromMouse);
      else if (i == 2) garageGo(4, fromMouse);
      else {
        garageBuildLoadout();
        if (champ_.active()) {  // the roster keeps what was bought
          ChampDriver& d = champ_.driver(champ_.humanShip());
          d.money = cash_; d.load = setup_.loadout; d.loader = loader_;
          setup_.grid = champ_.grid();
          setup_.championship = true;
        } else { setup_.grid = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}; setup_.championship = false; }
        race_ = true;
      }
      break;
    case 1:
      if (i < 2) { pod_ = i; garageGo(2, fromMouse); }
      else garageGo(0, fromMouse);
      break;
    case 2:
      if (i == 11) { garageGo(1, fromMouse); break; }  // Cancel
      if (weaponPrice(i + 1) > cash_) break;           // too expensive: the click is ignored (0x4C61B)
      cash_ -= weaponPrice(i + 1);
      podW_[pod_] = i + 1;
      {
        const int pack = table_ ? table_->w[size_t(i + 1)].pack : -1;
        podAmmo_[pod_] = pack >= 0 && loader_ ? std::min(pack * 2, 9) : pack;
      }
      garageGo(1, fromMouse);
      break;
    case 3:
      if (i == 5) { garageGo(0, fromMouse); break; }
      if (i == booster_ || boosterPrice(i) > cash_) break;
      cash_ -= boosterPrice(i);
      booster_ = i;
      garageGo(0, fromMouse);
      break;
    default:
      if (i == 3) { garageGo(0, fromMouse); break; }
      {
        bool* f[3] = {&fastRecharge_, &wideLock_, &loader_};
        if (*f[i] || kSysPrice[i] > cash_) break;
        cash_ -= kSysPrice[i];
        *f[i] = true;
      }
      break;
  }
}

void FrontEnd::garageKey(Key k) {
  int n;
  garZones(garPage_, &n);
  const int cols = garPage_ == 2 ? 4 : garPage_ == 3 ? 3 : garPage_ == 4 ? 4 : 1;
  if (k == Key::Left || k == Key::Right || k == Key::Up || k == Key::Down) {
    if (garHov_ < 0) { garHov_ = 0; return; }
    const int c = garHov_ % cols;
    if (k == Key::Left && c > 0) --garHov_;
    else if (k == Key::Right && c < cols - 1 && garHov_ + 1 < n) ++garHov_;
    else if (k == Key::Up && garHov_ - cols >= 0) garHov_ -= cols;
    else if (k == Key::Down && garHov_ + cols < n) garHov_ += cols;
  } else if (k == Key::Back) {  // the original has no Esc here; it steps back like the Ok / Cancel / Exit entries
    if (garPage_ == 0) go(champ_.active() ? Screen::Main : Screen::Tracks);
    else if (garPage_ == 2) garageGo(1, false);
    else garageGo(0, false);
  } else if (k == Key::Select) {
    if (garHov_ >= 0) garageChoose(garHov_, false);
  }
}

void FrontEnd::garageClick(int x, int y) {
  const int i = garageZoneAt(x, y);
  if (i >= 0) { garHov_ = i; garageChoose(i, true); }
}

void FrontEnd::drawGaragePanel(const Sprite& panel) {  // sub_4C9B2: the status panel, drawn at its final place
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(panel, kPanelX, kPanelY, -1);
  const Font* f = font("GARWEAP.FNT");
  if (!f) return;
  auto centre = [&](const std::string& s, int x1, int x2, int y) { drawText(*f, s, x1 + (x2 - x1 + 1 - f->textWidth(s)) / 2, y, -1); };
  const int px = kPanelX, py = kPanelY;
  centre("Cash: $" + std::to_string(cash_), px + 15, px + 148, py + 14);
  const int slotX[2] = {18, 86};
  for (int p = 0; p < 2; ++p) {
    const int x1 = px + slotX[p], x2 = x1 + 59;
    if (podW_[p] > 0) {
      if (const Sprite* ic = cropIcon(podW_[p])) c.blit(*ic, x1, py + 25, -1);
      centre(table_ ? table_->w[size_t(podW_[p])].name + " " : "", x1, x2, py + 28);
      if (podAmmo_[p] >= 0) { const std::string a = "[" + std::to_string(podAmmo_[p]) + "]"; drawText(*f, a, x2 - f->textWidth(a), py + 40, -1); }
    } else centre("Empty", x1, x2, py + 28);
  }
  const bool on[3] = {fastRecharge_, wideLock_, loader_};
  for (int i = 0; i < 3; ++i) centre(std::string(kSysName[i]) + ": " + (on[i] ? "Fitted" : "Not Fitted"), px + 15, px + 148, py + 59 + 7 * i);
  centre("Turbo: " + (table_ && booster_ >= 0 ? table_->boosters[size_t(booster_)].name : std::string("Not Fitted")), px + 15, px + 148, py + 88);
}

void FrontEnd::drawGarage() {
  const std::string n = std::to_string(setup_.ship);
  const Sprite* A = spr("GARAGE" + n + "A.SPR");
  if (!A) A = spr("GARAGEA.SPR");
  if (!A) return;
  const Sprite* B = spr("GARAGE" + n + "B.SPR");
  if (!B) B = spr("GARAGEB.SPR");
  if (!B) B = A;
  if (A->palette) usePalette(*A->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*A, 0, 0, -1);
  const Font* fw = font("GARWEAP.FNT");
  const Font* fc = font("CNFFONT.FNT");
  const Font* fr = font("RESULTS.FNT");
  auto centre = [&](const Font* f, const std::string& s, const Zone& z, int y, int idx = -1) { if (f) drawText(*f, s, z.x1 + (z.x2 - z.x1 + 1 - f->textWidth(s)) / 2, y, idx); };
  auto middle = [&](const Font* f, const std::string& s, const Zone& z) { if (f) centre(f, s, z, z.y1 + (z.y2 - z.y1 + 1 - f->height) / 2); };
  auto button = [&](const Zone& z, bool hot, const std::string& label) {  // sub_5609F
    c.fillIndex(z.x1, z.y1, z.x1, z.y2, 0x25);
    c.fillIndex(z.x1, z.y1, z.x2, z.y1, 0x2b);
    c.fillIndex(z.x1, z.y2, z.x2, z.y2, 0x0a);
    c.fillIndex(z.x2, z.y1, z.x2, z.y2, 0x0a);
    const Sprite* src = hot ? A : B;
    for (int y = z.y1 + 1; y < z.y2; ++y)
      for (int x = z.x1 + 1; x < z.x2; ++x) buf_[size_t(y) * W + size_t(x)] = argb(pal_.rgba[src->pixels[size_t(y) * size_t(src->w) + size_t(x)]]);
    centre(fc, label, z, z.y1 + 5);
  };
  auto price = [&](const Zone& z, int p) {  // the hover price over the item: green when affordable, red when not
    if (fr) centre(fr, "$" + std::to_string(p), z, z.y1 + 10, p > cash_ ? 0xfd : 0xfc);
  };
  const Sprite* panel = spr("GARBOX1.SPR");
  if (garPage_ == 0 || garPage_ == 1) {
    if (panel) {
      if (garPage_ == 1 || garOpen_ >= 1.0) drawGaragePanel(*panel);
      else if (garOpen_ > 0) {  // the panel grows from (217,118) to its place (sub_5BA9C)
        std::vector<uint32_t> bg(size_t(kPanelW) * kPanelH);
        for (int y = 0; y < kPanelH; ++y) for (int x = 0; x < kPanelW; ++x) bg[size_t(y) * kPanelW + size_t(x)] = buf_[size_t(kPanelY + y) * W + size_t(kPanelX + x)];
        drawGaragePanel(*panel);
        std::vector<uint32_t> img(bg.size());
        for (int y = 0; y < kPanelH; ++y) for (int x = 0; x < kPanelW; ++x) { img[size_t(y) * kPanelW + size_t(x)] = buf_[size_t(kPanelY + y) * W + size_t(kPanelX + x)]; buf_[size_t(kPanelY + y) * W + size_t(kPanelX + x)] = bg[size_t(y) * kPanelW + size_t(x)]; }
        const double t = garOpen_;
        const int x1 = int(217 + (kPanelX - 217) * t), y1 = int(118 + (kPanelY - 118) * t);
        const int x2 = int(217 + (kPanelX + kPanelW - 1 - 217) * t), y2 = int(118 + (kPanelY + kPanelH - 1 - 118) * t);
        const int dw = x2 - x1 + 1, dh = y2 - y1 + 1;
        for (int y = 0; y < dh; ++y)
          for (int x = 0; x < dw; ++x) buf_[size_t(y1 + y) * W + size_t(x1 + x)] = img[size_t(y * kPanelH / dh) * kPanelW + size_t(x * kPanelW / dw)];
      }
    }
    if (garPage_ == 0) {
      const char* tag[4] = {"BUT1", "BUT2", "BUT3", "BUT4"};
      for (int i = 0; i < 4; ++i) button(kMainZ[i], garHov_ == i, str("GARAGE.ST0", tag[i]));
    } else {
      button(kMainZ[0], true, str("GARAGE.ST0", "WEP0"));  // "Load..." is only a title: always drawn with the bright picture
      button(kPodZ[0], garHov_ == 0, str("GARAGE.ST0", "WEP1"));
      button(kPodZ[1], garHov_ == 1, str("GARAGE.ST0", "WEP2"));
      button(kPodZ[2], garHov_ == 2, str("GARAGE.ST0", "REP3"));
    }
  } else if (garPage_ == 2) {
    if (const Sprite* box = spr("GARBOX2.SPR")) c.blit(*box, 12, 82, -1);
    for (int i = 0; i < 11; ++i) if (table_) centre(fw, table_->w[size_t(i + 1)].name, kWeapZ[i], kWeapZ[i].y1 + 1);
    centre(fw, str("GARAGE.ST0", "CANC"), kWeapZ[11], kWeapZ[11].y1 + 10);
    if (garHov_ >= 0 && garHov_ < 11) price(kWeapZ[garHov_], weaponPrice(garHov_ + 1));
  } else if (garPage_ == 3) {
    if (const Sprite* box = spr("GARBOX3.SPR")) c.blit(*box, 13, 83, -1);
    for (int i = 0; i < 5; ++i) if (table_) centre(fw, table_->boosters[size_t(i)].name, kTurboZ[i], kTurboZ[i].y1 + 2);
    middle(fw, str("GARAGE.ST0", "CANC"), kTurboZ[5]);
    if (booster_ >= 0)
      if (const Sprite* ok = spr("GARGOT.SPR")) c.blit(*ok, kTurboCheck[booster_][0] - 24, kTurboCheck[booster_][1] - 18, 0);
    if (garHov_ >= 0 && garHov_ < 5 && garHov_ != booster_) price(kTurboZ[garHov_], boosterPrice(garHov_));
  } else {
    if (const Sprite* box = spr("GARBOX4.SPR")) c.blit(*box, 12, 101, -1);
    const bool on[3] = {fastRecharge_, wideLock_, loader_};
    for (int i = 0; i < 3; ++i) {
      centre(fw, kSysName[i], kSysZ[i], kSysZ[i].y1 + 2);
      if (on[i]) if (const Sprite* ok = spr("GARGOT.SPR")) c.blit(*ok, kSysCheck[i][0] - 24, kSysCheck[i][1] - 18, 0);
    }
    middle(fw, str("GARAGE.ST0", "EXIT"), kSysZ[3]);
    if (garHov_ >= 0 && garHov_ < 3 && !on[garHov_]) price(kSysZ[garHov_], kSysPrice[garHov_]);
  }
}


// ------------------------------------------------------------------------------------------------------------------ records, best drivers, results
const Palette* FrontEnd::facePalette() {  // the pilot faces (BESTF*) have no palette of their own: they use the Best Drivers screen's
  const Sprite* b = spr("BESTBACK.SPR");
  return b && b->palette ? &*b->palette : &pal_;
}

std::string FrontEnd::timeText(double sec) {
  const int cs = int(sec * 100 + 0.5);
  char b[32];
  std::snprintf(b, sizeof b, "%d'%02d\"%02d", cs / 6000, (cs / 100) % 60, cs % 100);
  return b;
}

static std::string recordsPath() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/Library/Application Support/Slipstream/records.txt";
}

void FrontEnd::loadRecords() {
  std::ifstream f(recordsPath());
  int t, ship, ms;
  while (f >> t >> ship >> ms)
    if (t >= 1 && t <= 10 && ship >= 0 && ship < 10) records_[t].push_back({ship, ms});
  for (auto& v : records_) { std::sort(v.begin(), v.end(), [](const Record& a, const Record& b) { return a.ms < b.ms; }); if (v.size() > 5) v.resize(5); }
}

void FrontEnd::saveRecords() const {
  const std::string p = recordsPath();
  const size_t slash = p.rfind('/');
  std::error_code ec;
  std::filesystem::create_directories(p.substr(0, slash), ec);
  std::ofstream f(p);
  for (int t = 1; t <= 10; ++t) for (const Record& r : records_[t]) f << t << ' ' << r.ship << ' ' << r.ms << '\n';
}

void FrontEnd::addRecord(int track, int ship, double seconds) {
  if (track < 1 || track > 10 || seconds <= 0) return;
  auto& v = records_[track];
  v.push_back({ship, int(seconds * 1000 + 0.5)});
  std::sort(v.begin(), v.end(), [](const Record& a, const Record& b) { return a.ms < b.ms; });
  if (v.size() > 5) v.resize(5);
  saveRecords();
}

void FrontEnd::showResults(const RaceResult& r) {
  result_ = r;
  addRecord(r.track, r.ship, r.bestLap);
  resSel_ = 1;
  if (!champ_.active() && mode_ == 1 && !r.projected[std::clamp(r.ship, 0, 9)] && r.place[std::clamp(r.ship, 0, 9)] <= 4 && progress_ < 10 && r.track == kChampTrackOrder[progress_ - 1]) {
    ++progress_;  // 0x5A85B: the human finished in the first four on the newest track
    progressChanged_ = true;
  }
  go(Screen::Results);
  if (audio_) audio_->playMusic(r.place[std::clamp(r.ship, 0, 9)] > 3 ? "LOSE.HMP" : "WIN.HMP", false);  // results screen 0x5A820: WIN.HMP for the first three places, else LOSE.HMP
}

void FrontEnd::drawBest() {
  const Sprite* bg = spr("BESTBACK.SPR");
  if (!bg) return;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  c.darken(8, 4, W - 9, H - 8, 35);
  const Font* f = font("BESTDRV.FNT");
  const Font* sm = font("SMALL.FNT");
  const int tr = std::clamp(bestTrack_, 1, 10);
  std::string title = str("BESTDRV.ST0", "BU" + std::to_string(tr - 1) + "1");
  if (f) drawText(*f, title, 160 - f->textWidth(title) / 2, 8, -1);
  for (int i = 0; i < 5; ++i) {
    const int y = 32 + i * 32;
    const bool have = i < int(records_[tr].size());
    HudCanvas fcv = c;
    fcv.pal = facePalette();
    if (const Sprite* b3 = spr("BEST3DBK.SPR")) fcv.blitScaled(*b3, 40, y, 36, 30, transparentOf(*b3));
    if (have) if (const Sprite* fc = spr("BESTF" + std::to_string(records_[tr][size_t(i)].ship) + ".SPR")) fcv.blitScaled(*fc, 40, y, 36, 30, transparentOf(*fc));
    if (f) {
      drawText(*f, std::to_string(i + 1), 20, y + 8, -1);
      drawText(*f, have ? timeText(records_[tr][size_t(i)].ms / 1000.0) : "-'--\"--", 100, y + 8, -1);
    }
  }
  if (sm) {
    const std::string hint = str("BESTDRV.ST0", "BUT2") + "  " + str("BESTDRV.ST0", "BUT3") + "  " + str("BESTDRV.ST0", "BUT4");
    drawText(*sm, hint, 160 - sm->textWidth(hint) / 2, 190, -1);
  }
}

// Race results, 0x5A820; championship positions, 0x5623E; final positions, 0x56FF0. RACERES.SPR (FINALPOS.SPR for the final screen) with the title box and the
// buttons cut from the dark copy RACERESD / FINPOSD (the hovered button shows the bright picture), frame colours 0x25 / 0x2B / 0x0A (sub_56137 / sub_5609F);
// ten rows from y = 40 every 13 px: "%d." at x 20, the pilot's name (exe table 0x54E94) at x 40, the time (results) or the points (championship) at x 240 / 250;
// the human's row uses RESULTSB.FNT (gold; RESULTSD on the final screen), the AI rows RESULTSA.FNT (silver; RESULTSC). Zones 0x5AD18 / 0x565D2 / 0x572D4:
// Replay (40,175)-(127,191) and Continue (190,175)-(277,191); the final screen has only Ok (109,175)-(209,191). CONFIRMED.
// The port's Replay restarts the race (the original replays the recorded race, not ported; not offered in the championship); unfinished ships show their projected
// time instead of "Retired".
namespace {
struct RZone { int x1, y1, x2, y2; };
constexpr RZone kResZ[2] = {{40, 175, 127, 191}, {190, 175, 277, 191}};
constexpr RZone kOkZ = {109, 175, 209, 191};
}  // namespace

int FrontEnd::resultZoneAt(int x, int y) const {
  if (screen_ == Screen::FinalPos) return x >= kOkZ.x1 && x <= kOkZ.x2 && y >= kOkZ.y1 && y <= kOkZ.y2 ? 0 : -1;
  for (int i = 0; i < 2; ++i) if (x >= kResZ[i].x1 && x <= kResZ[i].x2 && y >= kResZ[i].y1 && y <= kResZ[i].y2) return i;
  return -1;
}

void FrontEnd::resultChoose(int i) {
  if (screen_ == Screen::Results) {
    if (i == 0) { if (!champ_.active()) race_ = true; }  // Replay: the same race again
    else if (champ_.active()) champRaceFinished();
    else go(Screen::Main);
  } else if (screen_ == Screen::ChampPos) {
    if (i == 0) openSlots(true);
    else { champ_.nextRace(); champBeginRace(); }  // 0x55DAD
  } else if (screen_ == Screen::FinalPos) {
    go(Screen::Main);
  }
}

const std::string& FrontEnd::pilotName(int ship) {
  if (names_.empty()) {
    names_.assign(10, "");
    if (auto exe = data_->read("SLIPSTRM.EXE")) {
      const std::string key = "Charles Edward-Royce";
      const std::string hay(exe->begin(), exe->end());
      size_t p = hay.find(key);
      for (int i = 0; i < 10 && p != std::string::npos && p < hay.size(); ++i) { names_[size_t(i)] = hay.c_str() + p; p += names_[size_t(i)].size() + 1; }
    }
  }
  return names_[size_t(std::clamp(ship, 0, 9))];
}

void FrontEnd::drawRanking() {
  const bool fin = screen_ == Screen::FinalPos, champ = screen_ == Screen::ChampPos || fin;
  const Sprite* bg = spr(fin ? "FINALPOS.SPR" : "RACERES.SPR");
  const Sprite* dark = spr(fin ? "FINPOSD.SPR" : "RACERESD.SPR");
  if (!bg) return;
  if (!dark) dark = bg;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  const Font* fa = font(fin ? "RESULTSC.FNT" : "RESULTSA.FNT");
  const Font* fb = font(fin ? "RESULTSD.FNT" : "RESULTSB.FNT");
  if (!fb) fb = fa;
  const char* file = screen_ == Screen::Results ? "RACERES.ST0" : fin ? "FINALPOS.ST0" : "CHAMPPOS.ST0";
  auto box = [&](int x1, int y1, int x2, int y2, bool hot, const std::string& label) {  // sub_56137
    c.fillIndex(x1, y1, x1, y2, 0x25);
    c.fillIndex(x1, y1, x2, y1, 0x2b);
    c.fillIndex(x1, y2, x2, y2, 0x0a);
    c.fillIndex(x2, y1, x2, y2, 0x0a);
    const Sprite* src = hot ? bg : dark;
    for (int y = y1 + 1; y < y2; ++y)
      for (int x = x1 + 1; x < x2; ++x) buf_[size_t(y) * W + size_t(x)] = argb(pal_.rgba[src->pixels[size_t(y) * size_t(src->w) + size_t(x)]]);
    if (fa) drawText(*fa, label, x1 + (x2 - x1 + 1 - fa->textWidth(label)) / 2, y1 + 4, -1);
  };
  if (fin) {
    box(92, 11, 226, 27, false, str(file, "TITL"));
    box(kOkZ.x1, kOkZ.y1, kOkZ.x2, kOkZ.y2, resSel_ == 0, str(file, "BUT1"));
  } else {
    box(59, 10, 258, 26, false, screen_ == Screen::Results ? str(file, "TIT" + std::to_string(std::clamp(result_.track, 1, 10) - 1)) : str(file, "TITL"));
    box(kResZ[0].x1, kResZ[0].y1, kResZ[0].x2, kResZ[0].y2, resSel_ == 0, str(file, "BUT1"));
    box(kResZ[1].x1, kResZ[1].y1, kResZ[1].x2, kResZ[1].y2, resSel_ == 1, str(file, "BUT2"));
  }
  int order[10];
  int human = result_.ship;
  if (champ) {
    const auto st = champ_.standings();
    for (int p = 0; p < 10; ++p) order[p] = st[size_t(p)];
    human = champ_.humanShip();
  } else {
    for (int& o : order) o = -1;
    for (int sh = 0; sh < 10; ++sh) order[std::clamp(result_.place[sh] - 1, 0, 9)] = sh;
  }
  for (int p = 0; p < 10; ++p) {
    const int sh = order[p];
    if (sh < 0) continue;
    const Font* f = sh == human ? fb : fa;  // record +2 == 2 is an AI ship (A / C), the human uses B / D
    if (!f) continue;
    const int y = 40 + 13 * p;
    drawText(*f, std::to_string(p + 1) + ".", 20, y, -1);
    drawText(*f, pilotName(sh), 40, y, -1);
    char t[32];
    if (champ) std::snprintf(t, sizeof t, "%d", champ_.driver(sh).points);
    else {
      const int cs = int(result_.time[sh] * 100 + 0.5);
      std::snprintf(t, sizeof t, "%02d'%02d\"%02d", cs / 6000, (cs / 100) % 60, cs % 100);
    }
    drawText(*f, t, champ ? 250 : 240, y, -1);
  }
}

void FrontEnd::drawNotice() {
  drawMain();
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.darken(0, 0, W - 1, H - 1, 50);
  if (const Font* f = font("CNFFONT.FNT")) drawText(*f, notice_, 160 - f->textWidth(notice_) / 2, 95, -1);
}

}  // namespace slip
