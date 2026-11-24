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
#include <cmath>
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

// Why the Tests below keep a Ground-truth comparison (repeated verbatim).
constexpr const char *kBindingGroundTruth =
    "the lit colour of a material binding has no analytic answer in the Test; "
    "effects alone would pass swapped bindings";

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
  // Scales the sampled value (the sampler's outTransform), for a parameter
  // with a range other than [0, 1] (a thickness in nanometres).
  float outScale{1.f};
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

// The same attributes on a tessellated unit sphere, for the PBR lobes: a flat
// surface facing the camera shows no highlight, sheen or Fresnel change, and a
// sphere shows every normal direction. attribute3 is a left-to-right ramp from
// 100 to 800, a film thickness in nanometres.
anari::Geometry materialSphereGeometry(anari::Device d)
{
  constexpr int kLon = 32, kLat = 16;
  constexpr float kPi = float(M_PI);
  auto point = [](int i, int j) {
    const float u = float(i) / kLon, v = float(j) / kLat;
    const float phi = 2.f * kPi * u, theta = kPi * v;
    return float3(std::cos(phi) * std::sin(theta),
        std::cos(theta),
        std::sin(phi) * std::sin(theta));
  };
  std::vector<float3> pos;
  std::vector<float2> uv;
  std::vector<float> ramp;
  for (int j = 0; j < kLat; ++j)
    for (int i = 0; i < kLon; ++i) {
      const int ci[6] = {i, i + 1, i + 1, i, i + 1, i};
      const int cj[6] = {j, j, j + 1, j, j + 1, j + 1};
      for (int k = 0; k < 6; ++k) {
        pos.push_back(point(ci[k], cj[k]));
        const float u = float(ci[k]) / kLon, v = float(cj[k]) / kLat;
        uv.push_back(float2(2.f * u, 2.f * v));
        ramp.push_back(u);
      }
    }
  std::vector<float4> thickness(pos.size());
  std::vector<float> op(pos.size());
  for (size_t i = 0; i < pos.size(); ++i) {
    thickness[i] = float4(100.f + 700.f * ramp[i], 0.f, 0.f, 0.f);
    op[i] = ramp[i];
  }

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(
      d, geom, "vertex.position", anari::newArray1D(d, pos.data(), pos.size()));
  // The sphere is centred on the origin, so a position is its own normal.
  anari::setAndReleaseParameter(
      d, geom, "vertex.normal", anari::newArray1D(d, pos.data(), pos.size()));
  auto cols = scenes::colors::getColorVectorFromPalette(pos.size());
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute0",
      anari::newArray1D(d, cols.data(), cols.size()));
  anari::setAndReleaseParameter(
      d, geom, "vertex.attribute1", anari::newArray1D(d, uv.data(), uv.size()));
  anari::setAndReleaseParameter(
      d, geom, "vertex.attribute2", anari::newArray1D(d, op.data(), op.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute3",
      anari::newArray1D(d, thickness.data(), thickness.size()));
  anari::commitParameters(d, geom);
  return geom;
}

// What a PBR lobe Test's scene fixes before the Test binds its parameters.
// `physicallyBased.metallic` defaults to 1, which multiplies every dielectric
// lobe away (specular, ior, transmission, most of clearcoat), so the lobe
// Tests make the surface a dielectric unless metallic is what they vary.
struct LobeScene
{
  bool dielectric{true}; // metallic 0
  bool smooth{true}; // roughness 0.3, so a highlight shows
  bool light{true}; // the directional light; without it, only ambient light
};

constexpr LobeScene kDielectricLobeScene{true, true};
// Lit by ambient light alone.
constexpr LobeScene kAmbientLobeScene{true, true, false};
// metallic and roughness are what the Test varies: unset is the spec default.
constexpr LobeScene kMetalLobeScene{false, false};

anari::World materialWorld(BuildContext &ctx,
    const char *subtype,
    int count,
    const std::vector<SamplerSpec> &specs,
    const ApplyFn &apply,
    const LobeScene *lobe = nullptr)
{
  auto d = ctx.device();
  auto geom = lobe ? materialSphereGeometry(d) : materialBaseGeometry(d, count);

  // Only the samplers the Case binds: a Case binding none builds none, so it
  // runs on a device lacking the sampler (requireFeatureFor); an unbound slot
  // stays null.
  const auto bound = ctx.boundSamplerSlots();
  std::vector<anari::Sampler> samplers;
  for (size_t i = 0; i < specs.size(); ++i) {
    anari::Sampler sm = nullptr;
    if (bound.count(i)) {
      const auto &s = specs[i];
      sm = newImageSampler(d, s.subtype, s.inAttribute, s.normalMap);
      if (s.outScale != 1.f)
        anari::setParameter(d,
            sm,
            "outTransform",
            math::mat4(float4(s.outScale, 0.f, 0.f, 0.f),
                float4(0.f, s.outScale, 0.f, 0.f),
                float4(0.f, 0.f, s.outScale, 0.f),
                float4(0.f, 0.f, 0.f, 1.f)));
      anari::commitParameters(d, sm);
    }
    samplers.push_back(sm);
  }

  auto mat = anari::newObject<anari::Material>(d, subtype);
  if (lobe) {
    // A mid grey base: a white one saturates under a lobe's added light.
    anari::setParameter(d, mat, "baseColor", float3(0.3f));
    if (lobe->dielectric)
      anari::setParameter(d, mat, "metallic", 0.f);
    if (lobe->smooth)
      anari::setParameter(d, mat, "roughness", 0.3f);
  }
  apply(ctx, d, mat, samplers);
  anari::commitParameters(d, mat);

  auto surface = makeSurface(d, geom, mat);
  WorldContents wc;
  wc.surfaces = {surface};
  anari::Light light = nullptr;
  if (!lobe || lobe->light) {
    light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
    wc.lights = {light};
  }
  auto world = assembleWorld(d, wc);

  anari::release(d, geom);
  for (auto sm : samplers)
    if (sm)
      anari::release(d, sm);
  anari::release(d, mat);
  anari::release(d, surface);
  if (light)
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

// The PBR lobe Tests light a grey dielectric sphere with a weak ambient term,
// so the directional light's highlight, and the lobes added to it, show.
void lobeRenderer(BuildContext &ctx, anari::Renderer r)
{
  anari::setParameter(ctx.device(), r, "ambientRadiance", 0.1f);
}

// The iridescence and specular lobes light only the highlight and the rim of
// the sphere, so under the default framing (the sphere about a third of the
// frame) their smallest effects fell below the default 1 %. Their Tests frame
// the sphere as large as it fits: the camera looks down -z, as the default
// framing does, from 1 / sin(fovy / 2) (2 at the spec's default fovy) from the
// unit sphere's centre, so its silhouette touches the frame's edges (ADR-0010
// 66). The other lobe Tests keep the default framing: a larger sphere also
// lowers the Ground-truth PSNR of a Candidate that shades a lobe the Reference
// doesn't, which fails visrtx's clearcoatNormal Case, and the other lobe axes
// clear 1 % without it.
anari::Camera sphereFillingCamera(BuildContext &ctx, const scenes::Bounds &)
{
  CameraProjection projection; // perspective, the spec's default fovy
  projection.position =
      float3(0.f, 0.f, 1.f / std::sin(0.5f * projection.fovy));
  auto camera = ctx.newCamera(projection);
  anari::commitParameters(ctx.device(), camera);
  return camera;
}

// helide shades with its eye-light model and reads only a physicallyBased
// material's baseColor and opacity; halcyon's preview renderer reads, beyond
// them, only metallic, roughness, ior, emissive and normal (its README, Known
// limitations). Their images can't show the other parameters, so those axes'
// effect checks hold only on devices that render them.
TestBuilder &notShadedByHelide(
    TestBuilder &test, std::initializer_list<const char *> axes)
{
  for (const char *axis : axes)
    test.noEffectCheckOn(axis,
        "helide",
        {},
        "helide shades only a physicallyBased material's baseColor and "
        "opacity");
  return test;
}

TestBuilder &notShadedByPreview(
    TestBuilder &test, std::initializer_list<const char *> axes)
{
  for (const char *axis : axes)
    test.noEffectCheckOn(axis,
        "halcyon",
        {"default", "preview"},
        "the preview renderer shades only baseColor, metallic, roughness, ior, "
        "emissive and normal of a physicallyBased material");
  return test;
}

// A clearcoat lobe adds light to the highlight and the rim of the sphere, a
// small part of the default framing, so a bound value that is only a little
// different from a constant can change under the default 1 % of the pixels.
// The clearcoat axes carry a margin of 0.3 % (about 200 pixels at 256x256),
// between the noise floor (0.2 %, the equivalence maximum the convergence
// guard admits; measured 0) and the smallest effect measured on them, 0.41 %
// (ADR-0010 65 and 66, which has the measurements). The call states its axes'
// smallest effect over all pairs, at 256x256 and --accumulation 1024.
constexpr double kLobeEffectMargin = 0.003;

TestBuilder &lobeEffectMargins(
    TestBuilder &test, std::initializer_list<const char *> axes)
{
  for (const char *axis : axes)
    test.effectMargin(axis, kLobeEffectMargin);
  return test;
}

// Both of them: the axis is a lobe neither shades.
TestBuilder &notShadedByPreviewOrHelide(
    TestBuilder &test, std::initializer_list<const char *> axes)
{
  return notShadedByHelide(notShadedByPreview(test, axes), axes);
}

constexpr const char *kNoDeviceShowsThickness =
    "no renderer in the SDK or visrtx shows a transmissive surface's "
    "`thickness` or attenuation on a thin surface: halcyon quality does not "
    "read attenuationDistance and attenuationColor, and thickness alone "
    "changes no pixel there or on visrtx quality";

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
      .requireFeatureFor("color", sampler(0), "ANARI_KHR_SAMPLER_IMAGE2D")
      .groundTruth(kBindingGroundTruth)
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
      .requireFeatureFor("opacity", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
      .groundTruth(kBindingGroundTruth)
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
      .requireFeatureFor("baseColor", sampler(0), "ANARI_KHR_SAMPLER_IMAGE2D")
      .groundTruth(kBindingGroundTruth)
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  {
    auto test = makeTest("material", "pbr_clearcoat");
    test.description(
            "Checks PBR clearcoat amount, roughness, and normal parameters.")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
              "physicallyBased",
              16,
              {{"image1D", "attribute1", false},
                  {"image2D", "attribute1", true}},
              [](BuildContext &ctx,
                  anari::Device d,
                  anari::Material mat,
                  const std::vector<anari::Sampler> &s) {
                bind(ctx, d, mat, "clearcoat", s);
                bind(ctx, d, mat, "clearcoatRoughness", s);
                bind(ctx, d, mat, "clearcoatNormal", s);
              },
              &kDielectricLobeScene);
        })
        .renderer(lobeRenderer)
        .rendererParam("ignoreAmbientLighting", "false")
        .simplified()
        .permute("clearcoat",
            V{constant(1.0f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor("clearcoat", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
        .permute("clearcoatRoughness",
            V{constant(0.3f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor(
            "clearcoatRoughness", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
        .permute("clearcoatNormal", V{unsetBinding(), sampler(1)})
        .requireFeatureFor(
            "clearcoatNormal", sampler(1), "ANARI_KHR_SAMPLER_IMAGE2D");
    notShadedByPreviewOrHelide(
        test, {"clearcoat", "clearcoatRoughness", "clearcoatNormal"});
    // Smallest effect, halcyon quality / visrtx quality: clearcoat 0.41 % /
    // 1.63 % (attribute0 vs 1), clearcoatRoughness 2.05 % / 0.83 % (sampler
    // vs 0.3). clearcoatNormal (4.32 % / 17.65 %) clears the default 1 %.
    // sphereFillingCamera would clear 1 % on both axes, but it fails
    // visrtx's clearcoatNormal Case against Ground truth (ADR-0010 66).
    lobeEffectMargins(test, {"clearcoat", "clearcoatRoughness"});
    test.groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  {
    auto test = makeTest("material", "pbr_emissive");
    test.description(
            "Checks PBR emissive color from constants, attributes, and "
            "samplers.")
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
        .requireFeatureFor("emissive", sampler(0), "ANARI_KHR_SAMPLER_IMAGE2D");
    notShadedByHelide(test, {"emissive"});
    test.groundTruth(kBindingGroundTruth)
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  {
    auto test = makeTest("material", "pbr_ior");
    test.description("Checks the PBR index-of-refraction parameter.")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
              "physicallyBased",
              16,
              {},
              [](BuildContext &ctx,
                  anari::Device d,
                  anari::Material mat,
                  const std::vector<anari::Sampler> &s) {
                // ior only changes the Fresnel reflectance of the specular
                // lobe, which the default specular of 0 zeroes.
                anari::setParameter(d, mat, "specular", 1.f);
                bind(ctx, d, mat, "ior", s);
              },
              &kDielectricLobeScene);
        })
        .renderer(lobeRenderer)
        .rendererParam("ignoreAmbientLighting", "false")
        .permute("ior", V{unsetBinding(), constant(2.42f)});
    notShadedByHelide(test, {"ior"});
    test.groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  // The occlusion map attenuates ambient light only ("only for otherwise
  // unoccluded ambient lighting", 1.2, section 5.11.2): under ambient light
  // alone it changes the image, and under a directional light alone it does
  // not.
  for (bool ambient : {true, false}) {
    auto test = makeTest("material",
        ambient ? "pbr_occlusion_ambient" : "pbr_occlusion_directional");
    test.description(ambient
                ? "Checks that a PBR occlusion map darkens ambient light."
                : "Checks that a PBR occlusion map doesn't change a surface "
                  "lit by a directional light alone.")
        .build([ambient](BuildContext &ctx) {
          return materialWorld(
              ctx,
              "physicallyBased",
              16,
              {{"image2D", "attribute1", false}},
              [](BuildContext &ctx,
                  anari::Device d,
                  anari::Material mat,
                  const std::vector<anari::Sampler> &s) {
                bind(ctx, d, mat, "occlusion", s);
              },
              ambient ? &kAmbientLobeScene : &kDielectricLobeScene);
        })
        .renderer([ambient](BuildContext &ctx, anari::Renderer r) {
          anari::setParameter(
              ctx.device(), r, "ambientRadiance", ambient ? 1.f : 0.f);
        })
        .rendererParam("ignoreAmbientLighting", "false");
    if (ambient)
      test.permute("occlusion", V{unsetBinding(), sampler(0)});
    else
      test.variant("occlusion", V{unsetBinding(), sampler(0)});
    test.requireFeatureFor("occlusion", sampler(0), "ANARI_KHR_SAMPLER_IMAGE2D")
        .noGroundTruth()
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  {
    auto test = makeTest("material", "pbr_iridescence");
    test.description(
            "Checks PBR iridescence amount, index, and thickness parameters "
            "(the thickness in nanometres).")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
              "physicallyBased",
              16,
              {{"image1D", "attribute1", false},
                  {"image1D", "attribute1", false, 800.f}},
              [](BuildContext &ctx,
                  anari::Device d,
                  anari::Material mat,
                  const std::vector<anari::Sampler> &s) {
                bind(ctx, d, mat, "iridescence", s);
                bind(ctx, d, mat, "iridescenceIor", s);
                bind(ctx, d, mat, "iridescenceThickness", s);
              },
              &kDielectricLobeScene);
        })
        .renderer(lobeRenderer)
        .camera(sphereFillingCamera)
        .rendererParam("ignoreAmbientLighting", "false")
        .simplified()
        .permute("iridescence",
            V{constant(1.0f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor(
            "iridescence", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
        .permute("iridescenceIor", V{unsetBinding(), constant(2.0f)})
        // A film's thickness is in nanometres (default 0: no film): the
        // constant is a visible film, attribute3 is a 100 to 800 nm ramp and
        // the sampler's output is scaled to 0 to 800 nm.
        .permute("iridescenceThickness",
            V{constant(400.f),
                unsetBinding(),
                attribute("attribute3"),
                sampler(1)})
        .requireFeatureFor(
            "iridescenceThickness", sampler(1), "ANARI_KHR_SAMPLER_IMAGE1D");
    notShadedByPreviewOrHelide(
        test, {"iridescence", "iridescenceIor", "iridescenceThickness"});
    test.groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  {
    auto test = makeTest("material", "pbr_metallic_roughness");
    test.description("Checks PBR metallic, roughness, and normal parameters.")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
              "physicallyBased",
              16,
              {{"image1D", "attribute1", false},
                  {"image2D", "attribute1", true}},
              [](BuildContext &ctx,
                  anari::Device d,
                  anari::Material mat,
                  const std::vector<anari::Sampler> &s) {
                bind(ctx, d, mat, "metallic", s);
                bind(ctx, d, mat, "roughness", s);
                bind(ctx, d, mat, "normal", s);
              },
              &kMetalLobeScene);
        })
        .renderer(lobeRenderer)
        .rendererParam("ignoreAmbientLighting", "false")
        .simplified()
        .permute("metallic",
            V{constant(0.5f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor("metallic", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
        .permute("roughness",
            V{constant(0.5f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor("roughness", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
        .permute("normal", V{unsetBinding(), sampler(1)})
        .requireFeatureFor("normal", sampler(1), "ANARI_KHR_SAMPLER_IMAGE2D");
    notShadedByHelide(test, {"metallic", "roughness", "normal"});
    test.groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

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
      .requireFeatureFor("opacity", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
      .groundTruth(kBindingGroundTruth)
      .covers(kPbr)
      .requireFeatures(kSupportTriangle)
      .registerInto(catalog);

  {
    auto test = makeTest("material", "pbr_sheen");
    test.description("Checks PBR sheen color and roughness parameters.")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
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
              },
              &kDielectricLobeScene);
        })
        .renderer(lobeRenderer)
        .rendererParam("ignoreAmbientLighting", "false")
        .simplified()
        .permute("sheenColor",
            V{constant(float3(0.98f, 0.5f, 0.44f)),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor(
            "sheenColor", sampler(0), "ANARI_KHR_SAMPLER_IMAGE2D")
        .permute("sheenRoughness",
            V{constant(0.5f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(1)})
        .requireFeatureFor(
            "sheenRoughness", sampler(1), "ANARI_KHR_SAMPLER_IMAGE1D");
    notShadedByPreviewOrHelide(test, {"sheenColor", "sheenRoughness"});
    test.groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  {
    auto test = makeTest("material", "pbr_specular");
    test.description("Checks PBR specular amount and color parameters.")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
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
              },
              &kDielectricLobeScene);
        })
        .renderer(lobeRenderer)
        .camera(sphereFillingCamera)
        .rendererParam("ignoreAmbientLighting", "false")
        .simplified()
        .permute("specular",
            V{constant(1.0f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(1)})
        .requireFeatureFor("specular", sampler(1), "ANARI_KHR_SAMPLER_IMAGE1D")
        .permute("specularColor",
            V{constant(float3(0.98f, 0.5f, 0.44f)),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor(
            "specularColor", sampler(0), "ANARI_KHR_SAMPLER_IMAGE2D");
    notShadedByPreviewOrHelide(test, {"specular", "specularColor"});
    test.groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  {
    auto test = makeTest("material", "pbr_thickness");
    test.description("Checks PBR thickness and volume attenuation parameters.")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
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
              },
              &kDielectricLobeScene);
        })
        .renderer(lobeRenderer)
        .rendererParam("ignoreAmbientLighting", "false")
        .simplified()
        .permute("thickness",
            V{constant(0.5f),
                unsetBinding(),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor("thickness", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D")
        .permute("attenuationDistance", V{constant(0.5f), unsetBinding()})
        .permute("attenuationColor",
            V{constant(float3(0.98f, 0.5f, 0.44f)), unsetBinding()})
        .noEffectCheck("thickness", kNoDeviceShowsThickness)
        .noEffectCheck("attenuationDistance", kNoDeviceShowsThickness)
        .noEffectCheck("attenuationColor", kNoDeviceShowsThickness)
        .groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }

  {
    auto test = makeTest("material", "pbr_transmission");
    test.description(
            "Checks PBR transmission from constants, attributes, and samplers.")
        .build([](BuildContext &ctx) {
          return materialWorld(
              ctx,
              "physicallyBased",
              16,
              {{"image1D", "attribute1", false}},
              [](BuildContext &ctx,
                  anari::Device d,
                  anari::Material mat,
                  const std::vector<anari::Sampler> &s) {
                bind(ctx, d, mat, "transmission", s);
              },
              &kDielectricLobeScene);
        })
        .renderer(lobeRenderer)
        .rendererParam("ignoreAmbientLighting", "false")
        .permute("transmission",
            V{unsetBinding(),
                constant(0.5f),
                attribute("attribute0"),
                sampler(0)})
        .requireFeatureFor(
            "transmission", sampler(0), "ANARI_KHR_SAMPLER_IMAGE1D");
    notShadedByPreviewOrHelide(test, {"transmission"});
    test.groundTruth("the shading of a PBR lobe has no analytic answer")
        .covers(kPbr)
        .requireFeatures(kSupportTriangle)
        .registerInto(catalog);
  }
}

} // namespace cts
} // namespace anari
