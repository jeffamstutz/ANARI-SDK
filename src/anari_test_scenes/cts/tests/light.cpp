// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../GeometryBuilder.h"
#include "../Image.h"
#include "../LightBuilder.h"
#include "../ScreenQuad.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

using LightFn =
    std::function<void(BuildContext &, anari::Device, anari::Light)>;

anari::World lightWorld(
    BuildContext &ctx, const char *subtype, const LightFn &apply)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = 16;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
  auto surface = makeSurface(d, geom, mat);

  auto light = newLight(d, subtype);
  apply(ctx, d, light);
  anari::commitParameters(d, light);

  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);

  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

// Configure a black background and no ambient light, so the test light is the
// only illumination.
void darkRenderer(BuildContext &ctx, anari::Renderer r)
{
  auto d = ctx.device();
  anari::setParameter(d, r, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::setParameter(d, r, "ambientRadiance", 0.f);
  anari::setParameter(d, r, "ambientColor", float3(0.f, 0.f, 0.f));
}

void lp(BuildContext &ctx, anari::Device d, anari::Light l, const char *param)
{
  applyParameterValue(d, l, param, ctx.value(param).raw());
}

// Set an intensityDistribution array from a per-Case token ("none"/"1d"/"2d").
void setIntensityDistribution(
    BuildContext &ctx, anari::Device d, anari::Light l)
{
  const std::string id = ctx.getString("intensityDistribution", "none");
  if (id == "1d") {
    std::vector<float> a = {0.2f, 0.8f, 0.4f, 0.6f};
    anari::setAndReleaseParameter(d,
        l,
        "intensityDistribution",
        anari::newArray1D(d, a.data(), a.size()));
  } else if (id == "2d") {
    std::vector<float> a = {
        0.2f, 0.8f, 0.4f, 0.3f, 0.7f, 0.5f, 0.1f, 0.9f, 0.6f};
    anari::setAndReleaseParameter(
        d, l, "intensityDistribution", anari::newArray2D(d, a.data(), 3, 3));
  }
}

// ---- KHR_LIGHT_PRIMARY_VISIBILITY
// --------------------------------------------
//
// `visible` says whether a light can be directly seen; the spec says it is
// only meaningful for area lights. The Test seen by the screen camera
// (ScreenQuad.h, at (0, 0, 1) looking down -z, fovy pi/2): a lit surface in the
// lower part of the frame and, above it, an area light facing the camera,
// whose footprint is background where the light isn't visible. The Test does
// not assume what the background is: a `visible` value `absent` leaves the
// light out of the scene, and the hidden light's footprint must match that
// render there.

using Footprint = std::function<bool(float2 screen)>;

// Where the camera's ray through normalized screen point `s` meets the plane
// through `point` with normal `n`.
std::optional<float3> screenRayHitsPlane(float2 s, float3 point, float3 n)
{
  const float3 origin(0.f, 0.f, 1.f);
  const float3 dir(2.f * s.x - 1.f, 2.f * s.y - 1.f, -1.f);
  const float denom = anari::math::dot(dir, n);
  if (std::abs(denom) < 1e-6f)
    return std::nullopt;
  const float t = anari::math::dot(point - origin, n) / denom;
  if (t <= 0.f)
    return std::nullopt;
  return origin + t * dir;
}

// The lit surface: a rectangle of the plane z = 0, [-0.8, 0.8] x [-0.9, -0.1],
// which is [0.1, 0.9] x [0.05, 0.45] on screen.
bool onVisibilitySurface(float2 s)
{
  return s.x >= 0.1f && s.x <= 0.9f && s.y >= 0.05f && s.y <= 0.45f;
}

anari::Geometry visibilitySurfaceGeometry(anari::Device d)
{
  const std::vector<float3> verts = {float3(-0.8f, -0.9f, 0.f),
      float3(0.8f, -0.9f, 0.f),
      float3(0.8f, -0.1f, 0.f),
      float3(-0.8f, -0.9f, 0.f),
      float3(0.8f, -0.1f, 0.f),
      float3(-0.8f, -0.1f, 0.f)};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  anari::commitParameters(d, geom);
  return geom;
}

// A quad light of the plane z = 0.5, [-0.3, 0.3] x [0.2, 0.5], emitting on
// both sides so it lights the surface below it and shows its emitting side to
// the camera whichever way the device orients "front".
const float3 kQuadLightPosition(-0.3f, 0.2f, 0.5f);
const float3 kQuadLightEdge1(0.6f, 0.f, 0.f);
const float3 kQuadLightEdge2(0.f, 0.3f, 0.f);

bool onQuadLight(float2 s)
{
  const auto hit =
      screenRayHitsPlane(s, kQuadLightPosition, float3(0.f, 0.f, 1.f));
  if (!hit)
    return false;
  const float3 r = *hit - kQuadLightPosition;
  const float u = anari::math::dot(r, kQuadLightEdge1)
      / anari::math::dot(kQuadLightEdge1, kQuadLightEdge1);
  const float v = anari::math::dot(r, kQuadLightEdge2)
      / anari::math::dot(kQuadLightEdge2, kQuadLightEdge2);
  return u >= 0.f && u <= 1.f && v >= 0.f && v <= 1.f;
}

// A ring light (a disc, with its default innerRadius 0) above the surface,
// tilted so the camera and the surface are both inside its emission cone.
const float3 kRingLightPosition(0.f, 0.2f, 0.5f);
const float3 kRingLightDirection(0.f, -0.8660254f, 0.5f);
constexpr float kRingLightRadius = 0.2f;

bool onRingLight(float2 s)
{
  const auto hit =
      screenRayHitsPlane(s, kRingLightPosition, kRingLightDirection);
  return hit
      && anari::math::length(*hit - kRingLightPosition) <= kRingLightRadius;
}

// An hdri light is the background itself: its footprint is everywhere the
// surface isn't.
bool onHdriBackground(float2 s)
{
  return !onVisibilitySurface(s);
}

// Whether a pixel is wholly on (`inside`) or wholly off a footprint, with
// `margin` pixels of slack on each side: the pixel's centre and the eight
// points `margin` pixels past its edges and corners all agree.
bool pixelOnFootprint(const Footprint &footprint,
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h,
    bool inside,
    float margin)
{
  const float reach = 0.5f + margin;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      const float2 s((x + 0.5f + dx * reach) / w, (y + 0.5f + dy * reach) / h);
      if (footprint(s) != inside)
        return false;
    }
  return true;
}

constexpr float kFootprintMargin = 3.f;

// The `visible` value that leaves the light out of the scene.
constexpr const char *kLightAbsent = "absent";

bool lightAbsent(const BuildContext &values)
{
  return values.getString("visible", "") == kLightAbsent;
}

// The light's footprint shows the light when it is visible (unset or true).
// What a hidden light must render there is the derived expectation's to
// judge; here a hidden or absent light's footprint must only be one flat
// colour (the Test's renderer sets a uniform background), so the Case is not
// a render of a light nobody turned off.
void footprintShowsLight(ExactCheck &x, const Footprint &footprint)
{
  const auto color = x.rgba(Channel::Color);
  PixelMask inside(color.width, color.height);
  std::optional<Rgba8> first;
  for (uint32_t y = 0; y < color.height; ++y)
    for (uint32_t px = 0; px < color.width; ++px)
      if (pixelOnFootprint(footprint,
              px,
              y,
              color.width,
              color.height,
              true,
              kFootprintMargin)) {
        inside.set(px, y);
        if (!first)
          first = color.at(px, y);
      }

  if (!lightAbsent(x.values()) && x.get<bool>("visible", true)) {
    x.allPixels(
        "the footprint of a visible light shows the light",
        color,
        "the light (some colour channel at least 32)",
        [&](uint32_t, uint32_t, const Rgba8 &c) {
          return std::max({c[0], c[1], c[2]}) >= 32;
        },
        &inside);
    return;
  }
  const Rgba8 flat = first.value_or(Rgba8{});
  x.allPixels(
      "the footprint without a visible light is one flat colour",
      color,
      "the colour of the footprint's first pixel (within 2 levels)",
      [&](uint32_t, uint32_t, const Rgba8 &c) {
        for (int k = 0; k < 3; ++k)
          if (std::abs(int(c[k]) - int(flat[k])) > 2)
            return false;
        return true;
      },
      &inside);
}

// What a hidden light must render: the render without the light inside its
// footprint (whatever the background is), the default render everywhere else
// (the light still lights the surface). Pixels near the footprint's edge are
// not compared.
ExpectedImage hiddenLightExpectation(const Image &withLight,
    const Image &withoutLight,
    const Footprint &footprint)
{
  Image image = withLight;
  Image mask;
  mask.width = withLight.width;
  mask.height = withLight.height;
  mask.rgba.assign(size_t(mask.width) * mask.height * 4, 0);
  for (uint32_t y = 0; y < mask.height; ++y)
    for (uint32_t x = 0; x < mask.width; ++x) {
      const bool in = pixelOnFootprint(
          footprint, x, y, mask.width, mask.height, true, kFootprintMargin);
      const bool out = pixelOnFootprint(
          footprint, x, y, mask.width, mask.height, false, kFootprintMargin);
      const size_t i = (size_t(y) * mask.width + x) * 4;
      for (int c = 0; c < 4; ++c) {
        if (in)
          image.rgba[i + c] = withoutLight.rgba[i + c];
        if (in || out)
          mask.rgba[i + c] = 255;
      }
    }
  return maskedBy(std::move(image), std::move(mask));
}

// One light of the visibility Tests.
struct VisibleLight
{
  const char *name; // the Test's name
  const char *subtype;
  const char *extension; // the extension that adds the subtype
  std::function<void(anari::Device, anari::Light)> configure;
  Footprint footprint;
};

anari::World visibilityWorld(BuildContext &ctx, const VisibleLight &l)
{
  auto d = ctx.device();
  auto geom = visibilitySurfaceGeometry(d);
  auto mat = makeMatteMaterial(d, float3(0.8f, 0.8f, 0.8f));
  auto surface = makeSurface(d, geom, mat);

  WorldContents wc;
  wc.surfaces = {surface};
  anari::Light light = nullptr;
  if (!lightAbsent(ctx)) {
    light = newLight(d, l.subtype);
    l.configure(d, light);
    lp(ctx, d, light, "visible");
    anari::commitParameters(d, light);
    wc.lights = {light};
  }
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  if (light)
    anari::release(d, light);
  return world;
}

void registerVisibilityTest(Catalog &catalog, const VisibleLight &l)
{
  std::vector<std::string> required = kSupportMatteTriangle;
  required.push_back(l.extension);
  const Footprint footprint = l.footprint;
  makeTest("light", l.name)
      .description(std::string("Checks that a ") + l.subtype
          + " light with visible false leaves its footprint as it is "
            "without the light and the lit surface unchanged, and that it "
            "is visible by default.")
      .build([l](BuildContext &ctx) { return visibilityWorld(ctx, l); })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .renderer(darkRenderer)
      // `absent` renders the scene without the light.
      .permute("visible", V{none(), Any(false), Any(true), Any(kLightAbsent)})
      // Lights are visible unless said otherwise.
      .expectDerived("visible",
          "true",
          "the default image: a light is visible by default",
          [](const Siblings &s) -> ExpectedImage { return s.at("none"); })
      // Hiding the light changes only where it is: its footprint looks like
      // the scene without the light (whatever the background), and the rest of
      // the frame, the lit surface included, is the default image.
      .expectDerived("visible",
          "false",
          "the scene without the light inside the light's footprint, the "
          "default image outside it",
          [footprint](const Siblings &s) {
            return hiddenLightExpectation(
                s.at("none"), s.at(kLightAbsent), footprint);
          })
      .expectExact("the light's footprint",
          [footprint](ExactCheck &x) { footprintShowsLight(x, footprint); })
      .noGroundTruth()
      .covers("ANARI_KHR_LIGHT_PRIMARY_VISIBILITY")
      .requireFeatures(required)
      .registerInto(catalog);
}

void registerVisibilityTests(Catalog &catalog)
{
  registerVisibilityTest(catalog,
      {"quad_primary_visibility",
          "quad",
          "ANARI_KHR_LIGHT_QUAD",
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", kQuadLightPosition);
            anari::setParameter(d, l, "edge1", kQuadLightEdge1);
            anari::setParameter(d, l, "edge2", kQuadLightEdge2);
            anari::setParameter(d, l, "side", std::string("both"));
            anari::setParameter(d, l, "radiance", 4.f);
          },
          onQuadLight});
  registerVisibilityTest(catalog,
      {"ring_primary_visibility",
          "ring",
          "ANARI_KHR_LIGHT_RING",
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", kRingLightPosition);
            anari::setParameter(d, l, "direction", kRingLightDirection);
            anari::setParameter(d, l, "radius", kRingLightRadius);
            anari::setParameter(d, l, "radiance", 4.f);
          },
          onRingLight});
  registerVisibilityTest(catalog,
      {"hdri_primary_visibility",
          "hdri",
          "ANARI_KHR_LIGHT_HDRI",
          [](anari::Device d, anari::Light l) {
            // A uniform environment, bright and unlike the black background.
            const std::vector<float3> radiance(8 * 4, float3(1.f, 0.5f, 0.1f));
            anari::setAndReleaseParameter(
                d, l, "radiance", anari::newArray2D(d, radiance.data(), 8, 4));
          },
          onHdriBackground});
}
} // namespace

void registerLightTests(Catalog &catalog)
{
  // ---- directional ----------------------------------------------------------
  makeTest("light", "directional")
      .description("Checks directional light direction and irradiance.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "directional",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.7f, 0.62f, 0.78f));
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "irradiance");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("direction",
          V{Any(float3(0.f, -0.242536f, -0.970143f)),
              Any(float3(0.f, 0.f, -0.970143f))})
      .permute("irradiance", V{none(), Any(0.5f)})
      .covers("ANARI_KHR_LIGHT_DIRECTIONAL")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- point ----------------------------------------------------------------
  makeTest("light", "point")
      .description("Checks point light position, intensity, and power.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "point",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.3f, 0.59f, 0.9f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("position",
          V{Any(float3(0.5f, 0.5f, 10.f)), Any(float3(0.5f, 0.5f, 1.f))})
      .permute("intensity", V{none(), Any(0.6f)})
      .permute("power", V{none(), Any(0.4f)})
      .covers("ANARI_KHR_LIGHT_POINT")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- spot -----------------------------------------------------------------
  makeTest("light", "spot")
      .description(
          "Checks spot light placement, cone angles, intensity, and power.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "spot",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.74f, 0.73f, 0.85f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "openingAngle");
              lp(ctx, d, l, "falloffAngle");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      // The baseline cone, straight down from above the grid's centre, lights
      // most of it, so moving, turning, narrowing or dimming the light changes
      // a large share of the lit pixels.
      .permute("position",
          V{Any(float3(0.f, 0.f, 1.f)), Any(float3(0.4f, 0.4f, 1.f))})
      .permute("direction",
          V{Any(float3(0.f, 0.f, -1.f)),
              Any(float3(0.f, 0.371391f, -0.928477f))})
      .permute("openingAngle", V{Any(1.6f), Any(0.8f)})
      .permute("falloffAngle", V{none(), Any(0.6f)})
      .permute("intensity", V{none(), Any(0.3f)})
      .permute("power", V{none(), Any(0.5f)})
      .groundTruth("falloff shape has no analytic answer")
      .covers("ANARI_KHR_LIGHT_SPOT")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- quad -----------------------------------------------------------------
  makeTest("light", "quad")
      .description(
          "Checks quad light shape, emission, sidedness, and distribution.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "quad",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.7f, 0.87f, 0.41f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "edge1");
              lp(ctx, d, l, "edge2");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
              lp(ctx, d, l, "radiance");
              lp(ctx, d, l, "side");
              setIntensityDistribution(ctx, d, l);
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("position", V{Any(float3(0.f, 0.f, 0.1f)), none()})
      .permute("edge1", V{Any(float3(0.f, 0.1f, -1.f)), none()})
      .permute("edge2", V{Any(float3(-0.1f, 1.f, 0.f)), none()})
      .permute("intensity", V{none(), Any(0.6f)})
      .permute("power", V{none(), Any(0.4f)})
      .permute("radiance", V{none(), Any(0.2f)})
      .permute("side", V{Any("both"), Any("front"), Any("back"), none()})
      .permute("intensityDistribution", {"none", "1d", "2d"})
      .covers("ANARI_KHR_LIGHT_QUAD")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- ring -----------------------------------------------------------------
  makeTest("light", "ring")
      .description(
          "Checks ring light shape, emission, orientation, and distribution.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "ring",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.98f, 0.5f, 0.44f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "radius");
              lp(ctx, d, l, "innerRadius");
              lp(ctx, d, l, "openingAngle");
              lp(ctx, d, l, "falloffAngle");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
              lp(ctx, d, l, "radiance");
              lp(ctx, d, l, "c0");
              setIntensityDistribution(ctx, d, l);
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("position", V{Any(float3(0.2f, 0.2f, 0.1f)), none()})
      .permute("direction", V{Any(float3(0.f, -0.20601f, -0.97855f)), none()})
      .permute("radius", V{Any(0.3f), none()})
      .permute("innerRadius", V{Any(0.15f), none()})
      .permute("openingAngle", V{none(), Any(0.5f)})
      .permute("falloffAngle", V{none(), Any(1.1f)})
      .permute("intensity", V{none(), Any(0.6f)})
      .permute("power", V{none(), Any(0.4f)})
      .permute("radiance", V{none(), Any(0.2f)})
      .permute("c0", V{none(), Any(float3(0.7071f, 0.7071f, 0.f))})
      .permute("intensityDistribution", {"none", "1d", "2d"})
      .covers("ANARI_KHR_LIGHT_RING")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- hdri -----------------------------------------------------------------
  makeTest("light", "hdri")
      .description("Checks HDRI light orientation and intensity scaling.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "hdri",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              lp(ctx, d, l, "up");
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "scale");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("up", V{none(), Any(float3(0.f, 1.f, 0.f))})
      .permute("direction", V{none(), Any(float3(0.f, -1.f, 0.f))})
      .permute("scale", V{none(), Any(0.4f)})
      .covers("ANARI_KHR_LIGHT_HDRI")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  registerVisibilityTests(catalog);
}

} // namespace cts
} // namespace anari
