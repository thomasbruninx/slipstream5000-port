#include "game/championship.hpp"

#include <algorithm>
#include <random>
#include <sstream>

namespace slip {

int champRaceCount(int difficulty) {
  static const int kRaces[3] = {6, 8, 10};
  return kRaces[std::clamp(difficulty, 0, 2)];
}

void Championship::start(int humanShip, int difficulty, unsigned seed) {
  active_ = true;
  difficulty_ = std::clamp(difficulty, 0, 2);
  race_ = 1;
  human_ = std::clamp(humanShip, 0, 9);
  int k = 0;
  order_[size_t(k++)] = human_;
  for (int s = 0; s < 10; ++s) if (s != human_) order_[size_t(k++)] = s;
  for (int s = 0; s < 10; ++s) {
    ChampDriver& d = d_[size_t(s)];
    d = ChampDriver{};
    d.ship = s;
    d.human = s == human_;
    d.rank = int(std::find(order_.begin(), order_.end(), s) - order_.begin()) + 1;
    d.load = d.human ? Loadout{} : aiLoadout(s);
    if (d.human) d.load.booster = 0;  // the roster record starts with turbo item 0 (0x585D1 clears the field, the garage shows Delphine Injection)
  }
  std::mt19937 rng(seed ? seed : std::random_device{}());
  for (int s = 0; s < 10; ++s) grid1_[size_t(s)] = s;
  std::shuffle(grid1_.begin(), grid1_.end(), rng);
  rank();
}

std::array<int, 10> Championship::grid() const {
  std::array<int, 10> g{};
  for (int s = 0; s < 10; ++s) {
    const ChampDriver& d = d_[size_t(s)];
    g[size_t(s)] = race_ == 1 || d.place == 0 ? grid1_[size_t(s)] : 10 - d.place;  // rank = 11 - place, slot = rank - 1
  }
  return g;
}

void Championship::applyRace(const std::array<int, 10>& place) {
  for (int s = 0; s < 10; ++s) {
    ChampDriver& d = d_[size_t(s)];
    d.place = std::clamp(place[size_t(s)], 1, 10);
    d.points += kChampPoints[d.place - 1];
    d.money += kChampPrize[d.place - 1];
  }
  rank();
}

void Championship::nextRace() { race_ = std::min(race_ + 1, raceCount()); }

// 0x56209..0x5623A: positions 1..10 are given one by one to the unranked driver with the most points; on equal points the later roster entry wins (jl skips only
// smaller values).
void Championship::rank() {
  std::array<bool, 10> taken{};
  for (int pos = 1; pos <= 10; ++pos) {
    int best = -1, bestPts = -1;
    for (int k = 0; k < 10; ++k) {
      const int s = order_[size_t(k)];
      if (taken[size_t(s)]) continue;
      if (d_[size_t(s)].points >= bestPts) { bestPts = d_[size_t(s)].points; best = s; }
    }
    taken[size_t(best)] = true;
    d_[size_t(best)].rank = pos;
  }
}

std::array<int, 10> Championship::standings() const {
  std::array<int, 10> r{};
  for (int s = 0; s < 10; ++s) r[size_t(std::clamp(d_[size_t(s)].rank - 1, 0, 9))] = s;
  return r;
}

std::string Championship::serialize() const {
  std::ostringstream o;
  o << "champ 1 " << difficulty_ << ' ' << race_ << ' ' << human_ << '\n';
  for (int s = 0; s < 10; ++s) {
    const ChampDriver& d = d_[size_t(s)];
    o << s << ' ' << d.points << ' ' << d.money << ' ' << d.place << ' ' << d.load.weaponA << ' ' << d.load.weaponB << ' ' << d.load.ammoA << ' ' << d.load.ammoB << ' '
      << d.load.booster << ' ' << int(d.load.fastRecharge) << ' ' << int(d.load.wideLock) << ' ' << int(d.loader) << '\n';
  }
  return o.str();
}

bool Championship::deserialize(const std::string& text) {
  std::istringstream in(text);
  std::string tag;
  int ver = 0, diff = 0, race = 0, human = 0;
  if (!(in >> tag >> ver >> diff >> race >> human) || tag != "champ" || ver != 1) return false;
  if (diff < 0 || diff > 2 || human < 0 || human > 9 || race < 1 || race > champRaceCount(diff)) return false;
  start(human, diff);
  race_ = race;
  for (int i = 0; i < 10; ++i) {
    int s, fr, wl, ld;
    ChampDriver t;
    if (!(in >> s >> t.points >> t.money >> t.place >> t.load.weaponA >> t.load.weaponB >> t.load.ammoA >> t.load.ammoB >> t.load.booster >> fr >> wl >> ld) || s < 0 || s > 9) return false;
    t.load.fastRecharge = fr != 0; t.load.wideLock = wl != 0;
    t.loader = ld != 0;
    ChampDriver& d = d_[size_t(s)];
    d.points = t.points; d.money = t.money; d.place = t.place; d.load = t.load; d.loader = t.loader;
  }
  rank();
  return true;
}

}  // namespace slip
