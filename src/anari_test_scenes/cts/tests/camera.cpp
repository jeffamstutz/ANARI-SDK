// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../GeometryBuilder.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using V = std::vector<Any>;

// What cameraSubjectWorld is made of. Each camera Test covers (so requires)
// the camera it renders with; the others don't need the default perspective.
const std::vector<std::string> kSubjectSurface = {
    "ANARI_KHR_GEOMETRY_TRIANGLE", "ANARI_KHR_MATERIAL_MATTE"};

// The subject lies in the z = 0 plane; from this side the default direction
// (0, 0, -1) faces it.
const float3 kEye(0.5f, 0.5f, 1.f);

// A fixed lit triangle surface, the subject every camera test views.
anari::World cameraSubjectWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = 16;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
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

// An imageRegion (box2) axis value: a 2x2 box stored with its ANARI type so it
// is set as FLOAT32_BOX2, not a plain vec4.
Any box2(float lx, float ly, float hx, float hy)
{
  const float v[4] = {lx, ly, hx, hy};
  return Any(ANARI_FLOAT32_BOX2, v);
}

// camera_general's imageRegion: the upper-left quarter of the sensor. It is
// not symmetric, so a device that flips or mirrors the region doesn't pass.
// Its edges are whole pixels at a run size that is a multiple of 4, so the
// Case renders the crop at the crop's own size and must equal the crop of the
// full frame.
struct Region
{
  float loX, loY, hiX, hiY;
};
constexpr Region kRegion = {0.0f, 0.5f, 0.5f, 1.0f};

Any regionValue(const Region &r)
{
  return box2(r.loX, r.loY, r.hiX, r.hiY);
}

// A clip plane that removes the whole subject (which stands a few units from
// the camera): `near` this far, or `far` this close.
constexpr float kNearClipsAll = 50.f;
constexpr float kFarClipsAll = 0.5f;

// A Case whose clip planes remove the whole subject shows only background:
// every pixel equals the corner's (the default background is one colour). A
// Case that clips nothing shows the subject, so some pixel differs from it.
void clippedFrameIsBackground(ExactCheck &x)
{
  const auto color = x.rgba(Channel::Color);
  const Rgba8 corner = color.at(0, 0);
  size_t differing = 0;
  for (uint32_t y = 0; y < color.height; ++y)
    for (uint32_t px = 0; px < color.width; ++px)
      if (valueDistance(color.at(px, y), corner) > 0)
        ++differing;
  const bool clipsAll = x.get<float>("near", 0.01f) >= kNearClipsAll
      || x.get<float>("far", 100.f) <= kFarClipsAll;
  if (clipsAll)
    x.measure("pixels differing from the background corner, all clipped",
        double(differing),
        Limit::AtMost,
        0.0);
  else
    x.measure("pixels of the unclipped subject",
        double(differing),
        Limit::AtLeast,
        1.0);
}

void cp(BuildContext &ctx, anari::Device d, anari::Camera c, const char *param)
{
  applyParameterValue(d, c, param, ctx.value(param).raw());
}

// The default-aspect Tests' frame: twice as wide as tall, so an aspect left
// unset (the frame's, since ANARI 1.2) is 2, not the square run's 1.
CaseFrameSize wideFrame(FrameSize run)
{
  if (run.width < 2 || run.width % 2 != 0)
    return CaseFrameSize::skip("the run's width " + std::to_string(run.width)
        + " is not even, so half of it isn't whole pixels");
  return FrameSize{run.width, run.width / 2};
}

// A camera of `subtype` viewing the subject, with `aspect` from the Case.
anari::Camera aspectCamera(BuildContext &ctx, const char *subtype)
{
  auto d = ctx.device();
  auto c = anari::newObject<anari::Camera>(d, subtype);
  anari::setParameter(d, c, "position", float3(0.5f, 0.5f, -1.f));
  cp(ctx, d, c, "aspect");
  anari::commitParameters(d, c);
  return c;
}

// ANARI 1.2 gives an unset `aspect` as the Frame's width over height. It adds
// that "a potentially set image region needs to be regarded as well" but gives
// no formula, so no Test here sets an `imageRegion` with an unset aspect.
void registerDefaultAspectTest(Catalog &catalog,
    const std::string &name,
    const char *subtype,
    const char *extension)
{
  // The frame is 2:1, so the explicit value is 2.
  const float frameAspect = 2.f;
  makeTest("camera", name)
      .description(std::string("Checks that an unset ") + subtype
          + " aspect is the frame's width over height (ANARI 1.2).")
      .build(cameraSubjectWorld)
      .camera([subtype](BuildContext &ctx, const scenes::Bounds &) {
        return aspectCamera(ctx, subtype);
      })
      .frameSize([](BuildContext &, FrameSize run) { return wideFrame(run); })
      // The first value is the reference: aspect set explicitly.
      .variant("aspect", V{Any(frameAspect), none()})
      .noGroundTruth()
      .covers(extension)
      .requireFeatures(kSubjectSurface)
      .registerInto(catalog);
}

// An unset `fovy` is pi/3 (the spec's default): rendering it must equal
// rendering that value. The runner's own camera sets `fovy` explicitly, so
// nothing else pins the device default.
void registerDefaultFovyTest(Catalog &catalog)
{
  makeTest("camera", "perspective_default_fovy")
      .description("Checks that an unset perspective fovy is pi/3 (ANARI 1.2).")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "perspective");
        anari::setParameter(d, c, "position", float3(0.5f, 0.5f, -1.f));
        cp(ctx, d, c, "fovy");
        anari::commitParameters(d, c);
        return c;
      })
      // The first value is the reference: fovy set to the default.
      .variant("fovy", V{Any(float(M_PI / 3.0)), none()})
      .noGroundTruth()
      .covers("ANARI_KHR_CAMERA_PERSPECTIVE")
      .requireFeatures(kSubjectSurface)
      .registerInto(catalog);
}

} // namespace

void registerCameraTests(Catalog &catalog)
{
  // ---- perspective: general (direction / up / imageRegion) ------------------
  makeTest("camera", "camera_general")
      .description("Checks camera orientation and image-region parameters.")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "perspective");
        anari::setParameter(d, c, "position", kEye);
        anari::setParameter(d, c, "near", 0.01f);
        anari::setParameter(d, c, "far", 100.f);
        cp(ctx, d, c, "direction");
        cp(ctx, d, c, "up");
        cp(ctx, d, c, "imageRegion");
        anari::commitParameters(d, c);
        return c;
      })
      // The region Case renders at the region's pixel size (a skip when the
      // run's size doesn't put its edges on whole pixels).
      .frameSize([](BuildContext &ctx, FrameSize run) -> CaseFrameSize {
        if (!ctx.value("imageRegion").valid())
          return run;
        return regionFrameSize(
            run, kRegion.loX, kRegion.loY, kRegion.hiX, kRegion.hiY);
      })
      .simplified()
      .permute("direction", V{none(), Any(float3(0.20601f, 0.f, -0.97855f))})
      .permute("up", V{none(), Any(float3(0.f, -0.97855f, 0.20601f))})
      .permute("imageRegion", V{none(), regionValue(kRegion)})
      .expectDerived("imageRegion",
          anyToString(regionValue(kRegion)),
          "the region's crop of the full frame",
          [](const Siblings &s) -> ExpectedImage {
            const Image &full = s.at("none");
            const auto rect = regionPixels(full.width,
                full.height,
                kRegion.loX,
                kRegion.loY,
                kRegion.hiX,
                kRegion.hiY);
            return rect ? crop(full, *rect) : Image{};
          })
      .noGroundTruth()
      .covers("ANARI_KHR_CAMERA_PERSPECTIVE")
      .requireFeatures(kSubjectSurface)
      .registerInto(catalog);

  // ---- perspective: intrinsics ----------------------------------------------
  makeTest("camera", "perspective")
      .description(
          "Checks perspective field of view, aspect, and clipping planes.")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "perspective");
        anari::setParameter(d, c, "position", kEye);
        cp(ctx, d, c, "fovy");
        cp(ctx, d, c, "aspect");
        cp(ctx, d, c, "near");
        cp(ctx, d, c, "far");
        anari::commitParameters(d, c);
        return c;
      })
      .simplified()
      .permute("fovy", V{none(), Any(0.6f)})
      .permute("aspect", V{none(), Any(1.77f)})
      .permute("near", V{Any(0.01f), Any(kNearClipsAll)})
      .permute("far", V{Any(100.0f), Any(kFarClipsAll)})
      .expectExact(
          "a clipped subject leaves only background", clippedFrameIsBackground)
      .noGroundTruth()
      .covers("ANARI_KHR_CAMERA_PERSPECTIVE")
      .requireFeatures(kSubjectSurface)
      .registerInto(catalog);

  // ---- orthographic ---------------------------------------------------------
  makeTest("camera", "orthographic")
      .description("Checks orthographic height, aspect, and clipping planes.")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "orthographic");
        anari::setParameter(d, c, "position", kEye);
        cp(ctx, d, c, "aspect");
        cp(ctx, d, c, "height");
        cp(ctx, d, c, "near");
        cp(ctx, d, c, "far");
        anari::commitParameters(d, c);
        return c;
      })
      .simplified()
      .permute("aspect", V{none(), Any(1.77f)})
      .permute("height", V{none(), Any(0.5f)})
      .permute("near", V{Any(0.01f), Any(kNearClipsAll)})
      .permute("far", V{Any(100.0f), Any(kFarClipsAll)})
      .expectExact(
          "a clipped subject leaves only background", clippedFrameIsBackground)
      .noGroundTruth()
      .covers("ANARI_KHR_CAMERA_ORTHOGRAPHIC")
      .requireFeatures(kSubjectSurface)
      .registerInto(catalog);

  // ---- omnidirectional ------------------------------------------------------
  makeTest("camera", "omnidirectional")
      .description("Checks omnidirectional camera layout handling.")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "omnidirectional");
        anari::setParameter(d, c, "position", kEye);
        cp(ctx, d, c, "layout");
        anari::commitParameters(d, c);
        return c;
      })
      // The spec's default layout is equirectangular, so setting it renders
      // like leaving it unset.
      .variant("layout", V{Any("equirectangular"), none()})
      .groundTruth("the equirectangular projection has no analytic answer here")
      .covers("ANARI_KHR_CAMERA_OMNIDIRECTIONAL")
      .requireFeatures(kSubjectSurface)
      .registerInto(catalog);
  // ---- default aspect (ANARI 1.2: the frame's) ------------------------------
  registerDefaultAspectTest(catalog,
      "perspective_default_aspect",
      "perspective",
      "ANARI_KHR_CAMERA_PERSPECTIVE");
  registerDefaultAspectTest(catalog,
      "orthographic_default_aspect",
      "orthographic",
      "ANARI_KHR_CAMERA_ORTHOGRAPHIC");
  registerDefaultFovyTest(catalog);
}

} // namespace cts
} // namespace anari
