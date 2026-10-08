#include "game/hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace slip {

namespace {
bool loadSprite(const GameData& d, const std::string& name, Sprite* out) {
  auto b = d.read(name);
  if (!b) return false;
  auto s = parseSprite(*b);
  if (!s) return false;
  *out = std::move(*s);
  return true;
}
bool loadFont(const GameData& d, const std::string& name, Font* out) {
  auto b = d.read(name);
  if (!b) return false;
  auto f = parseFont(*b);
  if (!f) return false;
  *out = std::move(*f);
  return true;
}
int transparentOf(const Sprite& s) { return s.hdr8 == 0xFFFF ? -1 : int(s.hdr8 & 0xFF); }
}  // namespace

bool HudAssets::load(const GameData& d, int ship, const Palette& pal) {
  palette = pal;
  bool ok = true;
  ok &= loadFont(d, "TIME.FNT", &time);
  ok &= loadFont(d, "SPD.FNT", &spd);
  ok &= loadFont(d, "SMALLEST.FNT", &smallest);
  loadFont(d, "SMALL.FNT", &small);
  loadFont(d, "SHADE.FNT", &shade);
  for (int i = 0; i < 10; ++i) loadSprite(d, "GAMEF" + std::to_string(i) + ".SPR", &portrait[size_t(i)]);
  loadFont(d, "MENUFONT.FNT", &menu);
  for (int i = 0; i < 10; ++i) ok &= loadSprite(d, "POS" + std::to_string(i) + ".SPR", &pos[size_t(i)]);
  ok &= loadSprite(d, "NSIGHT.SPR", &nsight);
  ok &= loadSprite(d, "TSIGHT0.SPR", &tsight[0]);
  ok &= loadSprite(d, "TSIGHT1.SPR", &tsight[1]);
  ok &= loadSprite(d, "TURBO_N.SPR", &turbo);
  ok &= loadSprite(d, "CONS_EXT.SPR", &consExt);
  const std::string c = "CON" + std::to_string(std::clamp(ship, 0, 9)) + "_";
  for (int f = 0; f < 2; ++f) {
    ok &= loadSprite(d, c + "TN" + std::to_string(f + 1) + ".SPR", &consTop[f]);
    ok &= loadSprite(d, c + "BN" + std::to_string(f + 1) + ".SPR", &consBottom[f]);
  }
  loaded = ok;
  return ok;
}

// ---------------------------------------------------------------------------------------------------------------- canvas
static inline uint32_t argb(uint32_t rgb) { return 0xff000000u | (rgb & 0xffffffu); }

void HudCanvas::fill(int x0, int y0, int x1, int y1, uint32_t color) const {
  const int px0 = std::clamp(int(std::floor(x0 * sx())), 0, w), px1 = std::clamp(int(std::floor((x1 + 1) * sx())), 0, w);
  const int py0 = std::clamp(int(std::floor(y0 * sy())), 0, h), py1 = std::clamp(int(std::floor((y1 + 1) * sy())), 0, h);
  for (int y = py0; y < py1; ++y)
    for (int x = px0; x < px1; ++x) fb[size_t(y) * size_t(w) + size_t(x)] = color;
}

void HudCanvas::fillIndex(int x0, int y0, int x1, int y1, int idx) const { fill(x0, y0, x1, y1, argb(pal->rgba[size_t(idx & 255)])); }

void HudCanvas::darken(int x0, int y0, int x1, int y1, int percent) const {
  const int px0 = std::clamp(int(std::floor(x0 * sx())), 0, w), px1 = std::clamp(int(std::floor((x1 + 1) * sx())), 0, w);
  const int py0 = std::clamp(int(std::floor(y0 * sy())), 0, h), py1 = std::clamp(int(std::floor((y1 + 1) * sy())), 0, h);
  for (int y = py0; y < py1; ++y)
    for (int x = px0; x < px1; ++x) {
      uint32_t& p = fb[size_t(y) * size_t(w) + size_t(x)];
      const uint32_t r = ((p >> 16) & 255) * uint32_t(100 - percent) / 100, g = ((p >> 8) & 255) * uint32_t(100 - percent) / 100, b = (p & 255) * uint32_t(100 - percent) / 100;
      p = 0xff000000u | (r << 16) | (g << 8) | b;
    }
}

void HudCanvas::blitScaled(const Sprite& s, int x, int y, int tw, int th, int transparent) const {
  if (s.w <= 0 || s.h <= 0) return;
  const Palette& pl = s.palette ? *s.palette : *pal;
  const int px0 = std::max(0, int(std::floor(x * sx()))), px1 = std::min(w, int(std::floor((x + tw) * sx())));
  const int py0 = std::max(0, int(std::floor(y * sy()))), py1 = std::min(h, int(std::floor((y + th) * sy())));
  for (int py = py0; py < py1; ++py) {
    const int v = std::clamp(int((double(py) + 0.5) / sy() - y) * s.h / th, 0, s.h - 1);
    for (int px = px0; px < px1; ++px) {
      const int u = std::clamp(int((double(px) + 0.5) / sx() - x) * s.w / tw, 0, s.w - 1);
      const uint8_t idx = s.pixels[size_t(v) * size_t(s.w) + size_t(u)];
      if (int(idx) == transparent) continue;
      fb[size_t(py) * size_t(w) + size_t(px)] = argb(pl.rgba[idx]);
    }
  }
}

void HudCanvas::blit(const Sprite& s, int x, int y, int transparent) const { blitScaled(s, x, y, s.w, s.h, transparent); }

void HudCanvas::text(const Font& f, const std::string& str, int x, int y, int palIndex) const {
  const uint32_t col = argb(pal->rgba[size_t(palIndex & 255)]);
  int pen = x;
  for (unsigned char ch : str) {
    const uint8_t* bm = f.bitmap(ch);
    if (bm) {
      for (int gy = 0; gy < f.height; ++gy)
        for (int gx = 0; gx < f.cellW; ++gx)
          if (bm[gy * f.cellW + gx]) fill(pen + gx, y + gy, pen + gx, y + gy, col);
    }
    pen += f.advance(ch);
  }
}

void HudCanvas::textCentered(const Font& f, const std::string& str, int x0, int x1, int y, int palIndex) const {
  text(f, str, x0 + (x1 - x0 + 1 - f.textWidth(str)) / 2, y, palIndex);
}

// -------------------------------------------------------------------------------------------------------------------- HUD
void Hud::viewport(int w, int h, int cx, int cy, int* x0, int* y0, int* x1, int* y1, float* pcx, float* pcy) {
  const double sx = double(w) / 320.0, sy = double(h) / 200.0;
  *x0 = int(std::floor(HudLayout::vx0 * sx));
  *y0 = int(std::floor(HudLayout::vy0 * sy));
  *x1 = int(std::floor((HudLayout::vx1 + 1) * sx)) - 1;
  *y1 = int(std::floor((HudLayout::vy1 + 1) * sy)) - 1;
  *pcx = float(cx * sx);
  *pcy = float(cy * sy);
}

namespace {
// 0x572F2: damage bar, 3 rows: outer rows 0x33, middle row 0x39, filled part = value percent of the width.
void damageBar(const HudCanvas& c, int x0, int x1, int y, double value) {
  if (value <= 0) return;
  int end = x1;
  if (value < 100) end = x0 + int(double(x1 - x0 + 1) * value / 100.0);
  c.fillIndex(x0, y, end, y, 0x33);
  c.fillIndex(x0, y + 2, end, y + 2, 0x33);
  c.fillIndex(x0, y + 1, end, y + 1, 0x39);
}
// 0x5733B / 0x57394: energy bar, 3 rows; filled part 0x85 / 0xFC (0x78 / 0xFC when full), the rest 0x33 / 0x39.
void energyBar(const HudCanvas& c, int x0, int x1, int y, double energy) {
  auto rows = [&](int a, int b, int outer, int mid) {
    if (b < a) return;
    c.fillIndex(a, y, b, y, outer);
    c.fillIndex(a, y + 2, b, y + 2, outer);
    c.fillIndex(a, y + 1, b, y + 1, mid);
  };
  if (energy >= 1.0) { rows(x0, x1, 0x78, 0xFC); return; }
  if (energy <= 0.0) { rows(x0, x1, 0x33, 0x39); return; }
  const int end = x0 + int(double(x1 - x0 + 1) * energy);
  rows(x0, end, 0x85, 0xFC);
  rows(end, x1, 0x33, 0x39);
}
std::string timeText(double t) {  // 0x17DB4 / 0x453B8: "M:SS;cc" (the ':' and ';' glyphs of TIME.FNT are the separators)
  t = std::max(0.0, t);
  const int cs = int((t - std::floor(t)) * 100.0), s = int(t) % 60, m = std::min(9, int(t) / 60);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d:%02d;%02d", m, s, cs);
  return buf;
}
}  // namespace

void Hud::draw(uint32_t* fb, int w, int h, const HudState& st, const HudAssets& a) const {
  HudCanvas c;
  c.fb = fb; c.w = w; c.h = h; c.pal = &a.palette;
  // frame around the 3D window (0x44458..0x444D5): colour 7
  c.fillIndex(0, 0, 319, 7, 7);
  c.fillIndex(0, 8, 3, 192, 7);
  c.fillIndex(316, 8, 319, 192, 7);
  c.fillIndex(0, 193, 319, 199, 7);
  const int frame = std::clamp(st.consoleFrame, 1, 2) - 1;
  if (st.cockpit) {
    c.blit(a.consTop[frame], 4, 152, transparentOf(a.consTop[frame]));      // TN: top strip over the bottom of the window
    c.blit(a.consBottom[frame], 4, 167, transparentOf(a.consBottom[frame]));
  } else {
    c.fillIndex(0, 167, 319, 167, 0);
    c.blit(a.consExt, 4, 168, transparentOf(a.consExt));
  }
  const int yBar = st.cockpit ? 184 : 185;   // 0x44557: 0xB8 / 0xB9
  damageBar(c, 24, 67, yBar, st.damageA);     // ENGINE DAMAGE
  damageBar(c, 252, 295, yBar, st.damageB);   // CONTROL DAMAGE
  // weapon panel (0x5C12D / 0x5BE65): SMALLEST.FNT, centred between x 128 and 191
  const int dy = st.cockpit ? 0 : 7;
  if (st.selected == 3) {
    c.textCentered(a.smallest, "Turbo", 129, 191, 169 + dy, 255);
    energyBar(c, 137, 182, 175 + dy, st.boosterFuel);
  } else if (!st.weaponName.empty()) {
    std::string name = st.weaponName;
    if (st.ammo >= 0) name += "[" + std::to_string(std::min(st.ammo, 9)) + "]";
    c.textCentered(a.smallest, name, 128, 191, 168 + dy + (st.ammo < 0 ? 1 : 0), 255);
    if (st.ammo >= 0) c.textCentered(a.smallest, st.energy >= 1.0 ? "READY" : "LOADING", 128, 191, 174 + dy, 255);
    else energyBar(c, 137, 182, 175 + dy, st.energy);
  }
  if (st.booster) c.blit(a.turbo, 194, 168 + (st.cockpit ? 0 : 5), transparentOf(a.turbo));
  // speed (0x453B8): SPD.FNT, green, top left of the window; the unit glyph follows the number
  {
    const int v = int(st.speed / (st.kph ? 444.0 : 715.0));
    c.text(a.spd, std::to_string(v) + (st.kph ? ";" : ":"), 14, 14, 0xFC);
  }
  // position sprite (top right), lap time, lap number
  if (st.rank >= 1 && st.rank <= 10) c.blit(a.pos[size_t(st.rank - 1)], 289, 12, transparentOf(a.pos[size_t(st.rank - 1)]));
  c.text(a.time, timeText(st.lapTime), 255, 14, 0xFE);
  if (st.lastLapTime >= 0) c.text(a.time, timeText(st.lastLapTime), 255, 32, 0xFE);
  if (st.lap > 0) {
    char b[16];
    std::snprintf(b, sizeof b, "=%2d", st.lap);
    c.textCentered(a.time, b, 253, 291, 23, 0xFE);
  }
  // messages: TIME.FNT centred in the window, 5 pixels below its top edge
  if (st.countdown > 0) c.textCentered(a.time, "Prepare to Race " + std::to_string(st.countdown), 0, 319, 13, 0xFE);
  if (st.finalLapTimer > 0) c.textCentered(a.time, "Final Lap!!", 0, 319, 13, 0xFF);
  if (st.gameOverTimer > 0) c.textCentered(a.time, "GAME OVER", 0, 319, 13, 0xFF);
  if (st.finishedPosition > 0) c.textCentered(a.time, "Finished Position " + std::to_string(st.finishedPosition), 0, 319, 13, 0xFE);
  // talking pilot (0x58BEA..0x58C6F): the pilot's GAMEF face at (260, 40) while his voice line plays, his rank (or FINISHED) in SMALL.FNT, yellow,
  // centred under the top of the face's lower edge (y 79)
  if (st.portraitPilot >= 1 && st.portraitPilot <= 10 && a.portrait[size_t(st.portraitPilot - 1)].w > 0) {
    const Sprite& f = a.portrait[size_t(st.portraitPilot - 1)];
    c.blit(f, 260, 40, transparentOf(f));
    if (a.small.height > 0) c.textCentered(a.small, st.portraitText, 260, 311, 79, 0xFE);
  }
  // sights (0x5C234): crosshair for lock capable weapons, flashing marker on the locked ship
  if (st.canLock) {
    if (st.cockpit) c.blit(a.nsight, st.centerX - 14, st.centerY - 11, transparentOf(a.nsight));  // the crosshair only makes sense along the nose: hidden in the chase views
    if (st.lockVisible) {
      const int k = (int(st.time * 12.0) & 1);
      c.blit(a.tsight[k], int(st.lockX) - 6, int(st.lockY) - 6, transparentOf(a.tsight[k]));
    }
  }
}

}  // namespace slip
