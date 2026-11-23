// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/linalg.h"
// std
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace anari {
namespace cts {

// A dense box of float voxels, as a NanoVDB grid: what the `nanovdb` spatial
// field Tests hand a device as `data`. Voxel (i, j, k), each index in
// [0, voxels), holds value(i, j, k); index space maps to world space by
// `world = origin + voxelSize * ijk`. The grid's world bounds follow NanoVDB's
// own tools (a voxel covers [ijk, ijk + 1] in index space): [origin,
// origin + voxelSize * voxels].
struct NanoVdbGridSpec
{
  std::array<uint32_t, 3> voxels{8, 8, 8}; // each at most 128
  float voxelSize{1.f};
  anari::math::float3 origin{0.f, 0.f, 0.f};
  std::function<float(uint32_t i, uint32_t j, uint32_t k)> value;
};

// The grid as a NanoVDB 32 buffer (one grid, breadth-first, float values,
// checksum disabled), built with NanoVDB.h's own data layout. Throws
// std::invalid_argument for an empty or too large box.
std::vector<uint8_t> makeNanoVdbFloatGrid(const NanoVdbGridSpec &spec);

} // namespace cts
} // namespace anari
