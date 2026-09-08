#pragma once
#include "ashfall/agent/action.hpp"
#include "ashfall/sim/forest.hpp"
#include <array>
namespace ashfall {
struct KingdomStats {
  int64_t population = 0, military = 0, infected = 0, grain_balance = 0, treasury_balance = 0;
  int area = 0, perimeter = 0, components = 0, nodes = 0, functional = 0, giant = 0;
  int64_t distance_sum = 0;
};
struct Kingdom {
  int id = 0, capital = 0, generation = 0;
  bool alive = true, external_policy = false;
  Policy policy = Policy::Sustainer;
  std::array<int64_t, 4> stock{}; // Q48.16 aggregates; never sum a world into Q16.16.
  std::array<fx16, GENES> genome{};
  std::array<fx16, 12> reputation{};
  Action action = Action::balanced();
  KingdomStats stats;
  int collapse_clock = 0, starve_clock = 0, capital_clock = 0;
  std::array<int64_t, 5> pressure{};
};
struct KingdomLayers {
  DoubleLayer<fx16> pop, claim, infected, immune, military;
  Layer<uint16_t> supply;
  explicit KingdomLayers(int n)
      : pop(n), claim(n), infected(n), immune(n), military(n), supply(n, 65535) {}
};
struct Node {
  int id = 0, owner = 0, cell = 0;
  StructureType type = StructureType::Capital;
  fx16 integrity = FX_ONE, progress = FX_ONE;
  bool functional = false, destroyed = false;
  uint8_t required = 0;
  std::array<int, 3> dependencies{-1, -1, -1};
};
struct Treaty {
  int a = 0, b = 0, type = 0, proposer = -1;
  uint64_t expires = 0;
  bool active = false;
  fx16 quota = fx16::from_raw(16384);
};
enum class EventType : uint8_t { Construction, Cascade, Collapse, Founding, Treaty, Violation };
struct KingdomEvent {
  uint64_t tick = 0;
  EventType type{};
  int kingdom = 0, cell = 0, magnitude = 0, cause = 0;
};
class Civilization {
public:
  Civilization(const Config &, TerrainLayers &, ForestSystem &, uint64_t seed);
  void tick(uint64_t tick);
  bool apply(std::span<const Action> actions);
  const std::vector<Kingdom> &kingdoms() const { return kingdoms_; }
  const KingdomLayers &layers(int k) const { return layers_[static_cast<size_t>(k)]; }
  const std::vector<Node> &nodes() const { return nodes_; }
  const Layer<int16_t> &owner() const { return owner_; }
  const Layer<fx16> &refugees() const { return refugees_.front(); }
  std::span<const KingdomEvent> events() const { return {events_.data(), event_count_}; }
  void hash_into(Hasher &) const;
  // Exposed deterministic graph primitive for reference and property tests.
  static void prune(std::span<Node> nodes, std::span<const uint8_t> eligible, bool recover);

private:
  void policies();
  void logistics();
  void structures();
  void cascade();
  void economy();
  void population();
  void epidemic();
  void combat();
  void claim();
  void refugees();
  void collapse();
  void diplomacy();
  void summarize();
  void found(int k, int cell, bool successor);
  void build(int k);
  void dependencies(Node &);
  bool accessible(int cell) const;
  int root_component(int kingdom);
  void emit(EventType, int, int, int, int = 0);
  fx16 carrying(int k, int cell) const;
  const Config &cfg_;
  TerrainLayers &terrain_;
  ForestSystem &forest_;
  Grid grid_;
  RngBank rng_;
  std::vector<Kingdom> kingdoms_;
  std::vector<KingdomLayers> layers_;
  std::vector<Node> nodes_;
  Layer<int16_t> owner_;
  Layer<int32_t> node_at_;
  Layer<uint8_t> visited_;
  DoubleLayer<fx16> refugees_, refugee_infected_;
  std::array<DoubleLayer<fx16>, GENES> refugee_genome_;
  std::vector<int> queue_, heap_, heap_position_;
  std::vector<uint8_t> eligible_;
  std::array<Treaty, 66> treaties_{};
  std::array<KingdomEvent, 2048> events_{};
  size_t event_count_ = 0;
  uint64_t tick_ = 0, spontaneous_ = 0;
  int64_t found_threshold_ = 0;
  fx16 giant_fraction_, detection_base_, detection_scale_;
  fx16 growth_, diffusion_, allee_, famine_, food_, claim_gen_, claim_diff_, claim_decay_,
      claim_contest_, claim_floor_, fire_damage_, decay_, integrity_min_, beta_, gamma_, delta_,
      omega_, war_, ref_diff_, ref_mortality_, ref_absorb_, scatter_, soil_depletion_;
  std::array<fx16, 12> fed_{};
};
} // namespace ashfall
