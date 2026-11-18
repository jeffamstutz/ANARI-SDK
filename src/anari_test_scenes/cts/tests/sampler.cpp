// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../LightBuilder.h"
#include "../SamplerBuilder.h"
#include "../ScreenQuad.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "generators/ColorPalette.h"
// std
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using anari::math::mat4;
using anari::math::uint3;
using V = std::vector<Any>;

// Why the Tests below keep a Ground-truth comparison (repeated verbatim).
constexpr const char *kWrapGroundTruth =
    "a lit, filtered and wrapped texel colour has no analytic answer in the "
    "Test; effects alone would pass swapped wrap modes";

// The transform/offset constants the JSON scenes reused (column-major: each
// argument is a matrix column; the last column is the translation).
const mat4 kInTransform = mat4(float4(-0.5f, -1.f, 0.f, 0.f),
    float4(1.f, -0.5f, 0.f, 0.f),
    float4(0.f, 0.f, -0.5f, 0.f),
    float4(1.f, 1.f, 1.f, 1.f));
const mat4 kOutTransform = mat4(float4(-1.f, -1.f, 0.f, 0.f),
    float4(1.f, -1.f, 0.f, 0.f),
    float4(0.f, 0.f, -1.f, 0.f),
    float4(1.f, 1.f, 1.f, 1.f));
const float4 kOffset = float4(0.7f, -0.4f, 0.6f, -0.2f);

using GeomFn = std::function<void(anari::Device, anari::Geometry)>;
using SamplerFn =
    std::function<void(BuildContext &, anari::Device, anari::Sampler)>;

// A quad (two triangles) facing the camera, with attribute0 = palette colors
// (used by the transform sampler). Uncommitted so the caller can add the
// per-test texcoord attribute.
anari::Geometry samplerQuad(anari::Device d)
{
  std::vector<float3> pos = {
      {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}};
  std::vector<uint3> idx = {{0u, 1u, 2u}, {2u, 1u, 3u}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(
      d, geom, "vertex.position", anari::newArray1D(d, pos.data(), pos.size()));
  anari::setAndReleaseParameter(
      d, geom, "primitive.index", anari::newArray1D(d, idx.data(), idx.size()));
  std::vector<float4> cols;
  for (size_t i = 0; i < pos.size(); ++i)
    cols.emplace_back(scenes::colors::getColorFromPalette(i), 1.f);
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute0",
      anari::newArray1D(d, cols.data(), cols.size()));
  return geom;
}

anari::World samplerWorld(BuildContext &ctx,
    const char *samplerSubtype,
    const char *inAttribute,
    const GeomFn &setupGeom,
    const SamplerFn &setupSampler)
{
  auto d = ctx.device();
  auto geom = samplerQuad(d);
  if (setupGeom)
    setupGeom(d, geom);
  anari::commitParameters(d, geom);

  const std::string st = samplerSubtype;
  anari::Sampler sampler;
  if (st.rfind("image", 0) == 0) {
    sampler = newImageSampler(d, st, inAttribute, false);
  } else {
    sampler = anari::newObject<anari::Sampler>(d, st.c_str());
    if (inAttribute && *inAttribute)
      anari::setParameter(d, sampler, "inAttribute", inAttribute);
  }
  if (setupSampler)
    setupSampler(ctx, d, sampler);
  anari::commitParameters(d, sampler);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", sampler);
  anari::commitParameters(d, mat);

  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);

  anari::release(d, geom);
  anari::release(d, sampler);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

void set(
    BuildContext &ctx, anari::Device d, anari::Sampler s, const char *param)
{
  applyParameterValue(d, s, param, ctx.value(param).raw());
}

// The primitive sampler's inOffset (ANARI_UINT64 per the spec) and an array of
// kPrimitiveOffset + 2 elements of width T: {0.5, 1} splatted at [0,1], the
// same values swapped at the offset (so ignoring it flips both triangles), and
// black between.
const uint64_t kPrimitiveOffset = 16;

template <typename T>
void setPrimitiveArray(anari::Device d, anari::Sampler s)
{
  std::vector<T> a(kPrimitiveOffset + 2, T(0.f));
  a[0] = T(0.5f);
  a[1] = T(1.f);
  a[kPrimitiveOffset] = T(1.f);
  a[kPrimitiveOffset + 1] = T(0.5f);
  anari::setAndReleaseParameter(
      d, s, "array", anari::newArray1D(d, a.data(), a.size()));
}

// ---- clamp-to-border (KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER) ------------------
//
// The image sampler of `dims` dimensions on the screen quad (ScreenQuad.h),
// its texture coordinate running outside [0, 1] in known screen regions:
// coordinate 1 runs with screen x and coordinate 2 with screen y, each over
// [-0.5, 1.5] across the frame (outside [0, 1] in the outer quarters);
// coordinate 3 runs with screen y over [0.25, 1.75] (above 1 in the top
// half). Every Case filters `nearest`, so the border's edge is sharp.
//
// Axis "wrap": "clampToEdge" (every wrap mode), "borderColorSurface" (the
// quad in the border colour, no sampler), "clampToBorder<N>" (wrap mode N
// clampToBorder with kBorderColor, the rest clampToEdge), and two with wrap
// mode 1 clampToBorder: "zeroBorderColor" (borderColor (0, 0, 0, 0)
// explicitly) and "defaultBorderColor" (borderColor unset), which must
// render alike. The default is compared with the explicit (0, 0, 0, 0)
// rather than with a picture of it because its alpha 0, through the
// material's opacity, makes the border transparent: that is the opacity
// Tests' subject, not this one's.

const float4 kBorderColor(0.9f, 0.15f, 0.6f, 1.f);

std::string wrapParam(int dims, int dim)
{
  return dims == 1 ? "wrapMode" : "wrapMode" + std::to_string(dim);
}

std::string borderValue(int dim)
{
  return "clampToBorder" + std::to_string(dim);
}

// Whether screen point (x, y) samples coordinate `dim` outside [0, 1].
bool outsideImage(int dim, float x, float y)
{
  if (dim == 1)
    return x < 0.25f || x > 0.75f;
  if (dim == 2)
    return y < 0.25f || y > 0.75f;
  return y > 0.5f;
}

// Set the sampler's image (an ARRAY<dims>D): opaque, every texel unlike
// kBorderColor.
void setBorderTestImage(anari::Device d, anari::Sampler s, int dims)
{
  const size_t n = dims == 3 ? 4 : 8;
  const float step = 1.f / float(n - 1);
  std::vector<float4> texels;
  if (dims == 1) {
    for (size_t i = 0; i < n; ++i)
      texels.emplace_back(i * step, i * step, i * step, 1.f);
    anari::setParameterArray1D(d, s, "image", texels.data(), n);
  } else if (dims == 2) {
    for (size_t j = 0; j < n; ++j)
      for (size_t i = 0; i < n; ++i)
        texels.emplace_back(i * step, j * step, 0.2f, 1.f);
    anari::setParameterArray2D(d, s, "image", texels.data(), n, n);
  } else {
    for (size_t k = 0; k < n; ++k)
      for (size_t j = 0; j < n; ++j)
        for (size_t i = 0; i < n; ++i)
          texels.emplace_back(i * step, j * step, k * step, 1.f);
    anari::setParameterArray3D(d, s, "image", texels.data(), n, n, n);
  }
}

anari::World clampToBorderWorld(BuildContext &ctx, int dims)
{
  auto d = ctx.device();
  const std::string wrap = ctx.getString("wrap", "clampToEdge");
  auto geom = makeScreenQuad(
      d, {{"attribute1", dims, [](float2 s) {
             return float4(
                 2.f * s.x - 0.5f, 2.f * s.y - 0.5f, 0.25f + 1.5f * s.y, 0.f);
           }}});
  auto mat = anari::newObject<anari::Material>(d, "matte");
  if (wrap == "borderColorSurface") {
    anari::setParameter(d,
        mat,
        "color",
        float3(kBorderColor.x, kBorderColor.y, kBorderColor.z));
  } else {
    const std::string subtype = "image" + std::to_string(dims) + "D";
    auto sampler = anari::newObject<anari::Sampler>(d, subtype.c_str());
    setBorderTestImage(d, sampler, dims);
    anari::setParameter(d, sampler, "inAttribute", "attribute1");
    anari::setParameter(d, sampler, "filter", "nearest");
    for (int dim = 1; dim <= dims; ++dim) {
      const bool border = wrap == borderValue(dim)
          || (dim == 1
              && (wrap == "zeroBorderColor" || wrap == "defaultBorderColor"));
      anari::setParameter(d,
          sampler,
          wrapParam(dims, dim).c_str(),
          border ? "clampToBorder" : "clampToEdge");
    }
    if (wrap == "zeroBorderColor")
      anari::setParameter(d, sampler, "borderColor", float4(0.f));
    else if (wrap != "defaultBorderColor" && wrap != "clampToEdge")
      anari::setParameter(d, sampler, "borderColor", kBorderColor);
    anari::commitParameters(d, sampler);
    anari::setAndReleaseParameter(d, mat, "color", sampler);
  }
  anari::commitParameters(d, mat);
  auto surface = makeSurface(d, geom, mat);
  auto world = makeScreenQuadWorld(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

void registerClampToBorderTest(Catalog &catalog, int dims)
{
  const std::string subtype = "image" + std::to_string(dims) + "D";
  std::vector<std::string> wraps = {"clampToEdge", "borderColorSurface"};
  for (int dim = 1; dim <= dims; ++dim)
    wraps.push_back(borderValue(dim));
  wraps.push_back("zeroBorderColor");
  wraps.push_back("defaultBorderColor");
  V values;
  for (const auto &w : wraps)
    values.emplace_back(w.c_str());

  auto test = makeTest("sampler", subtype + "_clamp_to_border");
  test.description("Checks that " + subtype
          + " clampToBorder shows borderColor (by default (0, 0, 0, 0))"
            " outside [0, 1] on its own dimension only, and the image inside.")
      .build([dims](BuildContext &ctx) {
        return clampToBorderWorld(ctx, dims);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .permute("wrap", values);
  for (int dim = 1; dim <= dims; ++dim) {
    test.expectDerived("wrap",
        borderValue(dim),
        "borderColorSurface where coordinate " + std::to_string(dim)
            + " leaves [0, 1], clampToEdge elsewhere",
        [dim](const Siblings &s) {
          return regionComposite(s.at("borderColorSurface"),
              s.at("clampToEdge"),
              [dim](float x, float y) { return outsideImage(dim, x, y); });
        });
  }
  test.expectDerived("wrap",
          "defaultBorderColor",
          "zeroBorderColor: the default borderColor is (0, 0, 0, 0)",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("zeroBorderColor");
          })
      .noGroundTruth()
      .covers("ANARI_KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER")
      .requireFeature("ANARI_KHR_SAMPLER_IMAGE" + std::to_string(dims) + "D")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
}

} // namespace

void registerSamplerTests(Catalog &catalog)
{
  // `filter` defaults to linear and the wrap modes to clampToEdge (the spec):
  // an unset value renders like the explicit default, and the default's value
  // is the baseline.
  const V filter = {Any("nearest"), Any("linear"), none()};
  const V wrap = {
      Any("clampToEdge"), Any("repeat"), Any("mirrorRepeat"), none()};
  const V xform = {none(), Any(kInTransform)};
  const V outXform = {none(), Any(kOutTransform)};
  const V offset = {none(), Any(kOffset)};

  // ---- image1D --------------------------------------------------------------
  makeTest("sampler", "image1D")
      .description(
          "Checks 1D image filtering, wrapping, transforms, and offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(
            ctx,
            "image1D",
            "attribute1",
            [](anari::Device d, anari::Geometry g) {
              std::vector<float> uv = {0.f, 2.f, 0.f, 2.f};
              anari::setAndReleaseParameter(d,
                  g,
                  "vertex.attribute1",
                  anari::newArray1D(d, uv.data(), uv.size()));
            },
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "filter");
              set(ctx, d, s, "wrapMode");
              set(ctx, d, s, "inTransform");
              set(ctx, d, s, "outTransform");
              set(ctx, d, s, "inOffset");
              set(ctx, d, s, "outOffset");
            });
      })
      .simplified()
      .permute("filter", filter)
      .expectDerived("filter",
          "none",
          "the linear image (the default filter)",
          [](const Siblings &s) -> ExpectedImage { return s.at("linear"); })
      .permute("wrapMode", wrap)
      .expectDerived("wrapMode",
          "none",
          "the clampToEdge image (the default wrap mode)",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("clampToEdge");
          })
      .permute("inTransform", xform)
      .permute("outTransform", outXform)
      .permute("inOffset", offset)
      .permute("outOffset", offset)
      .groundTruth(kWrapGroundTruth)
      .covers("ANARI_KHR_SAMPLER_IMAGE1D")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- image2D --------------------------------------------------------------
  makeTest("sampler", "image2D")
      .description(
          "Checks 2D image filtering, wrapping, transforms, and offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(
            ctx,
            "image2D",
            "attribute1",
            [](anari::Device d, anari::Geometry g) {
              std::vector<float2> uv = {
                  {0.f, 0.f}, {2.f, 0.f}, {0.f, 2.f}, {2.f, 2.f}};
              anari::setAndReleaseParameter(d,
                  g,
                  "vertex.attribute1",
                  anari::newArray1D(d, uv.data(), uv.size()));
            },
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "filter");
              set(ctx, d, s, "wrapMode1");
              set(ctx, d, s, "wrapMode2");
              set(ctx, d, s, "inTransform");
              set(ctx, d, s, "outTransform");
              set(ctx, d, s, "inOffset");
              set(ctx, d, s, "outOffset");
            });
      })
      .simplified()
      .permute("filter", filter)
      .expectDerived("filter",
          "none",
          "the linear image (the default filter)",
          [](const Siblings &s) -> ExpectedImage { return s.at("linear"); })
      .permute("wrapMode1", wrap)
      .expectDerived("wrapMode1",
          "none",
          "the clampToEdge image (the default wrap mode)",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("clampToEdge");
          })
      .permute("wrapMode2", wrap)
      .expectDerived("wrapMode2",
          "none",
          "the clampToEdge image (the default wrap mode)",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("clampToEdge");
          })
      .permute("inTransform", xform)
      .permute("outTransform", outXform)
      .permute("inOffset", offset)
      .permute("outOffset", offset)
      .groundTruth(kWrapGroundTruth)
      .covers("ANARI_KHR_SAMPLER_IMAGE2D")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- image3D --------------------------------------------------------------
  makeTest("sampler", "image3D")
      .description(
          "Checks 3D image filtering, wrapping, transforms, and offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(
            ctx,
            "image3D",
            "attribute1",
            [](anari::Device d, anari::Geometry g) {
              std::vector<float3> uv = {{0.f, 0.f, 0.f},
                  {2.f, 0.f, 0.f},
                  {0.f, 2.f, 0.f},
                  {2.f, 2.f, 2.f}};
              anari::setAndReleaseParameter(d,
                  g,
                  "vertex.attribute1",
                  anari::newArray1D(d, uv.data(), uv.size()));
            },
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "filter");
              set(ctx, d, s, "wrapMode1");
              set(ctx, d, s, "wrapMode2");
              set(ctx, d, s, "wrapMode3");
              set(ctx, d, s, "inTransform");
              set(ctx, d, s, "outTransform");
              set(ctx, d, s, "inOffset");
              set(ctx, d, s, "outOffset");
            });
      })
      .simplified()
      .permute("filter", filter)
      .expectDerived("filter",
          "none",
          "the linear image (the default filter)",
          [](const Siblings &s) -> ExpectedImage { return s.at("linear"); })
      .permute("wrapMode1", wrap)
      .expectDerived("wrapMode1",
          "none",
          "the clampToEdge image (the default wrap mode)",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("clampToEdge");
          })
      .permute("wrapMode2", wrap)
      .expectDerived("wrapMode2",
          "none",
          "the clampToEdge image (the default wrap mode)",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("clampToEdge");
          })
      .permute("wrapMode3", wrap)
      .expectDerived("wrapMode3",
          "none",
          "the clampToEdge image (the default wrap mode)",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("clampToEdge");
          })
      .permute("inTransform", xform)
      .permute("outTransform", outXform)
      .permute("inOffset", offset)
      .permute("outOffset", offset)
      .groundTruth(kWrapGroundTruth)
      .covers("ANARI_KHR_SAMPLER_IMAGE3D")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- primitive ------------------------------------------------------------
  // The array value is itself an axis; encode the per-Case element width as a
  // token and build the array of that width in the sampler setup. The quad's
  // two triangles read elements [0,1] with no offset and the two at
  // kPrimitiveOffset with it; the elements in between are black so a
  // misapplied offset shows.
  makeTest("sampler", "primitive")
      .description("Checks primitive sampler offsets and array element widths.")
      .build([](BuildContext &ctx) {
        return samplerWorld(ctx,
            "primitive",
            "",
            nullptr,
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "inOffset");
              const std::string dim = ctx.getString("arrayDim", "1");
              if (dim == "1")
                setPrimitiveArray<float>(d, s);
              else if (dim == "2")
                setPrimitiveArray<float2>(d, s);
              else if (dim == "3")
                setPrimitiveArray<float3>(d, s);
              else
                setPrimitiveArray<float4>(d, s);
            });
      })
      .permute("inOffset", V{none(), Any(kPrimitiveOffset)})
      .permute("arrayDim", {"1", "2", "3", "4"})
      // The sampler's output is a colour: a fourth component is alpha, which
      // the colour ignores.
      .expectDerived("arrayDim",
          "4",
          "the 3-component image (the fourth component is alpha)",
          [](const Siblings &s) -> ExpectedImage { return s.at("3"); })
      .groundTruth(kWrapGroundTruth)
      .covers("ANARI_KHR_SAMPLER_PRIMITIVE")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- transform ------------------------------------------------------------
  makeTest("sampler", "transform")
      .description("Checks transform sampler matrices and output offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(ctx,
            "transform",
            "attribute0",
            nullptr,
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "outTransform");
              set(ctx, d, s, "outOffset");
            });
      })
      .permute("outTransform", outXform)
      .permute("outOffset", offset)
      .groundTruth(kWrapGroundTruth)
      .covers("ANARI_KHR_SAMPLER_TRANSFORM")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
  // ---- clamp-to-border ------------------------------------------------------
  for (int dims = 1; dims <= 3; ++dims)
    registerClampToBorderTest(catalog, dims);
}

} // namespace cts
} // namespace anari
