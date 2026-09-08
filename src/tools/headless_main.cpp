#include "ashfall/sim/world.hpp"
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
using namespace ashfall;
int main(int argc, char **argv) {
  Config cfg;
  uint64_t seed = 1, ticks = 20000;
  int threads = 1;
  bool empty = false, paranoid = false, forest_only = false;
  std::string events_path, metrics_path, replay_path, kingdom_events_path;
  int size_override = 0, warmup_override = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") {
      std::cout << "ashfall_headless [--config FILE] [--seed N] [--ticks N] [--threads 1..32]\n"
                   "  [--size 64..1024] [--warmup N] [--empty] [--paranoid]\n"
                   "  [--events fires.csv] [--metrics metrics.csv] [--out run.replay]\n"
                   "  [--kingdom-events events.csv] [--forest-only]\n"
                   "Kingdom simulation; --forest-only disables civilization systems.\n";
      return 0;
    }
    if (arg == "--forest-only") {
      forest_only = true;
      continue;
    }
    if (arg == "--empty") {
      empty = true;
      continue;
    }
    if (arg == "--paranoid") {
      paranoid = true;
      continue;
    }
    if (i + 1 >= argc) {
      std::cerr << "missing value for " << arg << '\n';
      return 2;
    }
    const std::string value = argv[++i];
    if (arg == "--config") {
      auto r = load_config(value);
      if (!r) {
        std::cerr << r.error << '\n';
        return 2;
      }
      cfg = r.config;
      continue;
    }
    if (arg == "--kingdom-events") {
      kingdom_events_path = value;
      continue;
    }
    if (arg == "--events") {
      events_path = value;
      continue;
    }
    if (arg == "--metrics") {
      metrics_path = value;
      continue;
    }
    if (arg == "--out") {
      replay_path = value;
      continue;
    }
    uint64_t n = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (ec != std::errc{} || end != value.data() + value.size()) {
      std::cerr << "invalid number: " << value << '\n';
      return 2;
    }
    if (arg == "--seed")
      seed = n;
    else if (arg == "--ticks" && n <= 200000)
      ticks = n;
    else if (arg == "--threads" && n >= 1 && n <= 32)
      threads = static_cast<int>(n);
    else if (arg == "--size" && n >= 64 && n <= 1024)
      size_override = static_cast<int>(n);
    else if (arg == "--warmup" && n <= 200000)
      warmup_override = static_cast<int>(n);
    else {
      std::cerr << "unknown option or invalid value: " << arg << '\n';
      return 2;
    }
  }
  if (forest_only)
    cfg.simulation.enabled = 0;
  if (size_override)
    cfg.world.size = size_override;
  if (warmup_override >= 0)
    cfg.world.forest_warmup = warmup_override;
  if (!cfg.validate().empty()) {
    std::cerr << cfg.validate() << '\n';
    return 2;
  }
  if ((!events_path.empty() && (events_path == metrics_path || events_path == replay_path)) ||
      (!metrics_path.empty() && metrics_path == replay_path)) {
    std::cerr << "output paths must be distinct\n";
    return 2;
  }
  const std::array<std::string, 4> paths = {events_path, metrics_path, replay_path,
                                            kingdom_events_path};
  for (size_t a = 0; a < paths.size(); ++a)
    for (size_t b = a + 1; b < paths.size(); ++b)
      if (!paths[a].empty() && paths[a] == paths[b]) {
        std::cerr << "output paths must be distinct\n";
        return 2;
      }
  std::ofstream events, metrics, replay, kingdom_events;
  auto open = [](std::ofstream &file, const std::string &path) {
    if (path.empty())
      return true;
    file.open(path, std::ios::binary);
    if (!file)
      std::cerr << "cannot write " << path << '\n';
    return static_cast<bool>(file);
  };
  if (!open(events, events_path) || !open(metrics, metrics_path) || !open(replay, replay_path) ||
      !open(kingdom_events, kingdom_events_path))
    return 2;
  if (events.is_open())
    events << "id,size,start_tick,end_tick,centroid_x,centroid_y\n";
  if (kingdom_events.is_open())
    kingdom_events << "tick,type,kingdom,cell,magnitude,cause\n";
  if (metrics.is_open()) {
    metrics
        << "tick,trees,saplings,burning,ash,land,forest_fraction,completed_events,total_ignitions";
    if (cfg.simulation.enabled && !empty)
      for (int k = 0; k < cfg.world.kingdoms; ++k)
        metrics << ",k" << k << "_population,k" << k << "_area,k" << k << "_grain,k" << k
                << "_nodes,k" << k << "_functional,k" << k << "_alive";
    metrics << '\n';
  }
  World world(cfg, seed, threads, empty);
  if (replay.is_open()) {
    const auto config = cfg.canonical();
    replay << "ASHF-REPLAY 2\n"
           << seed << ' ' << empty << ' ' << ticks << '\n'
           << config.size() << '\n'
           << config;
    replay << 0 << ' ' << std::hex << world.digest() << std::dec << '\n';
  }
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t t = 0; t < ticks; ++t) {
    world.tick();
    if (kingdom_events.is_open() && world.civilization())
      for (const auto &e : world.civilization()->events())
        kingdom_events << e.tick << ',' << static_cast<int>(e.type) << ',' << e.kingdom << ','
                       << e.cell << ',' << e.magnitude << ',' << e.cause << '\n';
    if (!empty && events.is_open())
      for (const auto &e : world.forest().events())
        events << e.id << ',' << e.size << ',' << e.start_tick << ',' << e.end_tick << ','
               << static_cast<double>(e.sum_x) / static_cast<double>(e.size) << ','
               << static_cast<double>(e.sum_y) / static_cast<double>(e.size) << '\n';
    if (metrics.is_open() && (world.current_tick() % 16 == 0 || t + 1 == ticks)) {
      const auto &m = world.forest().metrics();
      const double fraction =
          m.land ? static_cast<double>(m.trees + m.saplings) / static_cast<double>(m.land) : 0;
      metrics << world.current_tick() << ',' << m.trees << ',' << m.saplings << ',' << m.burning
              << ',' << m.ash << ',' << m.land << ',' << fraction << ',' << m.events << ','
              << m.burned;
      if (world.civilization())
        for (const auto &k : world.civilization()->kingdoms())
          metrics << ',' << static_cast<double>(k.stats.population) / 65536 << ',' << k.stats.area
                  << ',' << static_cast<double>(k.stock[2]) / 65536 << ',' << k.stats.nodes << ','
                  << k.stats.functional << ',' << k.alive;
      metrics << '\n';
    }
    if (replay.is_open() &&
        (paranoid || world.current_tick() % static_cast<uint64_t>(cfg.world.digest_interval) == 0 ||
         t + 1 == ticks))
      replay << world.current_tick() << ' ' << std::hex << world.digest() << std::dec << '\n';
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  const auto &m = world.forest().metrics();
  std::cout << "tick=" << world.current_tick() << " digest=" << std::hex << world.digest()
            << std::dec << " seconds=" << seconds
            << " ticks_per_second=" << (seconds > 0 ? static_cast<double>(ticks) / seconds : 0)
            << " trees=" << m.trees << " fires=" << m.events << '\n';
  if (world.civilization())
    for (const auto &k : world.civilization()->kingdoms())
      std::cout << "kingdom=" << k.id << " alive=" << k.alive
                << " population=" << static_cast<double>(k.stats.population) / 65536
                << " nodes=" << k.stats.nodes << " functional=" << k.stats.functional
                << " grain=" << static_cast<double>(k.stock[2]) / 65536 << '\n';
  for (auto *file : {&events, &metrics, &replay, &kingdom_events})
    if (file->is_open()) {
      file->flush();
      if (!*file) {
        std::cerr << "output write failed\n";
        return 1;
      }
    }
}
