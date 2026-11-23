// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../CameraProjection.h"
// anari
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <array>

// The scenes of the depth-of-field and stereo Tests (optics.cpp), shared with
// their unit tests so a synthetic image can be drawn where a Test expects its
// subjects. Both look from a perspective camera at the origin, down -z with +y
// up and the spec's default fovy, at two square plates facing it, one near
// and one far.
namespace anari {
namespace cts {
namespace optics {

inline CameraProjection camera()
{
  CameraProjection c;
  c.type = CameraProjection::Type::Perspective;
  c.position = math::float3(0.f, 0.f, 0.f);
  c.direction = math::float3(0.f, 0.f, -1.f);
  c.up = math::float3(0.f, 1.f, 0.f);
  return c;
}

// A square plate facing the camera, in the plane through `centre`: a square
// of half extent `half`, and (depth of field only) a dark border around it
// out to half extent `outer`.
struct Plate
{
  math::float3 centre;
  float half;
  float outer;
};

inline constexpr float kNearDepth = 2.f;
inline constexpr float kFarDepth = 6.f;

// Depth of field ///////////////////////////////////////////////////////////

// Side by side, the near plate left and the far one right; the far plate is
// three times as large, so both project to the same size at the default fovy:
// a bright square 0.17 of the frame's height across, in a dark border 1.5
// times its half extent wide. The border keeps each bright square's edges in
// its plate's plane, so they blur by the plate's depth alone, and keeps the
// background (whatever a device shows there) away from them.
inline const std::array<Plate, 2> kFocusPlates = {
    Plate{math::float3(-0.5f, 0.f, -kNearDepth), 0.2f, 0.5f},
    Plate{math::float3(1.5f, 0.f, -kFarDepth), 0.6f, 1.5f}};

// The aperture of the Cases focused on one plate. Through a thin lens of this
// radius (in world units) focused on one plate, the other blurs by a disk of
// 1/29 of the frame's height in radius (8.9 pixels at 256). Read as a
// diameter, half that still blurs past the Test's bound.
inline constexpr float kApertureRadius = 0.12f;
// The aperture of the Cases focused at the spec's default distance, 1: both
// plates blur (radius 1/33 and 1/20 of the frame's height), each within its
// border.
inline constexpr float kDefaultFocusApertureRadius = 0.07f;
inline constexpr float kDefaultFocusDistance = 1.f;

// Stereo ///////////////////////////////////////////////////////////////////

// One above the other on the view axis (x = 0), the near plate above and the
// far one below, so in a mono image the two are centred on the same column
// and the horizontal offset between them in an eye's image is that eye's
// disparity. Both project to the same size (0.17 of the frame's height). The
// near plate is red, the far one blue, so each is found by its colour.
inline const Plate kStereoNear{
    math::float3(0.f, 0.4f, -kNearDepth), 0.2f, 0.2f};
inline const Plate kStereoFar{math::float3(0.f, -1.2f, -kFarDepth), 0.6f, 0.6f};
inline const math::float3 kStereoNearColor(0.9f, 0.15f, 0.1f);
inline const math::float3 kStereoFarColor(0.1f, 0.25f, 0.9f);

// The interpupillary distance of the stereo Test: the near plate's offset
// from the far one is 1/17 of the frame's height (15 pixels at 256) either
// way in an eye's image, whatever the convergence.
inline constexpr float kInterpupillaryDistance = 0.4f;
// The spec's default.
inline constexpr float kDefaultInterpupillaryDistance = 0.0635f;

} // namespace optics
} // namespace cts
} // namespace anari
