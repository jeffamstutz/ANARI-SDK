// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../GeometryLayout.h"
#include "../LightBuilder.h"
#include "../SamplerBuilder.h"
#include "../ScreenQuad.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "generators/ColorPalette.h"
// std
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

template <typename T>
ParameterBinding constant(T value)
{
  return constantBinding(std::move(value));
}

ParameterBinding attribute(const char *name)
{
  return attributeBinding(name);
}

ParameterBinding sampler(size_t index)
{
  return samplerBinding(index);
}

const char *kMatte = "ANARI_KHR_MATERIAL_MATTE";
const char *kPbr = "ANARI_KHR_MATERIAL_PHYSICALLY_BASED";

struct SamplerSpec
{
  std::string subtype;
  std::string inAttribute;
  bool normalMap{false};
};

using ApplyFn = std::function<void(BuildContext &,
    anari::Device,
    anari::Material,
    const std::vector<anari::Sampler> &)>;

// The canonical triangle Layout (ADR-0007) carrying attribute0 = palette
// colors, attribute1 = planar texcoords, attribute2 = a left-to-right opacity
// ramp, so material bindings to "attribute0"/"attribute2" and samplers reading
// "attribute1" all resolve. The UVs are derived planarly from the grid's XY
// extent (scaled so the checkerboard sampler tiles), and opacity ramps across
// the grid so an opacity binding visibly varies.
anari::Geometry materialBaseGeometry(anari::Device d, int count)
{
  auto verts = layoutTriangleSoup(count);

  float minX = verts[0].x, maxX = verts[0].x;
  float minY = verts[0].y, maxY = verts[0].y;
  for (const auto &v : verts) {
    minX = std::min(minX, v.x);
    maxX = std::max(maxX, v.x);
    minY = std::min(minY, v.y);
    maxY = std::max(maxY, v.y);
  }
  const float spanX = maxX > minX ? maxX - minX : 1.f;
  const float spanY = maxY > minY ? maxY - minY : 1.f;

  std::vector<float2> uv(verts.size());
  std::vector<float> op(verts.size());
  for (size_t i = 0; i < verts.size(); ++i) {
    const float u = (verts[i].x - minX) / spanX;
    const float w = (verts[i].y - minY) / spanY;
    uv[i] = float2(2.f * u, 2.f * w); // [0,2] so the checkerboard tiles
    op[i] = u; // left (transparent) -> right (opaque)
  }

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  auto cols = scenes::colors::getColorVectorFromPalette(verts.size());
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute0",
      anari::newArray1D(d, cols.data(), cols.size()));
  anari::setAndReleaseParameter(
      d, geom, "vertex.attribute1", anari::newArray1D(d, uv.data(), uv.size()));
  anari::setAndReleaseParameter(
      d, geom, "vertex.attribute2", anari::newArray1D(d, op.data(), op.size()));
  anari::commitParameters(d, geom);
  return geom;
}

anari::World materialWorld(BuildContext &ctx,
    const char *subtype,
    int count,
    const std::vector<SamplerSpec> &specs,
    const ApplyFn &apply)
{
  auto d = ctx.device();
  auto geom = materialBaseGeometry(d, count);

  std::vector<anari::Sampler> samplers;
  for (const auto &s : specs) {
    auto sm = newImageSampler(d, s.subtype, s.inAttribute, s.normalMap);
    anari::commitParameters(d, sm);
    samplers.push_back(sm);
  }

  auto mat = anari::newObject<anari::Material>(d, subtype);
  apply(ctx, d, mat, samplers);
  anari::commitParameters(d, mat);

  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);

  anari::release(d, geom);
  for (auto sm : samplers)
    anari::release(d, sm);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

// Apply one axis value to a material parameter (constant / attribute /
// sampler).
void bind(BuildContext &ctx,
    anari::Device d,
    anari::Material mat,
    const char *param,
    const std::vector<anari::Sampler> &s)
{
  applyParameterBinding(d, mat, param, ctx.binding(param), s);
}

// ---- alphaCutoff ------------------------------------------------------------
//
// The screen quad (ScreenQuad.h) whose opacity, bound to attribute0, ramps
// from 0 at its left edge to 1 at its right: opacity = (x + h) / (1 + 2 h)
// at screen x, h the quad's overhang. With alphaCutoff c the spec makes the
// quad opaque where opacity >= c and transparent elsewhere; unset, partial
// opacity blends.
//
// Axis "alphaCutoff": "unset", cutoffs "0", "0.25" and "0.75", and two
// references, "opaqueSurface" (opacity 1) and "noSurface" (no quad).

float opacityAt(float x)
{
  return (x + kScreenQuadOverhang) / (1.f + 2.f * kScreenQuadOverhang);
}

// The screen x where the ramp reaches opacity `c`.
float cutoffEdge(float c)
{
  return c * (1.f + 2.f * kScreenQuadOverhang) - kScreenQuadOverhang;
}

anari::World alphaCutoffWorld(BuildContext &ctx, const char *subtype)
{
  auto d = ctx.device();
  const std::string value = ctx.getString("alphaCutoff", "unset");
  if (value == "noSurface")
    return makeScreenQuadWorld(d, nullptr);

  auto geom =
      makeScreenQuad(d, {{"attribute0", 1, [](float2 s) {
                            return float4(opacityAt(s.x), 0.f, 0.f, 0.f);
                          }}});
  auto mat = anari::newObject<anari::Material>(d, subtype);
  const float3 color(0.9f, 0.3f, 0.2f);
  if (std::string(subtype) == "matte") {
    anari::setParameter(d, mat, "color", color);
  } else {
    anari::setParameter(d, mat, "baseColor", color);
    anari::setParameter(d, mat, "metallic", 0.f);
  }
  if (value != "opaqueSurface") {
    anari::setParameter(d, mat, "opacity", "attribute0");
    if (value != "unset")
      anari::setParameter(d, mat, "alphaCutoff", std::stof(value));
  }
  anari::commitParameters(d, mat);
  auto surface = makeSurface(d, geom, mat);
  auto world = makeScreenQuadWorld(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

void registerAlphaCutoffTest(Catalog &catalog,
    const std::string &name,
    const char *subtype,
    const char *extension)
{
  auto test = makeTest("material", name);
  test.description(std::string("Checks that ") + subtype
          + " alphaCutoff makes opacity binary (opaque at or above it,"
            " transparent below) and that, unset, partial opacity blends.")
      .build([subtype](BuildContext &ctx) {
        return alphaCutoffWorld(ctx, subtype);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .permute("alphaCutoff",
          {"unset", "opaqueSurface", "noSurface", "0", "0.25", "0.75"})
      .expectDerived("alphaCutoff",
          "0",
          "opaqueSurface: every opacity is at least 0",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("opaqueSurface");
          });
  for (const char *c : {"0.25", "0.75"}) {
    const float edge = cutoffEdge(std::stof(c));
    test.expectDerived("alphaCutoff",
        c,
        std::string("opaqueSurface where opacity >= ") + c
            + ", noSurface elsewhere",
        [edge](const Siblings &s) {
          return regionComposite(s.at("opaqueSurface"),
              s.at("noSurface"),
              [edge](float x, float) { return x >= edge; });
        });
  }
  test.noGroundTruth()
      .covers(extension)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);
}

} // namespace

void registerMaterialTests(Catalog &catalog)
{
  // ---- matte ----------------------------------------------------------------
  registerAlphaCutoffTest(catalog, "matte_alpha_cutoff", "matte", kMatte);

  makeTest("material", "matte_color")
      .description(
          "Checks matte color from constants, attributes, and samplers.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "matte",
            16,
            {{"image2D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "color", s);
            });
      })
      .permute("color",
          V{unsetBinding(),
              constant(float3(0.98f, 0.5f, 0.44f)),
              attribute("attribute0"),
              sampler(0)})
      .covers(kMatte)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "matte_opacity")
      .description(
          "Checks matte opacity from constants, attributes, and samplers.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "matte",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "color", float3(1.f, 0.f, 0.f));
              bind(ctx, d, mat, "opacity", s);
            });
      })
      .permute("opacity",
          V{unsetBinding(),
              constant(0.5f),
              attribute("attribute2"),
              sampler(0)})
      .covers(kMatte)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  // ---- physicallyBased ------------------------------------------------------
  registerAlphaCutoffTest(catalog, "pbr_alpha_cutoff", "physicallyBased", kPbr);

  makeTest("material", "pbr_base_color")
      .description(
          "Checks PBR base color from constants, attributes, and samplers.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "baseColor", s);
            });
      })
      .permute("baseColor",
          V{unsetBinding(),
              constant(float3(0.98f, 0.5f, 0.44f)),
              attribute("attribute0"),
              sampler(0)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_clearcoat")
      .description(
          "Checks PBR clearcoat amount, roughness, and normal parameters.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}, {"image2D", "attribute1", true}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "clearcoat", s);
              bind(ctx, d, mat, "clearcoatRoughness", s);
              bind(ctx, d, mat, "clearcoatNormal", s);
            });
      })
      .simplified()
      .permute("clearcoat",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .permute("clearcoatRoughness",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .permute("clearcoatNormal", V{unsetBinding(), sampler(1)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_emissive")
      .description(
          "Checks PBR emissive color from constants, attributes, and samplers.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "emissive", s);
            });
      })
      .permute("emissive",
          V{unsetBinding(),
              constant(float3(1.f, 0.f, 0.f)),
              attribute("attribute0"),
              sampler(0)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_ior")
      .description("Checks the PBR index-of-refraction parameter.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            2,
            {},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "transmission", 0.5f);
              anari::setParameter(d, mat, "thickness", 0.5f);
              bind(ctx, d, mat, "ior", s);
            });
      })
      .permute("ior", V{unsetBinding(), constant(2.42f)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_iridescence")
      .description(
          "Checks PBR iridescence amount, index, and thickness parameters.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "iridescence", s);
              bind(ctx, d, mat, "iridescenceIor", s);
              bind(ctx, d, mat, "iridescenceThickness", s);
            });
      })
      .simplified()
      .permute("iridescence",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .permute("iridescenceIor", V{unsetBinding(), constant(2.0f)})
      .permute("iridescenceThickness",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_metallic_roughness")
      .description("Checks PBR metallic, roughness, and normal parameters.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}, {"image2D", "attribute1", true}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "metallic", s);
              bind(ctx, d, mat, "roughness", s);
              bind(ctx, d, mat, "normal", s);
            });
      })
      .simplified()
      .permute("metallic",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .permute("roughness",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .permute("normal", V{unsetBinding(), sampler(1)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_opacity")
      .description(
          "Checks PBR opacity from constants, attributes, and samplers.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "baseColor", float3(1.f, 0.f, 0.f));
              bind(ctx, d, mat, "opacity", s);
            });
      })
      .permute("opacity",
          V{unsetBinding(),
              constant(0.5f),
              attribute("attribute2"),
              sampler(0)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_sheen")
      .description("Checks PBR sheen color and roughness parameters.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false},
                {"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "sheenColor", s);
              bind(ctx, d, mat, "sheenRoughness", s);
            });
      })
      .simplified()
      .permute("sheenColor",
          V{constant(float3(0.98f, 0.5f, 0.44f)),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .permute("sheenRoughness",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(1)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_specular")
      .description("Checks PBR specular amount and color parameters.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false},
                {"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "specular", s);
              bind(ctx, d, mat, "specularColor", s);
            });
      })
      .simplified()
      .permute("specular",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(1)})
      .permute("specularColor",
          V{constant(float3(0.98f, 0.5f, 0.44f)),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_thickness")
      .description("Checks PBR thickness and volume attenuation parameters.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "transmission", 0.5f);
              bind(ctx, d, mat, "thickness", s);
              bind(ctx, d, mat, "attenuationDistance", s);
              bind(ctx, d, mat, "attenuationColor", s);
            });
      })
      .simplified()
      .permute("thickness",
          V{constant(0.5f),
              unsetBinding(),
              attribute("attribute0"),
              sampler(0)})
      .permute("attenuationDistance", V{constant(0.5f), unsetBinding()})
      .permute("attenuationColor",
          V{constant(float3(0.98f, 0.5f, 0.44f)), unsetBinding()})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  makeTest("material", "pbr_transmission")
      .description(
          "Checks PBR transmission from constants, attributes, and samplers.")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "transmission", s);
            });
      })
      .permute("transmission",
          V{unsetBinding(),
              constant(0.5f),
              attribute("attribute0"),
              sampler(0)})
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
