#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>
namespace ashfall {
class Hasher {
public:
  Hasher() { XXH3_64bits_reset(&state_); }
  void bytes(const void *p, size_t n) { XXH3_64bits_update(&state_, p, n); }
  void integer(uint64_t v, unsigned width = 8) {
    std::array<uint8_t, 8> b{};
    for (unsigned j = 0; j < width; ++j)
      b[j] = static_cast<uint8_t>(v >> (8 * j));
    bytes(b.data(), width);
  }
  uint64_t finish() const { return XXH3_64bits_digest(&state_); }

private:
  XXH3_state_t state_{};
};
inline uint64_t hash_text(std::string_view text) { return XXH3_64bits(text.data(), text.size()); }
} // namespace ashfall
