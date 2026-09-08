#include "ashfall/core/config.hpp"
#include "ashfall/core/hash.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sstream>
#include <toml++/toml.hpp>
namespace ashfall {
std::string Config::validate() const {
  if (world.size < 64 || world.size > 1024)
    return "world.size must be in [64,1024]";
  if (world.kingdoms < 1 || world.kingdoms > 12)
    return "world.kingdoms must be in [1,12]";
  if (world.macro_interval < 4 || world.macro_interval > 64)
    return "world.macro_interval must be in [4,64]";
  if (world.episode_ticks < 2000 || world.episode_ticks > 200000)
    return "world.episode_ticks must be in [2000,200000]";
  if (world.digest_interval < 1 || world.forest_warmup < 0 || world.forest_warmup > 200000)
    return "invalid digest interval or warmup";
  for (double p : {forest.p_growth, forest.f_lightning, forest.base_spread, forest.ash_fertility,
                   forest.dryness_amp, soil.r_recover, soil.fertility_max})
    if (!std::isfinite(p) || p < 0 || p > 1)
      return "probabilities and normalized parameters must be finite and in [0,1]";
  if (forest.t_mature < 1 || forest.t_mature > 255 || forest.t_burn < 1 || forest.t_burn > 255 ||
      forest.t_ash < 1 || forest.t_ash > 255)
    return "forest durations must be in [1,255]";
  if (forest.dryness_period < 4 || forest.dryness_period > 200000)
    return "forest.dryness_period must be in [4,200000]";
  if (terrain.sea_level < 0 || terrain.sea_level > 63 || terrain.moisture_range < 1 ||
      terrain.moisture_range > 2048)
    return "invalid terrain parameters";

  if (simulation.enabled < 0 || simulation.enabled > 1 || simulation.max_nodes < world.kingdoms ||
      simulation.max_nodes > 4096 || simulation.policy < 0 || simulation.policy > 5)
    return "invalid simulation settings";
  for (double v : {pop.r_growth,
                   pop.D_diffusion,
                   pop.allee_A,
                   pop.food_per_capita,
                   pop.famine_rate,
                   claim.c_gen,
                   claim.c_diff,
                   claim.c_decay,
                   claim.c_contest,
                   claim.floor,
                   structures.integrity_min,
                   structures.decay_unpaid,
                   structures.fire_damage,
                   collapse.giant_frac,
                   collapse.scatter_survival,
                   ref.D_diffusion,
                   ref.mortality,
                   ref.absorb_rate,
                   epi.beta,
                   epi.gamma,
                   epi.delta,
                   epi.omega,
                   epi.p_spontaneous,
                   war.alpha,
                   dip.base_detect,
                   dip.monitor_scale})
    if (!std::isfinite(v) || v < 0 || v > 1)
      return "simulation rates must be finite in [0,1]";
  if (pop.D_diffusion > .25 || ref.D_diffusion > .25 || claim.c_diff > .25)
    return "diffusion exceeds stability bound";
  if (pop.allee_A >= 1 || pop.r_growth > .1 || (pop.allee_A > 0 && pop.allee_A < .01))
    return "invalid population reaction parameters";
  if (log.supply_max_cost < 1 || log.supply_max_cost > 60000 || structures.recovery_interval < 1 ||
      collapse.persist < 1 || collapse.capital_grace < 1 || collapse.starve_persist < 1 ||
      dip.treaty_duration < 1 || ref.found_min_dist < 0 || !std::isfinite(ref.found_threshold) ||
      ref.found_threshold <= 0 || ref.found_threshold > 1000)
    return "invalid simulation thresholds";
  return {};
}
// clang-format off
#define CONFIG_FIELDS(I, F) \
  I(world, size) \
  I(world, kingdoms) \
  I(world, macro_interval) \
  I(world, episode_ticks) \
  I(world, digest_interval) \
  I(world, forest_warmup) \
  F(forest, p_growth) \
  F(forest, f_lightning) \
  F(forest, base_spread) \
  F(forest, ash_fertility) \
  F(forest, dryness_amp) \
  I(forest, t_mature) \
  I(forest, t_burn) \
  I(forest, t_ash) \
  I(forest, dryness_period) \
  I(terrain, sea_level) \
  I(terrain, moisture_range) \
  F(soil, r_recover) \
  F(soil, fertility_max) \
  I(simulation, enabled) \
  I(simulation, max_nodes) \
  I(simulation, policy) \
  F(pop, r_growth) \
  F(pop, D_diffusion) \
  F(pop, allee_A) \
  F(pop, food_per_capita) \
  F(pop, famine_rate) \
  F(claim, c_gen) \
  F(claim, c_diff) \
  F(claim, c_decay) \
  F(claim, c_contest) \
  F(claim, floor) \
  F(structures, integrity_min) \
  F(structures, decay_unpaid) \
  F(structures, fire_damage) \
  I(structures, recovery_interval) \
  F(collapse, giant_frac) \
  F(collapse, scatter_survival) \
  I(collapse, persist) \
  I(collapse, capital_grace) \
  I(collapse, starve_persist) \
  F(ref, D_diffusion) \
  F(ref, mortality) \
  F(ref, absorb_rate) \
  F(ref, found_threshold) \
  I(ref, found_min_dist) \
  F(epi, beta) \
  F(epi, gamma) \
  F(epi, delta) \
  F(epi, omega) \
  F(epi, p_spontaneous) \
  F(war, alpha) \
  I(log, supply_max_cost) \
  I(dip, treaty_duration) \
  F(dip, base_detect) \
  F(dip, monitor_scale)
// clang-format on
std::string Config::canonical() const {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17);
#define WRITE(s, k) out << #s "." #k "=" << s.k << '\n';
  CONFIG_FIELDS(WRITE, WRITE)
#undef WRITE
  return out.str();
}
uint64_t Config::digest() const { return hash_text(canonical()); }
ConfigResult parse_config(std::string_view text) {
  ConfigResult result;
  auto parsed = toml::parse(text);
  if (!parsed) {
    result.error = std::string(parsed.error().description());
    return result;
  }
  for (auto &&[section, node] : parsed.table()) {
    auto *table = node.as_table();
    if (!table) {
      result.error = "expected section: " + std::string(section.str());
      return result;
    }
    const auto section_name = section.str();
    if (section_name != "world" && section_name != "forest" && section_name != "terrain" &&
        section_name != "soil" && section_name != "simulation" && section_name != "pop" &&
        section_name != "claim" && section_name != "structures" && section_name != "collapse" &&
        section_name != "ref" && section_name != "epi" && section_name != "war" &&
        section_name != "log" && section_name != "dip") {
      result.error = "unknown or not yet implemented section: " + std::string(section_name);
      return result;
    }
    for (auto &&[key, value] : *table) {
      const std::string name = std::string(section.str()) + "." + std::string(key.str());
      bool found = false;
#define READ_INT(s, k)                                                                             \
  if (name == #s "." #k) {                                                                         \
    auto v = value.value<int64_t>();                                                               \
    if (!value.is_integer() || !v || *v < INT32_MIN || *v > INT32_MAX) {                           \
      result.error = "expected integer: " + name;                                                  \
      return result;                                                                               \
    }                                                                                              \
    result.config.s.k = static_cast<int>(*v);                                                      \
    found = true;                                                                                  \
  }
#define READ_FLOAT(s, k)                                                                           \
  if (name == #s "." #k) {                                                                         \
    auto v = value.value<double>();                                                                \
    if (!v || (!value.is_integer() && !value.is_floating_point())) {                               \
      result.error = "expected number: " + name;                                                   \
      return result;                                                                               \
    }                                                                                              \
    result.config.s.k = *v;                                                                        \
    found = true;                                                                                  \
  }
      CONFIG_FIELDS(READ_INT, READ_FLOAT)
#undef READ_INT
#undef READ_FLOAT
      if (!found) {
        result.error = "unknown key: " + name;
        return result;
      }
    }
  }
  result.error = result.config.validate();
  return result;
}
ConfigResult load_config(const std::string &path) {
  std::ifstream in(path);
  if (!in)
    return {{}, "cannot read config: " + path};
  return parse_config(std::string(std::istreambuf_iterator<char>(in), {}));
}
} // namespace ashfall
