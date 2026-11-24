// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CameraProjection.h"
#include "anari/anari_cpp.hpp"
#include "anari_test_scenes.h"
// std
#include <cstdint>
#include <optional>
#include <string>

namespace anari {
namespace cts {

anari::Renderer newRenderer(
    anari::Device d, const std::string &subtype = "default");

anari::scenes::Camera cameraFromBounds(const anari::scenes::Bounds &bounds);

// The perspective projection of cameraFromBounds() at the spec's default
// fovy and an aspect of 1 (BuildContext::newCamera sets the frame's): the
// Runner's default framing.
CameraProjection projectionFromBounds(const anari::scenes::Bounds &bounds);

struct PerspectiveCameraOptions
{
  std::optional<float> fovy;
  std::optional<float> aspect;
  std::optional<float> near;
  std::optional<float> far;
};

anari::Camera makePerspectiveCamera(anari::Device d,
    const anari::scenes::Camera &camera,
    const PerspectiveCameraOptions &options = {});

anari::Frame makeColorFrame(
    anari::Device d, anari::World world, uint32_t width, uint32_t height);

} // namespace cts
} // namespace anari
