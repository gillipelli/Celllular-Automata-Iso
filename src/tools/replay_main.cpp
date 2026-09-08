#include "ashfall/sim/world.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
using namespace ashfall;
int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: ashfall_replay FILE [--verify] [--threads N]\n";
    return 2;
  }
  int threads = 1;
  for (int i = 2; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--verify")
      continue;
    if (option != "--threads" || i + 1 >= argc) {
      std::cerr << "invalid option\n";
      return 2;
    }
    const std::string value = argv[++i];
    auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), threads);
    if (ec != std::errc{} || p != value.data() + value.size() || threads < 1 || threads > 32) {
      std::cerr << "invalid thread count\n";
      return 2;
    }
  }
  auto fail = [](const char *message) {
    std::cerr << message << '\n';
    return 1;
  };
  std::ifstream in(argv[1], std::ios::binary);
  std::string magic;
  if (!std::getline(in, magic) || magic != "ASHF-REPLAY 2")
    return fail("invalid replay magic/version");
  uint64_t seed = 0, total = 0;
  int empty = 0;
  size_t length = 0;
  if (!(in >> seed >> empty >> total >> length) || (empty != 0 && empty != 1) || total > 200000 ||
      length > 65536 || length == 0)
    return fail("invalid replay header");
  if (in.get() != '\n')
    return fail("invalid config separator");
  std::string text(length, '\0');
  in.read(text.data(), static_cast<std::streamsize>(length));
  if (!in)
    return fail("truncated config");
  auto cfg = parse_config(text);
  if (!cfg) {
    std::cerr << cfg.error << '\n';
    return 1;
  }
  World world(cfg.config, seed, threads, empty != 0);
  uint64_t previous = 0;
  bool first = true;
  std::string line;
  while (std::getline(in, line)) {
    uint64_t tick = 0, expected = 0;
    std::istringstream record(line);
    std::string extra;
    if (!(record >> tick >> std::hex >> expected) || (record >> extra))
      return fail("invalid digest record");
    if (tick > total || (first && tick != 0) || (!first && tick <= previous))
      return fail("invalid checkpoint ordering");
    while (world.current_tick() < tick)
      world.tick();
    if (world.digest() != expected) {
      std::cerr << "digest mismatch at tick " << tick << " expected=" << std::hex << expected
                << " actual=" << world.digest() << '\n';
      return 1;
    }
    first = false;
    previous = tick;
  }
  if (!in.eof() || first || previous != total)
    return fail("truncated replay");
  std::cout << "verified through tick " << total << " digest=" << std::hex << world.digest()
            << '\n';
}
