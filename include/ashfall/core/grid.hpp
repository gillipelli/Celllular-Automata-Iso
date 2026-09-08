#pragma once
#include <array>
#include <cassert>
namespace ashfall {
class Grid {
public:
  explicit Grid(int n) : n_(n) { assert(n > 0 && n <= 1024); }
  int side() const { return n_; }
  int size() const { return n_ * n_; }
  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < n_ && y < n_; }
  int index(int x, int y) const {
    assert(contains(x, y));
    return y * n_ + x;
  }
  template <class F> void neighbors4(int i, F &&f) const {
    const int x = i % n_, y = i / n_;
    if (x > 0)
      f(i - 1);
    if (x + 1 < n_)
      f(i + 1);
    if (y > 0)
      f(i - n_);
    if (y + 1 < n_)
      f(i + n_);
  }
  template <class F> void neighbors8(int i, F &&f) const {
    const int x = i % n_, y = i / n_;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
        if ((dx || dy) && contains(x + dx, y + dy))
          f(index(x + dx, y + dy));
  }

private:
  int n_;
};
} // namespace ashfall
