#include "ashfall/sim/world.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#ifdef _WIN32
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C" __attribute__((visibility("default")))
#endif
namespace {
thread_local std::string error;
struct Environment {
  ashfall::Config cfg;
  std::unique_ptr<ashfall::World> world;
  int threads = 1;
  uint64_t seed = 1;
};
constexpr int CROP = 48, CHANNELS = 20, SCALARS = 64, OBS = 2 * CROP * CROP * CHANNELS + SCALARS,
              ACTIONS = 27;
float scalar(ashfall::fx16 v) { return static_cast<float>(v.raw) / 65536.f; }
void channels(const ashfall::World &world, int k, int cell, float *out) {
  using namespace ashfall;
  const auto &civ = *world.civilization();
  const auto &l = civ.layers(k);
  const auto &t = world.terrain();
  const auto &f = world.forest();
  const bool coverage = l.claim.front()[cell].raw > 3277 || l.pop.front()[cell].raw > 655;
  out[0] = scalar(l.pop.front()[cell]);
  out[1] = scalar(l.claim.front()[cell]);
  out[2] = scalar(l.military.front()[cell]);
  out[3] = l.supply[cell] == 65535
               ? 0.f
               : 1.f - static_cast<float>(l.supply[cell]) /
                           static_cast<float>(world.config().log.supply_max_cost);
  out[4] = l.pop.front()[cell].raw > 0 ? scalar(l.infected.front()[cell]) / out[0] : 0;
  if (coverage)
    for (const auto &rival : civ.kingdoms())
      if (rival.id != k) {
        const auto &other = civ.layers(rival.id);
        out[5] += scalar(other.pop.front()[cell]);
        out[6] += scalar(other.claim.front()[cell]);
        out[7] += scalar(other.military.front()[cell]);
      }
  out[8] = scalar(civ.refugees()[cell]);
  out[9] = f.state()[cell] == TREE ? 1.f : f.state()[cell] == SAPLING ? .5f : 0.f;
  out[10] = f.state()[cell] == BURNING ? 1.f : 0.f;
  out[11] = static_cast<float>(t.stone[cell]) / 65535.f;
  out[12] = scalar(f.fertility()[cell]);
  out[13] = scalar(t.moisture[cell]);
  out[14] = static_cast<float>(t.elevation[cell]) / 63.f;
  out[18] = coverage ? 1.f : 0.f;
  out[19] = out[9] * (1.f - .7f * out[13]);
  for (const auto &node : civ.nodes())
    if (!node.destroyed && node.cell == cell) {
      if (node.owner == k) {
        out[15] = 1;
        out[17] = node.functional ? 1.f : 0.f;
      } else if (coverage)
        out[16] = 1;
    }
}
} // namespace
API const char *ashfall_error() { return error.c_str(); }
API void *ashfall_create(const char *config, uint64_t seed, int threads) {
  error.clear();
  auto result = ashfall::parse_config(config ? config : "");
  if (!result) {
    error = result.error;
    return nullptr;
  }
  if (!result.config.simulation.enabled) {
    error = "training requires simulation.enabled=1";
    return nullptr;
  }
  if (threads < 1 || threads > 32) {
    error = "threads must be in [1,32]";
    return nullptr;
  }
  auto env = std::make_unique<Environment>();
  env->cfg = result.config;
  env->seed = seed;
  env->threads = threads;
  env->world = std::make_unique<ashfall::World>(env->cfg, seed, threads);
  return env.release();
}
API void ashfall_destroy(void *handle) { delete static_cast<Environment *>(handle); }
API int ashfall_agents(void *handle) {
  return handle ? static_cast<Environment *>(handle)->cfg.world.kingdoms : 0;
}
API int ashfall_observation_size() { return OBS; }
API int ashfall_action_size() { return ACTIONS; }
API uint64_t ashfall_digest(void *handle) {
  return handle ? static_cast<Environment *>(handle)->world->digest() : 0;
}
API int ashfall_reset(void *handle, uint64_t seed) {
  if (!handle)
    return 0;
  auto &env = *static_cast<Environment *>(handle);
  env.seed = seed;
  env.world = std::make_unique<ashfall::World>(env.cfg, seed, env.threads);
  return 1;
}
API int ashfall_observe(void *handle, float *output, size_t count) {
  if (!handle || !output)
    return 0;
  const auto &env = *static_cast<Environment *>(handle);
  const auto &world = *env.world;
  const auto &civ = *world.civilization();
  if (count != static_cast<size_t>(OBS * env.cfg.world.kingdoms)) {
    error = "wrong observation buffer size";
    return 0;
  }
  std::fill(output, output + count, 0.f);
  const int n = env.cfg.world.size;
  for (const auto &k : civ.kingdoms()) {
    float *base = output + k.id * OBS;
    for (int y = 0; y < CROP; ++y)
      for (int x = 0; x < CROP; ++x) {
        const int cx = k.capital % n + x - CROP / 2, cy = k.capital / n + y - CROP / 2;
        float local[CHANNELS]{};
        if (cx >= 0 && cy >= 0 && cx < n && cy < n)
          channels(world, k.id, cy * n + cx, local);
        for (int ch = 0; ch < CHANNELS; ++ch)
          base[ch * CROP * CROP + y * CROP + x] = local[ch];
        float global[CHANNELS]{};
        int cells = 0;
        for (int gy = y * n / CROP; gy < (y + 1) * n / CROP; ++gy)
          for (int gx = x * n / CROP; gx < (x + 1) * n / CROP; ++gx) {
            float values[CHANNELS]{};
            channels(world, k.id, gy * n + gx, values);
            for (int ch = 0; ch < CHANNELS; ++ch)
              global[ch] += values[ch];
            ++cells;
          }
        for (int ch = 0; ch < CHANNELS; ++ch)
          base[(CHANNELS + ch) * CROP * CROP + y * CROP + x] =
              cells ? global[ch] / static_cast<float>(cells) : 0.f;
      }
    float *scalar_out = base + 2 * CHANNELS * CROP * CROP;
    for (size_t r = 0; r < 4; ++r)
      scalar_out[r] = static_cast<float>(k.stock[r]) / 65536.f / 1000.f;
    scalar_out[4] = static_cast<float>(k.stats.population) / 65536.f / 1000.f;
    scalar_out[5] = static_cast<float>(k.stats.area) / static_cast<float>(n * n);
    scalar_out[6] = k.alive ? 1.f : 0.f;
    scalar_out[7] =
        static_cast<float>(world.current_tick()) / static_cast<float>(env.cfg.world.episode_ticks);
    scalar_out[8] = k.stats.nodes
                        ? static_cast<float>(k.stats.functional) / static_cast<float>(k.stats.nodes)
                        : 0;
    for (size_t g = 0; g < 8; ++g)
      scalar_out[9 + g] = scalar(k.genome[g]);
    for (const auto &rival : civ.kingdoms())
      scalar_out[17 + rival.id] = scalar(k.reputation[static_cast<size_t>(rival.id)]);
  }
  return 1;
}
API int ashfall_step(void *handle, const float *input, size_t count, double *rewards,
                     uint8_t *done) {
  using namespace ashfall;
  if (!handle || !input || !rewards || !done)
    return 0;
  auto &env = *static_cast<Environment *>(handle);
  if (count != static_cast<size_t>(ACTIONS * env.cfg.world.kingdoms)) {
    error = "wrong action buffer size";
    return 0;
  }
  if (env.world->current_tick() >= static_cast<uint64_t>(env.cfg.world.episode_ticks)) {
    error = "episode finished; reset before stepping";
    return 0;
  }
  std::array<Action, 12> actions;
  for (int k = 0; k < env.cfg.world.kingdoms; ++k) {
    const float *values = input + k * ACTIONS;
    auto &action = actions[static_cast<size_t>(k)];
    for (int j = 0; j < ACTIONS; ++j)
      if (!std::isfinite(values[j])) {
        error = "non-finite action";
        return 0;
      }
    auto simplex = [&](auto &out, int offset) {
      double sum = 0;
      for (size_t j = 0; j < out.size(); ++j) {
        if (values[offset + static_cast<int>(j)] < 0)
          return false;
        sum += values[offset + static_cast<int>(j)];
      }
      if (sum <= 0)
        return false;
      int accumulated = 0;
      for (size_t j = 0; j + 1 < out.size(); ++j) {
        out[j] = fx16::from_raw(static_cast<int32_t>(
            static_cast<double>(values[offset + static_cast<int>(j)]) * 65536 / sum));
        accumulated += out[j].raw;
      }
      out.back() = fx16::from_raw(65536 - accumulated);
      return true;
    };
    if (!simplex(action.allocation, 0) || !simplex(action.construction, 8)) {
      error = "action allocations must be nonnegative with positive sums";
      return 0;
    }
    for (int j = 21; j < 25; ++j)
      if (values[j] != std::floor(values[j]) || values[j] < -1 || values[j] > 12) {
        error = "invalid categorical action";
        return 0;
      }
    action.expand_dir = static_cast<int>(values[21]);
    action.military_target = static_cast<int>(values[22]);
    action.treaty_partner = static_cast<int>(values[23]);
    action.treaty_action = static_cast<int>(values[24]);
    if (values[25] < 0 || values[25] > 1 || values[26] < 0 || values[26] > 1) {
      error = "quarantine and trade must be in [0,1]";
      return 0;
    }
    action.quarantine = fx16::from_double(values[25]);
    action.trade = fx16::from_double(values[26]);
    if (!action.valid(env.cfg.world.kingdoms)) {
      error = "action category out of range";
      return 0;
    }
  }
  const auto result =
      env.world->step({actions.data(), static_cast<size_t>(env.cfg.world.kingdoms)});
  for (int k = 0; k < env.cfg.world.kingdoms; ++k) {
    rewards[k] = result.rewards[static_cast<size_t>(k)];
    done[k] = static_cast<uint8_t>(result.terminated[static_cast<size_t>(k)]);
  }
  done[env.cfg.world.kingdoms] = static_cast<uint8_t>(result.truncated);
  return 1;
}
