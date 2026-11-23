// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../CameraProjection.h"
#include "../Image.h"
// anari
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <algorithm>
#include <cmath>
#include <cstdint>

// The framing of the volume equivalences (ADR-0010, "Volume equivalence"),
// shared by volume.cpp, fields.cpp and their unit tests: an orthographic view
// straight down -z or -x at a field whose bounds are [-1, 1]^3, the field
// constant along the view, opaque at once. In the view, a field point's
// across-view coordinates (a, b) are where it lands: a to the right, b up.
// Looking down -z they are (x, y); looking down -x, with +y up, the right is
// -z, so they are (-z, y).
namespace anari {
namespace cts {
namespace volume_scene {

// The axis a Test's camera looks down. Each view checks the field across it,
// so between them a Test's two views check all three axes.
enum class ViewAxis
{
  Z,
  X
};

// The view is kViewHeight tall: the bounds' edges (+-1) fall at 1/2 +- 3/8
// of a square frame, on whole pixels when its size is a multiple of 8.
inline constexpr float kViewHeight = 8.f / 3.f;

inline CameraProjection viewCamera(ViewAxis view)
{
  CameraProjection c;
  c.type = CameraProjection::Type::Orthographic;
  c.direction = view == ViewAxis::X ? math::float3(-1.f, 0.f, 0.f)
                                    : math::float3(0.f, 0.f, -1.f);
  c.position = -3.f * c.direction;
  c.up = math::float3(0.f, 1.f, 0.f);
  c.height = kViewHeight;
  return c;
}

// A field point's across-view coordinates (a, b).
inline math::float2 acrossView(ViewAxis view, math::float3 p)
{
  return view == ViewAxis::X ? math::float2(-p.z, p.y) : math::float2(p.x, p.y);
}

// The across-view coordinates at the centre of pixel (x, y) of a w x h frame
// (row 0 at the bottom).
inline math::float2 pixelAcrossView(
    uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
  const float width = kViewHeight * float(w) / float(h);
  return math::float2(
      ((x + 0.5f) / w - 0.5f) * width, ((y + 0.5f) / h - 0.5f) * kViewHeight);
}

// The equivalences' field: linear (any interpolation over any cells
// reproduces it), constant along the view, with different slopes along a
// and b (a field mirrored or transposed across the view renders
// differently). Values in [0.1, 0.9].
inline float rampValue(ViewAxis view, math::float3 p)
{
  const math::float2 ab = acrossView(view, p);
  return 0.1f + 0.5f * (ab.x + 1.f) / 2.f + 0.3f * (ab.y + 1.f) / 2.f;
}

// The pixels of a w x h frame whose centres lie at least `band` (world
// units) plus `marginPixels` inside the bounds' silhouette, as a mask for a
// derived expectation (maskSelects()): white inside, black elsewhere.
inline Image interiorMask(
    uint32_t w, uint32_t h, float band, float marginPixels = 2.f)
{
  Image mask;
  mask.width = w;
  mask.height = h;
  mask.rgba.assign(size_t(w) * h * 4, 0);
  const float pixel = kViewHeight / float(h);
  const float limit = 1.f - band - marginPixels * pixel;
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      const math::float2 ab = pixelAcrossView(x, y, w, h);
      if (std::abs(ab.x) <= limit && std::abs(ab.y) <= limit) {
        uint8_t *p = &mask.rgba[(size_t(y) * w + x) * 4];
        p[0] = p[1] = p[2] = p[3] = 255;
      }
    }
  return mask;
}

} // namespace volume_scene
} // namespace cts
} // namespace anari
