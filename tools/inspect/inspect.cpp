// slipstream_inspect: dump information about original assets (read-only).
#include <cstdio>
#include <cstring>
#include <string>

#include "original_formats/formats.hpp"
#include "original_formats/game_data.hpp"
#include "original_formats/track.hpp"

using namespace slip;
int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "";
  auto root = findGameDirectory(dir);
  std::string err;
  auto data = GameData::open(root, &err);
  if (!data) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
  std::printf("data dir: %s, %zu entries, archives:", root.string().c_str(), data->entryCount());
  for (auto& a : data->archiveNames()) std::printf(" %s", a.c_str());
  std::printf("\n");
  for (int i = 1; i <= 10; ++i) {
    Track t;
    if (!loadTrack(*data, i, &t, &err)) { std::printf("track %d: %s\n", i, err.c_str()); continue; }
    size_t polys = 0;
    for (auto& r : t.records) polys += r.polys.size();
    std::printf("track %2d %-8s records %3zu pieces %3zu polys %5zu scenery %2zu materials %3zu\n", i, t.name.c_str(),
                t.records.size(), t.pieces.size(), polys, t.scenery.size(), t.materials.mats.size());
  }
  return 0;
}
