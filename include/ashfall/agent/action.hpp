#pragma once
#include "ashfall/core/fixed.hpp"
#include <array>
#include <span>
namespace ashfall {
constexpr int STRUCTURE_TYPES = 13, GENES = 8;
enum class StructureType : uint8_t {
  Capital,
  Farm,
  Granary,
  Woodcamp,
  Quarry,
  Kiln,
  Smithy,
  Barracks,
  Wall,
  Road,
  Depot,
  Market,
  Aqueduct
};
enum class Policy : uint8_t { Random, Greedy, Sustainer, Expansionist, TitForTat };
struct Action {
  // wood, stone, grain, construction, military, monitoring, openness, reserve
  std::array<fx16, 8> allocation{};
  std::array<fx16, STRUCTURE_TYPES> construction{};
  int expand_dir = 8, military_target = -1, treaty_partner = -1, treaty_action = 5;
  fx16 quarantine{}, trade = FX_ONE;
  static Action balanced();
  bool valid(int kingdoms) const;
};
struct StepResult {
  std::array<double, 12> rewards{};
  std::array<bool, 12> terminated{};
  bool truncated = false;
  uint64_t tick = 0;
};
} // namespace ashfall
