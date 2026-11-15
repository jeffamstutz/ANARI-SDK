// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../AnariObject.h"
#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../FrameAccumulation.h"
#include "../FrameReadback.h"
#include "../GeometryBuilder.h"
#include "../GeometryLayout.h"
#include "../InstanceBuilder.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../TestDef.h"
#include "../VolumeBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using V = std::vector<Any>;

// frame_primitiveID_channel's triangles: a soup of this many, so triangle i
// is primitive i.
constexpr int kPrimitiveIdTriangles = 8;

float cross2(float2 a, float2 b)
{
  return a.x * b.y - a.y * b.x;
}

// Whether point `p` lies inside triangle (a, b, c) or within `margin` of each
// of its edges' lines (pixel space): the triangle grown by `margin`.
bool nearTriangle(float2 a, float2 b, float2 c, float2 p, float margin)
{
  const float area = cross2(b - a, c - a);
  if (area == 0.f)
    return false;
  const float side = area > 0.f ? 1.f : -1.f;
  auto inside = [&](float2 u, float2 v) {
    const float2 e = v - u;
    return side * cross2(e, p - u) / anari::math::length(e) >= -margin;
  };
  return inside(a, b) && inside(b, c) && inside(c, a);
}

// The primitive ID channel holds each triangle's index where it is, and no
// triangle's index where none is. The spec (1.2, section 5.1) says the
// channel holds the "primitiveId attribute of the first hit" and gives no
// value for a pixel without a hit, so a background pixel is checked only for
// not claiming a triangle.
void primitiveIdsAreIndices(ExactCheck &x)
{
  const auto ids = x.ids(Channel::PrimitiveId);
  const auto positions = layoutTriangleSoup(kPrimitiveIdTriangles);
  auto pixelOf = [&](float3 p) {
    const auto at = x.project(p);
    if (!at)
      throw std::runtime_error("a triangle is behind the camera");
    return *at;
  };

  PixelMask centroids(ids.width, ids.height);
  PixelMask subject(ids.width, ids.height);
  std::vector<int> expected(size_t(ids.width) * ids.height, -1);
  for (int i = 0; i < kPrimitiveIdTriangles; ++i) {
    const float3 a = positions[3 * i], b = positions[3 * i + 1],
                 c = positions[3 * i + 2];
    const float2 centroid = pixelOf((a + b + c) / 3.f);
    if (centroid.x < 0.f || centroid.y < 0.f || centroid.x >= ids.width
        || centroid.y >= ids.height)
      throw std::runtime_error(
          "triangle " + std::to_string(i) + "'s centroid is outside the frame");
    const auto cx = uint32_t(centroid.x), cy = uint32_t(centroid.y);
    centroids.set(cx, cy);
    expected[size_t(cy) * ids.width + cx] = i;

    // Its footprint, grown by 1.5 pixels so edge pixels, which may hold
    // either value, are not judged as background.
    const float2 pa = pixelOf(a), pb = pixelOf(b), pc = pixelOf(c);
    for (uint32_t y = 0; y < ids.height; ++y)
      for (uint32_t px = 0; px < ids.width; ++px)
        if (nearTriangle(pa, pb, pc, float2(px + 0.5f, y + 0.5f), 1.5f))
          subject.set(px, y);
  }

  x.matches(
      "the ID at each triangle's projected centroid is the triangle's index",
      ids,
      [&](uint32_t px, uint32_t y) {
        return uint32_t(expected[size_t(y) * ids.width + px]);
      },
      0.0,
      &centroids);
  x.backgroundOutside("pixels outside every triangle hold no triangle's index",
      ids,
      subject,
      "no triangle's index (>= " + std::to_string(kPrimitiveIdTriangles) + ")",
      [](uint32_t id) { return id >= uint32_t(kPrimitiveIdTriangles); });
}

// A single lit triangle surface (the subject for the simple channel tests).
anari::World triangleSurfaceWorld(BuildContext &ctx, float3 color, int count)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = count;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, color);
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

// A surface over a single canonical triangle translated by `offset`, optionally
// tagged with an id. The id tests place two of these at different offsets so
// the two ids occupy distinct, non-overlapping regions of the id channel.
anari::Surface idSurface(
    anari::Device d, float3 offset, uint32_t id, bool setId)
{
  auto verts = layoutTriangleSoup(1);
  for (auto &v : verts)
    v = v + offset;
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  anari::commitParameters(d, geom);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
  auto s = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, s, "geometry", geom);
  anari::setParameter(d, s, "material", mat);
  if (setId)
    anari::setParameter(d, s, "id", id);
  anari::commitParameters(d, s);
  anari::release(d, geom);
  anari::release(d, mat);
  return s;
}

// An instance wrapping a group with one surface, translated by `offset` so two
// instances occupy distinct regions, optionally tagged with an id.
anari::Instance idInstance(
    anari::Device d, float3 offset, uint32_t id, bool setId)
{
  auto s = idSurface(d, float3(0.f), 0, false);
  auto inst = makeInstance(d, {s}, anari::math::translation_matrix(offset));
  anari::release(d, s);
  if (setId) {
    anari::setParameter(d, inst, "id", id);
    anari::commitParameters(d, inst);
  }
  return inst;
}

// A frame carrying the runner's world/camera/renderer, ready to render.
UniqueAnariObject<anari::Frame> behaviorFrame(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  UniqueAnariObject<anari::Frame> frame(d, anari::newObject<anari::Frame>(d));
  const auto f = frame.get();
  anari::setParameter(d, f, "size", anari::math::vec<uint32_t, 2>(w, h));
  anari::setParameter(d, f, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, f, "renderer", renderer);
  anari::setParameter(d, f, "camera", camera);
  anari::setParameter(d, f, "world", world);
  return frame;
}

// Behavioral check: the device must invoke the frame-completion callback
// exactly when a frame finishes. Passes if the callback fired during
// render/wait.
BehaviorResult checkFrameCompletionCallback(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  auto frame = behaviorFrame(d, world, camera, renderer, w, h);
  const auto f = frame.get();

  int fired = 0;
  anari::setParameter(d,
      f,
      "frameCompletionCallback",
      (anari::FrameCompletionCallback)(+[](const void *userPtr,
                                            ANARIDevice,
                                            ANARIFrame) {
        if (userPtr)
          ++*static_cast<int *>(const_cast<void *>(userPtr));
      }));
  anari::setParameter(
      d, f, "frameCompletionCallbackUserData", static_cast<void *>(&fired));
  anari::commitParameters(d, f);

  anari::render(d, f);
  anari::wait(d, f);
  // A conformant device fires the callback during the wait above (this assumes
  // synchronous completion).
  const bool ok = fired > 0;

  return {ok,
      ok ? "frame completion callback fired"
         : "frame completion callback did not fire"};
}

// The KHR_FRAME_ACCUMULATION check (FrameAccumulation.h) over the runner's
// world, camera and renderer: one frame with accumulation enabled. The world
// change replaces the world's surfaces with one triangle of a contrasting
// colour; the world is the Case's own, so changing it is safe. The Test's
// scene is a soup of triangles: their edges need sampling (antialiasing) on
// any Monte Carlo renderer without needing an extension beyond the support
// set. A renderer that converges in one render passes as converged.
class DeviceAccumulationTarget : public AccumulationTarget
{
 public:
  DeviceAccumulationTarget(anari::Device d,
      anari::World world,
      anari::Camera camera,
      anari::Renderer renderer,
      uint32_t w,
      uint32_t h)
      : m_d(d),
        m_world(world),
        m_camera(camera),
        m_renderer(renderer),
        m_w(w),
        m_h(h),
        m_frame(newFrame())
  {}

  void render() override
  {
    renderOnce(m_frame.get());
  }

  FrameReadbackResult color() override
  {
    return readColor(m_frame.get());
  }

  std::optional<float> refinementProgress() override
  {
    float p = 0.f;
    if (!anari::getProperty(
            m_d, m_frame.get(), "refinementProgress", p, ANARI_WAIT))
      return std::nullopt;
    return p;
  }

  void changeWorld() override
  {
    TriangleSpec spec;
    spec.primitiveCount = 1;
    auto geom = buildTriangleGeometry(m_d, spec);
    auto mat = makeMatteMaterial(m_d, float3(0.1f, 0.3f, 0.9f));
    auto surface = makeSurface(m_d, geom, mat);
    anari::setAndReleaseParameter(
        m_d, m_world, "surface", anari::newArray1D(m_d, &surface, 1));
    anari::commitParameters(m_d, m_world);
    anari::release(m_d, geom);
    anari::release(m_d, mat);
    anari::release(m_d, surface);
  }

  FrameReadbackResult renderFresh() override
  {
    auto frame = newFrame();
    renderOnce(frame.get());
    return readColor(frame.get());
  }

 private:
  void renderOnce(anari::Frame f)
  {
    anari::render(m_d, f);
    anari::wait(m_d, f);
  }

  FrameReadbackResult readColor(anari::Frame f)
  {
    return readFrameChannel(m_d, f, Channel::Color, m_w, m_h);
  }

  UniqueAnariObject<anari::Frame> newFrame()
  {
    auto frame = behaviorFrame(m_d, m_world, m_camera, m_renderer, m_w, m_h);
    anari::setParameter(m_d, frame.get(), "accumulation", true);
    anari::commitParameters(m_d, frame.get());
    return frame;
  }

  anari::Device m_d;
  anari::World m_world;
  anari::Camera m_camera;
  anari::Renderer m_renderer;
  uint32_t m_w;
  uint32_t m_h;
  UniqueAnariObject<anari::Frame> m_frame;
};

BehaviorResult checkProgressiveRendering(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  DeviceAccumulationTarget target(d, world, camera, renderer, w, h);
  return checkFrameAccumulation(target);
}

} // namespace

void registerFrameTests(Catalog &catalog)
{
  const V colorTypes = {
      Any("UFIXED8_VEC4"), Any("UFIXED8_RGBA_SRGB"), Any("FLOAT32_VEC4")};

  // ---- color channel output types -------------------------------------------
  makeTest("frame", "frame_color_channel")
      .description("Checks color channel output across supported buffer types.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.55f, 0.82f, 0.78f), 16);
      })
      .permute("frame_color_type", colorTypes)
      .channels({Channel::Color})
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- depth channel --------------------------------------------------------
  makeTest("frame", "frame_depth_channel")
      .description("Checks depth channel values for rendered geometry.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 16);
      })
      .channels({Channel::Depth})
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- albedo channel output types (skips where unsupported) ----------------
  // The runner renders albedo at the axis's buffer format (UFIXED8_VEC3 /
  // UFIXED8_RGB_SRGB / FLOAT32_VEC3) and reads it back accordingly (D2). helide
  // has no albedo channel, so these still skip there.
  makeTest("frame", "frame_albedo_channel")
      .description(
          "Checks albedo channel output across supported buffer types.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.55f, 0.82f, 0.78f), 16);
      })
      .permute("frame_albedo_type",
          {"UFIXED8_VEC3", "UFIXED8_RGB_SRGB", "FLOAT32_VEC3"})
      .channels({Channel::Albedo})
      .covers("ANARI_KHR_FRAME_CHANNEL_ALBEDO")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- normal channel output types ------------------------------------------
  makeTest("frame", "frame_normal_channel")
      .description(
          "Checks normal channel output across supported buffer types.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .permute("frame_normal_type", {"FIXED16_VEC3", "FLOAT32_VEC3"})
      .channels({Channel::Normal})
      .covers("ANARI_KHR_FRAME_CHANNEL_NORMAL")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- primitive id channel -------------------------------------------------
  makeTest("frame", "frame_primitiveID_channel")
      .description(
          "Checks that the primitive ID channel holds each triangle's index "
          "at its projected centroid, and no triangle's index outside every "
          "triangle.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        TriangleSpec spec;
        spec.primitiveCount = kPrimitiveIdTriangles;
        auto geom = buildTriangleGeometry(d, spec);
        anari::commitParameters(d, geom);
        auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
        auto surface = makeSurface(d, geom, mat);
        WorldContents wc;
        wc.surfaces = {surface};
        auto world = assembleWorld(d, wc);
        anari::release(d, geom);
        anari::release(d, mat);
        anari::release(d, surface);
        return world;
      })
      .channels({Channel::PrimitiveId})
      .expectExact("primitive IDs are triangle indices", primitiveIdsAreIndices)
      .noGroundTruth()
      .covers("ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- object id channel (per-surface ids) ----------------------------------
  makeTest("frame", "frame_objectID_channel_surface")
      .description("Checks surface object IDs returned by the frame channel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto s0 = idSurface(d, float3(-0.3f, 0.f, 0.f), 1, true);
        auto s1 = idSurface(d, float3(0.3f, 0.f, 0.f), 0, true);
        WorldContents wc;
        wc.surfaces = {s0, s1};
        auto world = assembleWorld(d, wc);
        anari::release(d, s0);
        anari::release(d, s1);
        return world;
      })
      .channels({Channel::ObjectId})
      .covers("ANARI_KHR_FRAME_CHANNEL_OBJECT_ID")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- object id channel (fallback to group/instance index) -----------------
  makeTest("frame", "frame_objectID_channel_group")
      .description(
          "Checks fallback object IDs for instanced groups and surfaces.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto i0 = idInstance(d, float3(-0.3f, 0.f, 0.f), 0, false);
        auto i1 = idInstance(d, float3(0.3f, 0.f, 0.f), 0, false);
        WorldContents wc;
        wc.instances = {i0, i1};
        auto world = assembleWorld(d, wc);
        anari::release(d, i0);
        anari::release(d, i1);
        return world;
      })
      .channels({Channel::ObjectId})
      .covers("ANARI_KHR_FRAME_CHANNEL_OBJECT_ID")
      .requireFeatures(kSupportMatteTriangle)
      .requireFeature("ANARI_KHR_INSTANCE_TRANSFORM")
      .registerInto(catalog);

  // ---- object id channel (per-volume ids) -----------------------------------
  makeTest("frame", "frame_objectID_channel_volume")
      .description("Checks volume object IDs returned by the frame channel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        // Two volumes side by side (origin shifted along X) so the two ids
        // occupy distinct, non-overlapping regions of the id channel.
        auto makeVol = [&](float originX, uint32_t id) {
          auto field = newStructuredRegularField(d, {3, 3, 3});
          anari::setParameter(d, field, "origin", float3(originX, -1.f, -1.f));
          anari::commitParameters(d, field);
          auto vol = makeVolume(d, field);
          anari::setParameter(d, vol, "id", id);
          anari::commitParameters(d, vol);
          anari::release(d, field);
          return vol;
        };
        auto v0 = makeVol(-2.5f, 1);
        auto v1 = makeVol(0.5f, 0);
        WorldContents wc;
        wc.volumes = {v0, v1};
        auto world = assembleWorld(d, wc);
        anari::release(d, v0);
        anari::release(d, v1);
        return world;
      })
      .channels({Channel::ObjectId})
      .covers("ANARI_KHR_FRAME_CHANNEL_OBJECT_ID")
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
          kSupportPerspective})
      .registerInto(catalog);

  // ---- instance id channel --------------------------------------------------
  makeTest("frame", "frame_instanceID_channel")
      .description("Checks instance IDs returned by the frame channel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto i0 = idInstance(d, float3(-0.3f, 0.f, 0.f), 1, true);
        auto i1 = idInstance(d, float3(0.3f, 0.f, 0.f), 0, true);
        WorldContents wc;
        wc.instances = {i0, i1};
        auto world = assembleWorld(d, wc);
        anari::release(d, i0);
        anari::release(d, i1);
        return world;
      })
      .channels({Channel::InstanceId})
      .covers("ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID")
      .requireFeatures(kSupportMatteTriangle)
      .requireFeature("ANARI_KHR_INSTANCE_TRANSFORM")
      .registerInto(catalog);

  // ---- behavioral checks (verified by the runner's behavior hook) -----------
  makeTest("frame", "frame_completion_callback")
      .description(
          "Checks that rendering invokes the frame completion callback.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .behavior(checkFrameCompletionCallback)
      .covers("ANARI_KHR_FRAME_COMPLETION_CALLBACK")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("frame", "progressive_rendering")
      .description(
          "Checks that frame accumulation converges across renders, restarts "
          "after a world change, and reports a sane refinementProgress.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 8);
      })
      .behavior(checkProgressiveRendering)
      .covers("ANARI_KHR_FRAME_ACCUMULATION")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
