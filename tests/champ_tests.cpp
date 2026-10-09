// Championship rules (game/championship): calendar, points, prize money, ranking with ties, reversed grid, save round trip.
#include <cstdio>

#include "game/championship.hpp"

using namespace slip;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  CHECK(champRaceCount(0) == 6 && champRaceCount(1) == 8 && champRaceCount(2) == 10);
  Championship c;
  c.start(3, 1);
  CHECK(c.raceNumber() == 1 && c.track() == 6 && c.raceCount() == 8 && !c.lastRace());
  CHECK(c.driver(3).human && c.driver(3).money == 750 && c.driver(3).points == 0);
  auto g = c.grid();
  for (int s = 0; s < 10; ++s) CHECK(g[size_t(s)] == s);
  // race 1: ship s finishes in place 10 - s (ship 9 wins)
  std::array<int, 10> place{};
  for (int s = 0; s < 10; ++s) place[size_t(s)] = 10 - s;
  c.applyRace(place);
  CHECK(c.driver(9).points == 10 && c.driver(9).money == 750 + 650 && c.driver(9).rank == 1);
  CHECK(c.driver(8).points == 6 && c.driver(4).points == 1 && c.driver(3).points == 0 && c.driver(0).money == 750);
  CHECK(c.standings()[0] == 9 && c.standings()[1] == 8);
  // reversed grid: the winner starts last
  c.nextRace();
  g = c.grid();
  CHECK(c.track() == 1);
  CHECK(g[9] == 9 && g[0] == 0);  // ship s finished in place 10 - s, so it starts in slot s
  // ties: ships 3..0 all have 0 points; the later roster entry ranks higher (roster = human 3 first, then 0,1,2,4,...): ship 2 before ship 1 before 0 before the human
  CHECK(c.driver(2).rank < c.driver(1).rank && c.driver(1).rank < c.driver(0).rank && c.driver(0).rank < c.driver(3).rank);
  // save / load round trip
  Championship d;
  CHECK(d.deserialize(c.serialize()));
  CHECK(d.raceNumber() == 2 && d.driver(9).points == 10 && d.driver(9).money == 1400 && d.humanShip() == 3 && d.standings()[0] == 9);
  CHECK(!d.deserialize("garbage"));
  // the last race
  Championship e;
  e.start(0, 0);
  for (int i = 1; i < 6; ++i) e.nextRace();
  CHECK(e.raceNumber() == 6 && e.lastRace());
  e.nextRace();
  CHECK(e.raceNumber() == 6);
  std::printf(failures ? "champ tests: %d FAILED\n" : "champ tests: ok\n", failures);
  return failures ? 1 : 0;
}
