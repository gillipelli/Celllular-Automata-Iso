#pragma once
#include "ashfall/core/fixed.hpp"
#include "ashfall/core/hash.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <new>
#include <vector>
namespace ashfall {
template <class T> struct AlignedAllocator {
  using value_type = T;
  AlignedAllocator() = default;
  template <class U> AlignedAllocator(const AlignedAllocator<U> &) {}
  T *allocate(size_t n) {
    return static_cast<T *>(::operator new(n * sizeof(T), std::align_val_t{64}));
  }
  void deallocate(T *p, size_t) { ::operator delete(p, std::align_val_t{64}); }
  template <class U> bool operator==(const AlignedAllocator<U> &) const { return true; }
};
template <class T> class Layer {
public:
  explicit Layer(int n, T fill = {}) : n_(n), data_(checked_size(n), fill) {
    assert(n > 0 && n <= 1024);
  }
  T &at(int x, int y) {
    assert(x >= 0 && y >= 0 && x < n_ && y < n_);
    return data_[static_cast<size_t>(y * n_ + x)];
  }
  const T &at(int x, int y) const {
    assert(x >= 0 && y >= 0 && x < n_ && y < n_);
    return data_[static_cast<size_t>(y * n_ + x)];
  }
  T &operator[](int i) { return data_[static_cast<size_t>(i)]; }
  const T &operator[](int i) const { return data_[static_cast<size_t>(i)]; }
  T *data() { return data_.data(); }
  const T *data() const { return data_.data(); }
  int side() const { return n_; }
  size_t size() const { return data_.size(); }
  void fill(T v) { std::fill(data_.begin(), data_.end(), v); }
  void hash_into(Hasher &h) const {
    for (const auto &v : data_) {
      if constexpr (std::is_same_v<T, fx16>)
        h.integer(static_cast<uint32_t>(v.raw), 4);
      else
        h.integer(static_cast<uint64_t>(v), sizeof(T));
    }
  }
  uint64_t digest() const {
    Hasher h;
    hash_into(h);
    return h.finish();
  }

private:
  static size_t checked_size(int n) {
    if (n < 1 || n > 1024)
      std::abort();
    return static_cast<size_t>(n) * static_cast<size_t>(n);
  }
  int n_;
  std::vector<T, AlignedAllocator<T>> data_;
};
template <class T> class DoubleLayer {
public:
  explicit DoubleLayer(int n, T value = {}) : a_(n, value), b_(n, value) {}
  const Layer<T> &front() const { return a_; }
  // Writer has no read API. Only the pass that owns this double buffer receives it.
  class Writer {
  public:
    void set(int i, T v) { layer_[i] = v; }

  private:
    friend class DoubleLayer;
    explicit Writer(Layer<T> &l) : layer_(l) {}
    Layer<T> &layer_;
  };
  void set_current(int i, T value) { a_[i] = value; }
  Writer back() { return Writer(b_); }
  void swap() { std::swap(a_, b_); }

private:
  Layer<T> a_, b_;
};
} // namespace ashfall
