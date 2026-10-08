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
static const double kTrackLatLon[10][2] = {{41.9, -87.6}, {21.3, -157.9}, {35.7, 139.7}, {60.5, 8.5}, {46.2, 2.2}, {36.1, -112.1}, {-3.1, -60.0}, {51.5, -0.1}, {30.0, 31.1}, {40.7, -74.0}};

void FrontEnd::usePalette(const Palette& p) {  // every screen's palette gets the executable's static UI colours in 248..255 (VideoSetPalette 0x557C7)
  pal_ = p;
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
  for (const char* n : {"MAINMENU.ST0", "CH_TEAM.ST0", "VIEWCAR.ST0", "GARAGE.ST0", "BESTDRV.ST0", "CHTRACK.ST0", "GENERAL.ST0", "RACERES.ST0"})
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

void FrontEnd::go(Screen s) {
  if (movieVoice_ && audio_) { audio_->stopVoice(movieVoice_); movieVoice_ = 0; }
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
  if (screen_ == Screen::Tracks) {  // the globe turns to the selected track
    const double kPi = 3.14159265358979323846;
    const int tr = std::clamp(sel_, 0, 9);
    const double lat = kTrackLatLon[tr][0] * kPi / 180, lon = (kTrackLatLon[tr][1] + 185.0) * kPi / 180;
    double dy = lon - gYaw_;
    while (dy > kPi) dy -= 2 * kPi;
    while (dy < -kPi) dy += 2 * kPi;
    const double k = std::min(1.0, dt * 4.0);
    gYaw_ += dy * k;
    gTilt_ += (-lat + 0.45 - gTilt_) * k;
  }
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
      for (int i = 1; i <= 6; ++i) add("MAINBT_" + std::to_string(i) + ".SPR", "MAINBTH" + std::to_string(i) + ".SPR", i <= 5 ? str("MAINMENU.ST0", "OPT" + std::to_string(i)) : "Exit Game", i);
      break;
    case Screen::OnePlayer:
      for (int i = 1; i <= 3; ++i) add("MAINBT_" + std::to_string(i) + ".SPR", "MAINBTH" + std::to_string(i) + ".SPR", str("MAINMENU.ST0", "OP1" + std::to_string(i)), i, i != 3);
      add("MAINBT_6.SPR", "MAINBTH6.SPR", "Back", 6);
      break;
    case Screen::Tracks:
      for (int i = 0; i < 10; ++i) {
        std::string n = str("BESTDRV.ST0", "BU" + std::to_string(i) + "1");  // "Fastest Laps - Chicago"
        if (const size_t d = n.find(" - "); d != std::string::npos) n = n.substr(d + 3);
        add("TRKBT_" + std::to_string(i) + ".SPR", "TRKBTH" + std::to_string(i) + ".SPR", n, i);
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
  if (screen_ == Screen::Team) {
    hoverShip_ = x < 0 ? -1 : zone("CH_TEAMZ.ZON", x, y) - 1;
    return;
  }
  const int h = hit(x, y);
  if (h >= 0 && btns_[size_t(h)].enabled) sel_ = h;
}

void FrontEnd::click(int x, int y) {
  mouseMove(x, y);
  if (screen_ <= Screen::Credits) { skipIntro(); return; }
  if (screen_ == Screen::Garage) { garageClick(x, y); return; }
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
    case Screen::Tracks:
      if (k == Key::Up) move(-1);
      else if (k == Key::Down) move(1);
      else if (k == Key::Select && n) activate(btns_[size_t(sel_)].id);
      else if (k == Key::Back) { if (screen_ == Screen::Main) quit_ = true; else go(screen_ == Screen::Tracks ? Screen::OnePlayer : Screen::Main); }
      break;
    case Screen::Team:
      if (k == Key::Left || k == Key::Up) hoverShip_ = (hoverShip_ + 9) % 10;
      else if (k == Key::Right || k == Key::Down) hoverShip_ = (hoverShip_ + 1) % 10;
      else if (k == Key::Select) { viewShip_ = std::clamp(hoverShip_, 0, 9); go(Screen::ViewCar); }
      else if (k == Key::Back) go(Screen::Tracks);
      break;
    case Screen::ViewCar:
      if (k == Key::Left) viewShip_ = (viewShip_ + 9) % 10;
      else if (k == Key::Right) viewShip_ = (viewShip_ + 1) % 10;
      else if (k == Key::Select) { setup_.ship = viewShip_; go(Screen::Garage); }
      else if (k == Key::Back) { hoverShip_ = viewShip_; go(Screen::Team); }
      break;
    case Screen::Garage: garageKey(k); break;
    case Screen::Best:
      if (k == Key::Left) bestTrack_ = (bestTrack_ + 8) % 10 + 1;
      else if (k == Key::Right) bestTrack_ = bestTrack_ % 10 + 1;
      else if (k == Key::Select || k == Key::Back) go(Screen::Main);
      break;
    case Screen::Results:
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
  if (screen_ == Screen::Main) {
    if (id == 1) go(Screen::OnePlayer);
    else if (id == 5) go(Screen::Best);
    else if (id == 6) quit_ = true;
    else if (id == 2) net_ = true;  // the original's link menu (split screen / serial / modem / network) is replaced by the port's network game
    else { notice_ = id == 3 ? "Saved games are not available" : "Use the pause menu during a race to configure the game"; go(Screen::Notice); }
  } else if (screen_ == Screen::OnePlayer) {
    if (id == 6) go(Screen::Main);
    else { mode_ = id - 1; go(Screen::Tracks); }
  } else if (screen_ == Screen::Tracks) {
    setup_.track = id + 1;
    hoverShip_ = setup_.ship;
    go(Screen::Team);
    hoverShip_ = setup_.ship;
  }
}

void FrontEnd::draw() {
  if (!ready_) return;
  switch (screen_) {
    case Screen::Logo: case Screen::Intro: drawIntro(); break;
    case Screen::Gremlin: drawGremlin(); break;
    case Screen::Credits: drawCredits(); break;
    case Screen::Main: case Screen::OnePlayer: drawMain(); break;
    case Screen::Tracks: drawTracks(); break;
    case Screen::Team: drawTeam(); break;
    case Screen::ViewCar: drawViewCar(); break;
    case Screen::Garage: drawGarage(); break;
    case Screen::Best: drawBest(); break;
    case Screen::Results: drawResults(); break;
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
    if (!b.label.empty())
      if (const Font* f = font("MENUFONT.FNT")) drawText(*f, b.label, b.x + (b.w - f->textWidth(b.label)) / 2, b.y + (b.h - f->height) / 2, -1);
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
  const Sprite* bg = spr(screen_ == Screen::Main || screen_ == Screen::OnePlayer ? "MAINMENU.SPR" : "TITLE.SPR");
  if (!bg) return;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  drawButtons(&pal_);
}

static void mul3(const double a[9], const double b[9], double o[9]) {
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) o[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}

void FrontEnd::drawTracks() {
  const Sprite* bg = spr("STARS.SPR");
  if (bg && bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  if (bg) c.blit(*bg, 0, 0, -1);
  if (globe_.loaded()) {  // GLOBE.SHP with GLOBE.MAT (Earth1..4 = EARTH1..4.SPR); the globe turns to the selected track and plants a flag on it
    const double kPi = 3.14159265358979323846;
    const int tr = std::clamp(sel_, 0, 9);
    const double lat = kTrackLatLon[tr][0] * kPi / 180, lon = (kTrackLatLon[tr][1] + 185.0) * kPi / 180;
    const double ca = std::cos(gYaw_), sa = std::sin(gYaw_), ct = std::cos(gTilt_), st = std::sin(gTilt_);
    const double ry[9] = {ca, 0, sa, 0, 1, 0, -sa, 0, ca}, rx[9] = {1, 0, 0, 0, ct, -st, 0, st, ct};
    double rot[9];
    mul3(rx, ry, rot);
    globe_.draw(buf_.data(), W, H, pal_, 88, 100, 5900, rot, 0, true);
    const double dir[3] = {std::cos(lat) * std::sin(lon), std::sin(lat), -std::cos(lat) * std::cos(lon)};
    globe_.drawFlag(buf_.data(), W, H, pal_, 88, 100, 5900, rot, dir);
  }
  if (const Sprite* t = spr("CH_TRACK.SPR")) {
    c.blit(*t, t->hdr4, t->hdr6, transparentOf(*t));
    if (const Font* f = font("MENUFONT.FNT")) { const std::string ti = str("CHTRACK.ST0", "TITL"); drawText(*f, ti, t->hdr4 + (t->w - f->textWidth(ti)) / 2, t->hdr6 + (t->h - f->height) / 2, -1); }
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

void FrontEnd::drawViewCar() {
  drawTeam();
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.darken(0, 0, W - 1, H - 1, 55);
  const bool portrait = int(t_ / 4) % 2 == 1;  // the panel alternates between the information card and the pilot's portrait
  if (const Sprite* s = spr((portrait ? "DRIVER" : "VIEWCAR") + std::to_string(viewShip_) + ".SPR")) {
    c.blit(*s, s->hdr4, s->hdr6, -1);
    if (!portrait)
      if (const Font* f = font("VIEWDESC.FNT")) wrapText(*f, str("VIEWCAR.ST0", "CAR" + std::to_string(viewShip_)), s->hdr4 + 10, s->hdr6 + 30, s->w - 20, -1, 2);
  }
  if (const Font* f = font("TEAMFONT.FNT")) {
    const std::string cancel = up(str("CH_TEAM.ST0", "OPT2")), accept = up(str("CH_TEAM.ST0", "OPT1"));
    drawText(*f, cancel, 60, 190, -1);
    drawText(*f, accept, 260 - f->textWidth(accept), 190, -1);
  }
}

// ------------------------------------------------------------------------------------------------------------------ garage
// The garage: the hangar with the chosen craft (GARAGE<n>A), the four-slot panel GARBOX4 (Weapons, Turbo, Systems, Start Race, GARAGE.ST0 BUT1..4),
// the pod window GARBOX1 (left / right weapon), the weapon grid GARBOX2 (the 11 purchasable weapons in table order) and the turbo grid GARBOX3
// (the 5 booster items). Prices come from the executable's tables; the starting money and the "Systems" upgrades are the port's own (SPECULATIVE).
constexpr int kFastRechargePrice = 1000, kWideLockPrice = 1200;

int FrontEnd::credits() const {
  int c = startCredits_;
  for (int p = 0; p < 2; ++p) if (podW_[p] > 0 && table_) c -= table_->w[size_t(podW_[p])].price[std::clamp(difficulty_, 0, 2)];
  if (booster_ >= 0 && table_) c -= table_->boosters[size_t(booster_)].price;
  if (fastRecharge_) c -= kFastRechargePrice;
  if (wideLock_) c -= kWideLockPrice;
  return c;
}

void FrontEnd::garageEnter() {
  garSel_ = 0; garPage_ = 0; pod_ = 0; grid_ = 0;
  podW_[0] = setup_.loadout.weaponA; podW_[1] = setup_.loadout.weaponB;
  booster_ = setup_.loadout.booster;
  fastRecharge_ = setup_.loadout.fastRecharge; wideLock_ = setup_.loadout.wideLock;
}

void FrontEnd::garageBuildLoadout() {
  Loadout l;
  l.weaponA = podW_[0]; l.weaponB = podW_[1];
  l.ammoA = podW_[0] > 0 && table_ ? table_->w[size_t(podW_[0])].pack : 0;
  l.ammoB = podW_[1] > 0 && table_ ? table_->w[size_t(podW_[1])].pack : 0;
  l.booster = booster_;
  l.fastRecharge = fastRecharge_; l.wideLock = wideLock_;
  setup_.loadout = l;
}

bool FrontEnd::garageCell(int page, int i, int* x, int* y, int* w, int* h) const {
  if (page == 0) { *x = 23 + int(i * 68.3); *y = 111; *w = 63; *h = 55; return i < 4; }                     // GARBOX4 slots
  if (page == 2) { *x = 23 + int((i % 4) * 67.7); *y = 98 + int((i / 4) * 30.5); *w = 62; *h = 25; return i < 11; }  // GARBOX2 weapons 3 x 4
  if (page == 3) { *x = 24 + (i % 3) * 90; *y = 101 + int((i / 3) * 43.5); *w = 82; *h = 36; return i < 5; }        // GARBOX3 boosters 2 x 3
  if (page == 1) { *x = 137 + 16 + (i % 2) * 69; *y = 83 + 24; *w = 63; *h = 27; return i < 2; }                    // GARBOX1 pod windows
  return false;
}

const Sprite* FrontEnd::cropIcon(int weapon) {  // the weapon picture of the grid cell, cut out of GARBOX2 (also used in the pod windows)
  auto it = icons_.find(weapon);
  if (it != icons_.end()) return &it->second;
  const Sprite* box = spr("GARBOX2.SPR");
  if (!box || weapon < 1 || weapon > 11) return nullptr;
  int x, y, w, h;
  garageCell(2, weapon - 1, &x, &y, &w, &h);
  Sprite ic;
  ic.w = w; ic.h = h;
  ic.pixels.assign(size_t(w) * size_t(h), 0);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
      const int sx = x + i - box->hdr4, sy = y + j - box->hdr6;
      if (sx >= 0 && sx < box->w && sy >= 0 && sy < box->h) ic.pixels[size_t(j) * size_t(w) + size_t(i)] = box->pixels[size_t(sy) * size_t(box->w) + size_t(sx)];
    }
  return &icons_.emplace(weapon, std::move(ic)).first->second;
}

void FrontEnd::drawRect(int x0, int y0, int x1, int y1, int idx) {
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  for (int t = 0; t < 2; ++t) {
    c.fillIndex(x0 - t, y0 - t, x1 + t, y0 - t, idx); c.fillIndex(x0 - t, y1 + t, x1 + t, y1 + t, idx);
    c.fillIndex(x0 - t, y0 - t, x0 - t, y1 + t, idx); c.fillIndex(x1 + t, y0 - t, x1 + t, y1 + t, idx);
  }
}

void FrontEnd::garageKey(Key k) {
  const int n = garPage_ == 0 ? 4 : garPage_ == 1 ? 2 : garPage_ == 2 ? 11 : garPage_ == 3 ? 5 : 2;
  const int cols = garPage_ == 2 ? 4 : garPage_ == 3 ? 3 : garPage_ == 1 ? 2 : garPage_ == 0 ? 4 : 1;
  int& sel = garPage_ == 0 ? garSel_ : garPage_ == 1 ? pod_ : grid_;
  if (k == Key::Left) sel = (sel + n - 1) % n;
  else if (k == Key::Right) sel = (sel + 1) % n;
  else if (k == Key::Up) sel = sel - cols >= 0 ? sel - cols : sel;
  else if (k == Key::Down) sel = sel + cols < n ? sel + cols : sel;
  else if (k == Key::Back) {
    if (garPage_ == 0) { go(Screen::ViewCar); }
    else if (garPage_ == 2) garPage_ = 1;
    else garPage_ = 0;
  } else if (k == Key::Select) {
    const int price = [&] { return 0; }();
    (void)price;
    if (garPage_ == 0) {
      if (garSel_ == 0) { garPage_ = 1; pod_ = 0; }
      else if (garSel_ == 1) { garPage_ = 3; grid_ = std::max(0, booster_); }
      else if (garSel_ == 2) { garPage_ = 4; grid_ = 0; }
      else { garageBuildLoadout(); race_ = true; }
    } else if (garPage_ == 1) { garPage_ = 2; grid_ = podW_[pod_] > 0 ? podW_[pod_] - 1 : 0; }
    else if (garPage_ == 2) {  // buy the weapon for this pod (selecting the same one again sells it back)
      const int wid = grid_ + 1;
      if (podW_[pod_] == wid) podW_[pod_] = -1;
      else {
        const int old = podW_[pod_];
        podW_[pod_] = wid;
        if (credits() < 0) podW_[pod_] = old;
      }
      garPage_ = 1;
    } else if (garPage_ == 3) {
      if (booster_ == grid_) booster_ = -1;
      else { const int old = booster_; booster_ = grid_; if (credits() < 0) booster_ = old; }
      garPage_ = 0;
    } else if (garPage_ == 4) {
      bool& f = grid_ == 0 ? fastRecharge_ : wideLock_;
      f = !f;
      if (credits() < 0) f = !f;
    }
  }
}

void FrontEnd::garageClick(int x, int y) {
  for (int i = 0; i < (garPage_ == 0 ? 4 : garPage_ == 1 ? 2 : garPage_ == 2 ? 11 : garPage_ == 3 ? 5 : 0); ++i) {
    int cx, cy, cw, ch;
    if (!garageCell(garPage_, i, &cx, &cy, &cw, &ch)) continue;
    if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) {
      (garPage_ == 0 ? garSel_ : garPage_ == 1 ? pod_ : grid_) = i;
      garageKey(Key::Select);
      return;
    }
  }
  if (garPage_ == 4) {
    const int row = (y - 105) / 20;
    if (row >= 0 && row < 2 && x > 30 && x < 290) { grid_ = row; garageKey(Key::Select); }
  }
}

void FrontEnd::drawGarage() {
  const Sprite* bg = spr("GARAGE" + std::to_string(setup_.ship) + (garPage_ == 0 && garSel_ == 3 ? "B" : "A") + ".SPR");  // B: the craft is lifted off for the start
  if (!bg) bg = spr("GARAGEA.SPR");
  if (!bg) return;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  const Font* f = font("GARWEAP.FNT");
  const int bright = brightIndex(true);
  auto text = [&](const std::string& s, int cx, int y) { if (f) drawText(*f, s, cx - f->textWidth(s) / 2, y, -1); };
  const char* kPanel[4] = {"BUT1", "BUT2", "BUT3", "BUT4"};
  if (garPage_ <= 1 || garPage_ == 4) {
    if (const Sprite* box = spr("GARBOX4.SPR")) c.blit(*box, box->hdr4, box->hdr6, transparentOf(*box));
    for (int i = 0; i < 4; ++i) {
      int x, y, w, h;
      garageCell(0, i, &x, &y, &w, &h);
      if (i == 3) text(str("GARAGE.ST0", kPanel[3]), x + w / 2, y + h / 2 - 4);
      else if (garPage_ == 0 && i == garSel_) {}
      if (garPage_ == 0 && i == garSel_) drawRect(x, y, x + w - 1, y + h - 1, bright);
      if (i < 3) text(str("GARAGE.ST0", kPanel[i]), x + w / 2, y + h - 11);
    }
  }
  if (garPage_ == 1 || garPage_ == 2) {
    if (const Sprite* box = spr("GARBOX1.SPR")) c.blit(*box, box->hdr4, box->hdr6, transparentOf(*box));
    text(str("GARAGE.ST0", "WEP0"), 137 + 81, 83 + 12);
    for (int p = 0; p < 2; ++p) {
      int x, y, w, h;
      garageCell(1, p, &x, &y, &w, &h);
      if (podW_[p] > 0) if (const Sprite* ic = cropIcon(podW_[p])) c.blit(*ic, x + 1, y + 1, -1);
      text(str("GARAGE.ST0", p == 0 ? "WEP1" : "WEP2"), x + w / 2, y + h + 4);
      if (garPage_ == 1 && p == pod_) drawRect(x, y, x + w - 1, y + h - 1, bright);
    }
  }
  if (garPage_ == 2) {
    if (const Sprite* box = spr("GARBOX2.SPR")) c.blit(*box, box->hdr4, box->hdr6, transparentOf(*box));
    int x, y, w, h;
    garageCell(2, grid_, &x, &y, &w, &h);
    drawRect(x, y, x + w - 1, y + h - 1, bright);
  }
  if (garPage_ == 3) {
    if (const Sprite* box = spr("GARBOX3.SPR")) c.blit(*box, box->hdr4, box->hdr6, transparentOf(*box));
    int x, y, w, h;
    garageCell(3, grid_, &x, &y, &w, &h);
    drawRect(x, y, x + w - 1, y + h - 1, bright);
  }
  if (garPage_ == 4) {
    if (const Sprite* box = spr("GARBOX3.SPR")) c.blit(*box, box->hdr4, box->hdr6, transparentOf(*box));
    c.fillIndex(20, 100, 299, 175, 0);
    const char* names[2] = {"Fast recharge", "Wide lock-on"};
    const int prices[2] = {kFastRechargePrice, kWideLockPrice};
    const bool on[2] = {fastRecharge_, wideLock_};
    for (int i = 0; i < 2; ++i) {
      if (i == grid_) c.fillIndex(30, 105 + i * 20 - 2, 289, 105 + i * 20 + 14, brightIndex(false) / 2);
      text(std::string(names[i]) + "  " + std::to_string(prices[i]) + (on[i] ? "  FITTED" : ""), 160, 105 + i * 20);
    }
  }
  // information strip: what is under the selection, and the money left
  std::string info;
  if (table_) {
    if (garPage_ == 2) { const auto& w = table_->w[size_t(grid_ + 1)]; info = w.name + "  " + std::to_string(w.price[std::clamp(difficulty_, 0, 2)]) + "  x" + std::to_string(w.pack); }
    else if (garPage_ == 3) { const auto& b = table_->boosters[size_t(grid_)]; info = b.name + "  " + std::to_string(b.price); }
  }
  c.darken(0, 0, W - 1, 17, 55);
  if (f) {
    drawText(*f, info, 8, 4, -1);
    const std::string cr = "Credits " + std::to_string(credits());
    drawText(*f, cr, W - 8 - f->textWidth(cr), 4, -1);
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
  go(Screen::Results);
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

void FrontEnd::drawResults() {
  const Sprite* bg = spr("RACERES.SPR");
  if (!bg) return;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  c.darken(10, 22, W - 11, 178, 45);
  const Font* f = font("RESULTS.FNT");
  if (!f) f = font("TEAMFONT.FNT");
  const Font* sm = font("SMALL.FNT");
  const int bright = brightIndex(true);
  const std::string title = str("RACERES.ST0", "TIT" + std::to_string(result_.track - 1));
  if (f) drawText(*f, title, 160 - f->textWidth(title) / 2, 6, -1);
  int order[10];
  for (int s = 0; s < 10; ++s) order[std::clamp(result_.place[s] - 1, 0, 9)] = s;
  for (int p = 0; p < 10; ++p) {
    const int s = order[p], col = p / 5, row = p % 5;
    const int x = 16 + col * 150, y = 28 + row * 30;
    if (const Sprite* fc = spr("BESTF" + std::to_string(s) + ".SPR")) { HudCanvas fcv = c; fcv.pal = facePalette(); fcv.blitScaled(*fc, x + 18, y, 32, 28, transparentOf(*fc)); }
    if (sm) {
      drawText(*sm, std::to_string(p + 1), x, y + 10, -1);
      drawText(*sm, timeText(result_.time[s]) + (result_.projected[s] ? " *" : ""), x + 56, y + 10, -1);
    }
  }
  if (f) { const std::string ok = str("RACERES.ST0", "BUT2"); drawText(*f, ok, 160 - f->textWidth(ok) / 2, 184, -1); }
}

void FrontEnd::drawNotice() {
  drawMain();
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.darken(0, 0, W - 1, H - 1, 50);
  if (const Font* f = font("CNFFONT.FNT")) drawText(*f, notice_, 160 - f->textWidth(notice_) / 2, 95, -1);
}

}  // namespace slip
