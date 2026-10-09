// GDV decoder against the user's INTRO.GDV / LOGO_S.GDV (skipped without game data). SLIP_GDV_DUMP=N,path writes frame N of INTRO.GDV as a PPM
// (used to compare with an independent decoder).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "original_formats/game_data.hpp"
#include "original_formats/formats.hpp"
#include "original_formats/gdv.hpp"

using namespace slip;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static std::vector<uint8_t> slurp(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

int main() {
  auto root = findGameDirectory("");
  if (root.empty()) { std::printf("SKIP: no game data\n"); return 0; }
  for (const char* name : {"INTRO.GDV", "LOGO_S.GDV"}) {
    auto data = slurp(root / name);
    if (data.empty()) { std::printf("SKIP %s\n", name); continue; }
    GdvDecoder g;
    std::string err;
    CHECK(g.open(std::move(data), &err));
    if (std::getenv("SLIP_GDV_PRELOAD") && std::string(name) == "LOGO_S.GDV") {  // experiment: the movie codes changes against SOFTLOGO.SPR
      std::string e;
      auto gd = GameData::open(root, &e);
      auto sb = gd->read("SOFTLOGO.SPR");
      auto sp = sb ? parseSprite(*sb) : std::nullopt;
      if (sp) { g.preload(sp->pixels.data(), sp->pixels.size()); g.setSize(320, 200); for (int i = 0; i < 256 && sp->palette; ++i) {} }
    }
    std::optional<Palette> alt;
    if (std::getenv("SLIP_GDV_PRELOAD") && std::string(name) == "LOGO_S.GDV") { auto gd = GameData::open(root, &err); auto sb = gd->read("SOFTLOGO.SPR"); auto sp = sb ? parseSprite(*sb) : std::nullopt; if (sp) alt = sp->palette; }
    int n = 0;
    std::vector<float> pcm;
    const char* dump = std::getenv("SLIP_GDV_DUMP");
    int dumpFrame = -1;
    std::string dumpPath;
    if (dump && std::string(name) == (std::getenv("SLIP_GDV_FILE") ? std::getenv("SLIP_GDV_FILE") : "INTRO.GDV")) { dumpFrame = std::atoi(dump); const char* c = std::strchr(dump, ','); if (c) dumpPath = c + 1; }
    std::ofstream all;
    if (const char* ap = std::getenv("SLIP_GDV_ALL"); ap && std::string(name) == "INTRO.GDV") all.open(ap, std::ios::binary);
    while (g.next()) {
      if (all.is_open()) for (int i = 0; i < g.width() * g.height(); ++i) { const uint32_t c = g.palette()[g.pixels()[i]]; all.put(char(c >> 16)); all.put(char(c >> 8)); all.put(char(c)); }
      g.appendAudio(&pcm);
      if (std::getenv("SLIP_GDV_TRACE")) std::printf("frame %d wrote %zu\n", n, g.lastWritten());
      if (n == dumpFrame && !dumpPath.empty()) {
        std::ofstream o(dumpPath, std::ios::binary);
        o << "P6\n" << g.width() << " " << g.height() << "\n255\n";
        for (int i = 0; i < g.width() * g.height(); ++i) { const uint32_t c = alt ? alt->rgba[g.pixels()[i]] : g.palette()[g.pixels()[i]]; o.put(char(c >> 16)); o.put(char(c >> 8)); o.put(char(c)); }
      }
      ++n;
    }
    std::printf("%s: %dx%d, %d of %d frames, %d fps, %zu audio samples at %d Hz, %d half-size frames\n", name, g.width(), g.height(), n, g.frameCount(), g.fps(), pcm.size(), g.audioRate(), g.unsupportedFrames());
    CHECK(n == g.frameCount());
    CHECK(pcm.size() == size_t(g.frameCount()) * size_t(g.audioRate() / g.fps()));  // no stray bytes between the audio chunks (they were audible as clicks)
  }
  {  // one decoder object reused for both movies (the front end does that): the logo's quirks must not leak into the intro
    GdvDecoder g;
    std::string e;
    auto l = slurp(root / "LOGO_S.GDV"), i = slurp(root / "INTRO.GDV");
    GdvDecoder ref;
    if (!l.empty() && !i.empty() && g.open(l, &e) && g.next() && g.open(i, &e) && ref.open(i, &e)) {
      for (int k = 0; k < 60; ++k) { CHECK(g.next() && ref.next()); CHECK(std::equal(g.pixels(), g.pixels() + g.width() * g.height(), ref.pixels())); }
    }
  }
  std::printf(failures ? "gdv: %d failure(s)\n" : "gdv ok\n", failures);
  return failures ? 1 : 0;
}
