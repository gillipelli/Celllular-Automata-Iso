#include "ashfall/sim/terrain.hpp"
#include <algorithm>
#include <limits>
namespace ashfall {
namespace {
int noise(uint64_t seed, int x, int y, int scale) {
  const int gx = x / scale, gy = y / scale, dx = x % scale, dy = y % scale;
  auto value = [&](int a, int b) {
    return static_cast<int>(splitmix64(seed ^ (static_cast<uint64_t>(a) * 0x9e3779b97f4a7c15ull) ^
                                       (static_cast<uint64_t>(b) * 0xbf58476d1ce4e5b9ull)) >>
                            48);
  };
  const int64_t top = static_cast<int64_t>(value(gx, gy)) * (scale - dx) +
                      static_cast<int64_t>(value(gx + 1, gy)) * dx;
  const int64_t bottom = static_cast<int64_t>(value(gx, gy + 1)) * (scale - dx) +
                         static_cast<int64_t>(value(gx + 1, gy + 1)) * dx;
  return static_cast<int>((top * (scale - dy) + bottom * dy) / (scale * scale));
}
} // namespace
TerrainLayers::TerrainLayers(const Config &cfg, uint64_t seed)
    : elevation(cfg.world.size), terrain(cfg.world.size), stone(cfg.world.size),
      moisture(cfg.world.size), initial_fertility(cfg.world.size) {
  Grid grid(cfg.world.size);
  const int n = grid.side();
  Layer<int32_t> heights(n);
  int low = INT32_MAX, high = INT32_MIN;
  for (int i = 0; i < grid.size(); ++i) {
    int h = 0;
    for (int octave = 0; octave < 5; ++octave)
      h += noise(splitmix64(seed + static_cast<uint64_t>(octave)), i % n, i / n,
                 std::max(2, n >> (octave + 1))) *
           (16 >> octave);
    heights[i] = h;
    low = std::min(low, h);
    high = std::max(high, h);
  }
  Layer<int32_t> distance(n, INT32_MAX);
  std::vector<int> queue;
  queue.reserve(elevation.size());
  for (int i = 0; i < grid.size(); ++i) {
    const int h =
        high == low ? 28
                    : static_cast<int>(static_cast<int64_t>(heights[i] - low) * 63 / (high - low));
    elevation[i] = static_cast<uint8_t>(h);
    terrain[i] = h < cfg.terrain.sea_level ? WATER
                 : h <= 28                 ? PLAIN
                 : h <= 44                 ? HILL
                 : h <= 58                 ? MOUNTAIN
                                           : ROCK;
    if (terrain[i] == WATER) {
      distance[i] = 0;
      queue.push_back(i);
    }
  }
  for (size_t q = 0; q < queue.size(); ++q)
    grid.neighbors4(queue[q], [&](int j) {
      if (distance[j] == INT32_MAX) {
        distance[j] = distance[queue[q]] + 1;
        queue.push_back(j);
      }
    });
  RngBank rng(seed);
  for (int i = 0; i < grid.size(); ++i) {
    const int d = std::min(distance[i], cfg.terrain.moisture_range);
    moisture[i] = FX_ONE - fx16::from_int(d) / fx16::from_int(cfg.terrain.moisture_range);
    const fx16 base = terrain[i] == PLAIN  ? fx16::from_raw(58982)
                      : terrain[i] == HILL ? fx16::from_raw(45875)
                                           : fx16::from_raw(26214);
    const fx16 perturb =
        fx16::from_raw(static_cast<int32_t>(rng.cell(RngDomain::SoilNoise, 0, i) % 3277));
    initial_fertility[i] =
        terrain[i] == WATER ? fx16{} : std::min(FX_ONE, base * moisture[i] + perturb);
  }
  // Spaced deposit centers with integer random-walk blobs. Full log-normal deposit
  // calibration is deferred to the economy milestone; these deposits are inert in M1.
  Layer<uint8_t> occupied(n);
  for (int i = 0; i < grid.size(); ++i)
    if (terrain[i] >= MOUNTAIN && !occupied[i] &&
        rng.cell(RngDomain::ScenarioInit, 30, i) % 300 == 0) {
      const int radius = 3 + static_cast<int>(rng.cell(RngDomain::ScenarioInit, 31, i) % 5);
      for (int y = std::max(0, i / n - radius * 2); y < std::min(n, i / n + radius * 2 + 1); ++y)
        for (int x = std::max(0, i % n - radius * 2); x < std::min(n, i % n + radius * 2 + 1); ++x)
          occupied.at(x, y) = 1;
      int j = i;
      for (int step = 0; step < radius * radius * 4; ++step) {
        if (terrain[j] != WATER)
          stone[j] = static_cast<uint16_t>(std::min(65535, static_cast<int>(stone[j]) + 128));
        const int direction = static_cast<int>(
            rng.cell(RngDomain::ScenarioInit, static_cast<uint64_t>(step + 32), j) % 4);
        const int x = j % n + (direction == 0) - (direction == 1),
                  y = j / n + (direction == 2) - (direction == 3);
        if (grid.contains(x, y) && std::abs(x - i % n) <= radius && std::abs(y - i / n) <= radius)
          j = grid.index(x, y);
      }
    }
}
void TerrainLayers::hash_into(Hasher &h) const {
  elevation.hash_into(h);
  terrain.hash_into(h);
  stone.hash_into(h);
  moisture.hash_into(h);
  initial_fertility.hash_into(h);
}
} // namespace ashfall
