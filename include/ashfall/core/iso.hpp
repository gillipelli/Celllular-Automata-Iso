#pragma once
namespace ashfall {
// Renderer-independent projection math, in pixel units (2:1 dimetric).
struct Point {
  float x, y;
};
inline Point project(float x, float y, float z = 0) { return {(x - y) * 16, (x + y) * 8 - z * 8}; }
inline Point unproject(float x, float y) { return {(y / 8 + x / 16) / 2, (y / 8 - x / 16) / 2}; }
} // namespace ashfall
