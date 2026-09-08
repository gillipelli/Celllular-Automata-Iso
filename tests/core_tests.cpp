#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "ashfall/core/grid.hpp"
#include "ashfall/sim/world.hpp"
#include <doctest/doctest.h>
#include <thread>
using namespace ashfall;
TEST_CASE("fixed-point boundaries and signed arithmetic") {
  CHECK((fx16::from_int(-3) * fx16::from_double(.5)).raw == -98304);
  CHECK((fx16::from_int(-3) / fx16::from_int(2)).raw == -98304);
  CHECK((fx16::from_raw(INT32_MAX) + FX_ONE).raw == INT32_MAX);
  CHECK((-fx16::from_raw(INT32_MIN)).raw == INT32_MAX);
  CHECK((fx16::from_raw(-1) * fx16::from_raw(1)).raw == -1);
  CHECK(fx16::from_int(65536).raw == INT32_MAX);
  CHECK(fx16::from_double(-32768).raw == INT32_MIN);
}
TEST_CASE("fixed boundaries and aligned double buffering") {
  Grid g(64);
  int count = 0;
  g.neighbors4(0, [&](int) { ++count; });
  CHECK(count == 2);
  count = 0;
  g.neighbors8(65, [&](int) { ++count; });
  CHECK(count == 8);
  Layer<fx16> l(64);
  CHECK(reinterpret_cast<uintptr_t>(l.data()) % 64 == 0);
  DoubleLayer<uint8_t> d(64);
  d.back().set(0, 7);
  CHECK(d.front()[0] == 0);
  d.swap();
  CHECK(d.front()[0] == 7);
}
TEST_CASE("TOML strict types, unknown keys, canonical defaults") {
  CHECK(parse_config("[world]\nsize=128").config.world.size == 128);
  CHECK_FALSE(parse_config("[world]\nsize=128.0"));
  CHECK_FALSE(parse_config("[world]\nszie=128"));
  CHECK_FALSE(parse_config("[forest]\np_growth=nan"));
  CHECK_FALSE(parse_config("[world]\nsize=1025"));
  CHECK_FALSE(parse_config("[unused]"));
  CHECK_FALSE(parse_config("[world]\nsize=true"));
  CHECK(parse_config("[forest]\np_growth=.02").error.size() > 0);
  CHECK(parse_config("[forest]\np_growth=0.02").config.digest() == Config{}.digest());
}
TEST_CASE("cell RNG confluence across threads and reverse traversal") {
  RngBank rng(42);
  Layer<uint32_t> expected(64);
  for (int i = 0; i < 4096; ++i)
    expected[i] = rng.cell(RngDomain::ForestGrowth, 99, i);
  for (int threads : {1, 2, 4, 8}) {
    Layer<uint32_t> actual(64);
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
      workers.emplace_back([&, t] {
        for (int i = (t + 1) * 4096 / threads - 1; i >= t * 4096 / threads; --i)
          actual[i] = rng.cell(RngDomain::ForestGrowth, 99, i);
      });
    for (auto &w : workers)
      w.join();
    CHECK(actual.digest() == expected.digest());
  }
  auto a = rng.stream(RngDomain::Mutation, 1, 5), b = rng.stream(RngDomain::Mutation, 1, 5);
  for (int i = 0; i < 100; ++i)
    CHECK(a() == b());
  CHECK(rng.cell(RngDomain::ForestGrowth, 99, 0) != rng.cell(RngDomain::ForestLightning, 99, 0));
}
TEST_CASE("10000 empty ticks reproducible") {
  Config cfg;
  World a(cfg, 42, 1, true);
  for (int i = 0; i < 10000; ++i)
    a.tick();
  for (int threads : {1, 2, 4, 8}) {
    World b(cfg, 42, threads, true);
    for (int i = 0; i < 10000; ++i)
      b.tick();
    CHECK(a.digest() == b.digest());
  }
  CHECK(a.current_tick() == 10000);
}
TEST_CASE("forest field and event accounting are confluent") {
  Config cfg;
  cfg.world.size = 64;
  cfg.world.forest_warmup = 0;
  cfg.forest.p_growth = .2;
  cfg.forest.f_lightning = .01;
  cfg.forest.base_spread = 1.;
  TerrainLayers terrain(cfg, 42);
  ForestSystem a(cfg, terrain, 42);
  ThreadPool single(64, 1);
  std::vector<uint64_t> checkpoints;
  for (int t = 0; t < 300; ++t) {
    a.step(static_cast<uint64_t>(t), single);
    Hasher h;
    a.hash_into(h);
    checkpoints.push_back(h.finish());
  }
  for (int count : {1, 2, 4, 8}) {
    ForestSystem b(cfg, terrain, 42);
    ThreadPool pool(64, count);
    uint64_t completed_size = 0;
    for (int t = 0; t < 300; ++t) {
      b.step(static_cast<uint64_t>(t), pool, true);
      Hasher h;
      b.hash_into(h);
      CHECK(h.finish() == checkpoints[static_cast<size_t>(t)]);
      for (const auto &e : b.events()) {
        CHECK(e.size > 0);
        CHECK(e.end_tick > e.start_tick);
        completed_size += e.size;
      }
      for (int i = 0; i < 4096; ++i) {
        if (terrain.terrain[i] == WATER)
          CHECK(b.state()[i] == EMPTY);
        REQUIRE(b.fertility()[i].raw >= 0);
        REQUIRE(b.fertility()[i].raw <= 65536);
      }
      CHECK(completed_size <= b.metrics().burned);
    }
    CHECK(completed_size > 0);
  }
}
TEST_CASE("zero lightning prevents spontaneous fires") {
  Config cfg;
  cfg.world.size = 64;
  cfg.world.forest_warmup = 0;
  cfg.forest.f_lightning = 0;
  World world(cfg, 10);
  for (int t = 0; t < 100; ++t)
    world.tick();
  CHECK(world.forest().metrics().burning == 0);
  CHECK(world.forest().metrics().events == 0);
}
TEST_CASE("canonical config round trip") {
  Config cfg;
  cfg.forest.f_lightning = 5e-7;
  auto parsed = parse_config(cfg.canonical());
  REQUIRE(parsed);
  CHECK(parsed.config.digest() == cfg.digest());
}
TEST_CASE("forest ignition, burn duration, ash recovery and accounting") {
  Config cfg;
  cfg.world.size = 64;
  cfg.forest.p_growth = 0;
  cfg.forest.f_lightning = 1;
  cfg.forest.base_spread = 0;
  cfg.soil.r_recover = 0;
  cfg.forest.t_burn = 1;
  cfg.forest.t_ash = 2;
  TerrainLayers terrain(cfg, 7);
  ForestSystem forest(cfg, terrain, 7);
  ThreadPool pool(64, 1);
  const auto trees = forest.metrics().trees;
  REQUIRE(trees > 0);
  forest.step(0, pool);
  CHECK(forest.metrics().burning == trees);
  CHECK(forest.events().empty());
  forest.step(1, pool);
  CHECK(forest.metrics().burning == 0);
  CHECK(forest.metrics().ash == trees);
  uint64_t sizes = 0;
  for (const auto &e : forest.events())
    sizes += e.size;
  CHECK(sizes == trees);
  CHECK(sizes == forest.metrics().burned);
  for (int i = 0; i < 4096; ++i)
    if (forest.state()[i] == ASH) {
      CHECK(forest.fertility()[i] ==
            std::min(FX_ONE,
                     terrain.initial_fertility[i] + fx16::from_double(cfg.forest.ash_fertility)));
    }
  forest.step(2, pool);
  CHECK(forest.metrics().ash == trees);
  forest.step(3, pool);
  CHECK(forest.metrics().ash == 0);
}
#include "ashfall/core/fixed_math.hpp"
#include "ashfall/core/iso.hpp"
TEST_CASE("fixed sine and projection round trips") {
  for (int i = 0; i < 2000; ++i)
    CHECK(std::abs(sine_phase(static_cast<uint64_t>(i), 2000).to_double() -
                   std::sin(i * 6.283185307179586 / 2000)) < 1. / 4096);
  for (int x = 0; x < 64; ++x)
    for (int y = 0; y < 64; ++y) {
      const auto p = project(static_cast<float>(x), static_cast<float>(y)), q = unproject(p.x, p.y);
      CHECK(q.x == static_cast<float>(x));
      CHECK(q.y == static_cast<float>(y));
    }
}
TEST_CASE("cascade computes greatest fixed point and pressure is monotone") {
  std::array<Node, 4> nodes{};
  for (int i = 0; i < 4; ++i) {
    nodes[static_cast<size_t>(i)].id = i;
    nodes[static_cast<size_t>(i)].functional = true;
  }
  nodes[0].required = 0;
  nodes[1].required = 1;
  nodes[1].dependencies = {0, -1, -1};
  nodes[2].required = 2;
  nodes[2].dependencies = {0, 1, -1};
  nodes[3].required = 1;
  nodes[3].dependencies = {2, -1, -1};
  std::array<uint8_t, 4> eligible = {1, 1, 1, 1};
  Civilization::prune(nodes, eligible, true);
  for (auto node : nodes)
    CHECK(node.functional);
  eligible[1] = 0;
  Civilization::prune(nodes, eligible, false);
  CHECK(nodes[0].functional);
  for (int i = 1; i < 4; ++i)
    CHECK_FALSE(nodes[static_cast<size_t>(i)].functional);
  eligible[1] = 1;
  Civilization::prune(nodes, eligible, false);
  CHECK_FALSE(nodes[1].functional);
  Civilization::prune(nodes, eligible, true);
  for (auto node : nodes)
    CHECK(node.functional);
}
TEST_CASE("macro actions validate atomically and affect simulation") {
  Config cfg;
  cfg.world.size = 64;
  cfg.world.forest_warmup = 0;
  cfg.forest.f_lightning = 0;
  World a(cfg, 31, 1), b(cfg, 31, 4);
  std::array<Action, 4> actions;
  for (auto &action : actions)
    action = Action::balanced();
  REQUIRE(actions[0].valid(4));
  actions[0].allocation[0] = fx16::from_raw(-1);
  CHECK_FALSE(actions[0].valid(4));
  actions[0] = Action::balanced();
  for (int t = 0; t < 10; ++t) {
    a.step(actions);
    b.step(actions);
    CHECK(a.digest() == b.digest());
  }
  actions[0].allocation.fill({});
  actions[0].allocation[4] = FX_ONE;
  a.step(actions);
  for (auto &action : actions)
    action = Action::balanced();
  b.step(actions);
  CHECK(a.digest() != b.digest());
}
TEST_CASE("civilization invariants during shortage and disease") {
  Config cfg;
  cfg.world.size = 64;
  cfg.world.forest_warmup = 0;
  cfg.pop.food_per_capita = .5;
  cfg.epi.p_spontaneous = .01;
  World world(cfg, 11);
  int collapses = 0;
  for (int t = 0; t < 700; ++t) {
    world.tick();
    const auto &civ = *world.civilization();
    for (const auto &event : civ.events())
      collapses += event.type == EventType::Collapse;
    for (const auto &kingdom : civ.kingdoms()) {
      for (auto stock : kingdom.stock)
        REQUIRE(stock >= 0);
      const auto &l = civ.layers(kingdom.id);
      for (int i = 0; i < 4096; ++i) {
        REQUIRE(l.pop.front()[i].raw >= 0);
        REQUIRE(l.infected.front()[i] + l.immune.front()[i] <= l.pop.front()[i]);
        REQUIRE(civ.refugees()[i].raw >= 0);
      }
    }
  }
  CHECK(collapses > 0);
}

TEST_CASE("nondefault epidemic and collapse controls survive replay configuration") {
  Config cfg;
  cfg.epi.gamma = .125;
  cfg.collapse.capital_grace = 517;
  const auto loaded = parse_config(cfg.canonical());
  REQUIRE(loaded);
  CHECK(loaded.config.epi.gamma == cfg.epi.gamma);
  CHECK(loaded.config.collapse.capital_grace == cfg.collapse.capital_grace);
  CHECK(loaded.config.digest() == cfg.digest());
}
