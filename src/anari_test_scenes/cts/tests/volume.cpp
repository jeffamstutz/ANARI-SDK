// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../VolumeBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

void setVolumeColor(BuildContext &ctx, anari::Device d, anari::Volume vol)
{
  const std::string c = ctx.getString("color", "array");
  if (c == "vec3") {
    anari::setParameter(d, vol, "color", float3(1.f, 0.75f, 0.f));
  } else if (c == "vec4") {
    anari::setParameter(d, vol, "color", float4(1.f, 0.75f, 0.f, 0.5f));
  } else {
    std::vector<float3> cols = {
        {0.25f, 0.9f, 0.2f}, {0.9f, 0.1f, 0.f}, {0.f, 0.f, 0.7f}};
    anari::setAndReleaseParameter(
        d, vol, "color", anari::newArray1D(d, cols.data(), cols.size()));
  }
}

void setVolumeOpacity(BuildContext &ctx, anari::Device d, anari::Volume vol)
{
  const std::string o = ctx.getString("opacity", "array");
  if (o == "scalar") {
    anari::setParameter(d, vol, "opacity", 1.0f);
  } else {
    std::vector<float> ops = {0.25f, 0.7f, 1.0f};
    anari::setAndReleaseParameter(
        d, vol, "opacity", anari::newArray1D(d, ops.data(), ops.size()));
  }
}

// ---- cell-centered structured regular fields --------------------------------
//
// The spec (5.13.1): a cell-centered field's values sit at its cells' centres,
// its bounds are [origin, origin + size x spacing], and filters other than
// nearest reconstruct it from neighbouring cells with clamp-to-edge outside
// them. With the linear filter that is a trilinear function over the bounds,
// so a vertex-centered field over the same bounds whose vertices (at half
// the spacing) hold that function's values is the same field: trilinear
// interpolation of samples of a function that is trilinear in each
// half-cell reproduces it exactly.

constexpr uint32_t kCells = 4; // per axis
const float3 kCellOrigin(-1.f);
const float kCellSpacing = 0.5f; // bounds [-1, 1]

// The cell values: smooth, and different along each axis, so a field that is
// shifted, mirrored or scaled renders differently.
float cellValue(uint32_t i, uint32_t j, uint32_t k)
{
  const float n = float(kCells - 1);
  const float x = i / n, y = j / n, z = k / n;
  return (x + y * y + 0.5f * z) / 2.5f;
}

// The cell-centered field at continuous cell index c (cell i's centre is
// c = i): trilinear between centres, clamped to the edge cells outside them.
float cellCenteredAt(std::array<float, 3> c)
{
  std::array<uint32_t, 3> i0;
  std::array<float, 3> f;
  for (int a = 0; a < 3; ++a) {
    const float x = std::clamp(c[a], 0.f, float(kCells - 1));
    i0[a] = std::min(uint32_t(std::floor(x)), kCells - 2);
    f[a] = x - float(i0[a]);
  }
  float v = 0.f;
  for (int corner = 0; corner < 8; ++corner) {
    float w = 1.f;
    std::array<uint32_t, 3> i;
    for (int a = 0; a < 3; ++a) {
      const bool hi = (corner >> a) & 1;
      i[a] = i0[a] + (hi ? 1 : 0);
      w *= hi ? f[a] : 1.f - f[a];
    }
    v += w * cellValue(i[0], i[1], i[2]);
  }
  return v;
}

// The Case's field: "cellCentered" (kCells^3 cells) or its vertex-centered
// equal (2 kCells + 1 vertices per axis at half the spacing).
anari::SpatialField cellCenteredCaseField(anari::Device d, bool cellCentered)
{
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameter(d, field, "origin", kCellOrigin);
  anari::setParameter(d, field, "filter", "linear");
  std::vector<float> data;
  uint32_t n = 0;
  if (cellCentered) {
    n = kCells;
    for (uint32_t k = 0; k < n; ++k)
      for (uint32_t j = 0; j < n; ++j)
        for (uint32_t i = 0; i < n; ++i)
          data.push_back(cellValue(i, j, k));
    anari::setParameter(d, field, "cellCentered", true);
    anari::setParameter(d, field, "spacing", float3(kCellSpacing));
  } else {
    // Vertex v sits at origin + v x spacing / 2: cell index v / 2 - 0.5.
    n = 2 * kCells + 1;
    for (uint32_t k = 0; k < n; ++k)
      for (uint32_t j = 0; j < n; ++j)
        for (uint32_t i = 0; i < n; ++i)
          data.push_back(cellCenteredAt(
              {0.5f * i - 0.5f, 0.5f * j - 0.5f, 0.5f * k - 0.5f}));
    anari::setParameter(d, field, "spacing", float3(0.5f * kCellSpacing));
  }
  anari::setParameterArray3D(d, field, "data", data.data(), n, n, n);
  anari::commitParameters(d, field);
  return field;
}

anari::World cellCenteredWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  auto field = cellCenteredCaseField(
      d, ctx.getString("grid", "cellCentered") == "cellCentered");
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setParameter(d, vol, "value", field);
  std::vector<float3> colors = {
      {0.1f, 0.2f, 0.9f}, {0.2f, 0.9f, 0.2f}, {0.9f, 0.2f, 0.1f}};
  anari::setAndReleaseParameter(
      d, vol, "color", anari::newArray1D(d, colors.data(), colors.size()));
  // Opaque at once: the image is the field's colour on the visible faces of
  // its bounds, which is the least noisy a ray-marched volume renders.
  anari::setParameter(d, vol, "opacity", 1.f);
  anari::setParameter(d, vol, "unitDistance", 0.001f);
  anari::setParameter(d, vol, "valueRange", anari::math::float2(0.f, 1.f));
  anari::commitParameters(d, vol);

  WorldContents wc;
  wc.volumes = {vol};
  auto world = assembleWorld(d, wc);
  anari::release(d, field);
  anari::release(d, vol);
  return world;
}

} // namespace

void registerVolumeTests(Catalog &catalog)
{
  makeTest("volume", "volume")
      .description(
          "Checks structured volume sampling and transfer-function parameters.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto field = newStructuredRegularField(d, {3, 3, 3});
        applyParameterValue(d, field, "filter", ctx.value("filter").raw());
        applyParameterValue(d, field, "origin", ctx.value("origin").raw());
        applyParameterValue(d, field, "spacing", ctx.value("spacing").raw());
        anari::commitParameters(d, field);

        auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
        anari::setParameter(d, vol, "value", field);
        setVolumeColor(ctx, d, vol);
        setVolumeOpacity(ctx, d, vol);
        applyParameterValue(
            d, vol, "valueRange", ctx.value("valueRange").raw());
        applyParameterValue(
            d, vol, "unitDistance", ctx.value("unitDistance").raw());
        anari::commitParameters(d, vol);

        WorldContents wc;
        wc.volumes = {vol};
        auto world = assembleWorld(d, wc);
        anari::release(d, field);
        anari::release(d, vol);
        return world;
      })
      .simplified()
      .permute("filter", V{none(), Any("nearest"), Any("linear")})
      .permute("origin", V{Any(float3(-1.f, -1.f, -1.f)), none()})
      .permute("spacing", V{none(), Any(float3(0.01f, 0.02f, 0.5f))})
      .permute("valueRange", V{none(), Any(anari::math::float2(0.25f, 0.75f))})
      .permute("color", {"array", "vec3", "vec4"})
      .permute("opacity", {"array", "scalar"})
      .permute("unitDistance", V{none(), Any(0.1f)})
      .channels({Channel::Color, Channel::Depth})
      .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
      .covers("ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D")
      .requireFeature(kSupportPerspective)
      .registerInto(catalog);
  makeTest("volume", "structured_regular_cell_centered")
      .description(
          "Checks that a cell-centered structured regular field renders as "
          "the vertex-centered field the spec defines it to equal.")
      .build(cellCenteredWorld)
      // A fixed camera, not one framing the world's bounds: a field whose
      // bounds are wrong then renders at the wrong size.
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "perspective");
        const float3 eye(2.4f, 1.8f, 3.f);
        anari::setParameter(d, c, "position", eye);
        anari::setParameter(d, c, "direction", -eye);
        anari::setParameter(d, c, "up", float3(0.f, 1.f, 0.f));
        anari::commitParameters(d, c);
        return c;
      })
      // The first value is the reference: the vertex-centered equal.
      .variant("grid", {"vertexCentered", "cellCentered"})
      // Ray marching samples the two grids at different points (devices step
      // by the spacing, and jitter), which moves colours along the silhouette
      // and steep gradients. On helide and halcyon, two grids holding one
      // trilinear function differ in 1.7 % of pixels by more than 32 levels;
      // a field read as vertex-centered (bounds and values wrong) in 21 %.
      .equivalenceTolerance("grid", 0.04, 32)
      .noGroundTruth()
      .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_CELL_CENTERED")
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
          kSupportPerspective})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
