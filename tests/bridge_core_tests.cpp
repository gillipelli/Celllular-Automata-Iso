// Exercise the C ABI and its private environment without adding production debug APIs.
#include "../src/bridge/c_api.cpp"
#include <doctest/doctest.h>
TEST_CASE("native mixed-control worlds match direct ticks") {
  ashfall::Config cfg;
  cfg.world.size = 64;
  cfg.world.forest_warmup = 0;
  ashfall::World mixed(cfg, 73), native(cfg, 73);
  std::array<ashfall::Action, 4> actions{};
  std::array<uint8_t, 4> mask{};
  for (int step = 0; step < 3; ++step) {
    mixed.step(actions, mask);
    for (int tick = 0; tick < cfg.world.macro_interval; ++tick)
      native.tick();
    CHECK(mixed.digest() == native.digest());
  }
  for (auto &action : actions)
    action = ashfall::Action::balanced();
  mixed.step(actions);
  mixed.step(actions, mask);
  for (const auto &kingdom : mixed.civilization()->kingdoms())
    CHECK_FALSE(kingdom.external_policy);
}
TEST_CASE("partial summary does not expose hidden rival state or unrelated treaties") {
  void *handle = ashfall_create("[world]\nsize=64\nforest_warmup=0\n", 97, 1);
  REQUIRE(handle != nullptr);
  auto &env = *static_cast<Environment *>(handle);
  auto *civ = const_cast<ashfall::Civilization *>(env.world->civilization());
  const std::string before = ashfall_metadata(handle, 0);
  auto &rival = const_cast<ashfall::Kingdom &>(civ->kingdoms()[1]);
  rival.stock[0] += 500000;
  rival.stats.population += 90000;
  rival.generation += 4;
  rival.alive = false;
  auto &treaty = const_cast<ashfall::Treaty &>(civ->treaties()[3]);
  treaty = {1, 2, 1, 1, 1000, true, ashfall::FX_ONE};
  CHECK(std::string(ashfall_metadata(handle, 0)) == before);
  ashfall_destroy(handle);
}
TEST_CASE("mixed C ABI validates every external action before mutation") {
  void *handle = ashfall_create("[world]\nsize=64\nforest_warmup=0\n", 12, 1);
  REQUIRE(handle != nullptr);
  std::array<float, 108> actions;
  actions.fill(1.f);
  for (int k = 0; k < 4; ++k) {
    actions[static_cast<size_t>(k * 27 + 21)] = 8;
    actions[static_cast<size_t>(k * 27 + 22)] = -1;
    actions[static_cast<size_t>(k * 27 + 23)] = -1;
    actions[static_cast<size_t>(k * 27 + 24)] = 5;
  }
  std::array<uint8_t, 4> mask{1, 1, 1, 1};
  std::array<double, 4> reward{};
  std::array<uint8_t, 5> done{};
  auto digest = ashfall_digest(handle);
  actions[107] = 2;
  CHECK_FALSE(ashfall_step_mixed(handle, actions.data(), actions.size(), mask.data(), mask.size(),
                                 reward.data(), done.data()));
  CHECK(ashfall_digest(handle) == digest);
  mask[3] = 0;
  CHECK(ashfall_step_mixed(handle, actions.data(), actions.size(), mask.data(), mask.size(),
                           reward.data(), done.data()));
  ashfall_destroy(handle);
}
