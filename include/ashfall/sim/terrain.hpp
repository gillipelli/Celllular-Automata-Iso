#pragma once
#include "ashfall/core/config.hpp"
#include "ashfall/core/grid.hpp"
#include "ashfall/core/layer.hpp"
#include "ashfall/core/rng.hpp"
namespace ashfall {
enum Terrain : uint8_t { WATER, PLAIN, HILL, MOUNTAIN, ROCK };
enum ForestState : uint8_t { EMPTY, SAPLING, TREE, BURNING, ASH };
struct TerrainLayers {
  Layer<uint8_t> elevation, terrain;
  Layer<uint16_t> stone;
  Layer<fx16> moisture, initial_fertility;
  TerrainLayers(const Config &cfg, uint64_t seed);
  void hash_into(Hasher &h) const;
};
} // namespace ashfall
