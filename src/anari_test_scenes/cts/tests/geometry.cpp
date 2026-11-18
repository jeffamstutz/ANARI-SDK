// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../AnariObject.h"
#include "../BuildContext.h"
#include "../FrameReadback.h"
#include "../GeometryBuilder.h"
#include "../LightBuilder.h"
#include "../Metrics.h"
#include "../ScreenQuad.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../VolumeBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "ScreenScene.h"
#include "generators/ColorPalette.h"
// std
#include <array>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;

template <typename Spec>
void applyCommonAxes(BuildContext &ctx, Spec &spec)
{
  if (ctx.has("primitiveMode"))
    spec.mode = parsePrimitiveMode(ctx.getString("primitiveMode", ""));
  spec.primitiveCount = ctx.get<int>("primitiveCount", spec.primitiveCount);
  if (ctx.has("color"))
    spec.attributes.color = parseColorAttribute(ctx.getString("color", ""));
}

template <typename Spec>
Spec withAxes(BuildContext &ctx, Spec spec)
{
  applyCommonAxes(ctx, spec);
  return spec;
}

ConeSpec withAxes(BuildContext &ctx, ConeSpec spec)
{
  applyCommonAxes(ctx, spec);
  if (ctx.has("globalCaps"))
    spec.caps = parseCapsMode(ctx.getString("globalCaps", ""));
  if (ctx.has("vertexCaps"))
    spec.vertexCaps = ctx.get<bool>("vertexCaps", true);
  return spec;
}

CylinderSpec withAxes(BuildContext &ctx, CylinderSpec spec)
{
  applyCommonAxes(ctx, spec);
  if (ctx.has("globalCaps"))
    spec.caps = parseCapsMode(ctx.getString("globalCaps", ""));
  if (ctx.has("vertexCaps"))
    spec.vertexCaps = ctx.get<bool>("vertexCaps", true);
  return spec;
}

anari::Geometry publishGeometry(anari::Device d, const TriangleSpec &spec)
{
  return buildTriangleGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const QuadSpec &spec)
{
  return buildQuadGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const SphereSpec &spec)
{
  return buildSphereGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const CurveSpec &spec)
{
  return buildCurveGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const ConeSpec &spec)
{
  return buildConeGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const CylinderSpec &spec)
{
  return buildCylinderGeometry(d, spec);
}

template <typename Spec>
anari::World surfaceWorld(BuildContext &ctx, const Spec &spec)
{
  auto d = ctx.device();
  auto geom = publishGeometry(d, spec);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  if (spec.attributes.color != ColorAttribute::None)
    anari::setParameter(d, mat, "color", "color");
  else
    anari::setParameter(d, mat, "color", float3(0.7f, 0.5f, 0.3f));
  anari::commitParameters(d, mat);

  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));

  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);

  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

TriangleSpec triangle(TriangleShape shape, int count)
{
  TriangleSpec spec;
  spec.shape = shape;
  spec.primitiveCount = count;
  return spec;
}

QuadSpec quad(QuadShape shape, int count)
{
  QuadSpec spec;
  spec.shape = shape;
  spec.primitiveCount = count;
  return spec;
}

SphereSpec sphere(int count)
{
  SphereSpec spec;
  spec.primitiveCount = count;
  return spec;
}

ConeSpec cone(int count)
{
  ConeSpec spec;
  spec.primitiveCount = count;
  return spec;
}

CylinderSpec cylinder(int count)
{
  CylinderSpec spec;
  spec.primitiveCount = count;
  return spec;
}

CurveSpec curve(int count)
{
  CurveSpec spec;
  spec.primitiveCount = count;
  return spec;
}

// A single-primitive world with explicit normals/tangents (the *_normal_tangent
// tests). normalMode in {set, unset}; tangentMode in {unset, vec3, vec4}. Matte
// ignores them, so all combinations must render identically (Variant axes).
anari::World normalTangentWorld(BuildContext &ctx, bool quad)
{
  auto d = ctx.device();
  std::vector<float3> pos;
  if (quad)
    pos = {{0.1f, 0.4f, 0.6f},
        {0.6f, 0.4f, 0.6f},
        {0.6f, 0.7f, 0.7f},
        {0.1f, 0.7f, 0.7f}};
  else
    pos = {{0.1f, 0.4f, 0.6f}, {0.6f, 0.4f, 0.6f}, {0.35f, 0.7f, 0.8f}};

  auto geom = anari::newObject<anari::Geometry>(d, quad ? "quad" : "triangle");
  anari::setAndReleaseParameter(
      d, geom, "vertex.position", anari::newArray1D(d, pos.data(), pos.size()));
  auto cols = scenes::colors::getColorVectorFromPalette(pos.size());
  anari::setAndReleaseParameter(
      d, geom, "vertex.color", anari::newArray1D(d, cols.data(), cols.size()));

  if (ctx.getString("normal", "set") == "set") {
    std::vector<float3> n(pos.size(), float3(0.f, 0.f, 1.f));
    n[0] = float3(1.f, 0.f, 0.f);
    n[1] = float3(0.f, 1.f, 0.f);
    anari::setAndReleaseParameter(
        d, geom, "vertex.normal", anari::newArray1D(d, n.data(), n.size()));
  }
  const std::string tangent = ctx.getString("tangent", "unset");
  if (tangent == "vec3") {
    std::vector<float3> t(pos.size(), float3(0.f, 1.f, 0.f));
    t[1] = float3(1.f, 0.f, 0.f);
    anari::setAndReleaseParameter(
        d, geom, "vertex.tangent", anari::newArray1D(d, t.data(), t.size()));
  } else if (tangent == "vec4") {
    std::vector<float4> t(pos.size(), float4(0.f, 1.f, 0.f, -1.f));
    t[1] = float4(1.f, 0.f, 0.f, -1.f);
    anari::setAndReleaseParameter(
        d, geom, "vertex.tangent", anari::newArray1D(d, t.data(), t.size()));
  }
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

anari::World isosurfaceWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  auto field = makeStructuredRegularField(d, {16, 16, 16});
  auto geom = anari::newObject<anari::Geometry>(d, "isosurface");
  anari::setParameter(d, geom, "field", field);
  std::vector<float> iso = {0.25f, 0.5f, 0.75f};
  anari::setAndReleaseParameter(
      d, geom, "isovalue", anari::newArray1D(d, iso.data(), iso.size()));
  anari::commitParameters(d, geom);

  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);
  anari::release(d, field);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

// ---- intersection edge cases ----------------------------------------------
// Single-primitive worlds paired with a bespoke camera, built to stress a
// device's ray/primitive intersector rather than its attribute plumbing:
// origin-inside hits (the far root), near-axis-parallel and grazing rays
// (the degenerate a~=0 / discriminant~=0 lateral-surface roots), the cone
// apex singularity, and every spec value of the tube-family `caps`
// parameter viewed where caps actually resolve differently. The tests
// encode the behavior the spec describes, independent of what any current
// device renders.
//
// Exterior views score color + depth: a wrong root can shade plausibly but
// lands at a very different depth. The origin-inside tests score depth only —
// it is the device-independent intersection oracle. Their interior (back-
// facing) hit is deliberately not lit: an interior point light does not
// reliably illuminate a back face (whether the shading normal flips there is
// device-dependent, and ambient/AO is legitimately black inside a closed
// object), so a color image would be near-black on some devices and mislead a
// color comparison. Depth is populated regardless of shading.

anari::World litSurfaceWorld(
    BuildContext &ctx, anari::Geometry geom, float3 lightDir)
{
  auto d = ctx.device();
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, lightDir);
  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

anari::Geometry spheres(anari::Device d,
    const std::vector<float3> &centers,
    const std::vector<float> &radii)
{
  auto g = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setAndReleaseParameter(d,
      g,
      "vertex.position",
      anari::newArray1D(d, centers.data(), centers.size()));
  anari::setAndReleaseParameter(
      d, g, "vertex.radius", anari::newArray1D(d, radii.data(), radii.size()));
  anari::commitParameters(d, g);
  return g;
}

anari::Geometry axisCylinder(
    anari::Device d, float3 p0, float3 p1, float radius, const char *caps)
{
  auto g = anari::newObject<anari::Geometry>(d, "cylinder");
  const float3 positions[2] = {p0, p1};
  anari::setAndReleaseParameter(
      d, g, "vertex.position", anari::newArray1D(d, positions, 2));
  anari::setParameter(d, g, "radius", radius);
  anari::setParameter(d, g, "caps", caps);
  anari::commitParameters(d, g);
  return g;
}

anari::Geometry axisCone(anari::Device d,
    float3 base,
    float3 apex,
    float baseRadius,
    const char *caps)
{
  auto g = anari::newObject<anari::Geometry>(d, "cone");
  const float3 positions[2] = {base, apex};
  anari::setAndReleaseParameter(
      d, g, "vertex.position", anari::newArray1D(d, positions, 2));
  const float radii[2] = {baseRadius, 0.f};
  anari::setAndReleaseParameter(
      d, g, "vertex.radius", anari::newArray1D(d, radii, 2));
  anari::setParameter(d, g, "caps", caps);
  anari::commitParameters(d, g);
  return g;
}

anari::Camera lookAtCamera(
    anari::Device d, float3 eye, float3 direction, float fovy, float near)
{
  auto c = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, c, "position", eye);
  anari::setParameter(d, c, "direction", direction);
  anari::setParameter(d, c, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, c, "fovy", fovy);
  anari::setParameter(d, c, "near", near);
  anari::setParameter(d, c, "far", 100.f);
  anari::commitParameters(d, c);
  return c;
}

const char *kPerspective = "ANARI_KHR_CAMERA_PERSPECTIVE";
const float3 kEdgeLight = float3(-1.f, -1.f, -1.f);

const std::vector<Channel> kColorDepth = {Channel::Color, Channel::Depth};
const std::vector<Channel> kColorOnly = {Channel::Color};
const std::vector<Channel> kDepthOnly = {Channel::Depth};

const std::vector<Any> kFrameColorTypes = {
    Any("UFIXED8_VEC4"), Any("UFIXED8_RGBA_SRGB"), Any("FLOAT32_VEC4")};

// Register the family of tests shared by every "shaped" geometry subtype
// (triangle/quad and the sphere-like subtypes differ only in available extras).
template <typename Spec>
void registerShape(Catalog &catalog,
    const char *prefix,
    Spec spec,
    const char *description,
    const char *feature,
    bool soupVariantOnly)
{
  // Basic soup-vs-indexed equivalence: a Variant sharing one ground truth.
  auto basic = makeTest("geometry", prefix);
  basic.description(description);
  basic.build([=](BuildContext &ctx) {
    return surfaceWorld(ctx, withAxes(ctx, spec));
  });
  if (soupVariantOnly)
    basic.variant("primitiveMode", {"soup", "indexed"});
  else
    basic.permute("primitiveMode", {"soup", "indexed"});
  basic.channels(kColorDepth)
      .covers(feature)
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);
}

// ---- KHR_ARRAY1D_REGION
// ------------------------------------------------------
//
// An array's `region` (lower inclusive, upper exclusive, in elements) makes
// the array behave as the slice of elements it names; mapping still gives the
// whole capacity. The Tests lay triangles out in screen coordinates, seen by
// the screen camera, so a device that ignores the region draws triangles the
// region leaves out, in places a device that honours it leaves empty.

using Region = std::array<uint64_t, 2>;

constexpr float kRegionTriangleHalf = 0.11f;

Triangle regionTriangle(float sx, float sy)
{
  return screenTriangle(sx, sy, kRegionTriangleHalf);
}

// The triangles a region Case draws (a top row) and the ones the region must
// leave out, before and after them (a bottom row).
struct RegionLayout
{
  std::vector<Triangle> before;
  std::vector<Triangle> subject;
  std::vector<Triangle> after;
};

RegionLayout regionLayout()
{
  RegionLayout l;
  l.before = {regionTriangle(0.2f, 0.25f), regionTriangle(0.5f, 0.25f)};
  l.subject = {regionTriangle(0.2f, 0.75f),
      regionTriangle(0.5f, 0.75f),
      regionTriangle(0.8f, 0.75f)};
  l.after = {regionTriangle(0.8f, 0.25f)};
  return l;
}

std::vector<Triangle> join(std::initializer_list<std::vector<Triangle>> parts)
{
  std::vector<Triangle> out;
  for (const auto &p : parts)
    out.insert(out.end(), p.begin(), p.end());
  return out;
}

// A committed array of `values`, with `region` set when given. The caller
// owns the returned reference.
template <typename T>
anari::Array1D regionArray(anari::Device d,
    const std::vector<T> &values,
    const std::optional<Region> &region)
{
  auto array = anari::newArray1D(d, values.data(), values.size());
  if (region)
    anariSetParameter(d, array, "region", ANARI_UINT64_REGION1, region->data());
  anari::commitParameters(d, array);
  return array;
}

// A triangle geometry built from `triangles` in the form a Case names. Every
// form must draw exactly the subject triangles:
//   shortArray             vertex.position holds only the subject
//   vertexRegionFromStart  the subject first, then triangles to leave out;
//                          the region starts at 0
//   vertexRegionAfterStart triangles to leave out on both sides of the
//                          subject; the region starts past 0
//   indexRegionAfterStart  every vertex, and a primitive.index array with
//                          triangles to leave out on both sides, whose region
//                          starts past 0
anari::Geometry regionTriangleGeometry(
    anari::Device d, const std::string &form, const RegionLayout &l)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  const uint64_t nBefore = l.before.size(), nSubject = l.subject.size(),
                 nAfter = l.after.size();
  if (form == "shortArray") {
    const auto verts = triangleSoup(l.subject);
    anari::setAndReleaseParameter(
        d, geom, "vertex.position", regionArray(d, verts, std::nullopt));
  } else if (form == "vertexRegionFromStart") {
    const auto verts = triangleSoup(join({l.subject, l.before, l.after}));
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.position",
        regionArray(d, verts, Region{0, 3 * nSubject}));
  } else if (form == "vertexRegionAfterStart") {
    const auto verts = triangleSoup(join({l.before, l.subject, l.after}));
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.position",
        regionArray(d, verts, Region{3 * nBefore, 3 * (nBefore + nSubject)}));
  } else if (form == "indexRegionAfterStart") {
    const auto verts = triangleSoup(join({l.before, l.subject, l.after}));
    std::vector<anari::math::uint3> indices;
    for (uint32_t t = 0; t < nBefore + nSubject + nAfter; ++t)
      indices.push_back(anari::math::uint3(3 * t, 3 * t + 1, 3 * t + 2));
    anari::setAndReleaseParameter(
        d, geom, "vertex.position", regionArray(d, verts, std::nullopt));
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.index",
        regionArray(d, indices, Region{nBefore, nBefore + nSubject}));
  } else {
    anari::release(d, geom);
    throw std::invalid_argument("unknown region form " + form);
  }
  anari::commitParameters(d, geom);
  return geom;
}

// The world of one geometry, lit head-on, for the screen camera.
anari::World regionWorld(anari::Device d, anari::Geometry geom)
{
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto world = makeScreenQuadWorld(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

// What the region-changes Test draws: two triangles on the left (the array's
// first half) and two on the right (its second half).
std::vector<Triangle> halfTriangles(float sx)
{
  return {regionTriangle(sx, 0.7f), regionTriangle(sx, 0.3f)};
}

// The region of the region-changes Test's value: nothing (unset, the whole
// array), the left half or the right half.
std::optional<Region> halfRegion(const std::string &value)
{
  if (value == "first")
    return Region{0, 6};
  if (value == "second")
    return Region{6, 12};
  return std::nullopt;
}

// A device-side rig for the recommit check: a world of one geometry whose
// vertex array the check re-regions after rendering.
class RegionRig
{
 public:
  RegionRig(anari::Device d,
      anari::Camera camera,
      anari::Renderer renderer,
      uint32_t w,
      uint32_t h)
      : m_d(d), m_w(w), m_h(h)
  {
    const auto verts =
        triangleSoup(join({halfTriangles(0.25f), halfTriangles(0.75f)}));
    m_array = regionArray(d, verts, halfRegion("first"));
    m_geometry = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, m_geometry, "vertex.position", m_array);
    anari::commitParameters(d, m_geometry);
    auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
    auto surface = makeSurface(d, m_geometry, mat);
    m_world = makeScreenQuadWorld(d, surface);
    anari::release(d, mat);
    anari::release(d, surface);

    m_frame =
        UniqueAnariObject<anari::Frame>(d, anari::newObject<anari::Frame>(d));
    const auto f = m_frame.get();
    anari::setParameter(d, f, "size", anari::math::vec<uint32_t, 2>(w, h));
    anari::setParameter(d, f, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
    anari::setParameter(d, f, "renderer", renderer);
    anari::setParameter(d, f, "camera", camera);
    anari::setParameter(d, f, "world", m_world);
    anari::commitParameters(d, f);
  }

  ~RegionRig()
  {
    m_frame = {};
    anari::release(m_d, m_world);
    anari::release(m_d, m_geometry);
    anari::release(m_d, m_array);
  }

  // Change the region and commit the chain that leads to the world.
  void setRegion(const std::string &value)
  {
    const auto region = halfRegion(value);
    if (region)
      anariSetParameter(
          m_d, m_array, "region", ANARI_UINT64_REGION1, region->data());
    else
      anariUnsetParameter(m_d, m_array, "region");
    anari::commitParameters(m_d, m_array);
    anari::commitParameters(m_d, m_geometry);
    anari::commitParameters(m_d, m_world);
  }

  FrameReadbackResult render()
  {
    anari::render(m_d, m_frame.get());
    anari::wait(m_d, m_frame.get());
    return readFrameChannel(m_d, m_frame.get(), Channel::Color, m_w, m_h);
  }

 private:
  anari::Device m_d;
  uint32_t m_w, m_h;
  anari::Array1D m_array{nullptr};
  anari::Geometry m_geometry{nullptr};
  anari::World m_world{nullptr};
  UniqueAnariObject<anari::Frame> m_frame;
};

// Rendering, then changing an array's region and committing it, must show the
// new region: the image changes, and equals a render of the same region set
// before the first render.
BehaviorResult checkRegionRecommit(anari::Device d,
    anari::World,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  constexpr int kTau = 8;
  constexpr double kEffect = 0.01, kSame = 0.002;

  RegionRig rig(d, camera, renderer, w, h);
  const auto first = rig.render();
  if (!first)
    return {false, "rendering the first region failed: " + first.detail};

  rig.setRegion("second");
  const auto changed = rig.render();
  if (!changed)
    return {false, "rendering the changed region failed: " + changed.detail};
  const double moved = changedFraction(first.image, changed.image, kTau);

  // The same region, set before any render.
  RegionRig fresh(d, camera, renderer, w, h);
  fresh.setRegion("second");
  const auto expected = fresh.render();
  if (!expected)
    return {false, "rendering the reference failed: " + expected.detail};
  const double off = changedFraction(changed.image, expected.image, kTau);

  auto percent = [](double f) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f %%", 100.0 * f);
    return std::string(text);
  };
  if (!(moved >= kEffect))
    return {false,
        "changing the region and committing it moved only " + percent(moved)
            + " of the pixels (at least " + percent(kEffect) + " expected)"};
  if (!(off <= kSame))
    return {false,
        "after the region changed, the image differs from one rendered with "
        "that region in "
            + percent(off) + " of the pixels (at most " + percent(kSame)
            + " expected)"};
  return {true,
      "the region change moved " + percent(moved) + " of the pixels and "
          "matches a fresh render ("
          + percent(off) + " differ)"};
}
} // namespace

void registerGeometryTests(Catalog &catalog)
{
  using V = std::vector<Any>;

  // ---- triangle -------------------------------------------------------------
  registerShape(catalog,
      "triangle",
      triangle(TriangleShape::Triangle, 16),
      "Checks basic triangle geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      /*soupVariantOnly=*/true);
  registerShape(catalog,
      "triangle_quad",
      triangle(TriangleShape::Quad, 16),
      "Checks triangles arranged as a quad in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      true);
  registerShape(catalog,
      "triangle_cube",
      triangle(TriangleShape::Cube, 16),
      "Checks triangles arranged as a cube in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      true);

  makeTest("geometry", "triangle_colors")
      .description("Checks primitive and vertex colors on triangle geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(
            ctx, withAxes(ctx, triangle(TriangleShape::Triangle, 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_TRIANGLE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "triangle_normal_tangent")
      .description(
          "Checks optional triangle normal and tangent attribute layouts.")
      .build([](BuildContext &ctx) { return normalTangentWorld(ctx, false); })
      .variant("normal", {"set", "unset"})
      .variant("tangent", {"unset", "vec3", "vec4"})
      .simplified()
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_TRIANGLE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "triangle_random_attributes")
      .description(
          "Checks triangle vertex and primitive attributes with varied values.")
      .build([](BuildContext &ctx) {
        auto spec = triangle(TriangleShape::Triangle, 16);
        spec.attributes.vertexRamps = true;
        spec.attributes.primitiveRamps = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_TRIANGLE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "triangle_unused_vertices")
      .description(
          "Checks indexed triangles that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = triangle(TriangleShape::Triangle, 4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_TRIANGLE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // ---- quad -----------------------------------------------------------------
  registerShape(catalog,
      "quad",
      quad(QuadShape::Quad, 16),
      "Checks basic quad geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_QUAD",
      true);
  registerShape(catalog,
      "quad_cube",
      quad(QuadShape::Cube, 16),
      "Checks quads arranged as a cube in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_QUAD",
      true);

  makeTest("geometry", "quad_colors")
      .description("Checks primitive and vertex colors on quad geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, quad(QuadShape::Quad, 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_QUAD")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "quad_frame_color_types")
      .description("Checks quad rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, quad(QuadShape::Quad, 16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_QUAD")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "quad_normal_tangent")
      .description("Checks optional quad normal and tangent attribute layouts.")
      .build([](BuildContext &ctx) { return normalTangentWorld(ctx, true); })
      .variant("normal", {"set", "unset"})
      .variant("tangent", {"unset", "vec3", "vec4"})
      .simplified()
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_QUAD")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "quad_unused_vertices")
      .description("Checks indexed quads that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = quad(QuadShape::Quad, 4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_QUAD")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // ---- sphere ---------------------------------------------------------------
  makeTest("geometry", "sphere")
      .description(
          "Checks sphere counts and equivalent soup and indexed layouts.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, sphere(16)));
      })
      .permute("primitiveCount", {1, 16})
      .variant("primitiveMode", {"soup", "indexed"})
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_SPHERE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "sphere_colors")
      .description("Checks primitive and vertex colors on sphere geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, sphere(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .permute("primitiveMode", {"soup", "indexed"})
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_SPHERE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "sphere_frame_color_types")
      .description(
          "Checks sphere rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, sphere(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_SPHERE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "sphere_global_radii")
      .description("Checks the global radius parameter on sphere geometry.")
      .build([](BuildContext &ctx) {
        auto spec = sphere(16);
        spec.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_SPHERE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "sphere_unused_vertices")
      .description("Checks indexed spheres that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = sphere(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_SPHERE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // ---- cone -----------------------------------------------------------------
  registerShape(catalog,
      "cone",
      cone(16),
      "Checks basic cone geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_CONE",
      true);

  makeTest("geometry", "cone_colors")
      .description("Checks primitive and vertex colors on cone geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_CONE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cone_frame_color_types")
      .description("Checks cone rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_CONE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cone_global_caps")
      .description("Checks global end-cap modes on cone geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(16)));
      })
      .permute("globalCaps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CONE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cone_vertex_caps")
      .description("Checks per-vertex end-cap control on cone geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(16)));
      })
      .permute("vertexCaps", V{Any(true), Any(false)})
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CONE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cone_unused_vertices")
      .description("Checks indexed cones that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = cone(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CONE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // ---- cylinder -------------------------------------------------------------
  registerShape(catalog,
      "cylinder",
      cylinder(16),
      "Checks basic cylinder geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_CYLINDER",
      true);

  makeTest("geometry", "cylinder_colors")
      .description("Checks primitive and vertex colors on cylinder geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cylinder_frame_color_types")
      .description(
          "Checks cylinder rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cylinder_global_caps")
      .description("Checks global end-cap modes on cylinder geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(16)));
      })
      .permute("globalCaps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cylinder_global_radii")
      .description("Checks the global radius parameter on cylinder geometry.")
      .build([](BuildContext &ctx) {
        auto spec = cylinder(16);
        spec.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cylinder_vertex_caps")
      .description("Checks per-vertex end-cap control on cylinder geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(16)));
      })
      .permute("vertexCaps", V{Any(true), Any(false)})
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "cylinder_unused_vertices")
      .description(
          "Checks indexed cylinders that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = cylinder(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // ---- intersection edge cases ----------------------------------------------
  // Camera inside a large sphere, slightly off-center so the generic interior
  // root (b != 0) is exercised and the frame is not rotationally symmetric:
  // every primary ray must return the far (exit) hit against the interior
  // wall. The small foreground sphere the camera sits outside of serves the
  // depth oracle twice: central rays now carry two candidate hits — its near
  // exterior root must win over the enclosing sphere's far interior root
  // (nearest-hit ordering with an origin-inside primitive) — and its step
  // discontinuity keeps the depth comparison sensitive where a lone interior
  // wall would be a near-uniform gradient.
  makeTest("geometry", "sphere_interior")
      .description(
          "Checks sphere intersection when the ray origin is inside the "
          "sphere (far-root hit against the interior wall).")
      .build([](BuildContext &ctx) {
        return litSurfaceWorld(ctx,
            spheres(ctx.device(),
                {float3(0.f), float3(0.f, 0.f, -0.6f)},
                {1.f, 0.3f}),
            kEdgeLight);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return lookAtCamera(ctx.device(),
            float3(0.15f, 0.1f, 0.1f),
            float3(0.f, 0.f, -1.f),
            1.2f,
            0.001f);
      })
      .channels(kDepthOnly)
      .covers("ANARI_KHR_GEOMETRY_SPHERE")
      .requireFeatures(kSupportMatte)
      .requireFeature(kPerspective)
      .registerInto(catalog);

  // Eye offset just outside the cylinder radius, view direction parallel to
  // the axis: the frame contains exactly-parallel misses, a band of
  // near-parallel rays grazing the outer wall (the a ~= 0 lateral-surface
  // root), and the near end face-on. Every spec caps value resolves
  // differently down the bore: "second"/"both" close the near end (p1, the
  // end facing the eye), "first" leaves it open onto the far cap (p0) seen
  // down the bore past the interior wall, and "none" leaves the bore open to
  // the background.
  makeTest("geometry", "cylinder_axis_view")
      .description(
          "Checks cylinder intersection for rays nearly parallel to the "
          "cylinder axis (degenerate lateral-surface root) across all spec "
          "cap modes, viewed down the bore.")
      .build([](BuildContext &ctx) {
        return litSurfaceWorld(ctx,
            axisCylinder(ctx.device(),
                float3(0.f, 0.f, -0.5f),
                float3(0.f, 0.f, 0.5f),
                0.3f,
                ctx.getString("caps", "none").c_str()),
            kEdgeLight);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return lookAtCamera(ctx.device(),
            float3(0.45f, 0.1f, 2.f),
            float3(0.f, 0.f, -1.f),
            0.6f,
            0.01f);
      })
      .permute("caps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .requireFeature(kPerspective)
      .registerInto(catalog);

  // Camera inside the tube on its axis, looking at the p0 end: rays originate
  // inside and must hit the interior lateral surface (far root). The caps
  // permutation resolves the viewed end per spec: "first"/"both" close it (a
  // cap seen from its back side), "none"/"second" leave the bore open to the
  // background.
  makeTest("geometry", "cylinder_interior")
      .description(
          "Checks cylinder intersection when the ray origin is inside the "
          "tube (far-root interior-wall hits) across all spec cap modes.")
      .build([](BuildContext &ctx) {
        return litSurfaceWorld(ctx,
            axisCylinder(ctx.device(),
                float3(0.f, 0.f, -0.6f),
                float3(0.f, 0.f, 0.6f),
                0.3f,
                ctx.getString("caps", "none").c_str()),
            kEdgeLight);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return lookAtCamera(
            ctx.device(), float3(0.f), float3(0.f, 0.f, -1.f), 1.2f, 0.001f);
      })
      .permute("caps", {"none", "first", "second", "both"})
      .channels(kDepthOnly)
      .covers("ANARI_KHR_GEOMETRY_CYLINDER")
      .requireFeatures(kSupportMatte)
      .requireFeature(kPerspective)
      .registerInto(catalog);

  // View direction parallel to the cone axis from the base side, eye slightly
  // off-axis: the base-cap rim is the whole silhouette (the cone only narrows
  // behind it), so every spec caps value resolves visibly: "first"/"both"
  // close the base (p0, the end facing the eye) with a face-on disc — the
  // *_global_caps tests only ever see caps edge-on — while "none"/"second"
  // open it onto the interior of the flank converging toward the apex.
  makeTest("geometry", "cone_axis_view")
      .description(
          "Checks cone intersection with the view direction parallel to the "
          "cone axis across all spec cap modes, viewed from the base.")
      .build([](BuildContext &ctx) {
        return litSurfaceWorld(ctx,
            axisCone(ctx.device(),
                float3(0.f, 0.f, -0.5f),
                float3(0.f, 0.f, 0.5f),
                0.4f,
                ctx.getString("caps", "none").c_str()),
            kEdgeLight);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return lookAtCamera(ctx.device(),
            float3(0.1f, 0.05f, -2.f),
            float3(0.f, 0.f, 1.f),
            0.6f,
            0.01f);
      })
      .permute("caps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CONE")
      .requireFeatures(kSupportMatte)
      .requireFeature(kPerspective)
      .registerInto(catalog);

  // Camera on the cone axis beyond the apex, looking at the tip: rays near
  // the axis converge on the zero-radius apex (apex singularity), and the
  // whole visible flank is hit by near-axis-parallel rays — the cone's
  // degenerate lateral-root case. caps stays at the spec default: the flank
  // covers the full silhouette from this side, so no cap can be visible.
  makeTest("geometry", "cone_apex_view")
      .description(
          "Checks cone intersection viewed head-on at the apex, where the "
          "primitive radius goes to zero (apex singularity).")
      .build([](BuildContext &ctx) {
        return litSurfaceWorld(ctx,
            axisCone(ctx.device(),
                float3(0.f, 0.f, -0.5f),
                float3(0.f, 0.f, 0.5f),
                0.4f,
                "none"),
            kEdgeLight);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return lookAtCamera(ctx.device(),
            float3(0.f, 0.f, 2.f),
            float3(0.f, 0.f, -1.f),
            0.6f,
            0.01f);
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CONE")
      .requireFeatures(kSupportMatte)
      .requireFeature(kPerspective)
      .registerInto(catalog);

  // Camera aimed at a sphere's limb so its silhouette runs across the frame: a
  // wide band of primary rays is near-tangent (discriminant ~= 0), stressing
  // grazing-ray robustness.
  makeTest("geometry", "sphere_tangent")
      .description(
          "Checks sphere intersection for grazing rays near the silhouette "
          "(near-zero discriminant).")
      .build([](BuildContext &ctx) {
        return litSurfaceWorld(
            ctx, spheres(ctx.device(), {float3(0.f)}, {1.f}), kEdgeLight);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        const float3 eye(0.f, 0.f, 3.5f);
        const float3 at(0.95f, 0.f, 0.f);
        return lookAtCamera(
            ctx.device(), eye, math::normalize(at - eye), 0.5f, 0.01f);
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_SPHERE")
      .requireFeatures(kSupportMatte)
      .requireFeature(kPerspective)
      .registerInto(catalog);

  // ---- curve ----------------------------------------------------------------
  registerShape(catalog,
      "curve",
      curve(16),
      "Checks basic curve geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_CURVE",
      /*soupVariantOnly=*/false);

  makeTest("geometry", "curve_colors")
      .description("Checks primitive and vertex colors on curve geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, curve(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_CURVE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "curve_frame_color_types")
      .description(
          "Checks curve rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, curve(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .covers("ANARI_KHR_GEOMETRY_CURVE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "curve_global_radii")
      .description("Checks the global radius parameter on curve geometry.")
      .build([](BuildContext &ctx) {
        auto spec = curve(16);
        spec.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CURVE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  makeTest("geometry", "curve_unused_vertices")
      .description("Checks indexed curves that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = curve(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_CURVE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // ---- isosurface -----------------------------------------------------------
  makeTest("geometry", "isosurface")
      .description(
          "Checks isosurface extraction from a structured regular field.")
      .build([](BuildContext &ctx) { return isosurfaceWorld(ctx); })
      .channels(kColorDepth)
      .covers("ANARI_KHR_GEOMETRY_ISOSURFACE")
      .requireFeature("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // ---- array region ---------------------------------------------------------
  makeTest("geometry", "array1d_region")
      .description(
          "Checks that an array whose region names a slice of it (starting "
          "at 0 or past it, on vertex positions or primitive indices) draws "
          "what an array holding just that slice draws.")
      .build([](BuildContext &ctx) {
        return regionWorld(ctx.device(),
            regionTriangleGeometry(
                ctx.device(), ctx.getString("array", ""), regionLayout()));
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      // The first value is the reference: no region, just the elements.
      .variant("array",
          {"shortArray",
              "vertexRegionFromStart",
              "vertexRegionAfterStart",
              "indexRegionAfterStart"})
      .channels(kColorDepth)
      .noGroundTruth()
      .covers("ANARI_KHR_ARRAY1D_REGION")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("geometry", "array1d_region_changes")
      .description(
          "Checks that setting an array's region changes what is drawn: the "
          "first half, the second half, or (unset) all of the array.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        const auto verts =
            triangleSoup(join({halfTriangles(0.25f), halfTriangles(0.75f)}));
        auto geom = anari::newObject<anari::Geometry>(d, "triangle");
        anari::setAndReleaseParameter(d,
            geom,
            "vertex.position",
            regionArray(d, verts, halfRegion(ctx.getString("region", ""))));
        anari::commitParameters(d, geom);
        return regionWorld(d, geom);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .permute("region", {"unset", "first", "second"})
      // The whole array draws both halves: the left half of the frame is the
      // first half's, the right half the second's.
      .expectDerived("region",
          "unset",
          "the first half's image on the left, the second half's on the right",
          [](const Siblings &s) {
            return regionComposite(s.at("first"),
                s.at("second"),
                [](float x, float) { return x < 0.5f; });
          })
      .channels(kColorDepth)
      .noGroundTruth()
      .covers("ANARI_KHR_ARRAY1D_REGION")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("geometry", "array1d_region_recommit")
      .description(
          "Checks that changing the region of an array that was already "
          "rendered, and committing it, shows the new region.")
      .build([](BuildContext &ctx) {
        return assembleWorld(ctx.device(), WorldContents{});
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .behavior(checkRegionRecommit)
      .covers("ANARI_KHR_ARRAY1D_REGION")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
