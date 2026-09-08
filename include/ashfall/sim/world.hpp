#pragma once
#include "ashfall/sim/kingdom.hpp"
#include <functional>
#include <memory>
namespace ashfall {
class World {
public:
  World(const Config &config, uint64_t seed, int threads = 1, bool empty = false);
  void tick();
  StepResult step(std::span<const Action> actions);
  const Civilization *civilization() const { return civilization_.get(); }
  uint64_t current_tick() const { return tick_; }
  uint64_t digest() const;
  const Config &config() const { return config_; }
  const TerrainLayers &terrain() const { return terrain_; }
  const ForestSystem &forest() const { return forest_; }
  const Layer<uint8_t> &forest_state() const { return forest_.state(); }
  void set_dirty_callback(std::function<void(int, int)> callback) { dirty_ = std::move(callback); }

private:
  const Config config_;
  uint64_t seed_, tick_ = 0, config_hash_;
  bool empty_;
  TerrainLayers terrain_;
  ForestSystem forest_;
  ThreadPool pool_;
  std::unique_ptr<Civilization> civilization_;
  Layer<uint8_t> previous_state_, previous_age_;
  Layer<fx16> previous_smoke_;
  std::function<void(int, int)> dirty_;
};
} // namespace ashfall
