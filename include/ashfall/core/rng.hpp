#pragma once
#include <cstdint>
#include <pcg_random.hpp>
namespace ashfall {
inline constexpr uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}
enum class RngDomain : uint16_t {
  ForestGrowth,
  ForestLightning,
  FireSpread,
  SoilNoise,
  PopulationNoise,
  EpidemicSeed,
  EpidemicSpread,
  CombatNoise,
  RefugeeWander,
  StructureFailure,
  Mutation,
  AgentExploration,
  ScenarioInit
};
using Pcg64 = pcg64;
class RngBank {
public:
  explicit RngBank(uint64_t seed) : seed_(seed) {}
  uint32_t cell(RngDomain d, uint64_t tick, int idx) const {
    uint64_t h = splitmix64(seed_ ^ (static_cast<uint64_t>(d) * 0x9E3779B97F4A7C15ull));
    h = splitmix64(h ^ tick);
    h = splitmix64(h ^ static_cast<uint64_t>(idx));
    return static_cast<uint32_t>(h >> 32);
  }
  Pcg64 stream(RngDomain d, int kingdom, uint64_t tick) const {
    const auto key = splitmix64(seed_ ^ static_cast<uint64_t>(d));
    return Pcg64(splitmix64(key ^ tick), splitmix64(key ^ static_cast<uint64_t>(kingdom)));
  }

private:
  uint64_t seed_;
};
} // namespace ashfall
