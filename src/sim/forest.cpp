#include "ashfall/sim/forest.hpp"
#include "ashfall/core/fixed_math.hpp"
#include <algorithm>
namespace ashfall {
namespace {
uint64_t threshold(double p) { return static_cast<uint64_t>(p * 4294967296.0); }
uint64_t probability(fx16 p) { return static_cast<uint64_t>(std::clamp(p.raw, 0, 65536)) * 65536; }
} // namespace
ForestSystem::ForestSystem(const Config &cfg, const TerrainLayers &terrain, uint64_t seed)
    : terrain_(terrain), grid_(cfg.world.size), rng_(seed), state_(cfg.world.size),
      age_(cfg.world.size), fertility_(cfg.world.size), smoke_(cfg.world.size),
      ids_(cfg.world.size, -1), ids_next_(cfg.world.size, -1),
      slots_(static_cast<size_t>(grid_.size())), events_(static_cast<size_t>(grid_.size())),
      row_metrics_(static_cast<size_t>(grid_.side())), growth_(threshold(cfg.forest.p_growth)),
      lightning_(threshold(cfg.forest.f_lightning)), mature_(cfg.forest.t_mature),
      burn_(cfg.forest.t_burn), ash_(cfg.forest.t_ash), period_(cfg.forest.dryness_period),
      spread_(fx16::from_double(cfg.forest.base_spread)),
      amplitude_(fx16::from_double(cfg.forest.dryness_amp)),
      ash_bonus_(fx16::from_double(cfg.forest.ash_fertility)),
      recover_(fx16::from_double(cfg.soil.r_recover)),
      fertility_max_(fx16::from_double(cfg.soil.fertility_max)) {
  free_.reserve(static_cast<size_t>(grid_.size()));
  for (int i = grid_.size() - 1; i >= 0; --i)
    free_.push_back(i);
  auto s = state_.back();
  auto f = fertility_.back();
  for (int i = 0; i < grid_.size(); ++i) {
    const auto quality = terrain_.initial_fertility[i] * terrain_.moisture[i];
    const bool tree = terrain_.terrain[i] != WATER && terrain_.terrain[i] != ROCK &&
                      rng_.cell(RngDomain::ScenarioInit, 0, i) < probability(quality);
    s.set(i, tree ? TREE : EMPTY);
    f.set(i, std::min(fertility_max_, terrain_.initial_fertility[i]));
    metrics_.trees += tree;
    metrics_.land += terrain_.terrain[i] != WATER;
  }
  state_.swap();
  fertility_.swap();
}
void ForestSystem::step(uint64_t tick, ThreadPool &pool, bool reverse) {
  tick_ = tick;
  reverse_ = reverse;
  event_count_ = 0;
  dryness_ = FX_ONE + amplitude_ * sine_phase(tick, period_);
  pool.run([](void *context, int begin,
              int end) { static_cast<ForestSystem *>(context)->rows(begin, end); },
           this);
  account();
  state_.swap();
  age_.swap();
  fertility_.swap();
  smoke_.swap();
  std::swap(ids_, ids_next_);
}
void ForestSystem::rows(int begin, int end) {
  const auto &current = state_.front();
  const auto &ages = age_.front();
  auto next = state_.back();
  auto next_age = age_.back();
  auto soil = fertility_.back();
  auto smoke = smoke_.back();
  const int first = begin * grid_.side(), last = end * grid_.side();
  for (int y = begin; y < end; ++y)
    row_metrics_[static_cast<size_t>(y)] = {};
  for (int offset = 0; offset < last - first; ++offset) {
    const int i = reverse_ ? last - 1 - offset : first + offset;
    const uint8_t old = current[i];
    uint8_t state = old;
    int age = std::min(255, static_cast<int>(ages[i]) + 1);
    const fx16 moisture = terrain_.moisture[i];
    int burning = 0, source = -1, slope = 0, uphill = 0;
    grid_.neighbors4(i, [&](int j) {
      slope = std::max(slope,
                       std::abs(static_cast<int>(terrain_.elevation[i]) - terrain_.elevation[j]));
      if (current[j] == BURNING) {
        ++burning;
        if (source < 0 || j < source)
          source = j;
        uphill = std::max(uphill, static_cast<int>(terrain_.elevation[i]) - terrain_.elevation[j]);
      }
    });
    if (terrain_.terrain[i] == WATER || terrain_.terrain[i] == ROCK)
      state = EMPTY;
    else if (old == EMPTY) {
      const fx16 penalty =
          FX_ONE - fx16::from_raw(std::min(slope, 16) * 2048); // 0.5 * normalized slope
      const fx16 quality = fertility_.front()[i] * moisture * penalty;
      if (rng_.cell(RngDomain::ForestGrowth, tick_, i) <
          ((growth_ * static_cast<uint64_t>(quality.raw)) >> 16))
        state = SAPLING;
    } else if (old == TREE || old == SAPLING) {
      if (old == SAPLING && age >= mature_)
        state = TREE;
      if (burning > 0) {
        fx16 spread =
            spread_ * (FX_ONE - fx16::from_raw(45875) * moisture) *
            (FX_ONE + fx16::from_raw(26214) * fx16::from_raw(std::min(uphill, 16) * 4096)) *
            dryness_;
        if (old == SAPLING)
          spread = spread * fx16::from_raw(39322);
        spread = std::clamp(spread, fx16{}, FX_ONE);
        const fx16 p = FX_ONE - pow_fx(FX_ONE - spread, burning);
        if (rng_.cell(RngDomain::FireSpread, tick_, i) < probability(p))
          state = BURNING;
      } else if (rng_.cell(RngDomain::ForestLightning, tick_, i) < lightning_)
        state = BURNING;
    } else if (old == BURNING && age >= burn_)
      state = ASH;
    else if (old == ASH && age >= ash_)
      state = EMPTY;
    if (state != old)
      age = 0;
    next.set(i, state);
    next_age.set(i, static_cast<uint8_t>(age));
    auto &counters = row_metrics_[static_cast<size_t>(i / grid_.side())];
    counters.trees += state == TREE;
    counters.saplings += state == SAPLING;
    counters.ash += state == ASH;
    ids_next_[i] = state == BURNING ? (old == BURNING ? ids_[i]
                                       : source < 0   ? -2
                                                      : ids_[source])
                                    : -1;
    fx16 fertility = fertility_.front()[i];
    if (terrain_.terrain[i] != WATER) {
      fertility = fertility + recover_ * (fertility_max_ - fertility);
      if (state == ASH && old == BURNING)
        fertility = fertility + ash_bonus_;
    }
    soil.set(i, std::clamp(fertility, fx16{}, fertility_max_));
    smoke.set(i, state == BURNING ? FX_ONE : smoke_.front()[i] * fx16::from_raw(58982));
  }
}
int ForestSystem::root(int slot) {
  assert(slot >= 0);
  int r = slot;
  while (slots_[static_cast<size_t>(r)].parent != r)
    r = slots_[static_cast<size_t>(r)].parent;
  while (slot != r) {
    const int p = slots_[static_cast<size_t>(slot)].parent;
    slots_[static_cast<size_t>(slot)].parent = r;
    slot = p;
  }
  return r;
}
int ForestSystem::unite(int a, int b) {
  a = root(a);
  b = root(b);
  if (a == b)
    return a;
  if (slots_[static_cast<size_t>(a)].event.id > slots_[static_cast<size_t>(b)].event.id)
    std::swap(a, b);
  auto &left = slots_[static_cast<size_t>(a)];
  auto &right = slots_[static_cast<size_t>(b)];
  right.parent = a;
  left.event.size += right.event.size;
  left.event.sum_x += right.event.sum_x;
  left.event.sum_y += right.event.sum_y;
  left.event.start_tick = std::min(left.event.start_tick, right.event.start_tick);
  return a;
}
void ForestSystem::account() {
  metrics_.trees = metrics_.saplings = metrics_.burning = metrics_.ash = 0;
  // Resolve event topology sequentially only after the confluent field pass.
  for (auto &slot : slots_)
    slot.active = 0;
  for (int i = 0; i < grid_.size(); ++i) {
    int id = ids_next_[i];
    if (id == -2) {
      // A newly ignited cell itself guarantees at least one unused slot.
      assert(!free_.empty());
      id = free_.back();
      free_.pop_back();
      slots_[static_cast<size_t>(id)] = {{next_id_++, 0, tick_, tick_, 0, 0}, id, 0, true};
      ids_next_[i] = id;
    }
    if (id < 0 || state_.front()[i] == BURNING)
      continue;
    grid_.neighbors4(i, [&](int j) {
      if (state_.front()[j] == BURNING)
        id = unite(id, ids_[j]);
    });
    auto &event = slots_[static_cast<size_t>(root(id))].event;
    ++event.size;
    event.sum_x += static_cast<uint64_t>(i % grid_.side());
    event.sum_y += static_cast<uint64_t>(i / grid_.side());
    ++metrics_.burned;
  }
  // Normalize references before recycling merged slots.
  for (int i = 0; i < grid_.size(); ++i)
    if (ids_next_[i] >= 0) {
      ids_next_[i] = root(ids_next_[i]);
      ++slots_[static_cast<size_t>(ids_next_[i])].active;
      ++metrics_.burning;
    }
  for (int i = 0; i < grid_.size(); ++i) {
    auto &slot = slots_[static_cast<size_t>(i)];
    if (!slot.used)
      continue;
    if (slot.parent == i && slot.active == 0) {
      slot.event.end_tick = tick_;
      events_[event_count_++] = slot.event;
      ++metrics_.events;
    }
    if (slot.parent != i || slot.active == 0) {
      slot = {};
      free_.push_back(i);
    }
  }
  for (const auto &row : row_metrics_) {
    metrics_.trees += row.trees;
    metrics_.saplings += row.saplings;
    metrics_.ash += row.ash;
  }
}
void ForestSystem::hash_into(Hasher &h) const {
  state_.front().hash_into(h);
  age_.front().hash_into(h);
  fertility_.front().hash_into(h);
  smoke_.front().hash_into(h);
  ids_.hash_into(h);
  h.integer(next_id_);
  h.integer(metrics_.events);
  h.integer(metrics_.burned);
  for (const auto &slot : slots_) {
    h.integer(slot.used, 1);
    if (slot.used) {
      h.integer(static_cast<uint32_t>(slot.parent), 4);
      h.integer(static_cast<uint32_t>(slot.active), 4);
      h.integer(slot.event.id);
      h.integer(slot.event.size);
      h.integer(slot.event.start_tick);
      h.integer(slot.event.sum_x);
      h.integer(slot.event.sum_y);
    }
  }
  h.integer(free_.size());
  for (int i : free_)
    h.integer(static_cast<uint32_t>(i), 4);
}
} // namespace ashfall
