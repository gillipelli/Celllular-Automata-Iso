#pragma once
#include "ashfall/core/fixed.hpp"
#include <cstdint>
#include <string>
#include <string_view>
namespace ashfall {
struct Config {
  struct World {
    int size = 256, kingdoms = 4, macro_interval = 16, episode_ticks = 20000, digest_interval = 64,
        forest_warmup = 3000;
  } world;
  struct Forest {
    double p_growth = .02, f_lightning = .00002, base_spread = .65, ash_fertility = .15,
           dryness_amp = .35;
    int t_mature = 40, t_burn = 1, t_ash = 30, dryness_period = 2000;
  } forest;
  struct Terrain {
    int sea_level = 12, moisture_range = 64;
  } terrain;
  struct Soil {
    double r_recover = .0002, fertility_max = 1.;
  } soil;
  struct Simulation {
    int enabled = 1, max_nodes = 512, policy = 2;
  } simulation;
  struct Pop {
    double r_growth = .03, D_diffusion = .08, allee_A = .12, food_per_capita = .05,
           famine_rate = .02;
  } pop;
  struct Claim {
    double c_gen = .05, c_diff = .02, c_decay = .004, c_contest = .03, floor = .15;
  } claim;
  struct Structures {
    double integrity_min = .3, decay_unpaid = .006, fire_damage = .25;
    int recovery_interval = 8;
  } structures;
  struct Collapse {
    double giant_frac = .25, scatter_survival = .6;
    int persist = 100, capital_grace = 300, starve_persist = 150;
  } collapse;
  struct Refugee {
    double D_diffusion = .24, mortality = .008, absorb_rate = .05, found_threshold = 9.;
    int found_min_dist = 40;
  } ref;
  struct Epidemic {
    double beta = .28, gamma = .06, delta = .012, omega = .001, p_spontaneous = .0000005;
  } epi;
  struct War {
    double alpha = .04;
  } war;
  struct Logistics {
    int supply_max_cost = 900;
  } log;
  struct Diplomacy {
    int treaty_duration = 2000;
    double base_detect = .05, monitor_scale = .6;
  } dip;
  std::string validate() const;
  std::string canonical() const;
  uint64_t digest() const;
};
struct ConfigResult {
  Config config;
  std::string error;
  explicit operator bool() const { return error.empty(); }
};
ConfigResult parse_config(std::string_view text);
ConfigResult load_config(const std::string &path);
} // namespace ashfall
