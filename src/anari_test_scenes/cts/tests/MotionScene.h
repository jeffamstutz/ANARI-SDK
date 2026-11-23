// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../CameraProjection.h"
// anari
#include "anari/anari_cpp/ext/linalg.h"

// The scene of the motion and shutter Tests (motion.cpp), shared with their
// unit tests so a synthetic image can be drawn where a Test expects its
// subject. Every Test looks at a flat rectangle in the plane z = 0 from the
// rest camera (camera motion moves the camera instead, and its exact checks
// describe where the subject appears from the rest camera); the subject moves
// (or the camera does) by translation only wherever an exact check has to know
// where it is at a time between keys, as any interpolation of a translation
// agrees on where it is.
namespace anari {
namespace cts {
namespace motion {

// The rest camera: perspective, at z = 4 looking down -z with +y up and the
// spec's default fovy. At z = 0 the frame is 4 tan(pi/6) * 2 = 4.62 units
// tall.
inline CameraProjection restCamera()
{
  CameraProjection c;
  c.type = CameraProjection::Type::Perspective;
  c.position = math::float3(0.f, 0.f, 4.f);
  c.direction = math::float3(0.f, 0.f, -1.f);
  c.up = math::float3(0.f, 1.f, 0.f);
  return c;
}

// The open-shutter Tests: a rectangle of these half extents whose centre goes
// from kOpenFrom at time 0 to kOpenTo at time 1, along x. The two
// silhouettes overlap in the middle (always covered) and each has a part the
// other lacks (covered for part of the shutter only).
inline constexpr float kOpenHalfWidth = 0.6f;
inline constexpr float kOpenHalfHeight = 0.4f;
inline const math::float3 kOpenFrom(-0.3f, 0.f, 0.f);
inline const math::float3 kOpenTo(0.3f, 0.f, 0.f);

// The open-shutter rectangle's centre at time t.
inline math::float3 openCentreAt(float t)
{
  return kOpenFrom + t * (kOpenTo - kOpenFrom);
}

// The identical-keys Tests: every key (and the static scene) puts the
// open-shutter rectangle's centre here.
inline const math::float3 kIdenticalAt(0.2f, 0.1f, 0.f);

// The rolling-shutter Tests: a bar crossing the whole frame, moving
// perpendicular to the scan. Rows (directions down and up) scan rows, so the
// bar is tall and moves along x; columns (left and right) scan columns, so it
// is wide and moves along y. The scene is symmetric across the frame's middle
// row (rows) or column (columns), so a scan run from the other edge renders
// the mirror image.
inline constexpr float kBarHalfThickness = 0.2f;
inline constexpr float kBarHalfLength = 4.f;
inline constexpr float kBarTravel = 1.2f; // from -kBarTravel to +kBarTravel

inline math::float3 barFrom(bool rows)
{
  return rows ? math::float3(-kBarTravel, 0.f, 0.f)
              : math::float3(0.f, -kBarTravel, 0.f);
}
inline math::float3 barTo(bool rows)
{
  return rows ? math::float3(kBarTravel, 0.f, 0.f)
              : math::float3(0.f, kBarTravel, 0.f);
}
inline math::float2 barHalfExtent(bool rows)
{
  return rows ? math::float2(kBarHalfThickness, kBarHalfLength)
              : math::float2(kBarHalfLength, kBarHalfThickness);
}

// The rolling-shutter Tests' duration: strictly inside the shutter [0, 1],
// so the image is sheared whatever the line schedule (the spec gives
// `rollingShutterDuration` but not when each line opens, nor which edge a
// direction starts from; ADR-0010 48).
inline constexpr float kRollingDuration = 0.5f;

} // namespace motion
} // namespace cts
} // namespace anari
