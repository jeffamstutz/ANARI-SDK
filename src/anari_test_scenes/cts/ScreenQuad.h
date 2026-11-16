// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <functional>
#include <string>
#include <vector>

// A quad that fills the frame, seen head-on, for Tests whose expectation
// depends on where on screen something happens (a sampler's border, an
// opacity cutoff). Its vertex attributes are given as functions of screen
// position, so a Test knows which screen region holds which attribute value
// and can say what each region must look like (regionComposite(), Image.h).
//
// Screen coordinates are normalized frame coordinates, (0, 0) the bottom left
// and (1, 1) the top right. The quad lies in the plane z = 0, where the
// camera of makeScreenQuadCamera() sees screen point s at world
// (2 s.x - 1, 2 s.y - 1, 0) at any frame size. Since the plane faces the
// camera, an attribute affine in world position is affine on screen.
namespace anari {
namespace cts {

// How far the quad extends past each edge of the frame, in screen units, so
// no edge pixel misses it.
inline constexpr float kScreenQuadOverhang = 0.125f;

// A vertex attribute of the quad: `value` at a corner's screen position (an
// affine function, as interpolation across the quad is). Only the first
// `components` (1-4) are set, as an array of FLOAT32 .. FLOAT32_VEC4.
struct ScreenAttribute
{
  std::string name; // e.g. "attribute0"
  int components{4};
  std::function<anari::math::float4(anari::math::float2 screen)> value;
};

// The quad, a committed triangle geometry of two triangles with `attributes`
// as `vertex.<name>` arrays.
anari::Geometry makeScreenQuad(
    anari::Device d, const std::vector<ScreenAttribute> &attributes = {});

// The committed perspective camera the quad fills: at (0, 0, 1) looking down
// -z with +y up, fovy pi/2 and aspect 1, so the screen maps to the quad the
// same way at any frame size.
anari::Camera makeScreenQuadCamera(anari::Device d);

// A world holding `surface` (when given) lit head-on by a directional light,
// so every point of the quad is shaded alike.
anari::World makeScreenQuadWorld(anari::Device d, anari::Surface surface);

} // namespace cts
} // namespace anari
