#pragma once
#include "ashfall/core/thread_pool.hpp"
#include "ashfall/sim/terrain.hpp"
#include <span>
namespace ashfall {
struct FireEvent {
  uint64_t id = 0, size = 0, start_tick = 0, end_tick = 0, sum_x = 0, sum_y = 0;
};
struct ForestMetrics {
  uint64_t trees = 0, saplings = 0, burning = 0, ash = 0, land = 0, events = 0, burned = 0;
};
class ForestSystem {
public:
  ForestSystem(const Config &cfg, const TerrainLayers &terrain, uint64_t seed);
  void step(uint64_t tick, ThreadPool &pool, bool reverse = false);
  const Layer<uint8_t> &state() const { return state_.front(); }
  const Layer<uint8_t> &age() const { return age_.front(); }
  const Layer<fx16> &fertility() const { return fertility_.front(); }
  const Layer<fx16> &smoke() const { return smoke_.front(); }
  const ForestMetrics &metrics() const { return metrics_; }
  std::span<const FireEvent> events() const { return {events_.data(), event_count_}; }
  void hash_into(Hasher &h) const;
  bool harvest(int i) {
    if (state_.front()[i] != TREE)
      return false;
    state_.set_current(i, EMPTY);
    age_.set_current(i, 0);
    --metrics_.trees;
    return true;
  }
  void farm(int i, fx16 amount) {
    fertility_.set_current(i, std::max(fx16{}, fertility_.front()[i] - amount));
  }

private:
  struct Slot {
    FireEvent event;
    int parent = -1, active = 0;
    bool used = false;
  };
  void rows(int begin, int end);
  void account();
  int root(int slot);
  int unite(int a, int b);
  const TerrainLayers &terrain_;
  Grid grid_;
  RngBank rng_;
  DoubleLayer<uint8_t> state_, age_;
  DoubleLayer<fx16> fertility_, smoke_;
  Layer<int32_t> ids_, ids_next_;
  std::vector<Slot> slots_;
  std::vector<int> free_;
  std::vector<FireEvent> events_;
  std::vector<ForestMetrics> row_metrics_;
  size_t event_count_ = 0;
  ForestMetrics metrics_;
  uint64_t tick_ = 0, next_id_ = 1, growth_, lightning_;
  int mature_, burn_, ash_, period_;
  fx16 spread_, amplitude_, ash_bonus_, recover_, fertility_max_, dryness_;
  bool reverse_ = false;
};
} // namespace ashfall
