#include "ashfall/sim/kingdom.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>
namespace ashfall {
namespace {
constexpr fx16 q(int raw) { return fx16::from_raw(raw); }
constexpr size_t ix(int i) { return static_cast<size_t>(i); }
constexpr int64_t UNIT = 65536;
constexpr std::array<int, 13> WOOD_COST = {0, 20, 40, 25, 30, 40, 60, 50, 10, 15, 35, 45, 30};
constexpr std::array<int, 13> STONE_COST = {0, 0, 20, 5, 10, 30, 60, 40, 60, 10, 25, 25, 70};
constexpr std::array<uint8_t, 13> REQUIRE = {0, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 1};
constexpr std::array<std::array<int, 3>, 13> DEP_TYPES = {{{-1, -1, -1},
                                                           {2, -1, -1},
                                                           {0, 10, -1},
                                                           {10, 0, -1},
                                                           {10, 0, -1},
                                                           {3, 4, 10},
                                                           {5, 4, 2},
                                                           {2, 6, 10},
                                                           {10, 0, -1},
                                                           {9, 10, 0},
                                                           {9, 2, 0},
                                                           {10, 2, 9},
                                                           {12, -1, -1}}};
fx16 clamp(fx16 v) { return std::clamp(v, fx16{}, FX_ONE); }
int distance(int a, int b, int n) { return std::abs(a % n - b % n) + std::abs(a / n - b / n); }
void hash_action(Hasher &h, const Action &a) {
  for (auto v : a.allocation)
    h.integer(static_cast<uint32_t>(v.raw), 4);
  for (auto v : a.construction)
    h.integer(static_cast<uint32_t>(v.raw), 4);
  for (int v : {a.expand_dir, a.military_target, a.treaty_partner, a.treaty_action})
    h.integer(static_cast<uint32_t>(v), 4);
  h.integer(static_cast<uint32_t>(a.trade.raw), 4);
  h.integer(static_cast<uint32_t>(a.quarantine.raw), 4);
}
} // namespace
Civilization::Civilization(const Config &cfg, TerrainLayers &terrain, ForestSystem &forest,
                           uint64_t seed)
    : cfg_(cfg), terrain_(terrain), forest_(forest), grid_(cfg.world.size), rng_(seed),
      owner_(cfg.world.size, -1), node_at_(cfg.world.size, -1), visited_(cfg.world.size),
      refugees_(cfg.world.size), refugee_infected_(cfg.world.size),
      refugee_genome_{DoubleLayer<fx16>(cfg.world.size), DoubleLayer<fx16>(cfg.world.size),
                      DoubleLayer<fx16>(cfg.world.size), DoubleLayer<fx16>(cfg.world.size),
                      DoubleLayer<fx16>(cfg.world.size), DoubleLayer<fx16>(cfg.world.size),
                      DoubleLayer<fx16>(cfg.world.size), DoubleLayer<fx16>(cfg.world.size)},
      heap_position_(ix(grid_.size()), -1), eligible_(ix(cfg.simulation.max_nodes)),
      growth_(fx16::from_double(cfg.pop.r_growth)),
      diffusion_(fx16::from_double(cfg.pop.D_diffusion)),
      allee_(fx16::from_double(cfg.pop.allee_A)), famine_(fx16::from_double(cfg.pop.famine_rate)),
      food_(fx16::from_double(cfg.pop.food_per_capita)),
      claim_gen_(fx16::from_double(cfg.claim.c_gen)),
      claim_diff_(fx16::from_double(cfg.claim.c_diff)),
      claim_decay_(fx16::from_double(cfg.claim.c_decay)),
      claim_contest_(fx16::from_double(cfg.claim.c_contest)),
      claim_floor_(fx16::from_double(cfg.claim.floor)),
      fire_damage_(fx16::from_double(cfg.structures.fire_damage)),
      decay_(fx16::from_double(cfg.structures.decay_unpaid)),
      integrity_min_(fx16::from_double(cfg.structures.integrity_min)),
      beta_(fx16::from_double(cfg.epi.beta)), gamma_(fx16::from_double(cfg.epi.gamma)),
      delta_(fx16::from_double(cfg.epi.delta)), omega_(fx16::from_double(cfg.epi.omega)),
      war_(fx16::from_double(cfg.war.alpha)), ref_diff_(fx16::from_double(cfg.ref.D_diffusion)),
      ref_mortality_(fx16::from_double(cfg.ref.mortality)),
      ref_absorb_(fx16::from_double(cfg.ref.absorb_rate)),
      scatter_(fx16::from_double(cfg.collapse.scatter_survival)), soil_depletion_(q(262)) {
  spontaneous_ = static_cast<uint64_t>(cfg.epi.p_spontaneous * 4294967296.0);
  found_threshold_ = static_cast<int64_t>(cfg.ref.found_threshold * 65536);
  giant_fraction_ = fx16::from_double(cfg.collapse.giant_frac);
  detection_base_ = fx16::from_double(cfg.dip.base_detect);
  detection_scale_ = fx16::from_double(cfg.dip.monitor_scale);
  nodes_.reserve(ix(cfg.simulation.max_nodes));
  kingdoms_.resize(ix(cfg.world.kingdoms));
  layers_.reserve(ix(cfg.world.kingdoms));
  queue_.reserve(ix(grid_.size()));
  heap_.reserve(ix(grid_.size()));
  for (int k = 0; k < cfg.world.kingdoms; ++k) {
    layers_.emplace_back(cfg.world.size);
    kingdoms_[ix(k)].id = k;
    kingdoms_[ix(k)].alive = false;
  }
  for (int k = 0; k < cfg.world.kingdoms; ++k) {
    int best = -1;
    int64_t score = INT64_MIN;
    for (int i = 0; i < grid_.size(); ++i)
      if (accessible(i)) {
        int nearest = grid_.side();
        for (int j = 0; j < k; ++j)
          nearest = std::min(nearest, distance(i, kingdoms_[ix(j)].capital, grid_.side()));
        const int64_t value =
            static_cast<int64_t>(forest_.fertility()[i].raw) * terrain_.moisture[i].raw / 65536 +
            std::min(nearest, grid_.side() / 2) * 4096 +
            static_cast<int>(rng_.cell(RngDomain::ScenarioInit, 100 + static_cast<uint64_t>(k), i) %
                             4096);
        if (value > score) {
          best = i;
          score = value;
        }
      }
    if (best >= 0)
      found(k, best, false);
  }
  summarize();
}
bool Civilization::accessible(int i) const {
  return terrain_.terrain[i] != WATER && terrain_.terrain[i] != ROCK;
}
void Civilization::emit(EventType type, int k, int cell, int magnitude, int cause) {
  if (event_count_ < events_.size())
    events_[event_count_++] = {tick_, type, k, cell, magnitude, cause};
}
void Civilization::found(int k, int cell, bool successor) {
  auto &kingdom = kingdoms_[ix(k)];
  kingdom.alive = true;
  kingdom.capital = cell;
  ++kingdom.generation;
  kingdom.collapse_clock = kingdom.starve_clock = kingdom.capital_clock = 0;
  kingdom.pressure = {};
  kingdom.policy =
      static_cast<Policy>(cfg_.simulation.policy == 5 ? k % 5 : cfg_.simulation.policy);
  kingdom.stock = {200 * UNIT, 150 * UNIT, 300 * UNIT, 300 * UNIT};
  kingdom.action = Action::balanced();
  kingdom.external_policy = false;
  for (int g = 0; g < GENES; ++g) {
    if (!successor)
      kingdom.genome[ix(g)] =
          q(16384 + static_cast<int32_t>(rng_.cell(RngDomain::Mutation, 0, k * GENES + g) % 32768));
    else
      kingdom.genome[ix(g)] = clamp(
          kingdom.genome[ix(g)] +
          q(static_cast<int32_t>(rng_.cell(RngDomain::Mutation, tick_, k * GENES + g) % 3933) -
            1966));
  }
  for (int i = 0; i < grid_.size(); ++i) {
    const bool local = distance(i, cell, grid_.side()) <= 5 && accessible(i);
    layers_[ix(k)].pop.set_current(i, local && !successor ? q(45875) : fx16{});
    layers_[ix(k)].claim.set_current(i, local ? q(52429) : fx16{});
    layers_[ix(k)].infected.set_current(i, {});
    layers_[ix(k)].immune.set_current(i, {});
    layers_[ix(k)].military.set_current(i, {});
  }
  if (nodes_.size() < ix(cfg_.simulation.max_nodes)) {
    const int id = static_cast<int>(nodes_.size());
    nodes_.push_back(
        {id, k, cell, StructureType::Capital, FX_ONE, FX_ONE, true, false, 0, {-1, -1, -1}});
    node_at_[cell] = id;
  }
  if (successor)
    emit(EventType::Founding, k, cell, kingdom.generation);
}
bool Civilization::apply(std::span<const Action> actions) {
  if (actions.size() != kingdoms_.size())
    return false;
  for (const auto &a : actions)
    if (!a.valid(static_cast<int>(kingdoms_.size())))
      return false;
  for (size_t k = 0; k < kingdoms_.size(); ++k) {
    kingdoms_[k].action = actions[k];
    kingdoms_[k].external_policy = true;
  }
  return true;
}
void Civilization::policies() {
  for (auto &k : kingdoms_)
    if (k.alive && !k.external_policy) {
      auto a = Action::balanced();
      auto random = rng_.stream(RngDomain::AgentExploration, k.id, tick_);
      std::array<int, 8> weight = {8, 5, 30, 15, 5, 4, 8, 25};
      if (k.policy == Policy::Greedy)
        weight = {28, 15, 28, 12, 8, 0, 1, 8};
      if (k.policy == Policy::Expansionist)
        weight = {10, 8, 22, 20, 25, 2, 3, 10};
      if (k.policy == Policy::TitForTat)
        weight = {8, 5, 30, 15, 5, 12, 10, 15};
      if (k.policy == Policy::Random)
        for (auto &w : weight)
          w = 1 + static_cast<int>(random() % 30);
      weight[0] = std::max(1, weight[0] * (98304 - k.genome[2].raw) / 65536);
      weight[4] = weight[4] * (32768 + k.genome[0].raw) / 65536;
      weight[6] = weight[6] * (32768 + k.genome[3].raw) / 65536;
      if (k.stock[2] < k.stats.population * 2) {
        weight[2] += 40;
        weight[4] = 0;
      }
      int total = 0;
      for (int w : weight)
        total += w;
      int accumulated = 0;
      for (int j = 0; j < 7; ++j) {
        a.allocation[ix(j)] = q(weight[ix(j)] * 65536 / total);
        accumulated += a.allocation[ix(j)].raw;
      }
      a.allocation[7] = q(65536 - accumulated);
      // Build missing links before dependent industry. Later construction is policy-dependent.
      std::array<int, 13> count{};
      for (const auto &node : nodes_)
        if (node.owner == k.id && !node.destroyed)
          ++count[static_cast<size_t>(node.type)];
      int target = 1;
      for (int type : {2, 1, 3, 4, 10, 5, 6, 7, 9, 11})
        if (count[ix(type)] == 0) {
          target = type;
          break;
        }
      if (count[11] > 0)
        target = k.policy == Policy::Expansionist ? (tick_ / 16 % 3 == 0 ? 8 : 10)
                                                  : (tick_ / 16 % 3 == 0 ? 2 : 1);
      a.construction.fill({});
      a.construction[ix(target)] = FX_ONE;
      a.expand_dir = k.policy == Policy::Expansionist
                         ? static_cast<int>((tick_ / 512 + static_cast<uint64_t>(k.id)) % 8)
                         : 8;
      a.military_target =
          k.policy == Policy::Expansionist ? (k.id + 1) % static_cast<int>(kingdoms_.size()) : -1;
      if (k.policy == Policy::TitForTat) {
        a.treaty_partner = (k.id + 1) % static_cast<int>(kingdoms_.size());
        a.treaty_action = 0;
        for (const auto &t : treaties_)
          if (!t.active && t.proposer >= 0 && t.b == k.id) {
            a.treaty_partner = t.a;
            a.treaty_action = 3;
            break;
          }
        if (k.reputation[ix(a.treaty_partner)].raw < 0) {
          a.treaty_action = 4;
          a.military_target = a.treaty_partner;
        }
      }
      if (k.stats.infected > k.stats.population / 8) {
        a.quarantine = q(49152);
        a.trade = {};
      }
      k.action = a;
    }
}
void Civilization::logistics() {
  for (auto &k : kingdoms_) {
    auto &dist = layers_[ix(k.id)].supply;
    dist.fill(65535);
    if (!k.alive)
      continue;
    heap_.clear();
    std::fill(heap_position_.begin(), heap_position_.end(), -1);
    auto less = [&](int a, int b) { return dist[a] < dist[b] || (dist[a] == dist[b] && a < b); };
    auto swap = [&](int a, int b) {
      std::swap(heap_[ix(a)], heap_[ix(b)]);
      heap_position_[ix(heap_[ix(a)])] = a;
      heap_position_[ix(heap_[ix(b)])] = b;
    };
    auto push = [&](int cell) {
      int pos = heap_position_[ix(cell)];
      if (pos < 0) {
        pos = static_cast<int>(heap_.size());
        heap_.push_back(cell);
        heap_position_[ix(cell)] = pos;
      }
      while (pos > 0) {
        const int parent = (pos - 1) / 2;
        if (!less(heap_[ix(pos)], heap_[ix(parent)]))
          break;
        swap(pos, parent);
        pos = parent;
      }
    };
    for (const auto &node : nodes_)
      if (node.owner == k.id && node.functional && !node.destroyed &&
          (node.type == StructureType::Capital || node.type == StructureType::Granary ||
           node.type == StructureType::Depot)) {
        dist[node.cell] = 0;
        push(node.cell);
      }
    while (!heap_.empty()) {
      const int cell = heap_[0], last = heap_.back();
      heap_.pop_back();
      heap_position_[ix(cell)] = -1;
      if (!heap_.empty()) {
        heap_[0] = last;
        heap_position_[ix(last)] = 0;
        int pos = 0;
        for (;;) {
          int child = pos * 2 + 1;
          if (child >= static_cast<int>(heap_.size()))
            break;
          if (child + 1 < static_cast<int>(heap_.size()) &&
              less(heap_[ix(child + 1)], heap_[ix(child)]))
            ++child;
          if (!less(heap_[ix(child)], heap_[ix(pos)]))
            break;
          swap(pos, child);
          pos = child;
        }
      }
      grid_.neighbors4(cell, [&](int j) {
        if (!accessible(j))
          return;
        int cost =
            10 + std::abs(static_cast<int>(terrain_.elevation[j]) - terrain_.elevation[cell]);
        if (node_at_[j] >= 0 && nodes_[ix(node_at_[j])].type == StructureType::Road &&
            nodes_[ix(node_at_[j])].functional)
          cost = std::max(1, cost * 35 / 100);
        for (const auto &rival : kingdoms_)
          if (rival.id != k.id && (layers_[ix(rival.id)].claim.front()[j] > q(32768) ||
                                   layers_[ix(rival.id)].military.front()[j] > q(6553)))
            cost *= 2;
        const int candidate = dist[cell] + cost;
        if (candidate < dist[j] && candidate <= cfg_.log.supply_max_cost) {
          dist[j] = static_cast<uint16_t>(candidate);
          push(j);
        }
      });
    }
  }
}
void Civilization::dependencies(Node &node) {
  node.dependencies = {-1, -1, -1};
  const auto type = static_cast<size_t>(node.type);
  node.required = REQUIRE[type];
  if (node.type == StructureType::Aqueduct) {
    bool water = false;
    grid_.neighbors4(node.cell, [&](int j) { water = water || terrain_.terrain[j] == WATER; });
    if (water)
      node.required = 0;
  }
  for (size_t slot = 0; slot < 3; ++slot) {
    const int wanted = DEP_TYPES[type][slot];
    if (wanted < 0)
      continue;
    int best = INT32_MAX;
    for (const auto &other : nodes_)
      if (other.id != node.id && other.owner == node.owner && !other.destroyed &&
          static_cast<int>(other.type) == wanted) {
        const int d = distance(node.cell, other.cell, grid_.side());
        if (d < best && d <= 24) {
          best = d;
          node.dependencies[slot] = other.id;
        }
      }
  }
}
void Civilization::build(int k) {
  auto &kingdom = kingdoms_[ix(k)];
  if (nodes_.size() >= ix(cfg_.simulation.max_nodes) || kingdom.action.allocation[3].raw < 3277)
    return;
  uint32_t draw = rng_.cell(RngDomain::AgentExploration, tick_, k) >> 16;
  int type = 0, cumulative = 0;
  for (int t = 0; t < STRUCTURE_TYPES; ++t) {
    cumulative += kingdom.action.construction[ix(t)].raw;
    if (static_cast<int>(draw) < cumulative) {
      type = t;
      break;
    }
  }
  if (type == 0 || kingdom.stock[0] < WOOD_COST[ix(type)] * UNIT ||
      kingdom.stock[1] < STONE_COST[ix(type)] * UNIT)
    return;
  int best = -1;
  int64_t score = INT64_MIN;
  for (int i = 0; i < grid_.size(); ++i)
    if (accessible(i) && node_at_[i] < 0 && owner_[i] == k &&
        layers_[ix(k)].supply[i] < cfg_.log.supply_max_cost &&
        layers_[ix(k)].pop.front()[i].raw > 3277) {
      const int d = distance(i, kingdom.capital, grid_.side());
      int64_t value = forest_.fertility()[i].raw + layers_[ix(k)].pop.front()[i].raw;
      if (type == 3)
        value += forest_.state()[i] == TREE ? 65536 : 0;
      if (type == 4)
        value += terrain_.stone[i] * 16;
      value += (type == 10 || type == 8 ? d * 4096 : -d * 1024);
      value += rng_.cell(RngDomain::ScenarioInit, tick_, i) % 2048;
      if (value > score) {
        score = value;
        best = i;
      }
    }
  if (best < 0)
    return;
  Node node;
  node.id = static_cast<int>(nodes_.size());
  node.owner = k;
  node.cell = best;
  node.type = static_cast<StructureType>(type);
  node.progress = {};
  dependencies(node);
  kingdom.stock[0] -= WOOD_COST[ix(type)] * UNIT;
  kingdom.stock[1] -= STONE_COST[ix(type)] * UNIT;
  node_at_[best] = node.id;
  nodes_.push_back(node);
  emit(EventType::Construction, k, best, type);
}
void Civilization::structures() {
  for (auto &node : nodes_)
    if (!node.destroyed) {
      auto &k = kingdoms_[ix(node.owner)];
      if (!k.alive) {
        node.integrity = std::max(fx16{}, node.integrity - q(65));
        node.functional = false;
      } else {
        if (node.progress < FX_ONE && layers_[ix(k.id)].supply[node.cell] != 65535)
          node.progress = clamp(node.progress + q(655) + k.action.allocation[3] * q(6553));
        if (node.progress == FX_ONE) {
          const int64_t upkeep = node.type == StructureType::Capital ? 3277 : 655;
          if (k.stock[3] >= upkeep)
            k.stock[3] -= upkeep;
          else {
            node.integrity = std::max(fx16{}, node.integrity - decay_);
            k.pressure[1] += decay_.raw;
          }
        }
        if (forest_.state()[node.cell] == BURNING) {
          node.integrity = std::max(fx16{}, node.integrity - fire_damage_);
          k.pressure[2] += fire_damage_.raw;
        }
        for (const auto &rival : kingdoms_)
          if (rival.id != k.id) {
            const fx16 damage = layers_[ix(rival.id)].military.front()[node.cell] * war_;
            node.integrity = std::max(fx16{}, node.integrity - damage);
            k.pressure[4] += damage.raw;
          }
        if (k.stock[0] > UNIT && k.action.allocation[3] > q(3277) &&
            forest_.state()[node.cell] != BURNING && node.integrity < FX_ONE) {
          const fx16 repair = q(131);
          node.integrity = clamp(node.integrity + repair);
          k.stock[0] -= repair.raw;
        }
      }
      if (node.integrity.raw == 0) {
        node.destroyed = true;
        node.functional = false;
        node_at_[node.cell] = -1;
      }
    }
  if (tick_ % static_cast<uint64_t>(cfg_.world.macro_interval) == 0)
    for (auto &k : kingdoms_)
      if (k.alive)
        build(k.id);
}
void Civilization::prune(std::span<Node> nodes, std::span<const uint8_t> eligible, bool recover) {
  for (size_t i = 0; i < nodes.size(); ++i)
    nodes[i].functional = eligible[i] && (recover || nodes[i].functional);
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto &node : nodes)
      if (node.functional) {
        int count = 0;
        for (int dep : node.dependencies)
          if (dep >= 0 && static_cast<size_t>(dep) < nodes.size() && nodes[ix(dep)].functional)
            ++count;
        if (count < node.required) {
          node.functional = false;
          changed = true;
        }
      }
  }
}
void Civilization::cascade() {
  int before = 0;
  for (auto &node : nodes_) {
    before += node.functional;
    dependencies(node);
    eligible_[ix(node.id)] = static_cast<uint8_t>(
        !node.destroyed && kingdoms_[ix(node.owner)].alive && node.integrity >= integrity_min_ &&
        node.progress == FX_ONE &&
        (node.type == StructureType::Capital ||
         layers_[ix(node.owner)].supply[node.cell] <= cfg_.log.supply_max_cost));
  }
  prune(nodes_, {eligible_.data(), nodes_.size()},
        tick_ % static_cast<uint64_t>(cfg_.structures.recovery_interval) == 0);
  int after = 0;
  for (const auto &node : nodes_)
    after += node.functional;
  if (before > after)
    emit(EventType::Cascade, -1, -1, before - after);
}
void Civilization::economy() {
  for (auto &k : kingdoms_)
    if (k.alive) {
      auto &l = layers_[ix(k.id)];
      int64_t produced = 0;
      const int64_t old_grain = k.stock[2], old_treasury = k.stock[3];
      for (int i = 0; i < grid_.size(); ++i)
        if (owner_[i] == k.id && l.supply[i] != 65535 && l.pop.front()[i].raw > 0) {
          const fx16 labor = l.pop.front()[i];
          const fx16 wood = labor * k.action.allocation[0] * q(3277);
          if (rng_.cell(RngDomain::AgentExploration, tick_, i) <
                  static_cast<uint64_t>(wood.raw) * 65536 &&
              forest_.harvest(i))
            k.stock[0] += 5 * UNIT;
          const fx16 mine = labor * k.action.allocation[1];
          if (terrain_.stone[i] > 0 && rng_.cell(RngDomain::StructureFailure, tick_, i) <
                                           static_cast<uint64_t>(mine.raw) * 65536) {
            --terrain_.stone[i];
            k.stock[1] += UNIT;
          }
          fx16 grain =
              labor * k.action.allocation[2] * forest_.fertility()[i] * terrain_.moisture[i];
          const int node = node_at_[i];
          if (node >= 0 && nodes_[ix(node)].functional &&
              nodes_[ix(node)].type == StructureType::Farm)
            grain = grain * fx16::from_int(2);
          produced += grain.raw;
          forest_.farm(i,
                       soil_depletion_ * k.action.allocation[2] * labor * forest_.fertility()[i]);
        }
      k.stock[2] += produced;
      const int64_t demand = (k.stats.population + 2 * k.stats.military) * food_.raw / UNIT;
      fed_[ix(k.id)] =
          demand == 0
              ? FX_ONE
              : q(static_cast<int32_t>(std::min<int64_t>(65536, k.stock[2] * 65536 / demand)));
      k.stock[2] = std::max<int64_t>(0, k.stock[2] - demand);
      const int64_t tax = k.stats.population * (655 + k.action.allocation[7].raw / 10) / 65536;
      const int64_t admin = static_cast<int64_t>(k.stats.area) * 131 + k.stats.distance_sum * 4 +
                            static_cast<int64_t>(k.stats.perimeter) * 262 +
                            static_cast<int64_t>(k.stats.components) * 655;
      int64_t trade = 0;
      for (const auto &node : nodes_)
        if (node.owner == k.id && node.type == StructureType::Market && node.functional)
          trade += k.action.trade.raw / 4;
      const int64_t cost = admin + k.stats.population * k.action.allocation[5].raw / 65536 / 20 +
                           k.stats.population * k.action.quarantine.raw / 65536 / 20;
      if (k.stock[3] + tax + trade < cost)
        k.pressure[1] += cost - k.stock[3] - tax - trade;
      k.stock[3] = std::max<int64_t>(0, k.stock[3] + tax + trade - cost);
      k.stats.grain_balance = k.stock[2] - old_grain;
      k.stats.treasury_balance = k.stock[3] - old_treasury;
      if (produced == 0 && k.stock[2] == 0)
        ++k.starve_clock;
      else
        k.starve_clock = 0;
      k.pressure[0] += std::max<int64_t>(0, demand - produced);
    }
}
fx16 Civilization::carrying(int k, int cell) const {
  const auto &l = layers_[ix(k)];
  const fx16 supply = l.supply[cell] != 65535 ? fed_[ix(k)] : fx16{};
  return std::max(q(6554),
                  clamp(q(36045) * supply + q(16384) * terrain_.moisture[cell] + q(13107)));
}
void Civilization::population() {
  for (auto &k : kingdoms_) {
    auto &l = layers_[ix(k.id)];
    auto output = l.pop.back();
    for (int i = 0; i < grid_.size(); ++i) {
      if (!k.alive || !accessible(i)) {
        output.set(i, {});
        continue;
      }
      const fx16 n = l.pop.front()[i], capacity = carrying(k.id, i);
      fx16 lap{};
      grid_.neighbors4(i, [&](int j) {
        if (accessible(j))
          lap = lap + (l.pop.front()[j] - n);
      });
      const fx16 movement = diffusion_ * (FX_ONE - k.action.quarantine * q(49152));
      // Reaction and transport are split; the transport coefficient alone obeys D<=1/4.
      fx16 drift{};
      if (k.action.expand_dir < 8) {
        constexpr int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const int direction = k.action.expand_dir;
        grid_.neighbors4(i, [&](int j) {
          if (!accessible(j))
            return;
          const int along = (j % grid_.side() - i % grid_.side()) * dx[direction] +
                            (j / grid_.side() - i / grid_.side()) * dy[direction];
          if (along > 0)
            drift = drift - q(2621) * n;
          else if (along < 0)
            drift = drift + q(2621) * l.pop.front()[j];
        });
      }
      fx16 next = std::max(fx16{}, n + q(60293) * movement * lap + drift);
      fx16 reaction = growth_ * next * (FX_ONE - next / capacity);
      if (allee_.raw > 0)
        reaction = reaction * (next / allee_ - FX_ONE);
      const fx16 mortality = famine_ * (FX_ONE - fed_[ix(k.id)]) * next +
                             (forest_.state()[i] == BURNING ? q(6553) * next : fx16{});
      next = std::max(fx16{}, next + reaction - mortality);
      output.set(i, std::min(fx16::from_int(2), next));
    }
    l.pop.swap();
  }
}
void Civilization::epidemic() {
  fx16 market_infection{};
  int markets = 0;
  for (const auto &node : nodes_)
    if (node.functional && node.type == StructureType::Market &&
        kingdoms_[ix(node.owner)].action.trade.raw > 0) {
      market_infection = market_infection + layers_[ix(node.owner)].infected.front()[node.cell];
      ++markets;
    }
  if (markets)
    market_infection = market_infection / fx16::from_int(markets) * q(9830);
  for (auto &k : kingdoms_) {
    auto &l = layers_[ix(k.id)];
    auto infected = l.infected.back(), immune = l.immune.back();
    const fx16 trade_infection = market_infection * k.action.trade;
    for (int i = 0; i < grid_.size(); ++i) {
      const fx16 pop = l.pop.front()[i];
      fx16 old = std::min(pop, l.infected.front()[i]),
           recovered = std::min(pop - old, l.immune.front()[i]);
      if (pop.raw == 0) {
        infected.set(i, {});
        immune.set(i, {});
        continue;
      }
      fx16 local = old;
      int count = 1;
      grid_.neighbors4(i, [&](int j) {
        local = local + l.infected.front()[j];
        ++count;
      });
      local = local / fx16::from_int(count);
      if (old.raw == 0 &&
          rng_.cell(RngDomain::EpidemicSeed, tick_, i + grid_.size() * k.id) < spontaneous_)
        old = std::min(pop, q(655));
      const fx16 susceptible = std::max(fx16{}, pop - old - recovered);
      const fx16 transmission =
          std::min(susceptible, beta_ * (FX_ONE + q(52429) * pop) * susceptible *
                                    clamp((local + trade_infection) / pop) *
                                    (FX_ONE - k.action.quarantine * q(49152)));
      const fx16 recovery = gamma_ * old, death = delta_ * old;
      const fx16 next_pop = std::max(fx16{}, pop - death);
      l.pop.set_current(i, next_pop);
      k.pressure[3] += death.raw;
      const fx16 next_infected =
          std::min(next_pop, std::max(fx16{}, old + transmission - recovery - death));
      infected.set(i, next_infected);
      immune.set(i, std::min(next_pop - next_infected,
                             std::max(fx16{}, recovered + recovery - omega_ * recovered)));
    }
    l.infected.swap();
    l.immune.swap();
  }
}
void Civilization::combat() {
  for (auto &k : kingdoms_) {
    auto &l = layers_[ix(k.id)];
    auto next = l.military.back();
    bool barracks = false;
    for (const auto &node : nodes_)
      barracks = barracks ||
                 (node.owner == k.id && node.type == StructureType::Barracks && node.functional);
    for (int i = 0; i < grid_.size(); ++i) {
      if (!k.alive || !accessible(i)) {
        next.set(i, {});
        continue;
      }
      const fx16 own = l.military.front()[i];
      fx16 lap{}, attack{};
      grid_.neighbors4(i, [&](int j) {
        if (accessible(j))
          lap = lap + (l.military.front()[j] - own);
      });
      for (const auto &rival : kingdoms_)
        if (rival.id != k.id)
          attack = attack + layers_[ix(rival.id)].military.front()[i];
      fx16 recruit{};
      if (barracks && l.supply[i] != 65535) {
        recruit = k.action.allocation[4] * l.pop.front()[i] * q(655);
        const fx16 old_pop = l.pop.front()[i], remaining = std::max(fx16{}, old_pop - recruit);
        if (old_pop.raw > 0) {
          const fx16 retained = remaining / old_pop;
          l.infected.set_current(i, l.infected.front()[i] * retained);
          l.immune.set_current(i, l.immune.front()[i] * retained);
        }
        l.pop.set_current(i, remaining);
      }
      const int objective = k.action.military_target >= 0
                                ? kingdoms_[ix(k.action.military_target)].capital
                                : k.capital;
      fx16 drift{};
      grid_.neighbors4(i, [&](int j) {
        if (!accessible(j))
          return;
        const int a = distance(i, objective, grid_.side()),
                  b = distance(j, objective, grid_.side());
        if (b < a)
          drift = drift - q(2621) * own;
        else if (a < b)
          drift = drift + q(2621) * l.military.front()[j];
      });
      fx16 result = own + q(9830) * lap + drift + recruit - war_ * attack;
      if (l.supply[i] == 65535)
        result = result - q(983) * own;
      else
        result = result - q(131) * own;
      k.pressure[4] += (war_ * attack).raw;
      next.set(i, std::max(fx16{}, result));
    }
  }
  for (auto &l : layers_)
    l.military.swap();
}
void Civilization::claim() {
  for (auto &k : kingdoms_) {
    auto &l = layers_[ix(k.id)];
    auto next = l.claim.back();
    for (int i = 0; i < grid_.size(); ++i) {
      if (!accessible(i)) {
        next.set(i, {});
        continue;
      }
      const fx16 old = l.claim.front()[i];
      fx16 lap{}, rival{};
      grid_.neighbors4(i, [&](int j) {
        if (accessible(j))
          lap = lap + (l.claim.front()[j] - old);
      });
      for (const auto &other : kingdoms_)
        if (other.id != k.id)
          rival = rival + layers_[ix(other.id)].claim.front()[i];
      const fx16 generate = k.alive ? claim_gen_ * l.pop.front()[i] * (FX_ONE - old) : fx16{};
      next.set(i, clamp(old + generate + claim_diff_ * lap - claim_decay_ * old -
                        claim_contest_ * old * rival));
    }
  }
  for (auto &l : layers_)
    l.claim.swap();
  for (int i = 0; i < grid_.size(); ++i) {
    int owner = -1;
    fx16 best = claim_floor_;
    for (const auto &k : kingdoms_) {
      const auto value = layers_[ix(k.id)].claim.front()[i];
      if (value > best || (owner < 0 && value == best)) {
        best = value;
        owner = k.id;
      }
    }
    owner_[i] = static_cast<int16_t>(owner);
  }
}
void Civilization::refugees() {
  auto out = refugees_.back(), disease = refugee_infected_.back();
  for (int i = 0; i < grid_.size(); ++i) {
    if (!accessible(i)) {
      out.set(i, {});
      disease.set(i, {});
      for (auto &gene : refugee_genome_)
        gene.back().set(i, {});
      continue;
    }
    const fx16 old = refugees_.front()[i];
    fx16 lap{}, mass = old * (FX_ONE - ref_diff_ * fx16::from_int(4)),
                infection = refugee_infected_.front()[i] * (FX_ONE - ref_diff_ * fx16::from_int(4));
    std::array<int64_t, GENES> genes{};
    for (int g = 0; g < GENES; ++g)
      genes[ix(g)] = static_cast<int64_t>(mass.raw) * refugee_genome_[ix(g)].front()[i].raw;
    int neighbors = 0;
    grid_.neighbors4(i, [&](int j) {
      if (!accessible(j))
        return;
      ++neighbors;
      lap = lap + (refugees_.front()[j] - old);
      const fx16 incoming = ref_diff_ * refugees_.front()[j];
      infection = infection + ref_diff_ * refugee_infected_.front()[j];
      for (int g = 0; g < GENES; ++g)
        genes[ix(g)] += static_cast<int64_t>(incoming.raw) * refugee_genome_[ix(g)].front()[j].raw;
    });
    const fx16 retained = ref_diff_ * fx16::from_int(4 - neighbors) * old;
    for (int g = 0; g < GENES; ++g)
      genes[ix(g)] += static_cast<int64_t>(retained.raw) * refugee_genome_[ix(g)].front()[i].raw;
    infection =
        infection + ref_diff_ * fx16::from_int(4 - neighbors) * refugee_infected_.front()[i];
    fx16 total = std::max(fx16{}, old + ref_diff_ * lap);
    for (int g = 0; g < GENES; ++g)
      refugee_genome_[ix(g)].back().set(
          i, total.raw > 0 ? clamp(q(static_cast<int32_t>(genes[ix(g)] / total.raw))) : fx16{});
    total = std::max(fx16{}, total - ref_mortality_ * total);
    infection = std::min(total, std::max(fx16{}, infection - ref_mortality_ * infection));
    out.set(i, total);
    disease.set(i, infection);
  }
  refugees_.swap();
  refugee_infected_.swap();
  for (auto &gene : refugee_genome_)
    gene.swap();
  for (auto &k : kingdoms_)
    if (k.alive) {
      int64_t absorbed_total = 0;
      std::array<int64_t, GENES> gene_mass{};
      for (int i = 0; i < grid_.size(); ++i)
        if (owner_[i] == k.id && refugees_.front()[i].raw > 0) {
          auto &l = layers_[ix(k.id)];
          const fx16 pop = l.pop.front()[i], available = refugees_.front()[i];
          const fx16 taken = std::min(available, k.action.allocation[6] * ref_absorb_ *
                                                     std::max(fx16{}, carrying(k.id, i) - pop));
          if (taken.raw == 0)
            continue;
          const fx16 infected = refugee_infected_.front()[i] * (taken / available);
          l.pop.set_current(i, pop + taken);
          l.infected.set_current(i, l.infected.front()[i] + infected);
          refugees_.set_current(i, available - taken);
          refugee_infected_.set_current(i,
                                        std::max(fx16{}, refugee_infected_.front()[i] - infected));
          absorbed_total += taken.raw;
          for (int g = 0; g < GENES; ++g)
            gene_mass[ix(g)] +=
                static_cast<int64_t>(taken.raw) * refugee_genome_[ix(g)].front()[i].raw;
        }
      if (absorbed_total)
        for (int g = 0; g < GENES; ++g)
          k.genome[ix(g)] =
              q(static_cast<int32_t>((k.stats.population * k.genome[ix(g)].raw + gene_mass[ix(g)]) /
                                     (k.stats.population + absorbed_total)));
    }
  if (tick_ % 16 != 0)
    return;
  int slot = -1;
  for (const auto &k : kingdoms_)
    if (!k.alive) {
      slot = k.id;
      break;
    }
  if (slot < 0 || nodes_.size() >= ix(cfg_.simulation.max_nodes))
    return;
  visited_.fill(0);
  for (int i = 0; i < grid_.size(); ++i)
    if (owner_[i] < 0 && refugees_.front()[i].raw > 655 && !visited_[i]) {
      queue_.clear();
      queue_.push_back(i);
      visited_[i] = 1;
      int64_t total = 0;
      std::array<int64_t, GENES> gene_mass{};
      for (size_t p = 0; p < queue_.size(); ++p) {
        int j = queue_[p];
        total += refugees_.front()[j].raw;
        for (int g = 0; g < GENES; ++g)
          gene_mass[ix(g)] += static_cast<int64_t>(refugees_.front()[j].raw) *
                              refugee_genome_[ix(g)].front()[j].raw;
        grid_.neighbors4(j, [&](int v) {
          if (!visited_[v] && owner_[v] < 0 && refugees_.front()[v].raw > 655) {
            visited_[v] = 1;
            queue_.push_back(v);
          }
        });
      }
      if (total < found_threshold_)
        continue;
      bool near = false;
      for (const auto &k : kingdoms_)
        near = near || (k.alive && distance(k.capital, i, grid_.side()) < cfg_.ref.found_min_dist);
      if (near)
        continue;
      for (int g = 0; g < GENES; ++g)
        kingdoms_[ix(slot)].genome[ix(g)] = q(static_cast<int32_t>(gene_mass[ix(g)] / total));
      found(slot, i, true);
      for (int j : queue_) {
        layers_[ix(slot)].pop.set_current(j, refugees_.front()[j]);
        layers_[ix(slot)].infected.set_current(j, refugee_infected_.front()[j]);
        refugees_.set_current(j, {});
        refugee_infected_.set_current(j, {});
      }
      break;
    }
}
int Civilization::root_component(int k) {
  // Weak connectivity of the directed dependency graph, counted only on functional nodes.
  std::fill(eligible_.begin(), eligible_.end(), 0);
  int giant = 0;
  for (const auto &start : nodes_)
    if (start.owner == k && start.functional && !eligible_[ix(start.id)]) {
      queue_.clear();
      queue_.push_back(start.id);
      eligible_[ix(start.id)] = 1;
      for (size_t p = 0; p < queue_.size(); ++p) {
        const int id = queue_[p];
        for (const auto &node : nodes_)
          if (node.owner == k && node.functional && !eligible_[ix(node.id)]) {
            bool adjacent = false;
            for (int dep : node.dependencies)
              adjacent = adjacent || dep == id;
            for (int dep : nodes_[ix(id)].dependencies)
              adjacent = adjacent || dep == node.id;
            if (adjacent) {
              eligible_[ix(node.id)] = 1;
              queue_.push_back(node.id);
            }
          }
      }
      giant = std::max(giant, static_cast<int>(queue_.size()));
    }
  return giant;
}
void Civilization::summarize() {
  for (auto &k : kingdoms_) {
    const auto grain = k.stats.grain_balance, treasury = k.stats.treasury_balance;
    k.stats = {};
    k.stats.grain_balance = grain;
    k.stats.treasury_balance = treasury;
    const auto &l = layers_[ix(k.id)];
    for (int i = 0; i < grid_.size(); ++i) {
      k.stats.population += l.pop.front()[i].raw;
      k.stats.infected += l.infected.front()[i].raw;
      k.stats.military += l.military.front()[i].raw;
      if (owner_[i] == k.id) {
        ++k.stats.area;
        k.stats.distance_sum += distance(i, k.capital, grid_.side());
        bool border = i % grid_.side() == 0 || i % grid_.side() == grid_.side() - 1 ||
                      i < grid_.side() || i >= grid_.size() - grid_.side();
        grid_.neighbors4(i, [&](int j) { border = border || owner_[j] != k.id; });
        k.stats.perimeter += border;
      }
    }
    for (const auto &node : nodes_)
      if (node.owner == k.id && !node.destroyed) {
        ++k.stats.nodes;
        k.stats.functional += node.functional;
      }
    k.stats.giant = root_component(k.id);
    visited_.fill(0);
    for (int i = 0; i < grid_.size(); ++i)
      if (owner_[i] == k.id && !visited_[i]) {
        ++k.stats.components;
        queue_.clear();
        queue_.push_back(i);
        visited_[i] = 1;
        for (size_t p = 0; p < queue_.size(); ++p)
          grid_.neighbors4(queue_[p], [&](int j) {
            if (owner_[j] == k.id && !visited_[j]) {
              visited_[j] = 1;
              queue_.push_back(j);
            }
          });
      }
  }
}
void Civilization::collapse() {
  for (auto &k : kingdoms_)
    if (k.alive) {
      const int minimum =
          static_cast<int>(static_cast<int64_t>(k.stats.nodes) * giant_fraction_.raw / 65536);
      if (k.stats.giant <= minimum)
        ++k.collapse_clock;
      else
        k.collapse_clock = 0;
      bool capital = false;
      for (const auto &node : nodes_)
        capital = capital || (node.owner == k.id && node.type == StructureType::Capital &&
                              !node.destroyed && node.functional);
      if (!capital)
        ++k.capital_clock;
      else
        k.capital_clock = 0;
      const bool failed = k.collapse_clock >= cfg_.collapse.persist ||
                          k.capital_clock >= cfg_.collapse.capital_grace ||
                          k.starve_clock >= cfg_.collapse.starve_persist ||
                          k.stats.population < static_cast<int64_t>(allee_.raw) * 4;
      if (!failed)
        continue;
      int cause = 0;
      for (int c = 1; c < 5; ++c)
        if (k.pressure[ix(c)] > k.pressure[ix(cause)])
          cause = c;
      for (int i = 0; i < grid_.size(); ++i) {
        const fx16 pop = layers_[ix(k.id)].pop.front()[i] * scatter_, old = refugees_.front()[i],
                   total = old + pop;
        if (total.raw > 0)
          for (int g = 0; g < GENES; ++g)
            refugee_genome_[ix(g)].set_current(
                i, q(static_cast<int32_t>(
                       (static_cast<int64_t>(old.raw) * refugee_genome_[ix(g)].front()[i].raw +
                        static_cast<int64_t>(pop.raw) * k.genome[ix(g)].raw) /
                       total.raw)));
        refugees_.set_current(i, total);
        refugee_infected_.set_current(i, refugee_infected_.front()[i] +
                                             layers_[ix(k.id)].infected.front()[i] * scatter_);
        layers_[ix(k.id)].pop.set_current(i, {});
        layers_[ix(k.id)].infected.set_current(i, {});
        layers_[ix(k.id)].immune.set_current(i, {});
        layers_[ix(k.id)].military.set_current(i, {});
      }
      k.alive = false;
      k.stats.population = k.stats.military = k.stats.infected = 0;
      k.stats.functional = k.stats.giant = 0;
      for (auto &node : nodes_)
        if (node.owner == k.id)
          node.functional = false;
      emit(EventType::Collapse, k.id, k.capital, static_cast<int>(k.stats.population / 65536),
           cause);
    }
}
void Civilization::diplomacy() {
  for (auto &treaty : treaties_)
    if (treaty.proposer >= 0 && treaty.expires <= tick_) {
      treaty.active = false;
      treaty.proposer = -1;
    }
  for (auto &k : kingdoms_)
    if (k.alive) {
      const auto &action = k.action;
      const int partner = action.treaty_partner;
      if (partner < 0 || partner == k.id || !kingdoms_[ix(partner)].alive)
        continue;
      const int a = std::min(k.id, partner), b = std::max(k.id, partner);
      size_t pair = 0;
      for (int i = 0; i < a; ++i)
        pair += kingdoms_.size() - ix(i) - 1;
      pair += ix(b - a - 1);
      auto &treaty = treaties_[pair];
      if (action.treaty_action < 3 && !treaty.active && treaty.proposer < 0) {
        treaty = {k.id,
                  partner,
                  action.treaty_action,
                  k.id,
                  tick_ + static_cast<uint64_t>(cfg_.dip.treaty_duration),
                  false,
                  q(16384)};
      } else if (action.treaty_action == 3 && treaty.proposer == partner) {
        treaty.active = true;
        emit(EventType::Treaty, k.id, k.capital, treaty.type);
      } else if (action.treaty_action == 4 && treaty.active) {
        treaty.active = false;
        treaty.proposer = -1;
        kingdoms_[ix(partner)].reputation[ix(k.id)] = q(-65536);
        emit(EventType::Violation, k.id, k.capital, partner);
      }
    }
  for (auto &treaty : treaties_)
    if (treaty.active) {
      for (int side = 0; side < 2; ++side) {
        const int a = side == 0 ? treaty.a : treaty.b, b = side == 0 ? treaty.b : treaty.a;
        auto &actor = kingdoms_[ix(a)];
        auto &observer = kingdoms_[ix(b)];
        bool violation = false;
        if (treaty.type == 1)
          violation = actor.action.allocation[0] > treaty.quota;
        else
          for (int i = 0; i < grid_.size() && !violation; ++i)
            if (owner_[i] == b)
              violation = layers_[ix(a)].military.front()[i].raw > 655 ||
                          (treaty.type == 2 && layers_[ix(a)].claim.front()[i].raw > 19661);
        const fx16 detection =
            clamp(detection_base_ + detection_scale_ * observer.action.allocation[5]);
        if (violation && rng_.cell(RngDomain::AgentExploration, tick_, a * 12 + b) <
                             static_cast<uint64_t>(detection.raw) * 65536) {
          observer.reputation[ix(a)] = std::max(q(-65536), observer.reputation[ix(a)] - q(16384));
          emit(EventType::Violation, a, actor.capital, b);
        }
      }
    }
}
void Civilization::tick(uint64_t tick) {
  tick_ = tick;
  event_count_ = 0;
  if (tick_ % static_cast<uint64_t>(cfg_.world.macro_interval) == 0)
    policies();
  if (tick_ % 4 == 0)
    logistics();
  structures();
  cascade();
  economy();
  population();
  epidemic();
  combat();
  claim();
  refugees();
  summarize();
  collapse();
  if (tick_ % static_cast<uint64_t>(cfg_.world.macro_interval) == 0)
    diplomacy();
  // Rolling exponentially decayed pressure attribution, approximately 500 ticks.
  for (auto &k : kingdoms_)
    for (auto &p : k.pressure)
      p -= p / 500;
}
void Civilization::hash_into(Hasher &h) const {
  h.integer(tick_);
  owner_.hash_into(h);
  node_at_.hash_into(h);
  refugees_.front().hash_into(h);
  refugee_infected_.front().hash_into(h);
  for (const auto &gene : refugee_genome_)
    gene.front().hash_into(h);
  for (const auto &k : kingdoms_) {
    h.integer(static_cast<uint32_t>(k.id), 4);
    h.integer(static_cast<uint32_t>(k.capital), 4);
    h.integer(static_cast<uint32_t>(k.generation), 4);
    h.integer(k.alive, 1);
    h.integer(k.external_policy, 1);
    h.integer(static_cast<uint8_t>(k.policy), 1);
    for (auto v : k.stock)
      h.integer(static_cast<uint64_t>(v));
    for (auto v : k.genome)
      h.integer(static_cast<uint32_t>(v.raw), 4);
    for (auto v : k.reputation)
      h.integer(static_cast<uint32_t>(v.raw), 4);
    hash_action(h, k.action);
    for (int v : {k.collapse_clock, k.starve_clock, k.capital_clock})
      h.integer(static_cast<uint32_t>(v), 4);
    for (auto v : k.pressure)
      h.integer(static_cast<uint64_t>(v));
    const auto &l = layers_[ix(k.id)];
    l.pop.front().hash_into(h);
    l.claim.front().hash_into(h);
    l.military.front().hash_into(h);
    l.infected.front().hash_into(h);
    l.immune.front().hash_into(h);
    l.supply.hash_into(h);
  }
  h.integer(nodes_.size());
  for (const auto &node : nodes_) {
    for (int v : {node.id, node.owner, node.cell, node.integrity.raw, node.progress.raw})
      h.integer(static_cast<uint32_t>(v), 4);
    h.integer(static_cast<uint8_t>(node.type), 1);
    h.integer(node.functional, 1);
    h.integer(node.destroyed, 1);
    h.integer(node.required, 1);
    for (int d : node.dependencies)
      h.integer(static_cast<uint32_t>(d), 4);
  }
  for (const auto &t : treaties_) {
    for (int v : {t.a, t.b, t.type, t.proposer, t.quota.raw})
      h.integer(static_cast<uint32_t>(v), 4);
    h.integer(t.expires);
    h.integer(t.active, 1);
  }
}
} // namespace ashfall
