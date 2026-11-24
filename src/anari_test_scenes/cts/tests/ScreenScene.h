// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../GeometryLayout.h"
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Helpers for Tests that lay a scene out in screen coordinates, seen by
// makeScreenQuadCamera() (ScreenQuad.h): the plane z = 0 maps to the frame
// the same way at any frame size, so a Test knows which pixels a thing covers
// without a camera projection. Normalized screen coordinates have (0, 0) at
// the bottom left of the frame and (1, 1) at the top right; world (x, y) on
// the plane is (2 s.x - 1, 2 s.y - 1).
namespace anari {
namespace cts {

using Triangle = std::array<math::float3, 3>;

// The Layout's equilateral triangle (equilateralTriangle()) in the plane
// z = 0, centred at screen (sx, sy); `halfSize` (its circumradius) is in screen
// units (the frame is 1 across).
inline Triangle screenTriangle(float sx, float sy, float halfSize)
{
  return equilateralTriangle(
      math::float3(2.f * sx - 1.f, 2.f * sy - 1.f, 0.f), 2.f * halfSize);
}

// The vertices of `triangles` as a triangle soup.
inline std::vector<math::float3> triangleSoup(
    const std::vector<Triangle> &triangles)
{
  std::vector<math::float3> out;
  out.reserve(3 * triangles.size());
  for (const auto &t : triangles)
    out.insert(out.end(), t.begin(), t.end());
  return out;
}

// A point of the plane z = 0 as continuous pixel coordinates of a w x h frame
// (row 0 at the bottom, pixel (x, y) covering [x, x + 1) x [y, y + 1)).
inline math::float2 planePixel(math::float3 p, uint32_t w, uint32_t h)
{
  return math::float2((p.x + 1.f) * 0.5f * w, (p.y + 1.f) * 0.5f * h);
}

// Whether pixel point `p` lies inside triangle (a, b, c), grown by `margin`
// pixels (all in pixel coordinates).
inline bool nearTriangle(math::float2 a,
    math::float2 b,
    math::float2 c,
    math::float2 p,
    float margin)
{
  auto cross2 = [](math::float2 u, math::float2 v) {
    return u.x * v.y - u.y * v.x;
  };
  const float area = cross2(b - a, c - a);
  if (area == 0.f)
    return false;
  const float side = area > 0.f ? 1.f : -1.f;
  auto inside = [&](math::float2 u, math::float2 v) {
    const math::float2 e = v - u;
    return side * cross2(e, p - u) / std::sqrt(e.x * e.x + e.y * e.y)
        >= -margin;
  };
  return inside(a, b) && inside(b, c) && inside(c, a);
}

} // namespace cts
} // namespace anari
