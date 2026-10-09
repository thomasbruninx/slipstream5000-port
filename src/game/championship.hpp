// Championship mode rules of the original (mode 0x13 of the main loop, 0x559D8; docs/championship.md).
// SDL-free: the roster with points and money, the race calendar by difficulty, the points / prize tables, the championship ranking and the starting grid of
// the next race. The front end (game/frontend) drives the screens, the application runs the races.
#pragma once
#include <array>
#include <string>

#include "game/weapons.hpp"

namespace slip {

constexpr int kChampTrackOrder[10] = {6, 1, 7, 8, 4, 9, 5, 2, 3, 10};  // table 0x54E10: the calendar (also the order in which tracks are unlocked)
constexpr int kChampPoints[10] = {10, 6, 4, 3, 2, 1, 0, 0, 0, 0};       // table 0x556E8, by finishing place
constexpr int kChampPrize[10] = {650, 450, 350, 200, 100, 50, 0, 0, 0, 0};  // table 0x556FC (money)
constexpr int kChampStartMoney = 750;                                    // roster record +4 (0x58503 / 0x5860D)
int champRaceCount(int difficulty);                                      // table 0x543E4: 6 / 8 / 10 races for Easy / Normal / Hard

struct ChampDriver {
  int ship = 0;
  bool human = false;
  int points = 0;
  int money = kChampStartMoney;
  int rank = 1;      // championship position (record +0xA)
  int place = 0;     // place in the last race (record +0x24), 0 before the first race
  Loadout load;      // what the craft carries (human: bought in the garage)
  bool loader = false;  // the garage upgrade "Loader" (record flag +0x46 bit 2): later weapon purchases hold twice the rounds
};

class Championship {
 public:
  // The human flies `humanShip`; the nine other ships are the AI drivers. The roster order (used for ties, 0x56217: later entries win) is the human first, then the
  // ships by number.
  void start(int humanShip, int difficulty, unsigned seed = 0);  // seed 0: random
  bool active() const { return active_; }
  int difficulty() const { return difficulty_; }
  int raceNumber() const { return race_; }                    // 1-based
  int raceCount() const { return champRaceCount(difficulty_); }
  int track() const { return kChampTrackOrder[race_ - 1]; }   // 1..10
  bool lastRace() const { return race_ >= raceCount(); }
  int humanShip() const { return human_; }
  ChampDriver& driver(int ship) { return d_[size_t(ship)]; }
  const ChampDriver& driver(int ship) const { return d_[size_t(ship)]; }
  // Start slot (0 = pole) of every ship in the coming race: race 1 a random order (0x58448 shuffles the roster), later races the reversed result of the
  // previous race (record +0x24 = 11 - place, 0x55DC8).
  std::array<int, 10> grid() const;
  // The race is over: `place[ship]` = finishing place 1..10. Awards points and prize money (0x561CF) and ranks the championship.
  void applyRace(const std::array<int, 10>& place);
  void nextRace();                                            // 0x55DAD
  // Ships ordered by championship position (index 0 = leader).
  std::array<int, 10> standings() const;
  std::string serialize() const;
  bool deserialize(const std::string& text);

 private:
  void rank();
  bool active_ = false;
  int difficulty_ = 1, race_ = 1, human_ = 0;
  std::array<ChampDriver, 10> d_{};
  std::array<int, 10> order_{};  // roster order (ship numbers)
  std::array<int, 10> grid1_{};  // the first race's start slots
};

}  // namespace slip
