// The configuration screens of the main menu (ConfigMenu 0x472FB and its pages General 0x4796A, Controls 0x4827E, Detail 0x475D2, Sound 0x47CCA, Difficulty 0x47FEE):
// the generic hangar GARAGEA with 18 pixel buttons cut from the dark copy GARAGEB (the hovered one shows the bright picture), the text in CNFFONT. Rows are a wide
// button (x 25..281, 20 pixel pitch from y 43) whose value is centred while the category name stands on its left (x 31); Ok at (100,169)-(195,187). CONFIRMED layout.
// The level / damage values live in the executable's CFG; the port keeps them in settings_file. Controls lists the fixed keys of the port.
#include <algorithm>

#include "game/frontend.hpp"
#include "game/hud.hpp"

namespace slip {

namespace {
constexpr int W = 320, H = 200;
uint32_t argb(uint32_t rgb) { return 0xff000000u | (rgb & 0xffffffu); }
struct Z { int x1, y1, x2, y2; };
constexpr Z kTitleZ = {100, 10, 220, 28}, kOkZ = {100, 169, 195, 187};
// main page (table 0x4754C): title, General, Controls, Detail, Continue, Difficulty, Sound
constexpr Z kMainZ[6] = {{25, 43, 157, 61}, {163, 43, 281, 61}, {25, 65, 157, 83}, {113, 171, 207, 189}, {163, 65, 281, 83}, {25, 87, 157, 105}};
const char* const kMainTag[6] = {"BUT1", "BUT2", "BUT3", "BUT4", "BUT5", "BUT6"};
}  // namespace

// rows of a page: the category tag, its current value and what a step does (dir = +1 / -1)
struct FrontEnd::CfgRow {
  std::string tag;
  std::string value;
  std::function<void(int)> step;
};

std::vector<FrontEnd::CfgRow> FrontEnd::cfgRows() {
  GameSettings& s = *cfg_;
  auto onOff = [&](const std::string& file, bool v) { return text(file, v ? "OFF1" : "OFF0"); };
  auto flip = [](bool* b) { return [b](int) { *b = !*b; }; };
  std::vector<CfgRow> r;
  switch (cfgPage_) {
    case CfgPage::General:
      r.push_back({"CAT1", onOff("GENERAL.ST0", s.rearMonitor), flip(&s.rearMonitor)});
      r.push_back({"CAT2", onOff("GENERAL.ST0", s.weaponsMonitor), flip(&s.weaponsMonitor)});
      r.push_back({"CAT3", "English", [](int) {}});  // the port has the English texts and voices only
      r.push_back({"CAT4", onOff("GENERAL.ST0", s.trackMap), flip(&s.trackMap)});
      r.push_back({"CAT5", s.kph ? "km/h" : "mph", flip(&s.kph)});
      break;
    case CfgPage::Detail:
      r.push_back({"CAT1", std::to_string(s.detail + 1), [&s](int d) { s.detail = std::clamp(s.detail + d, 0, 3); }});
      r.push_back({"CAT2", onOff("DETAIL.ST0", s.clouds), flip(&s.clouds)});
      r.push_back({"CAT3", text("DETAIL.ST0", "SHA" + std::to_string(s.shading)), [&s](int d) { s.shading = (s.shading + d + 3) % 3; }});
      r.push_back({"CAT4", text("DETAIL.ST0", s.texturesCoarse ? "TEXC" : "TEXF"), flip(&s.texturesCoarse)});
      r.push_back({"CAT5", text("DETAIL.ST0", s.windowReduced ? "WIN1" : "WIN0"), flip(&s.windowReduced)});
      r.push_back({"CAT6", onOff("DETAIL.ST0", s.shadows), flip(&s.shadows)});
      break;
    case CfgPage::Difficulty:
      r.push_back({"CAT1", text("DIFF.ST0", "LEV" + std::to_string(s.difficulty)), [&s](int d) { s.difficulty = (s.difficulty + d + 3) % 3; }});
      r.push_back({"CAT2", onOff("DIFF.ST0", s.damage), flip(&s.damage)});
      break;
    case CfgPage::Controls:
      r.push_back({"REVA", onOff("CONTROLS.ST0", s.keys.reverseAccel), flip(&s.keys.reverseAccel)});
      r.push_back({"PLY1", "", [this](int) { cfgPage_ = CfgPage::Keys; cfgHov_ = -1; }});  // "Player 1 Controls": the key page
      r.push_back({"PAD", "", [this](int) { cfgPage_ = CfgPage::Pad; cfgHov_ = -1; }});     // the controller page (the original's joystick pages are not ported)
      break;
    case CfgPage::Pad: {
      static const char* kNames[kPadActions] = {"Accelerate", "Brake", "Fire", "Select weapon", "Camera", "Pause"};
      for (int a = 0; a < kPadActions; ++a) {
        const int code = cfg_->keys.pad[size_t(a)];
        const std::string nm = padWait_ && keyKeep_ == a ? "..." : code ? (padName_ ? padName_(code) : std::to_string(code)) : "-";
        r.push_back({"", std::string(kNames[a]) + ": " + nm, [this, a](int) { padWait_ = true; keyKeep_ = a; }});
      }
      r.push_back({"INVP", cfg_->keys.padInvertPitch ? "Invert pitch: On" : "Invert pitch: Off", flip(&cfg_->keys.padInvertPitch)});
      break;
    }
    case CfgPage::Keys:
      for (int a = 0; a < kKeyActions; ++a) {
        const int sc = cfg_->keys.sc[size_t(a)];
        std::string label = a < 7 ? text("CONTROLS.ST0", "DEF" + std::to_string(a)) : std::string("Brake: %s");
        const std::string nm = keyKeep_ == a && keyWait_ ? "..." : sc ? (keyName_ ? keyName_(sc) : std::to_string(sc)) : "-";
        const size_t p = label.find("%s");
        if (p != std::string::npos) label.replace(p, 2, nm);
        r.push_back({"", label, [this, a](int) { keyWait_ = true; keyKeep_ = a; }});
      }
      break;
    case CfgPage::Sound:
      r.push_back({"CAT1", onOff("SOUND.ST0", s.sfxOn), flip(&s.sfxOn)});
      r.push_back({"CAT2", text("SOUND.ST0", "ENG" + std::to_string(s.engine)), [&s](int d) { s.engine = (s.engine + d + 3) % 3; }});
      r.push_back({"CAT3", onOff("SOUND.ST0", s.speech), flip(&s.speech)});
      r.push_back({"CAT4", onOff("SOUND.ST0", s.musicOn), flip(&s.musicOn)});
      break;
    default: break;
  }
  return r;
}

const char* FrontEnd::cfgFile() const {
  switch (cfgPage_) {
    case CfgPage::General: return "GENERAL.ST0";
    case CfgPage::Controls: case CfgPage::Keys: case CfgPage::Pad: return "CONTROLS.ST0";
    case CfgPage::Detail: return "DETAIL.ST0";
    case CfgPage::Difficulty: return "DIFF.ST0";
    case CfgPage::Sound: return "SOUND.ST0";
    default: return "CONFIG.ST0";
  }
}

void FrontEnd::openConfig() {
  if (!cfg_) cfg_ = &cfgLocal_;
  cfgPage_ = CfgPage::Main;
  cfgHov_ = -1;
  go(Screen::Config);
}

// vertical extent of row i of the current page: the key page packs eight rows
static void rowSpan(bool keys, int i, int* y1, int* y2) { *y1 = keys ? 32 + 17 * i : 43 + 20 * i; *y2 = *y1 + (keys ? 14 : 16); }  // the key and controller pages pack their rows

// zone index under (x, y): rows 0.., then the Ok button (index = rows), -1 for none; on the main page 0..5 are the six buttons
int FrontEnd::cfgZoneAt(int x, int y) {
  auto in = [&](const Z& z) { return x >= z.x1 && x <= z.x2 && y >= z.y1 && y <= z.y2; };
  if (cfgPage_ == CfgPage::Main) {
    for (int i = 0; i < 6; ++i) if (in(kMainZ[i])) return i;
    return -1;
  }
  const int n = int(cfgRows().size());
  for (int i = 0; i < n; ++i) { int y1, y2; rowSpan(cfgPage_ == CfgPage::Keys || cfgPage_ == CfgPage::Pad, i, &y1, &y2); if (in(Z{25, y1, 281, y2})) return i; }
  return in(kOkZ) ? n : -1;
}

void FrontEnd::cfgActivate(int i, int dir) {
  if (i < 0) return;
  if (cfgPage_ == CfgPage::Main) {
    static const CfgPage kTarget[6] = {CfgPage::General, CfgPage::Controls, CfgPage::Detail, CfgPage::Main, CfgPage::Difficulty, CfgPage::Sound};
    if (i == 3) { cfgChanged_ = true; go(Screen::Main); return; }  // Continue
    cfgPage_ = kTarget[i];
    cfgHov_ = -1;
    return;
  }
  const int n = int(cfgRows().size());
  if (i >= n) {  // Ok: back (the key page to the controls page, the others to the main page)
    cfgPage_ = cfgPage_ == CfgPage::Keys || cfgPage_ == CfgPage::Pad ? CfgPage::Controls : CfgPage::Main;
    cfgHov_ = -1;
    return;
  }
  cfgRows()[size_t(i)].step(dir);
  cfgChanged_ = true;
}

void FrontEnd::cfgKey(Key k) {
  const int n = cfgPage_ == CfgPage::Main ? 6 : int(cfgRows().size()) + 1;
  if (keyWait_ || padWait_) { keyWait_ = padWait_ = false; return; }  // Esc cancels the capture (a pressed key / button arrives through rawKey / rawPad)
  if (k == Key::Back) {
    if (cfgPage_ == CfgPage::Main) { cfgChanged_ = true; go(Screen::Main); }
    else { cfgPage_ = cfgPage_ == CfgPage::Keys || cfgPage_ == CfgPage::Pad ? CfgPage::Controls : CfgPage::Main; cfgHov_ = -1; }
  } else if (k == Key::Up) cfgHov_ = cfgHov_ <= 0 ? n - 1 : cfgHov_ - 1;
  else if (k == Key::Down) cfgHov_ = cfgHov_ < 0 || cfgHov_ >= n - 1 ? 0 : cfgHov_ + 1;
  else if (k == Key::Left) { if (cfgPage_ == CfgPage::Main) { if (cfgHov_ == 1 || cfgHov_ == 4) cfgHov_ -= 1; else if (cfgHov_ < 0) cfgHov_ = 0; } else cfgActivate(cfgHov_, -1); }
  else if (k == Key::Right) { if (cfgPage_ == CfgPage::Main) { if (cfgHov_ == 0 || cfgHov_ == 2) cfgHov_ += 1; else if (cfgHov_ < 0) cfgHov_ = 0; } else cfgActivate(cfgHov_, 1); }
  else if (k == Key::Select) cfgActivate(cfgHov_, 1);
}

void FrontEnd::frameButton(int x1, int y1, int x2, int y2, const Sprite* src, const std::string& label) {  // sub_5609F with CNFFONT
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.fillIndex(x1, y1, x1, y2, 0x25);
  c.fillIndex(x1, y1, x2, y1, 0x2b);
  c.fillIndex(x1, y2, x2, y2, 0x0a);
  c.fillIndex(x2, y1, x2, y2, 0x0a);
  for (int y = y1 + 1; y < y2; ++y)
    for (int x = x1 + 1; x < x2; ++x) buf_[size_t(y) * W + size_t(x)] = argb(pal_.rgba[src->pixels[size_t(y) * size_t(src->w) + size_t(x)]]);
  if (const Font* f = font("CNFFONT.FNT")) drawText(*f, label, x1 + (x2 - x1 + 1 - f->textWidth(label)) / 2, y1 + 5, -1);
}

void FrontEnd::drawConfig() {
  const Sprite* A = spr("GARAGEA.SPR");
  const Sprite* B = spr("GARAGEB.SPR");
  if (!A) return;
  if (!B) B = A;
  if (A->palette) usePalette(*A->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*A, 0, 0, -1);
  const std::string file = cfgFile();
  frameButton(kTitleZ.x1, kTitleZ.y1, kTitleZ.x2, kTitleZ.y2, B, text(file, "TITL"));
  const Font* f = font("CNFFONT.FNT");
  if (cfgPage_ == CfgPage::Main) {
    for (int i = 0; i < 6; ++i) frameButton(kMainZ[i].x1, kMainZ[i].y1, kMainZ[i].x2, kMainZ[i].y2, cfgHov_ == i ? A : B, text("CONFIG.ST0", kMainTag[i]));
    return;
  }
  const auto rows = cfgRows();
  const bool keysPage = cfgPage_ == CfgPage::Keys || cfgPage_ == CfgPage::Pad;
  for (size_t i = 0; i < rows.size(); ++i) {
    int y, y2;
    rowSpan(keysPage, int(i), &y, &y2);
    frameButton(25, y, 281, y2, cfgHov_ == int(i) ? A : B, "");
    if (!f) continue;
    if (keysPage) drawText(*f, rows[i].value, 25 + (257 - f->textWidth(rows[i].value)) / 2, y + 1, -1);   // "Up: Left Arrow"
    else if (rows[i].tag == "PLY1" || rows[i].tag == "PAD") { const std::string t = rows[i].tag == "PAD" ? std::string("Controller") : text(file, "PLY1"); drawText(*f, t, 25 + (257 - f->textWidth(t)) / 2, y + 3, -1); }
    else {
      drawText(*f, text(file, rows[i].tag), 31, y + 3, -1);
      drawText(*f, rows[i].value, 165 + (281 - 165 + 1 - f->textWidth(rows[i].value)) / 2, y + 5, -1);  // the value in the right part: long category names would run into it
    }
  }
  frameButton(kOkZ.x1, kOkZ.y1, kOkZ.x2, kOkZ.y2, cfgHov_ == int(rows.size()) ? A : B, text(file, "BUT1"));
}

// a key was pressed while a key row waits for it: bind it (a key that another action uses is swapped over)
void FrontEnd::rawKey(int sc) {
  if (!keyWait_ || screen_ != Screen::Config) return;
  keyWait_ = false;
  if (keyKeep_ < 0 || keyKeep_ >= kKeyActions || !cfg_) return;
  auto& m = cfg_->keys.sc;
  if (sc != 0)
    for (int a = 0; a < kKeyActions; ++a)
      if (a != keyKeep_ && m[size_t(a)] == sc) m[size_t(a)] = m[size_t(keyKeep_)];
  m[size_t(keyKeep_)] = sc;
  cfgChanged_ = true;
}

}  // namespace slip

namespace slip {
// a controller button (or trigger) was pressed while a controller row waits for it: bind it (a code another action uses is swapped over)
void FrontEnd::rawPad(int code) {
  if (!padWait_ || screen_ != Screen::Config) return;
  padWait_ = false;
  if (keyKeep_ < 0 || keyKeep_ >= kPadActions || !cfg_) return;
  auto& m = cfg_->keys.pad;
  for (int a = 0; a < kPadActions; ++a)
    if (a != keyKeep_ && m[size_t(a)] == code) m[size_t(a)] = m[size_t(keyKeep_)];
  m[size_t(keyKeep_)] = code;
  cfgChanged_ = true;
}
}  // namespace slip
