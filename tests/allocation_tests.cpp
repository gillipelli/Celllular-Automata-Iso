#include "ashfall/sim/world.hpp"
#include <atomic>
#include <cstdlib>
#include <doctest/doctest.h>
#include <new>
#ifdef _MSC_VER
#include <malloc.h>
#endif
namespace {
std::atomic<bool> tracking = false;
std::atomic<size_t> allocations = 0;
void count() {
  if (tracking.load(std::memory_order_relaxed))
    allocations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace
void *operator new(size_t size) {
  count();
  if (auto *p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t size, std::align_val_t alignment) {
  count();
  const size_t align = static_cast<size_t>(alignment);
#ifdef _MSC_VER
  void *p = _aligned_malloc(size ? size : align, align);
#else
  void *p = std::aligned_alloc(align, ((size ? size : 1) + align - 1) / align * align);
#endif
  if (!p)
    throw std::bad_alloc();
  return p;
}
void *operator new[](size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}
void operator delete(void *p, std::align_val_t) noexcept {
#ifdef _MSC_VER
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void *p, size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void *p, size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
TEST_CASE("tick allocates nothing including worker bands and event accounting") {
  ashfall::Config cfg;
  cfg.world.size = 64;
  cfg.world.forest_warmup = 0;
  cfg.forest.f_lightning = .02;
  ashfall::World world(cfg, 51, 4);
  world.tick();
  allocations = 0;
  tracking = true;
  for (int i = 0; i < 200; ++i)
    world.tick();
  tracking = false;
  CHECK(allocations.load() == 0);
}
