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

// The axis a cell-centered Test's camera looks down. Each view checks the
// field across it, so between them the two Tests check all three axes.
enum class ViewAxis
{
  Z,
  X
};

// The cell values: smooth, and different along the two axes across the view
// (x and y looking down z; z and y looking down x), so a field that is
// shifted, mirrored or scaled across the view renders differently. Constant
// along the view axis: where a ray march first samples the field (devices
// step by the spacing, and jitter) then doesn't change its colour.
float cellValue(ViewAxis view, uint32_t i, uint32_t j, uint32_t k)
{
  const float n = float(kCells - 1);
  const float u = (view == ViewAxis::X ? k : i) / n, v = j / n;
  return (u + v * v) / 2.f;
}

// The cell-centered field at continuous cell index c (cell i's centre is
// c = i): trilinear between centres, clamped to the edge cells outside them.
float cellCenteredAt(ViewAxis view, std::array<float, 3> c)
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
    v += w * cellValue(view, i[0], i[1], i[2]);
  }
  return v;
}

// The Case's field: "cellCentered" (kCells^3 cells) or its vertex-centered
// equal (2 kCells + 1 vertices per axis at half the spacing).
anari::SpatialField cellCenteredCaseField(
    anari::Device d, bool cellCentered, ViewAxis view)
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
          data.push_back(cellValue(view, i, j, k));
    anari::setParameter(d, field, "cellCentered", true);
    anari::setParameter(d, field, "spacing", float3(kCellSpacing));
  } else {
    // Vertex v sits at origin + v x spacing / 2: cell index v / 2 - 0.5.
    n = 2 * kCells + 1;
    for (uint32_t k = 0; k < n; ++k)
      for (uint32_t j = 0; j < n; ++j)
        for (uint32_t i = 0; i < n; ++i)
          data.push_back(cellCenteredAt(
              view, {0.5f * i - 0.5f, 0.5f * j - 0.5f, 0.5f * k - 0.5f}));
    anari::setParameter(d, field, "spacing", float3(0.5f * kCellSpacing));
  }
  anari::setParameterArray3D(d, field, "data", data.data(), n, n, n);
  anari::commitParameters(d, field);
  return field;
}

anari::World cellCenteredWorld(BuildContext &ctx, ViewAxis view)
{
  auto d = ctx.device();
  auto field = cellCenteredCaseField(
      d, ctx.getString("grid", "cellCentered") == "cellCentered", view);
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

// Orthographic, looking straight down -z at the field's +z face (or down -x
// at its +x face), so every pixel shows the field on that face and no pixel
// sees a side face. The view is kCellCenteredViewHeight tall: the bounds'
// edges (+-1) fall at 1/2 +- 3/8 of the frame, on whole pixels for a square
// frame whose size is a multiple of 8.
constexpr float kCellCenteredViewHeight = 8.f / 3.f;

anari::Camera cellCenteredCamera(BuildContext &ctx, ViewAxis view)
{
  auto d = ctx.device();
  const float3 dir =
      view == ViewAxis::X ? float3(-1.f, 0.f, 0.f) : float3(0.f, 0.f, -1.f);
  auto c = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, c, "position", -3.f * dir);
  anari::setParameter(d, c, "direction", dir);
  anari::setParameter(d, c, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, c, "height", kCellCenteredViewHeight);
  anari::commitParameters(d, c);
  return c;
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
  // A volume equivalence covers the axis along its view with a second view
  // down another axis (ADR-0010, volume equivalence); nothing relates the two
  // views' images, so they are two Tests.
  struct View
  {
    const char *name;
    ViewAxis axis;
    const char *direction;
  };
  for (const View &v :
      {View{"structured_regular_cell_centered_down_z", ViewAxis::Z, "-z"},
          View{"structured_regular_cell_centered_down_x", ViewAxis::X, "-x"}}) {
    const ViewAxis view = v.axis;
    makeTest("volume", v.name)
        .description(
            "Checks that a cell-centered structured regular field renders as "
            "the vertex-centered field the spec defines it to equal, looking "
            "down "
            + std::string(v.direction) + ".")
        .build(
            [view](BuildContext &ctx) { return cellCenteredWorld(ctx, view); })
        // A fixed orthographic camera looking down -z or -x, not one framing
        // the world's bounds: a field whose bounds are wrong then renders at
        // the wrong size. See cellCenteredCamera() for the framing.
        .camera([view](BuildContext &ctx, const scenes::Bounds &) {
          return cellCenteredCamera(ctx, view);
        })
        // The first value is the reference: the vertex-centered equal.
        // Framed this way, the default tolerance holds on helide, halcyon
        // and visrtx (measurements in ADR-0010, volume equivalence).
        .variant("grid", {"vertexCentered", "cellCentered"})
        .noGroundTruth()
        .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_CELL_CENTERED")
        .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
            "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
            "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
        .registerInto(catalog);
  }
}

} // namespace cts
} // namespace anari
