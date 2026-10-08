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
    case Page::Controls: return optConfig_[1];
  }
  return "";
}

std::vector<std::string> PauseMenu::items(const GameSettings& s) const {
  auto pct = [](float v) { return std::to_string(int(std::lround(v * 100))) + "%"; };
  switch (page_) {
    case Page::Main: return {optMain_[0], optMain_[1], optMain_[2], optMain_[3]};
    case Page::Config: return {optConfig_[0], optConfig_[1], optConfig_[2], optConfig_[4], optConfig_[5], optConfig_[3]};
    case Page::Sound: return {"Music  " + pct(s.music), "Effects  " + pct(s.sfx), optConfig_[3]};
    case Page::General: return {std::string("Speed  ") + (s.kph ? "kph" : "mph"), std::string("Track map  ") + (s.trackMap ? "on" : "off"), optConfig_[3]};
    case Page::Difficulty: return {std::string("Level  ") + std::to_string(s.difficulty) + (s.difficulty == 0 ? " (easy)" : s.difficulty == 1 ? " (normal)" : " (hard)"), optConfig_[3]};
    case Page::Detail: return {"Detail  " + std::to_string(s.detail), optConfig_[3]};
    case Page::Controls:
      return {"W S  throttle  brake", "A D  steer", "E Q  pitch", "F  fire    X  next weapon", "V  view    Esc  pause", optConfig_[3]};
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
    page_ = (page_ == Page::Config) ? Page::Main : Page::Config;
    sel_ = 0;
    return Action::None;
  }
  const int dir = k == Key::Left ? -1 : k == Key::Right ? 1 : 0;  // Select acts like "right" on value rows
  auto step = [&](int cur, int lo, int hi) { return std::clamp(cur + (dir == 0 ? 1 : dir), lo, hi); };
  switch (page_) {
    case Page::Main:
      if (k != Key::Select) return Action::None;
      if (sel_ == 0) { open_ = false; return Action::Resume; }
      if (sel_ == 1) { page_ = Page::Config; sel_ = 0; return Action::None; }
      if (sel_ == 2) { open_ = false; return Action::QuitRace; }
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
      if (sel_ == 0) s->difficulty = dir == 0 ? (s->difficulty + 1) % 3 : step(s->difficulty, 0, 2);
      else if (k == Key::Select) { page_ = Page::Config; sel_ = 3; }
      return Action::None;
    case Page::Detail:
      if (sel_ == 0) s->detail = dir == 0 ? (s->detail + 1) % 4 : step(s->detail, 0, 3);
      else if (k == Key::Select) { page_ = Page::Config; sel_ = 2; }
      return Action::None;
    case Page::Controls:
      if (k == Key::Select && sel_ == n - 1) { page_ = Page::Config; sel_ = 1; }
      return Action::None;
  }
  return Action::None;
}

void PauseMenu::draw(const HudCanvas& c, const HudAssets& a, const GameSettings& s) const {
  if (!open_) return;
  c.darken(0, 0, 319, 199, 55);
  const auto list = items(s);
  const Font& f = a.menu.height > 0 ? a.menu : a.time;
  const int rowH = f.height + 5;
  const int boxH = int(list.size()) * rowH + f.height + 22;
  int boxW = f.textWidth(title());
  for (const auto& it : list) boxW = std::max(boxW, f.textWidth(it));
  boxW += 40;
  const int x0 = 160 - boxW / 2, y0 = 100 - boxH / 2;
  c.fillIndex(x0 - 2, y0 - 2, x0 + boxW + 1, y0 + boxH + 1, 7);   // frame colour of the HUD borders
  c.fillIndex(x0, y0, x0 + boxW - 1, y0 + boxH - 1, 0);
  c.textCentered(f, title(), x0, x0 + boxW - 1, y0 + 6, 0xFE);
  int y = y0 + 6 + f.height + 10;
  for (size_t i = 0; i < list.size(); ++i, y += rowH) {
    const bool sel = int(i) == sel_;
    if (sel) c.fillIndex(x0 + 6, y - 2, x0 + boxW - 7, y + f.height + 1, 0x33);
    c.textCentered(f, list[i], x0, x0 + boxW - 1, y, sel ? 0xFF : 0xFB);
  }
}

}  // namespace slip
