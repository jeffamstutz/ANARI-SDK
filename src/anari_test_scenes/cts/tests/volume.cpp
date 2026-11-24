// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../VolumeBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "FrameTypeAxis.h"
#include "SubjectShown.h"
#include "VolumeScene.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;
using volume_scene::ViewAxis;

// A valueRange (box1) axis value: stored with its ANARI type, FLOAT32_BOX1,
// since a plain float2 is a VEC2 that a device doesn't read as the range.
Any box1(float lower, float upper)
{
  const float v[2] = {lower, upper};
  return Any(ANARI_FLOAT32_BOX1, v);
}

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
  const float range[2] = {0.f, 1.f};
  anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
  anari::commitParameters(d, vol);

  WorldContents wc;
  wc.volumes = {vol};
  auto world = assembleWorld(d, wc);
  anari::release(d, field);
  anari::release(d, vol);
  return world;
}

// ---- the unitDistance transmittance relation -------------------------------
//
// The spec (5.12.1): the fraction of light absorbed while travelling
// `unitDistance` through the volume is `opacity`, the particles' density
// being set by the two. Through a uniform slab of thickness L the
// transmittance is then T(u) = (1 - opacity)^(L / u), so halving `unitDistance`
// squares it: T(u/2) = T(u)^2 (Appendix C's ray march, informative, gives the
// same). A pixel through the slab is E + T B in linear colour, whatever the
// slab emits (E) or the background is (B), so two backgrounds, black and
// white, give T per pixel: T = P(white) - P(black). The u/2 render over white
// must then be its render over black plus T(u)^2.

// The slab: a uniform field over [-1, 1]^3, seen down -z (thickness 2) with
// the volume equivalences' framing (VolumeScene.h). The opacity and
// unitDistances keep T within [0.25, 0.71], where the 8-bit images resolve it
// well, and the dim colour keeps the emission from swamping T over white
// (ADR-0010).
constexpr float kSlabOpacity = 0.5f;
const float3 kSlabColor(0.4f, 0.2f, 0.1f);

// The `render` axis: unitDistance u or u/2, over a black or a white
// background. The last is derived from the other three.
const char *const kUBlack = "u_black";
const char *const kUWhite = "u_white";
const char *const kHalfUBlack = "halfU_black";
const char *const kHalfUWhite = "halfU_white";

// What a `render` value sets: unitDistance u/2 (else u), a white background
// (else black).
struct SlabRender
{
  bool halfU{false};
  bool white{false};
};

SlabRender slabRender(const BuildContext &ctx)
{
  const std::string r = ctx.getString("render", kUBlack);
  return {
      r == kHalfUBlack || r == kHalfUWhite, r == kUWhite || r == kHalfUWhite};
}

anari::World unitDistanceSlab(BuildContext &ctx)
{
  auto d = ctx.device();
  // Any grid holds a uniform field; 9 vertices a side make the device march
  // the slab in a few steps or more.
  constexpr uint32_t n = 9;
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameter(d, field, "origin", float3(-1.f));
  anari::setParameter(d, field, "spacing", float3(2.f / float(n - 1)));
  // One value everywhere, mapped by the transfer function to kSlabOpacity.
  std::vector<float> data(size_t(n) * n * n, 0.5f);
  anari::setParameterArray3D(d, field, "data", data.data(), n, n, n);
  anari::commitParameters(d, field);

  const bool half = slabRender(ctx).halfU;
  const float u = ctx.get<float>("u", 2.f);

  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "color", kSlabColor);
  anari::setParameter(d, vol, "opacity", kSlabOpacity);
  anari::setParameter(d, vol, "unitDistance", half ? 0.5f * u : u);
  const float range[2] = {0.f, 1.f};
  anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
  anari::commitParameters(d, vol);

  WorldContents wc;
  wc.volumes = {vol};
  auto world = assembleWorld(d, wc);
  anari::release(d, field);
  anari::release(d, vol);
  return world;
}

// The u/2 render over white, from the other three (sRGB-encoded 8-bit, the
// Test's colour format): per pixel and colour component, in linear colour,
// P(u/2, black) + T(u)^2 with T(u) = P(u, white) - P(u, black). Judged only
// a few pixels inside the slab's silhouette, where a pixel is wholly slab or
// wholly background.
ExpectedImage halfUOverWhite(const Siblings &s)
{
  const Image &uBlack = s.at(kUBlack);
  const Image &uWhite = s.at(kUWhite);
  const Image &halfBlack = s.at(kHalfUBlack);
  if (uWhite.width != uBlack.width || uWhite.height != uBlack.height
      || halfBlack.width != uBlack.width || halfBlack.height != uBlack.height)
    throw std::runtime_error("the u and u/2 renders differ in size");
  Image out = halfBlack;
  for (size_t i = 0; i < out.rgba.size(); i += 4) {
    for (size_t c = 0; c < 3; ++c) {
      const float t = std::clamp(srgbDecodedValue(uWhite.rgba[i + c] / 255.f)
              - srgbDecodedValue(uBlack.rgba[i + c] / 255.f),
          0.f,
          1.f);
      const float v = std::clamp(
          srgbDecodedValue(halfBlack.rgba[i + c] / 255.f) + t * t, 0.f, 1.f);
      out.rgba[i + c] = uint8_t(std::lround(srgbEncodedValue(v) * 255.f));
    }
    out.rgba[i + 3] = 255;
  }
  return maskedBy(std::move(out),
      volume_scene::interiorMask(uBlack.width, uBlack.height, 0.f));
}

void registerUnitDistanceTest(Catalog &catalog)
{
  makeTest("volume", "unit_distance_transmittance")
      .description(
          "Checks that halving a volume's unitDistance squares a uniform "
          "slab's transmittance, solved per pixel from renders over a black "
          "and a white background.")
      .build(unitDistanceSlab)
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        auto c = ctx.newCamera(volume_scene::viewCamera(ViewAxis::Z));
        anari::commitParameters(ctx.device(), c);
        return c;
      })
      .renderer([](BuildContext &ctx, anari::Renderer r) {
        const bool white = slabRender(ctx).white;
        anari::setParameter(ctx.device(),
            r,
            "background",
            white ? float4(1.f, 1.f, 1.f, 1.f) : float4(0.f, 0.f, 0.f, 1.f));
      })
      // T(u) = 0.5^(2/u): 0.5, 0.63 and 0.71; at u/2 0.25, 0.40 and 0.5.
      .permute("u", {2.f, 3.f, 4.f})
      .permute("render", {kUBlack, kUWhite, kHalfUBlack, kHalfUWhite})
      .expectDerived("render",
          kHalfUWhite,
          "the u/2 render over black plus T(u)^2, T(u) being the u render "
          "over white minus over black (linear colour)",
          halfUOverWhite)
      .channels({Channel::Color})
      .noGroundTruth()
      .covers("ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D")
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
      .registerInto(catalog);
}

} // namespace

void registerVolumeTests(Catalog &catalog)
{
  makeTest("volume", "volume")
      .description(
          "Checks structured volume sampling and transfer-function parameters.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        // A ramp along x only, over the field: a view down -z sees it across
        // the frame, nearest filtering shows three bands where linear shows a
        // ramp, and `valueRange` re-maps its colours.
        auto field = newStructuredRegularField(d,
            {3, 3, 3},
            [](anari::math::float3 p) { return (p.x + 1.f) / 2.f; });
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
      // A fixed orthographic camera looking down -z: the default one frames
      // the world's bounds, which would re-centre a field moved by `origin`
      // and hide the move, and the ramp doesn't vary along the view.
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "orthographic");
        anari::setParameter(d, c, "position", float3(0.f, 0.f, 5.f));
        anari::setParameter(d, c, "direction", float3(0.f, 0.f, -1.f));
        anari::setParameter(d, c, "up", float3(0.f, 1.f, 0.f));
        anari::setParameter(d, c, "height", 3.f);
        anari::commitParameters(d, c);
        return c;
      })
      .simplified()
      // `filter` defaults to linear (the spec): an unset filter renders like
      // it.
      .permute("filter", V{Any("nearest"), Any("linear"), none()})
      .expectDerived("filter",
          "none",
          "the linear image (the default filter)",
          [](const Siblings &s) -> ExpectedImage { return s.at("linear"); })
      // The builder's field starts at (-1, -1, -1), so `origin` moves it.
      .permute("origin",
          V{Any(float3(-1.f, -1.f, -1.f)), Any(float3(-0.5f, -1.f, -1.f))})
      .permute("spacing", V{none(), Any(float3(0.01f, 0.02f, 0.5f))})
      .permute("valueRange", V{none(), box1(0.25f, 0.75f)})
      .permute("color", {"array", "vec3", "vec4"})
      .permute("opacity", {"array", "scalar"})
      .permute("unitDistance", V{none(), Any(0.1f)})
      .channels({Channel::Color, Channel::Depth})
      .groundTruth("a ray-marched volume has no analytic answer in the Test")
      .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
      .covers("ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D")
      .requireFeature("ANARI_KHR_CAMERA_ORTHOGRAPHIC")
      .registerInto(catalog);

  // The volume tests' camera: orthographic, looking down -z at a field that
  // doesn't vary along the view, so what the volume shows doesn't depend on
  // where the ray march first samples it.
  auto rampCamera = [](BuildContext &ctx, const scenes::Bounds &) {
    auto d = ctx.device();
    auto c = anari::newObject<anari::Camera>(d, "orthographic");
    anari::setParameter(d, c, "position", float3(0.f, 0.f, 5.f));
    anari::setParameter(d, c, "direction", float3(0.f, 0.f, -1.f));
    anari::setParameter(d, c, "up", float3(0.f, 1.f, 0.f));
    anari::setParameter(d, c, "height", 3.f);
    anari::commitParameters(d, c);
    return c;
  };
  auto rampVolume = [](BuildContext &ctx) {
    auto d = ctx.device();
    auto field = newStructuredRegularField(
        d, {3, 3, 3}, [](anari::math::float3 p) { return (p.x + 1.f) / 2.f; });
    anari::commitParameters(d, field);
    auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
    anari::setParameter(d, vol, "value", field);
    setVolumeColor(ctx, d, vol);
    setVolumeOpacity(ctx, d, vol);
    anari::release(d, field);
    return vol;
  };

  // `visible` false removes a volume from the picture, like a world without
  // it (`absent`); a volume is visible unless said otherwise.
  makeTest("volume", "visible")
      .description(
          "Checks that a volume with visible false renders as a world "
          "without it, and that a volume is visible by default.")
      .build([rampVolume](BuildContext &ctx) {
        auto d = ctx.device();
        const std::string visible = ctx.getString("visible", "none");
        WorldContents wc;
        anari::Volume vol = nullptr;
        if (visible != "absent") {
          vol = rampVolume(ctx);
          if (visible == "true" || visible == "false")
            anari::setParameter(d, vol, "visible", visible == "true");
          anari::commitParameters(d, vol);
          wc.volumes = {vol};
        }
        auto world = assembleWorld(d, wc);
        if (vol)
          anari::release(d, vol);
        return world;
      })
      .camera(rampCamera)
      .permute("visible", V{"absent", "true", "false", "none"})
      .expectDerived("visible",
          "false",
          "the world without the volume",
          [](const Siblings &s) -> ExpectedImage { return s.at("absent"); })
      .expectDerived("visible",
          "none",
          "the visible volume: a volume is visible by default",
          [](const Siblings &s) -> ExpectedImage { return s.at("true"); })
      .channels({Channel::Color})
      .noGroundTruth()
      .covers("ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D")
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
      .registerInto(catalog);

  // valueRange is a FLOAT32_BOX1 or a FLOAT64_BOX1.
  makeTest("volume", "value_range_types")
      .description(
          "Checks that a volume's valueRange as a FLOAT64_BOX1 re-maps its "
          "colours as the same FLOAT32_BOX1 does.")
      .build([rampVolume](BuildContext &ctx) {
        auto d = ctx.device();
        auto vol = rampVolume(ctx);
        if (ctx.getString("rangeType", "float32") == "float64") {
          const double range[2] = {0.25, 0.75};
          anariSetParameter(d, vol, "valueRange", ANARI_FLOAT64_BOX1, range);
        } else {
          const float range[2] = {0.25f, 0.75f};
          anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
        }
        anari::commitParameters(d, vol);
        WorldContents wc;
        wc.volumes = {vol};
        auto world = assembleWorld(d, wc);
        anari::release(d, vol);
        return world;
      })
      .camera(rampCamera)
      .variant("rangeType", V{"float32", "float64"})
      .channels({Channel::Color})
      .expectExact("the volume shows", subjectIsShown)
      .noGroundTruth()
      .covers("ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D")
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
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
        // the wrong size. See volume_scene::viewCamera() for the framing.
        .camera([view](BuildContext &ctx, const scenes::Bounds &) {
          auto c = ctx.newCamera(volume_scene::viewCamera(view));
          anari::commitParameters(ctx.device(), c);
          return c;
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

  registerUnitDistanceTest(catalog);
}

} // namespace cts
} // namespace anari
