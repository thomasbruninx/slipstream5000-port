// .ANN script parser against the user's game files (skipped without them): all 32 scripts parse, face programs only use ops 8 / 6 / 5, every voice tag has a sample and
// a string, the reporter tables of the first track.
#include <cstdio>

#include "original_formats/ann.hpp"
#include "original_formats/game_data.hpp"

using namespace slip;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  auto root = findGameDirectory("");
  if (root.empty()) { std::printf("SKIP: no game data\n"); return 0; }
  std::string err;
  auto d = GameData::open(root, &err);
  if (!d) { std::printf("SKIP: %s\n", err.c_str()); return 0; }
  int n = 0;
  for (const std::string& name : d->list("ANN")) {
    auto b = d->read(name);
    CHECK(b.has_value());
    auto a = parseAnn(*b);
    CHECK(a.has_value());
    if (!a) continue;
    ++n;
    CHECK(a->cmds.back().op == AnnCmd::Op::End);
    for (const AnnCmd& c : a->cmds)
      if (c.op == AnnCmd::Op::Face) {
        size_t p = 0;
        while (p + 2 <= c.face.size()) {  // ops: 8 (wait), 6 (set layer), 5 (end)
          const int op = c.face[p] | (c.face[p + 1] << 8);
          CHECK(op == 8 || op == 6 || op == 5);
          p += op == 6 ? 6 : op == 8 ? 4 : 2;
        }
        CHECK(p == c.face.size());
      } else if (c.op == AnnCmd::Op::Voice && a->name.compare(0, 6, "GARAGE") != 0 && a->name.compare(0, 7, "DEMOEND") != 0) {  // those two scripts (DLG / DLE lines) have no samples in this edition
        CHECK(d->exists("E" + c.tag.substr(1) + ".SMP"));
      }
  }
  CHECK(n == 32);
  auto a = parseAnn(*d->read("CHIINT.ANN"));
  CHECK(a && a->reporter == 0 && a->name.substr(0, 6) == "CHIINT" && a->cmds.size() == 11);
  auto f = parseAnn(*d->read("COLINT.ANN"));
  CHECK(f && f->reporter != 0);  // the female reporter introduces the Arizona (Colorado) track
  std::printf(failures ? "ann tests: %d FAILED\n" : "ann tests: ok (%d scripts)\n", failures ? failures : n);
  return failures ? 1 : 0;
}
