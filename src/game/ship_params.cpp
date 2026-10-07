#include "game/ship_params.hpp"

namespace slip {

std::array<ShipParams, 10> loadShipParams(const GameData& data) {
  std::array<ShipParams, 10> out{};  // defaults = ship 1's values
  auto exe = data.read("SLIPSTRM.EXE");
  constexpr size_t kBase = 0x4D854 + (0x502F8 - 0x10000);  // LE data page 1 + object offset
  if (!exe || exe->size() < kBase + 28 * 10) return out;
  auto rd = [&](size_t o) { return int32_t((*exe)[o] | ((*exe)[o + 1] << 8) | ((*exe)[o + 2] << 16) | (uint32_t((*exe)[o + 3]) << 24)); };
  // sanity check: block 1 = {71500, 35750, ...}
  if (rd(kBase) != 71500 || rd(kBase + 4) != 35750) return out;
  for (size_t i = 0; i < 10; ++i) {
    size_t o = kBase + 28 * i;
    out[i] = {rd(o), rd(o + 4), rd(o + 8), rd(o + 12), rd(o + 16), rd(o + 20), rd(o + 24), true};
  }
  return out;
}

}  // namespace slip
