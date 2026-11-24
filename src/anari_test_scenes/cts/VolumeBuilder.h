// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <array>
#include <cstdint>
#include <functional>

namespace anari {
namespace cts {

using FieldFn = std::function<float(anari::math::float3)>;

float radialDistanceField(anari::math::float3 p);

// A structuredRegular field whose vertices span [-1, 1] on every axis with
// more than one vertex (origin -1, spacing 2 / (n - 1)), each holding `field`
// at its position (by default the distance from the origin). `make` commits
// it; `new` leaves it uncommitted for the caller to set more parameters.
anari::SpatialField makeStructuredRegularField(anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

anari::SpatialField newStructuredRegularField(anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

anari::Volume makeVolume(anari::Device d, anari::SpatialField field);

} // namespace cts
} // namespace anari
