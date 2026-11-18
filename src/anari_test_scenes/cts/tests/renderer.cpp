// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../GeometryBuilder.h"
#include "../ScreenQuad.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "FrameTypeAxis.h"
#include "ParameterQuery.h"
#include "generators/TextureGenerator.h"
// std
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

// A matte triangle surface lit only by the renderer's ambient term.
anari::World ambientSubject(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = 16;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
  auto surface = makeSurface(d, geom, mat);
  WorldContents wc;
  wc.surfaces = {surface};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

// The background colour is what the Case set: every pixel of an empty world
// shows it, sRGB-encoded (the colour Channel's default format is sRGB), with
// the alpha the background has.
void backgroundIsWhatWasSet(ExactCheck &x)
{
  const auto color = x.rgba(Channel::Color);
  const float4 bg = x.get<float4>("background", float4(0.f, 0.f, 0.f, 1.f));
  const Rgba8 expected = {
      uint8_t(
          std::lround(std::clamp(srgbEncodedValue(bg.x), 0.f, 1.f) * 255.f)),
      uint8_t(
          std::lround(std::clamp(srgbEncodedValue(bg.y), 0.f, 1.f) * 255.f)),
      uint8_t(
          std::lround(std::clamp(srgbEncodedValue(bg.z), 0.f, 1.f) * 255.f)),
      uint8_t(std::lround(std::clamp(bg.w, 0.f, 1.f) * 255.f))};
  x.matches(
      "every pixel shows the background colour (sRGB-encoded)",
      color,
      [&](uint32_t, uint32_t) { return expected; },
      3.0);
}

// An empty world: the background-only tests render no geometry.
anari::World emptyWorld(BuildContext &ctx)
{
  return assembleWorld(ctx.device(), WorldContents{});
}

// A flat, head-on lit surface filling the frame of the screen camera.
anari::World flatSurfaceWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  auto geom = makeScreenQuad(d);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto world = makeScreenQuadWorld(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

// The middle of the frame shows the lit surface, not black or nothing.
void flatSurfaceRenders(ExactCheck &x)
{
  const auto color = x.rgba(Channel::Color);
  PixelMask middle(color.width, color.height);
  for (uint32_t y = 0; y < color.height; ++y)
    for (uint32_t px = 0; px < color.width; ++px) {
      const float sx = (px + 0.5f) / color.width,
                  sy = (y + 0.5f) / color.height;
      if (sx >= 0.2f && sx <= 0.8f && sy >= 0.2f && sy <= 0.8f)
        middle.set(px, y);
    }
  x.allPixels(
      "the middle of the frame shows the lit surface",
      color,
      "lit (some colour channel at least 32)",
      [](uint32_t, uint32_t, const Rgba8 &c) {
        return std::max({c[0], c[1], c[2]}) >= 32;
      },
      &middle);
}

// KHR_RENDERER_DENOISE adds `denoise` (BOOL) to the renderer; some renderer
// subtype must list it.
BehaviorResult checkDenoiseParameter(anari::Device d,
    anari::World,
    anari::Camera,
    anari::Renderer,
    uint32_t,
    uint32_t)
{
  std::string checked;
  const char **subtypes = anariGetObjectSubtypes(d, ANARI_RENDERER);
  for (int i = 0; subtypes && subtypes[i]; ++i) {
    if (listsParameter(d, ANARI_RENDERER, subtypes[i], "denoise", ANARI_BOOL))
      return {true,
          std::string("renderer '") + subtypes[i] + "' lists denoise (BOOL)"};
    checked += std::string(checked.empty() ? "" : ", ") + subtypes[i];
  }
  return {false,
      "no renderer subtype lists denoise (BOOL); checked: "
          + (checked.empty() ? std::string("none") : checked)};
}
} // namespace

void registerRendererTests(Catalog &catalog)
{
  makeTest("renderer", "renderer_ambient")
      .description("Checks renderer ambient light color and radiance.")
      .build(ambientSubject)
      .renderer([](BuildContext &ctx, anari::Renderer r) {
        auto d = ctx.device();
        applyParameterValue(
            d, r, "ambientColor", ctx.value("ambientColor").raw());
        applyParameterValue(
            d, r, "ambientRadiance", ctx.value("ambientRadiance").raw());
      })
      .rendererParam("ignoreAmbientLighting", "false")
      .simplified()
      .noEffectCheckOn("ambientColor",
          "helide",
          {},
          "helide reads the renderer's ambientRadiance but never its "
          "ambientColor (no device ticket)")
      .permute("ambientColor",
          V{Any(float3(0.f, 0.f, 1.f)),
              Any(float3(0.5f, 0.5f, 0.5f)),
              Any(float3(0.f, 0.f, 0.f)),
              Any(float3(1.f, 1.f, 1.f))})
      .permute("ambientRadiance", V{Any(0.5f), Any(4.0f), Any(0.0f)})
      .groundTruth(
          "the ambient-lit appearance has no analytic answer in the Test")
      .covers("ANARI_KHR_RENDERER_AMBIENT_LIGHT")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("renderer", "renderer_background_color")
      .description("Checks renderer background color and alpha values.")
      .build(emptyWorld)
      .renderer([](BuildContext &ctx, anari::Renderer r) {
        auto d = ctx.device();
        applyParameterValue(d, r, "background", ctx.value("background").raw());
      })
      .permute("background",
          V{Any(float4(0.f, 0.f, 0.f, 1.f)),
              Any(float4(0.5f, 0.5f, 0.5f, 1.f)),
              Any(float4(0.f, 0.f, 1.f, 1.f)),
              Any(float4(0.f, 0.f, 1.f, 0.5f))})
      .expectExact("the background colour", backgroundIsWhatWasSet)
      .noGroundTruth()
      .covers("ANARI_KHR_RENDERER_BACKGROUND_COLOR")
      .requireFeature(kSupportPerspective)
      .registerInto(catalog);

  makeTest("renderer", "renderer_background_image")
      .description("Checks image-based renderer backgrounds.")
      .build(emptyWorld)
      .renderer([](BuildContext &ctx, anari::Renderer r) {
        auto d = ctx.device();
        const size_t res = 32;
        auto img = scenes::TextureGenerator::generateCheckerBoard(res);
        anari::setAndReleaseParameter(
            d, r, "background", anari::newArray2D(d, img.data(), res, res));
      })
      .groundTruth(
          "the image the background shows (its orientation and sampling) has "
          "no analytic answer in the Test")
      .covers("ANARI_KHR_RENDERER_BACKGROUND_IMAGE")
      .requireFeature(kSupportPerspective)
      .registerInto(catalog);

  // ---- denoise --------------------------------------------------------------
  // The spec says only that the image "should be denoised", not how much it
  // may change, and no SDK device implements it or has a noisy setup to
  // calibrate a bound on, so the render Test requires only that a frame still
  // renders with denoise on and off.
  makeTest("renderer", "renderer_denoise")
      .description(
          "Checks that a flat lit surface still renders with denoise on and "
          "off.")
      .build(flatSurfaceWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .renderer([](BuildContext &ctx, anari::Renderer r) {
        anari::setParameter(
            ctx.device(), r, "denoise", ctx.get<bool>("denoise", false));
      })
      .permute("denoise", V{Any(false), Any(true)})
      .noEffectCheck("denoise",
          "a denoiser may leave an image with no noise unchanged, and the "
          "spec doesn't say how much it changes one")
      .expectExact("the flat surface renders", flatSurfaceRenders)
      .noGroundTruth()
      .covers("ANARI_KHR_RENDERER_DENOISE")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("renderer", "renderer_denoise_parameter")
      .description(
          "Checks that the device's introspection lists the renderer's "
          "denoise parameter.")
      .build(emptyWorld)
      .behavior(checkDenoiseParameter)
      .covers("ANARI_KHR_RENDERER_DENOISE")
      .requireFeature(kSupportPerspective)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
