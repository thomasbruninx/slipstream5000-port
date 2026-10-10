#include "game/pause_menu.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

void PauseMenu::load(const GameData& data) {
  if (auto b = data.read("PAUSED.ST0"))
    for (const auto& [tag, text] : parseStringTable(*b))
      if (tag.size() == 4 && tag.compare(0, 3, "OPT") == 0 && tag[3] >= '1' && tag[3] <= '4') optMain_[tag[3] - '1'] = text;
  optMain_[3] = "Exit Game";
  if (auto b = data.read("CONFIG.ST0"))
    for (const auto& [tag, text] : parseStringTable(*b)) {
      if (tag == "TITL") titleConfig_ = text;
      else if (tag.size() == 4 && tag.compare(0, 3, "BUT") == 0 && tag[3] >= '1' && tag[3] <= '6') optConfig_[tag[3] - '1'] = text;
    }
}

std::string PauseMenu::title() const {
  switch (page_) {
    case Page::Main: return "Paused";
    case Page::Config: return titleConfig_;
    case Page::Sound: return optConfig_[5];
    case Page::General: return optConfig_[0];
    case Page::Difficulty: return optConfig_[4];
    case Page::Detail: return optConfig_[2];
    case Page::Effects: return "Effects";
    case Page::Controls: return optConfig_[1];
  }
  return "";
}

std::vector<std::string> PauseMenu::items(const GameSettings& s) const {
  auto pct = [](float v) { return std::to_string(int(std::lround(v * 100))) + "%"; };
  switch (page_) {
    case Page::Main: return {optMain_[0], optMain_[1], optReset_, optMain_[2], optMain_[3]};
    case Page::Config: return {optConfig_[0], optConfig_[1], optConfig_[2], optConfig_[4], optConfig_[5], optConfig_[3]};
    case Page::Sound: return {"Music  " + pct(s.music), "Effects  " + pct(s.sfx), optConfig_[3]};
    case Page::General: return {std::string("Speed  ") + (s.kph ? "kph" : "mph"), std::string("Track map  ") + (s.trackMap ? "on" : "off"), optConfig_[3]};
    case Page::Difficulty: return {std::string("Level (main menu)  ") + std::to_string(s.difficulty) + (s.difficulty == 0 ? " (easy)" : s.difficulty == 1 ? " (normal)" : " (hard)"), optConfig_[3]};
    case Page::Detail:
      return {std::string("Renderer  ") + GameSettings::rendererName(s.renderer), std::string("Fullscreen  ") + (s.fullscreen ? "on" : "off"), std::string("Resolution  ") + GameSettings::resolutionName(s.resolution),
              std::string("Filter  ") + GameSettings::filterName(s.filter), std::string("Anti-aliasing  ") + GameSettings::aaName(s.aa), std::string("Lighting  ") + GameSettings::lightingName(s.lighting),
              std::string("Effects  ") + (s.fx ? "on" : "off"), "More effects...", s.graphicsChanged() ? "Continue (restart to apply)" : optConfig_[3]};
    case Page::Effects:
      return {std::string("Draw distance  ") + GameSettings::distanceName(s.detail), std::string("Post shader  ") + GameSettings::postName(s.postShader), std::string("Ambient occlusion  ") + (s.ao ? "on" : "off"), std::string("Bloom  ") + (s.bloom ? "on" : "off"),
              std::string("Ship shadows  ") + (s.shadows ? "on" : "off"), s.graphicsChanged() ? "Continue (restart to apply)" : optConfig_[3]};
    case Page::Controls:
      return {"Cursor keys  steer / pitch", "Space  accelerate", "Alt  fire    Ctrl  select weapon", "F1 cockpit  F2 chase  F3 rear", "F4 TV  F5 free (keypad)", optConfig_[3]};  // defaults; remap in the main menu
  }
  return {};
}

PauseMenu::Action PauseMenu::key(Key k, GameSettings* s) {
  const auto list = items(*s);
  const int n = int(list.size());
  if (k == Key::Up) { sel_ = (sel_ + n - 1) % n; return Action::None; }
  if (k == Key::Down) { sel_ = (sel_ + 1) % n; return Action::None; }
  if (k == Key::Back) {
    if (page_ == Page::Main) { open_ = false; return Action::Resume; }
    const bool wasEffects = page_ == Page::Effects;
    page_ = wasEffects ? Page::Detail : (page_ == Page::Config) ? Page::Main : Page::Config;
    sel_ = wasEffects ? 7 : 0;
    return Action::None;
  }
  const int dir = k == Key::Left ? -1 : k == Key::Right ? 1 : 0;  // Select acts like "right" on value rows
  auto step = [&](int cur, int lo, int hi) { return std::clamp(cur + (dir == 0 ? 1 : dir), lo, hi); };
  switch (page_) {
    case Page::Main:
      if (k != Key::Select) return Action::None;
      if (sel_ == 0) { open_ = false; return Action::Resume; }
      if (sel_ == 1) { page_ = Page::Config; sel_ = 0; return Action::None; }
      if (sel_ == 2) { open_ = false; return Action::ResetPlayer; }
      if (sel_ == 3) { open_ = false; return Action::QuitRace; }
      open_ = false;
      return Action::ExitGame;
    case Page::Config: {
      if (k != Key::Select) return Action::None;
      static const Page kTargets[6] = {Page::General, Page::Controls, Page::Detail, Page::Difficulty, Page::Sound, Page::Main};
      if (sel_ == 5) { page_ = Page::Main; sel_ = 1; return Action::None; }
      page_ = kTargets[sel_];
      sel_ = 0;
      return Action::None;
    }
    case Page::Sound:
      if (sel_ == 0) s->music = std::clamp(s->music + 0.1f * float(dir == 0 ? 1 : dir), 0.0f, 1.0f);
      else if (sel_ == 1) s->sfx = std::clamp(s->sfx + 0.1f * float(dir == 0 ? 1 : dir), 0.0f, 1.0f);
      else if (k == Key::Select) { page_ = Page::Config; sel_ = 4; }
      return Action::None;
    case Page::General:
      if (sel_ == 0) s->kph = !s->kph;
      else if (sel_ == 1) s->trackMap = !s->trackMap;
      else if (k == Key::Select) { page_ = Page::Config; sel_ = 0; }
      return Action::None;
    case Page::Difficulty:
      // the level can only be changed from the main menu's configuration screen: the original greys the button out during a race (0x47421, [0x47230] = 0)
      if (sel_ != 0 && k == Key::Select) { page_ = Page::Config; sel_ = 3; }
      return Action::None;
    case Page::Detail: {
      auto cyc = [&](int cur, int count) { return (cur + (dir == 0 ? 1 : dir) + count) % count; };
      switch (sel_) {
        case 0: s->renderer = cyc(s->renderer, 2); break;
        case 1: s->fullscreen = !s->fullscreen; break;
        case 2: s->resolution = cyc(s->resolution, GameSettings::kResolutionCount); break;
        case 3: s->filter = cyc(s->filter, 3); break;
        case 4: s->aa = cyc(s->aa, 4); break;
        case 5: s->lighting = cyc(s->lighting, 3); break;
        case 6: s->fx = !s->fx; break;
        case 7: if (k == Key::Select) { page_ = Page::Effects; sel_ = 0; } break;
        default: if (k == Key::Select) { page_ = Page::Config; sel_ = 2; } break;
      }
      return Action::None;
    }
    case Page::Effects: {
      auto cyc = [&](int cur, int count) { return (cur + (dir == 0 ? 1 : dir) + count) % count; };
      switch (sel_) {
        case 0: s->detail = cyc(s->detail, 4); break;
        case 1: s->postShader = cyc(s->postShader, 5); break;
        case 2: s->ao = !s->ao; break;
        case 3: s->bloom = !s->bloom; break;
        case 4: s->shadows = !s->shadows; break;
        default: if (k == Key::Select) { page_ = Page::Detail; sel_ = 7; } break;
      }
      return Action::None;
    }
    case Page::Controls:
      if (k == Key::Select && sel_ == n - 1) { page_ = Page::Config; sel_ = 1; }
      return Action::None;
  }
  return Action::None;
}

// The original's pause popup (0x5A34C / 0x5A3CB): four bevelled buttons directly over the frozen race at x 101..220, y 46..60 / 64..78 / 83..96 / 100..114
// (table 0x5401C), light edges 0x1C (top, left) and dark 0x0C (bottom, right), fill 0x14 (0xFD for the selected one), the PAUSED.ST0 text in the
// default font SMALL.FNT, colour 0xFF, centred. The configuration pages are the port's own (the original opens a full screen menu), drawn the same way.
int PauseMenu::itemAt(int x, int y, const GameSettings& s) const {
  if (!open_) return -1;
  const auto list = items(s);
  const int x0 = 101, x1 = 220, pitch = 18, h = 15;
  const int y0 = list.size() > 5 ? 34 : 46;
  if (x < x0 || x > x1) return -1;
  for (size_t i = 0; i < list.size(); ++i) {
    static const int kY[4] = {46, 64, 83, 100};
    const int yy = list.size() <= 4 ? kY[i] : y0 + int(i) * pitch;
    if (y >= yy && y < yy + h) return int(i);
  }
  return -1;
}

void PauseMenu::draw(const HudCanvas& c, const HudAssets& a, const GameSettings& s) const {
  if (!open_) return;
  const auto list = items(s);
  const Font& f = a.small.height > 0 ? a.small : a.time;
  const int x0 = 101, x1 = 220, pitch = 18, h = 15;
  const int y0 = list.size() > 5 ? 34 : 46;
  for (size_t i = 0; i < list.size(); ++i) {
    static const int kY[4] = {46, 64, 83, 100};  // table 0x5401C
    const int y = list.size() <= 4 ? kY[i] : y0 + int(i) * pitch;
    const bool sel = int(i) == sel_;
    c.fillIndex(x0, y, x1, y + h - 1, sel ? 0xFD : 0x14);
    c.fillIndex(x0, y, x1, y, 0x1C); c.fillIndex(x0, y, x0, y + h - 1, 0x1C);
    c.fillIndex(x0, y + h - 1, x1, y + h - 1, 0x0C); c.fillIndex(x1, y, x1, y + h - 1, 0x0C);
    c.textCentered(f, list[i], x0, x1, y + (h - f.height) / 2 + 1, 0xFF);
  }
}

}  // namespace slip
