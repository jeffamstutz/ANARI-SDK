// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../CameraProjection.h"
#include "NanoVdbGrid.h"
#include "VolumeScene.h"
// anari
#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <cstdint>
#include <vector>

// The fields of the spatial field Tests (fields.cpp), shared with their unit
// tests.
namespace anari {
namespace cts {
namespace fields {

// The unstructured cell types (the spec's, VTK's).
inline constexpr uint8_t kTetrahedron = 10;
inline constexpr uint8_t kHexahedron = 12;
inline constexpr uint8_t kPyramid = 14;

// Which cells an unstructured mesh over [-1, 1]^3 is made of: 2 x 2 x 2
// blocks, each one hexahedron, six tetrahedra, six pyramids around a centre
// vertex, or (Mixed) block n of the three kinds in turn. No wedges: the spec's
// text and VTK_WEDGE, which it says it follows, order a wedge's bottom
// triangle opposite ways (ADR-0010 58).
enum class CellLayout
{
  Hexahedra = 0,
  Tetrahedra = 1,
  Pyramids = 2,
  Mixed = 3,
};

struct UnstructuredMesh
{
  std::vector<anari::math::float3> positions;
  std::vector<uint32_t> index;
  std::vector<uint32_t> cellIndex;
  std::vector<uint8_t> cellType;
};

UnstructuredMesh unstructuredMesh(CellLayout layout);

// An `unstructured` field over `mesh`, with vertex.data and/or cell.data when
// given. Not committed.
anari::SpatialField newUnstructuredField(anari::Device d,
    const UnstructuredMesh &mesh,
    const std::vector<float> *vertexData,
    const std::vector<float> *cellData);

// The uniform value of the cell.data Cases.
inline constexpr float kUniformValue = 0.6f;

// The nanovdb ramp: kRampVoxels^3 voxels over [-1, 1]^3.
inline constexpr uint32_t kRampVoxels = 32;
inline constexpr float kRampVoxelSize = 2.f / kRampVoxels;
NanoVdbGridSpec rampGrid(volume_scene::ViewAxis view);

// A `nanovdb` field holding `spec`'s grid, its filter set when given. Not
// committed.
anari::SpatialField newNanoVdbField(
    anari::Device d, const NanoVdbGridSpec &spec, const char *filter);

// A vertex-centered structuredRegular field of n^3 vertices over [-1, 1]^3
// holding volume_scene::rampValue(), its filter set when given. Not
// committed.
anari::SpatialField newRampStructuredField(anari::Device d,
    volume_scene::ViewAxis view,
    uint32_t n,
    const char *filter = nullptr);

// The cubic ramp Tests' grid: 9^3 vertices, a cell 0.25 wide.
inline constexpr uint32_t kCubicRampVertices = 9;
inline constexpr float kCubicRampSpacing = 2.f / (kCubicRampVertices - 1);

// The filter Tests' coarse field across the view, sample (a, b) of 4 x 4:
// unlike any linear function, so nearest, linear and every cubic
// reconstruct it differently.
float zigzagValue(uint32_t a, uint32_t b);

// The isosurface sphere Test: an isosurface at kSphereRadius of the
// distance from the origin, sampled on 33^3 vertices over [-1, 1]^3, seen
// from a perspective camera at z = 3 looking down -z.
inline constexpr float kSphereRadius = 0.8f;
CameraProjection sphereCamera();

// Where the ray through pixel (x, y) of a w x h frame (row 0 at the bottom)
// of `camera` (perspective) passes the origin: its closest distance to it
// and, when it is closer than `radius`, the distance from the camera to the
// sphere of that radius.
struct SphereRay
{
  float closest{0.f};
  float hit{0.f};
};
SphereRay sphereRay(const CameraProjection &camera,
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h,
    float radius);

} // namespace fields
} // namespace cts
} // namespace anari
