// Championship glue of the front end: the race calendar loop (0x559D8 mode 0x13), the standings and the saved games screen (0x53536 / 0x53318, RES_GAME).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "game/frontend.hpp"
#include "game/hud.hpp"

namespace slip {

namespace {
constexpr int W = 320, H = 200;
uint32_t argb(uint32_t rgb) { return 0xff000000u | (rgb & 0xffffffu); }
std::string saveDir() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/Library/Application Support/Slipstream";
}
}  // namespace

// ------------------------------------------------------------------------------------------------------------------ calendar
void FrontEnd::champStart() {
  champ_.start(viewShip_, difficulty_);
  setup_.ship = viewShip_;
  champBeginRace();
}

void FrontEnd::champBeginRace() {
  setup_.track = champ_.track();
  setup_.championship = true;
  if (audio_) audio_->stopMusic();  // 0x55E3C: the menu music ends; the reporters talk without music, the fly-through has a race song
  startReporters(0);                 // 0x573B7, then the fly-through (0x57A79), the reporters again, the garage
}

// Results -> 0x422EC (records), 0x561CF (points and prize money) -> 0x5623E (positions) or, after the last race, 0x56FF0 (final positions).
void FrontEnd::champRaceFinished() {
  std::array<int, 10> place{};
  for (int s = 0; s < 10; ++s) place[size_t(s)] = result_.place[s];
  champ_.applyRace(place);
  if (result_.haveRemaining) champ_.driver(champ_.humanShip()).load = result_.remaining;  // the rounds that were fired are gone: the garage shows what is left
  resSel_ = 1;
  go(champ_.lastRace() ? Screen::FinalPos : Screen::ChampPos);
  if (audio_ && screen_ == Screen::ChampPos) audio_->playMusic("WIN.HMP", false);  // 0x5623E
}

// ------------------------------------------------------------------------------------------------------------------ saved games
std::string FrontEnd::slotPath(int slot) { return saveDir() + "/slot" + std::to_string(slot + 1) + ".sav"; }

void FrontEnd::refreshSlots() {
  for (int i = 0; i < 6; ++i) {
    slotName_[i].clear();
    std::ifstream f(slotPath(i));
    std::string name;
    if (f && std::getline(f, name) && !name.empty()) slotName_[i] = name;
  }
}

void FrontEnd::openSlots(bool save) {
  refreshSlots();
  slotSave_ = save;
  slotEntry_ = false;
  slotChosen_ = -1;
  slotHover_ = -1;
  slotAnim_ = 0;
  slotText_.clear();
  slotBack_ = save ? Screen::ChampPos : Screen::Main;
  if (!save) {
    bool any = false;
    for (const auto& n : slotName_) any = any || !n.empty();
    if (!any) { notice_ = str("SAVED.ST0", "NOGA"); go(Screen::Notice); return; }  // "You Haven't Saved Any Games Yet"
  }
  go(Screen::Slots);
}

int FrontEnd::slotZoneAt(int x, int y) {
  const int z = zone("RESGAMEZ.ZON", x, y);
  if (z == 7) return 6;                    // Cancel
  if (z >= 1 && z <= 6) return z - 1;      // the door of the slot
  if (z >= 129 && z <= 134) return z - 129;  // its lamp
  return -1;
}

void FrontEnd::slotsChoose(int slot) {
  if (slot == 6) { go(slotBack_); return; }  // Cancel
  if (!slotSave_ && slotName_[size_t(slot)].empty()) return;
  slotChosen_ = slot;
  slotAnim_ = 0;
}

// The door of the chosen slot has opened: ask for the name (save) or load the game.
void FrontEnd::slotsFinish() {
  const int k = slotChosen_;
  if (k < 0) return;
  if (slotSave_) {
    slotEntry_ = true;
    slotText_ = slotName_[size_t(k)];
    return;
  }
  std::ifstream f(slotPath(k));
  std::string name, rest, line;
  std::getline(f, name);
  while (std::getline(f, line)) rest += line + "\n";
  slotChosen_ = -1;
  if (champ_.deserialize(rest)) {
    setup_.ship = champ_.humanShip();
    viewShip_ = champ_.humanShip();
    difficulty_ = champ_.difficulty();
    mode_ = 2;
    resSel_ = 1;
    go(Screen::ChampPos);  // 0x55D98: the loaded game starts with the positions; Continue starts the next race
    if (audio_) audio_->playMusic("WIN.HMP", false);
  } else {
    notice_ = "The saved game is damaged";
    go(Screen::Notice);
  }
}

void FrontEnd::textInput(const std::string& t) {
  if (!wantsText()) return;
  std::string& s = screen_ == Screen::Best ? records_[std::clamp(bestTrack_, 1, 10)][size_t(bestEdit_)].name : slotText_;
  const size_t max = screen_ == Screen::Best ? 31 : 20;
  for (char c : t) if (c >= 32 && c < 127 && s.size() < max) s += c;
}

void FrontEnd::backspace() {
  if (!wantsText()) return;
  std::string& s = screen_ == Screen::Best ? records_[std::clamp(bestTrack_, 1, 10)][size_t(bestEdit_)].name : slotText_;
  if (!s.empty()) s.pop_back();
}

void FrontEnd::slotsKey(Key k) {
  if (slotChosen_ >= 0 && !slotEntry_) return;  // the door is opening
  if (slotEntry_) {
    if (k == Key::Back) { slotEntry_ = false; slotChosen_ = -1; }
    else if (k == Key::Select && !slotText_.empty()) {
      std::filesystem::create_directories(saveDir());
      std::ofstream f(slotPath(slotChosen_));
      f << slotText_ << '\n' << champ_.serialize();
      slotEntry_ = false;
      slotChosen_ = -1;
      champ_.nextRace();  // 0x536BD: saving returns 0, the championship goes on with the next race
      champBeginRace();
    }
    return;
  }
  // arrow keys walk along the doors in screen order 6 4 2 | 1 3 5, Down goes to Cancel
  static const int kOrder[6] = {5, 3, 1, 0, 2, 4};
  int pos = -1;
  for (int i = 0; i < 6; ++i) if (kOrder[i] == slotHover_) pos = i;
  if (k == Key::Left) slotHover_ = pos < 0 ? kOrder[0] : kOrder[(pos + 5) % 6];
  else if (k == Key::Right) slotHover_ = pos < 0 ? kOrder[0] : kOrder[(pos + 1) % 6];
  else if (k == Key::Down) slotHover_ = 6;
  else if (k == Key::Up) slotHover_ = slotHover_ == 6 ? kOrder[3] : slotHover_;
  else if (k == Key::Select) { if (slotHover_ >= 0) slotsChoose(slotHover_); }
  else if (k == Key::Back) go(slotBack_);
}

void FrontEnd::drawSlots() {
  const Sprite* bg = spr("RES_GAME.SPR");
  if (!bg) return;
  if (bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  c.blit(*bg, 0, 0, -1);
  auto sprite = [&](const std::string& n) { if (const Sprite* s = spr(n)) c.blit(*s, s->hdr4, s->hdr6, -1); };
  const int hot = slotChosen_ >= 0 ? slotChosen_ : slotHover_;
  if (hot >= 0 && hot < 6) sprite("RES_GL" + std::to_string(hot + 1) + ".SPR");  // the lamp of the door under the pointer
  if (slotChosen_ >= 0) sprite("RES_G" + std::to_string(std::clamp(int(slotAnim_ * 20.0), 0, 7)) + std::to_string(slotChosen_ + 1) + ".SPR");  // the door opens
  if (const Font* f = font("SMALL.FNT")) {
    auto centre = [&](const std::string& s, int x1, int x2, int y, int idx) { drawText(*f, s, x1 + (x2 - x1 + 1 - f->textWidth(s)) / 2, y, idx); };
    centre(slotEntry_ ? str("SAVED.ST0", "ENTR") : str("SAVED.ST0", slotSave_ ? "CHS1" : "CHSE"), 88, 230, 58, 0xF0);
    if (slotEntry_) centre(slotText_ + ((int(t_ * 2) & 1) ? "_" : " "), 88, 230, 70, 0xF0);
    else if (hot >= 0 && hot < 6) centre(slotName_[size_t(hot)].empty() ? "[Unused Slot]" : slotName_[size_t(hot)], 88, 230, 70, 0xF0);
    centre(str("SAVED.ST0", "CANC"), 119, 200, 95, slotHover_ == 6 ? 0xFF : 0xF0);
  }
}

}  // namespace slip
