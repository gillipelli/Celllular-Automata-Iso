#include "ashfall/sim/world.hpp"
#include <algorithm>
#include <cstdlib>
namespace ashfall {
namespace {
Config checked(Config config) {
  if (!config.validate().empty())
    std::abort();
  return config;
}
} // namespace
World::World(const Config &config, uint64_t seed, int threads, bool empty)
    : config_(checked(config)), seed_(seed), config_hash_(config_.digest()), empty_(empty),
      terrain_(config_, seed), forest_(config_, terrain_, seed),
      pool_(config_.world.size, std::clamp(threads, 1, std::min(32, config_.world.size / 8))),
      previous_state_(config_.world.size), previous_age_(config_.world.size),
      previous_smoke_(config_.world.size) {
  if (!empty_)
    for (int i = 0; i < config_.world.forest_warmup; ++i)
      forest_.step(static_cast<uint64_t>(i), pool_);
  if (!empty_ && config_.simulation.enabled)
    civilization_ = std::make_unique<Civilization>(config_, terrain_, forest_, seed_);
}
void World::tick() {
  if (!empty_) {
    forest_.step(tick_ + static_cast<uint64_t>(config_.world.forest_warmup), pool_);
    if (civilization_)
      civilization_->tick(tick_);
    if (dirty_) {
      const int n = config_.world.size;
      for (int cy = 0; cy < n; cy += 32)
        for (int cx = 0; cx < n; cx += 32) {
          bool changed = false;
          for (int y = cy; y < std::min(cy + 32, n); ++y)
            for (int x = cx; x < std::min(cx + 32, n); ++x) {
              const int i = y * n + x;
              const auto s = forest_.state()[i], a = forest_.age()[i];
              // Canopy growth is the only rendered age-dependent quantity.
              changed = changed || s != previous_state_[i] ||
                        (s == SAPLING && a / 8 != previous_age_[i] / 8) ||
                        forest_.smoke()[i].raw / 8192 != previous_smoke_[i].raw / 8192;
              previous_state_[i] = s;
              previous_age_[i] = a;
              previous_smoke_[i] = forest_.smoke()[i];
            }
          if (changed)
            dirty_(cx / 32, cy / 32);
        }
    }
  }
  ++tick_;
}
uint64_t World::digest() const {
  Hasher h;
  h.integer(2, 4);
  h.integer(config_hash_);
  h.integer(seed_);
  h.integer(tick_);
  h.integer(empty_, 1);
  if (!empty_) {
    terrain_.hash_into(h);
    forest_.hash_into(h);
    if (civilization_)
      civilization_->hash_into(h);
  }
  return h.finish();
}
StepResult World::step(std::span<const Action> actions) {
  if (!civilization_ || tick_ % static_cast<uint64_t>(config_.world.macro_interval) != 0 ||
      !civilization_->apply(actions))
    std::abort();
  StepResult result;
  std::array<int64_t, 12> before{};
  std::array<bool, 12> alive{};
  for (const auto &k : civilization_->kingdoms()) {
    before[static_cast<size_t>(k.id)] = k.stats.population;
    alive[static_cast<size_t>(k.id)] = k.alive;
  }
  for (int i = 0; i < config_.world.macro_interval &&
                  tick_ < static_cast<uint64_t>(config_.world.episode_ticks);
       ++i)
    tick();
  for (const auto &k : civilization_->kingdoms()) {
    const size_t id = static_cast<size_t>(k.id);
    result.rewards[id] = (k.alive     ? .001
                          : alive[id] ? -1.
                                      : 0.) +
                         static_cast<double>(k.stats.population - before[id]) / 65536 * .001;
    result.terminated[id] = !k.alive;
  }
  result.tick = tick_;
  result.truncated = tick_ >= static_cast<uint64_t>(config_.world.episode_ticks);
  return result;
}
} // namespace ashfall
