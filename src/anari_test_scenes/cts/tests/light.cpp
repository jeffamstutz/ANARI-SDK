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
    // Rows are the azimuth around the light's axis (row 0 at `c0`), columns
    // the angle from it. One bright azimuth, a dark one opposite it and half
    // between, at every angle from the axis, so that turning the C0 plane by
    // a quarter turn (`c0`) moves the lit side of the whole receiver, and a
    // device that ignores `c0` can't pass by averaging a checker.
    const float azimuth[4] = {1.f, 0.55f, 0.1f, 0.55f};
    std::vector<float> a;
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 4; ++col)
        a.push_back(azimuth[row]);
    anari::setAndReleaseParameter(
        d, l, "intensityDistribution", anari::newArray2D(d, a.data(), 4, 4));
  }
}

// A wall in the plane z = 0 with a sphere in front of it, lit from the side so
// the sphere's shadow falls on the wall: the shadow's edge is where the
// directional light's angular diameter shows (a penumbra as wide as the
// sphere's distance to the wall times the angle).
anari::World softShadowWorld(BuildContext &ctx, bool point = false)
{
  auto d = ctx.device();
  const std::vector<float3> wall = {float3(-1.2f, -1.2f, 0.f),
      float3(1.2f, -1.2f, 0.f),
      float3(1.2f, 1.2f, 0.f),
      float3(-1.2f, -1.2f, 0.f),
      float3(1.2f, 1.2f, 0.f),
      float3(-1.2f, 1.2f, 0.f)};
  auto wallGeom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      wallGeom,
      "vertex.position",
      anari::newArray1D(d, wall.data(), wall.size()));
  anari::commitParameters(d, wallGeom);
  auto ball = anari::newObject<anari::Geometry>(d, "sphere");
  const float3 center(-0.1f, 0.f, 0.8f);
  anari::setAndReleaseParameter(
      d, ball, "vertex.position", anari::newArray1D(d, &center, 1));
  anari::setParameter(d, ball, "radius", 0.35f);
  anari::commitParameters(d, ball);

  auto mat = makeMatteMaterial(d, float3(0.8f, 0.8f, 0.8f));
  auto wallSurface = makeSurface(d, wallGeom, mat);
  auto ballSurface = makeSurface(d, ball, mat);

  anari::Light light;
  if (point) {
    // A point light above and to the side of the sphere, so its shadow falls
    // on the wall; its radius makes the penumbra.
    light = anari::newObject<anari::Light>(d, "point");
    anari::setParameter(d, light, "position", float3(0.6f, 0.f, 2.f));
    anari::setParameter(d, light, "intensity", 8.f);
    lp(ctx, d, light, "radius");
  } else {
    light = anari::newObject<anari::Light>(d, "directional");
    anari::setParameter(
        d, light, "direction", anari::math::normalize(float3(0.5f, 0.f, -1.f)));
    anari::setParameter(d, light, "irradiance", 3.f);
    lp(ctx, d, light, "angularDiameter");
  }
  anari::commitParameters(d, light);

  WorldContents wc;
  wc.surfaces = {wallSurface, ballSurface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, wallGeom);
  anari::release(d, ball);
  anari::release(d, mat);
  anari::release(d, wallSurface);
  anari::release(d, ballSurface);
  anari::release(d, light);
  return world;
}

// A radiance image that is bright at the top and dark at the bottom, with a
// colour cast, unlike the checkerboard newLight() gives an hdri light: it
// lights a surface from above rather than in patches.
void setGradientRadiance(anari::Device d, anari::Light l)
{
  constexpr size_t kSize = 64;
  std::vector<float3> radiance;
  radiance.reserve(kSize * kSize);
  for (size_t row = 0; row < kSize; ++row) {
    const float up = float(row) / float(kSize - 1);
    for (size_t col = 0; col < kSize; ++col)
      radiance.emplace_back(0.1f + 2.f * up * up, 0.1f + 1.5f * up, 0.1f + up);
  }
  anari::setAndReleaseParameter(
      d, l, "radiance", anari::newArray2D(d, radiance.data(), kSize, kSize));
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
      .rendererParam("ignoreAmbientLighting", "false")
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
      .rendererParam("ignoreAmbientLighting", "false")
      .simplified()
      .permute("direction",
          V{Any(float3(0.f, 0.f, -1.f)), Any(float3(0.f, -0.9f, -0.43589f))})
      .permute("irradiance", V{none(), Any(0.5f)})
      .groundTruth("directional shading has no analytic answer in the Test")
      .covers("ANARI_KHR_LIGHT_DIRECTIONAL")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("light", "directional_angular_diameter")
      .description(
          "Checks that a directional light's angularDiameter softens the "
          "edge of a shadow: the penumbra grows with it, and 0 is the "
          "default hard edge.")
      .build([](BuildContext &ctx) { return softShadowWorld(ctx); })
      .renderer(darkRenderer)
      .rendererParam("ignoreAmbientLighting", "false")
      .permute("angularDiameter", V{none(), Any(0.25f), Any(0.6f), Any(0.f)})
      .noEffectCheckOn("angularDiameter",
          "halcyon",
          {"default", "preview"},
          "halcyon's preview renderer casts no shadows, so a shadow's edge "
          "has no penumbra to widen")
      .expectDerived("angularDiameter",
          "0",
          "the default hard shadow: angularDiameter 0 is the default",
          [](const Siblings &s) -> ExpectedImage { return s.at("none"); })
      .noGroundTruth()
      .covers("ANARI_KHR_LIGHT_DIRECTIONAL")
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE",
          "ANARI_KHR_GEOMETRY_SPHERE",
          "ANARI_KHR_MATERIAL_MATTE",
          kSupportPerspective})
      .registerInto(catalog);

  // The brightness is irradiance, else radiance (a radiance means something
  // only with an angular diameter above 0: its solid angle makes the
  // irradiance): irradiance 0.4, radiance 20 over 0.2 rad (about 0.63 W/m2)
  // and the default 1 all light the grid differently, and a radiance set
  // beside an explicit irradiance is ignored.
  makeTest("light", "directional_radiance")
      .description(
          "Checks that a directional light's radiance brightens the scene "
          "when no irradiance is set, and that an explicit irradiance takes "
          "precedence over it.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "directional",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              const std::string mode = ctx.getString("brightness", "default");
              anari::setParameter(d, l, "color", float3(0.7f, 0.62f, 0.78f));
              if (mode == "irradiance" || mode == "both")
                anari::setParameter(d, l, "irradiance", 0.4f);
              if (mode == "radiance" || mode == "both") {
                anari::setParameter(d, l, "angularDiameter", 0.2f);
                anari::setParameter(d, l, "radiance", 20.f);
              }
            });
      })
      .renderer(darkRenderer)
      .rendererParam("ignoreAmbientLighting", "false")
      .permute("brightness", V{"default", "irradiance", "radiance", "both"})
      .expectDerived("brightness",
          "both",
          "irradiance: an explicit irradiance takes precedence over radiance",
          [](const Siblings &s) -> ExpectedImage { return s.at("irradiance"); })
      .noGroundTruth()
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
      .rendererParam("ignoreAmbientLighting", "false")
      .simplified()
      // The baseline light is close enough to the grid, and the alternatives
      // for intensity and power bright enough, that each changes the lit
      // pixels by many 8-bit levels: a far, dim light is nearly black and
      // hides a change of intensity.
      .permute("position",
          V{Any(float3(0.5f, 0.5f, 2.f)), Any(float3(0.5f, 0.5f, 1.f))})
      .permute("intensity", V{none(), Any(4.f)})
      .permute("power", V{none(), Any(40.f)})
      .groundTruth("point light falloff has no analytic answer in the Test")
      .covers("ANARI_KHR_LIGHT_POINT")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("light", "point_radius")
      .description(
          "Checks that a point light's radius softens the edge of a shadow: "
          "the penumbra grows with it, and 0 is the default hard edge.")
      .build([](BuildContext &ctx) { return softShadowWorld(ctx, true); })
      .renderer(darkRenderer)
      .rendererParam("ignoreAmbientLighting", "false")
      .permute("radius", V{none(), Any(0.15f), Any(0.4f), Any(0.f)})
      .expectDerived("radius",
          "0",
          "the default hard shadow: radius 0 is the default",
          [](const Siblings &s) -> ExpectedImage { return s.at("none"); })
      .noEffectCheckOn("radius",
          "halcyon",
          {"default", "preview"},
          "halcyon's preview renderer casts no shadows, so a shadow's edge "
          "has no penumbra to widen")
      .noGroundTruth()
      .covers("ANARI_KHR_LIGHT_POINT")
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE",
          "ANARI_KHR_GEOMETRY_SPHERE",
          "ANARI_KHR_MATERIAL_MATTE",
          kSupportPerspective})
      .registerInto(catalog);

  // The brightness is intensity, else power, else radiance (a radiance means
  // something only with a radius above 0, whose surface area makes the
  // intensity): the default intensity 1, intensity 3 and a radiance of 60 over
  // radius 0.2 (an intensity of about 7.5 W/sr) all light the grid
  // differently, and a radiance beside an explicit intensity is ignored.
  makeTest("light", "point_radiance")
      .description(
          "Checks that a point light's radiance brightens the scene when no "
          "intensity or power is set, and that an explicit intensity takes "
          "precedence over it.")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx,
            "point",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              const std::string mode = ctx.getString("brightness", "default");
              anari::setParameter(d, l, "color", float3(0.3f, 0.59f, 0.9f));
              anari::setParameter(d, l, "position", float3(0.5f, 0.5f, 1.f));
              if (mode == "intensity" || mode == "both")
                anari::setParameter(d, l, "intensity", 3.f);
              if (mode == "radiance" || mode == "both") {
                anari::setParameter(d, l, "radius", 0.2f);
                anari::setParameter(d, l, "radiance", 60.f);
              }
            });
      })
      .renderer(darkRenderer)
      .rendererParam("ignoreAmbientLighting", "false")
      .permute("brightness", V{"default", "intensity", "radiance", "both"})
      .expectDerived("brightness",
          "both",
          "intensity: an explicit intensity takes precedence over radiance",
          [](const Siblings &s) -> ExpectedImage { return s.at("intensity"); })
      .noGroundTruth()
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
      .rendererParam("ignoreAmbientLighting", "false")
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
      .rendererParam("ignoreAmbientLighting", "false")
      .simplified()
      // A quad above the grid facing it (edge1 x edge2 points down, so the
      // front side lights the grid): the baseline covers most of the grid, and
      // each alternative moves, shrinks or dims it enough to change the lit
      // pixels by many 8-bit levels.
      .permute("position",
          V{Any(float3(0.1f, 0.1f, 0.5f)), Any(float3(0.5f, 0.5f, 0.5f))})
      .permute(
          "edge1", V{Any(float3(0.f, 0.8f, 0.f)), Any(float3(0.f, 0.3f, 0.f))})
      .permute(
          "edge2", V{Any(float3(0.8f, 0.f, 0.f)), Any(float3(0.3f, 0.f, 0.f))})
      .permute("intensity", V{none(), Any(4.f)})
      .permute("power", V{none(), Any(40.f)})
      .permute("radiance", V{none(), Any(0.2f)})
      // The side facing the grid lights it; the back faces away (a dark
      // grid); an unset side is the front (the spec's default).
      .permute("side", V{Any("both"), Any("front"), Any("back"), none()})
      .expectDerived("side",
          "none",
          "the front side (the default)",
          [](const Siblings &s) -> ExpectedImage { return s.at("front"); })
      .permute("intensityDistribution", {"none", "1d", "2d"})
      .groundTruth(
          "the falloff of an area light's shading has no analytic answer")
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
      .rendererParam("ignoreAmbientLighting", "false")
      .simplified()
      // A ring above the grid facing down. The baseline opens wide enough to
      // light the grid; each alternative moves, tilts, narrows or dims it
      // enough to change the lit pixels by many 8-bit levels. The baseline
      // intensity distribution is the 2D one, since `c0` (the reference
      // direction of its azimuth) only matters with it.
      .permute("position",
          V{Any(float3(0.5f, 0.5f, 1.f)), Any(float3(0.2f, 0.2f, 1.f))})
      .permute("direction",
          V{Any(float3(0.f, 0.f, -1.f)), Any(float3(0.3f, 0.f, -0.954f))})
      .permute("radius", V{Any(0.3f), Any(0.15f)})
      .permute("innerRadius", V{none(), Any(0.28f)})
      .permute("openingAngle", V{Any(1.4f), Any(0.7f)})
      .permute("falloffAngle", V{none(), Any(1.0f)})
      .permute("intensity", V{none(), Any(4.f)})
      .permute("power", V{none(), Any(40.f)})
      .permute("radiance", V{none(), Any(0.2f)})
      .permute("c0", V{none(), Any(float3(0.f, 1.f, 0.f))})
      .permute("intensityDistribution", {"2d", "none", "1d"})
      .groundTruth(
          "the falloff of an area light's shading has no analytic answer")
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
              lp(ctx, d, l, "color");
              lp(ctx, d, l, "layout");
              if (ctx.getString("radiance", "checker") == "gradient")
                setGradientRadiance(d, l);
            });
      })
      .renderer(darkRenderer)
      .rendererParam("ignoreAmbientLighting", "false")
      .simplified()
      .permute("up", V{none(), Any(float3(0.f, 1.f, 0.f))})
      .permute("direction", V{none(), Any(float3(0.f, -1.f, 0.f))})
      .permute("scale", V{none(), Any(0.4f)})
      // The light's colour tints its radiance (default white).
      .permute("color", V{none(), Any(float3(1.f, 0.5f, 0.2f))})
      // equirectangular is the only layout, and the default.
      .permute("layout", V{none(), Any("equirectangular")})
      .expectDerived("layout",
          "equirectangular",
          "the default layout: an explicit equirectangular is the default",
          [](const Siblings &s) -> ExpectedImage { return s.at("none"); })
      // Another radiance image lights the surface from other directions.
      .permute("radiance", {"checker", "gradient"})
      .groundTruth("environment lighting has no analytic answer")
      .covers("ANARI_KHR_LIGHT_HDRI")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  registerVisibilityTests(catalog);
}

} // namespace cts
} // namespace anari
