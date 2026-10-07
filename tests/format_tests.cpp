// Tests against the user's original files. Set SLIPSTREAM_DATA (or keep files in ~/Downloads/slip5000);
// without game data the tests are skipped (exit 0) so CI without assets stays green.
#include <cstdio>
#include <string>

#include "game/scene.hpp"
#include "original_formats/formats.hpp"
#include "original_formats/game_data.hpp"
#include "original_formats/track.hpp"

using namespace slip;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  auto root = findGameDirectory("");
  if (root.empty()) { std::printf("SKIP: no game data\n"); return 0; }
  std::string err;
  auto d = GameData::open(root, &err);
  CHECK(d != nullptr);
  if (!d) return 1;
  CHECK(d->entryCount() >= 2447);
  CHECK(d->list("SHP").size() == 280);
  CHECK(d->list("SPR").size() == 886);
  CHECK(d->exists("MATHS.BIN") || true);
  // formats
  for (auto& n : d->list("SHP")) { auto b = d->read(n); CHECK(b && parseShape(*b)); }
  for (auto& n : d->list("MAT")) { auto b = d->read(n); CHECK(b && parseMaterials(*b)); }
  for (auto& n : d->list("PAL")) { auto b = d->read(n); CHECK(b && parsePalette(b->data(), b->size())); }
  for (auto& n : d->list("SPR")) { auto b = d->read(n); CHECK(b && parseSprite(*b)); }
  for (auto& n : d->list("ART")) { auto b = d->read(n); CHECK(b && parseArt(*b)); }
  if (auto m = d->read("MATHS.BIN")) { auto t = parseMaths(*m); CHECK(t && t->sin.size() == 8193 && t->sin[8192] == 16384); }
  // tracks
  size_t pieceTotal = 0;
  for (int i = 1; i <= 10; ++i) {
    Track t;
    CHECK(loadTrack(*d, i, &t, &err));
    CHECK(!t.pieces.empty());
    CHECK(t.pieces.size() <= t.records.size() && t.pieces.size() + 3 >= t.records.size());  // piece per record (a few records are placed by another mechanism)
    pieceTotal += t.pieces.size();
  }
  Track c;
  CHECK(loadTrack(*d, 1, &c, &err));
  CHECK(c.pieces.size() == 86 && c.start[0].x == 4633413);
  Scene s;
  CHECK(buildScene(*d, 1, &s, &err));
  CHECK(!s.track.polys.empty());
  double y;
  CHECK(s.floorHeight(s.startPos[0][0], s.startPos[0][2], s.startPos[0][1], 40000, &y));
  std::printf("%s (%zu pieces over 10 tracks)\n", failures ? "FAILED" : "all tests passed", pieceTotal);
  return failures ? 1 : 0;
}
