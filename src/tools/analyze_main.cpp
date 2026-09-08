#include "ashfall/game/cpr.hpp"
#include <charconv>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
  if (argc != 3 || std::string(argv[1]) != "--cpr") {
    std::cerr
        << "usage: ashfall_analyze --cpr KINGDOMS\nFor forest fits use python/analysis/forest.py\n";
    return 2;
  }
  const std::string value = argv[2];
  int n = 0;
  auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
  if (ec != std::errc{} || p != value.data() + value.size() || n < 1 || n > 1000)
    return 2;
  const auto result = ashfall::cpr_equilibrium(n);
  std::cout << "kingdoms,nash,social,ratio\n"
            << n << ',' << result.nash << ',' << result.social << ',' << result.ratio << '\n';
}
