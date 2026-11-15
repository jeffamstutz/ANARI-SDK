// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CameraProjection.h"
// std
#include <cmath>

namespace anari {
namespace cts {

std::optional<math::float2> projectToPixel(
    const CameraProjection &camera, FrameSize size, const math::float3 &world)
{
  using math::float2;
  using math::float3;
  const float dirLength = math::length(camera.direction);
  if (!(dirLength > 0.f) || !(camera.aspect > 0.f))
    return std::nullopt;
  const float3 dir = camera.direction / dirLength;
  const float3 side = math::cross(dir, camera.up);
  const float sideLength = math::length(side);
  if (!(sideLength > 0.f))
    return std::nullopt;
  const float3 right = side / sideLength;
  const float3 up = math::cross(right, dir);

  const float3 v = world - camera.position;
  const float depth = math::dot(v, dir);
  // The point's position on the sensor, in [-1, 1] across its full height
  // and width.
  float2 ndc;
  if (camera.type == CameraProjection::Type::Perspective) {
    const float tanHalf = std::tan(0.5f * camera.fovy);
    if (!(depth > 0.f) || !(tanHalf > 0.f))
      return std::nullopt;
    ndc = float2(math::dot(v, right) / (depth * tanHalf * camera.aspect),
        math::dot(v, up) / (depth * tanHalf));
  } else {
    if (!(camera.height > 0.f))
      return std::nullopt;
    const float halfH = 0.5f * camera.height;
    ndc = float2(math::dot(v, right) / (halfH * camera.aspect),
        math::dot(v, up) / halfH);
  }
  // Normalized screen space, then the image region the frame shows.
  const float2 screen = 0.5f * ndc + float2(0.5f, 0.5f);
  const float2 extent = camera.regionHi - camera.regionLo;
  if (!(extent.x > 0.f) || !(extent.y > 0.f))
    return std::nullopt;
  const float2 inRegion = (screen - camera.regionLo) / extent;
  return float2(
      inRegion.x * float(size.width), inRegion.y * float(size.height));
}

} // namespace cts
} // namespace anari
