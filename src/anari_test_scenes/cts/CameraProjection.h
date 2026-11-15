// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FrameSize.h"
// anari
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <optional>

namespace anari {
namespace cts {

// What a Case's camera does to a world-space point, described in the CTS's
// own terms so an exact check can say where something must appear
// (ADR-0010, exact checks). The runner records it for the camera it frames
// itself; a Test with its own camera hook records it through
// BuildContext::setCameraProjection(), or its exact checks can't project.
//
// `fovy` (perspective) and `height` (orthographic) describe the whole sensor,
// `aspect` its width over height; `imageRegion` is the part of the sensor the
// frame shows ([lo, hi] in normalized screen space, (0, 0) at the bottom left).
struct CameraProjection
{
  enum class Type
  {
    Perspective,
    Orthographic,
  };
  Type type{Type::Perspective};
  anari::math::float3 position{0.f, 0.f, 0.f};
  anari::math::float3 direction{0.f, 0.f, -1.f};
  anari::math::float3 up{0.f, 1.f, 0.f};
  float fovy{1.04719755f}; // the spec's default, pi/3
  float height{1.f}; // orthographic: the sensor's height in world units
  float aspect{1.f};
  anari::math::float2 regionLo{0.f, 0.f};
  anari::math::float2 regionHi{1.f, 1.f};
};

// Where `world` lands in a frame of `size` pixels: continuous pixel
// coordinates in Image convention (row 0 at the bottom; pixel (x, y) covers
// [x, x + 1) x [y, y + 1)). Empty for a point behind a perspective camera
// (or in its plane) or a degenerate camera. The result may lie outside the
// frame.
std::optional<anari::math::float2> projectToPixel(const CameraProjection &camera,
    FrameSize size,
    const anari::math::float3 &world);

} // namespace cts
} // namespace anari
