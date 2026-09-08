#pragma once
#include <algorithm>
#include <cmath>
namespace ashfall {
struct CprEquilibrium {
  double nash, social, ratio;
};
// Offline reference only: spatial forest extraction need not equal this equilibrium.
inline CprEquilibrium cpr_equilibrium(int kingdoms, double r = 1, double q = 1, double price = 1,
                                      double capacity = 1, double cost = .1) {
  if (kingdoms < 1 || !std::isfinite(r) || !std::isfinite(q) || !std::isfinite(price) ||
      !std::isfinite(capacity) || !std::isfinite(cost) || r <= 0 || q <= 0 || price <= 0 ||
      capacity <= 0 || cost < 0)
    return {};
  const double surplus = std::max(0., 1 - cost / (price * q * capacity));
  const double social = r / (2 * q) * surplus, nash = r / q * kingdoms / (kingdoms + 1) * surplus;
  return {nash, social, social > 0 ? nash / social : 0};
}
} // namespace ashfall
