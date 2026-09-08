#include "ashfall/agent/action.hpp"
namespace ashfall {
Action Action::balanced() {
  Action a;
  constexpr std::array<int, 8> weights = {8, 5, 30, 15, 5, 4, 8, 25};
  int sum = 0;
  for (int i = 0; i < 7; ++i) {
    a.allocation[static_cast<size_t>(i)] =
        fx16::from_raw(weights[static_cast<size_t>(i)] * 65536 / 100);
    sum += a.allocation[static_cast<size_t>(i)].raw;
  }
  a.allocation[7] = fx16::from_raw(65536 - sum);
  a.construction[1] = fx16::from_raw(32768);
  a.construction[2] = fx16::from_raw(16384);
  a.construction[10] = fx16::from_raw(16384);
  return a;
}
bool Action::valid(int kingdoms) const {
  auto simplex = [](auto &values) {
    int64_t sum = 0;
    for (auto v : values) {
      if (v.raw < 0 || v.raw > 65536)
        return false;
      sum += v.raw;
    }
    return sum == 65536;
  };
  return simplex(allocation) && simplex(construction) && expand_dir >= 0 && expand_dir <= 8 &&
         military_target >= -1 && military_target < kingdoms && treaty_partner >= -1 &&
         treaty_partner < kingdoms && treaty_action >= 0 && treaty_action < 6 &&
         quarantine.raw >= 0 && quarantine.raw <= 65536 && trade.raw >= 0 && trade.raw <= 65536;
}
} // namespace ashfall
