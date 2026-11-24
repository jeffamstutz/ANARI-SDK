// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../GeometryBuilder.h"
#include "../GeometryLayout.h"
#include "../LightBuilder.h"
#include "../ScreenQuad.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "AttributeColors.h"
#include "Categories.h"
#include "RawArray.h"
#include "ScreenScene.h"
#include "SubjectShown.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using anari::math::mat4;

// ---- KHR_INSTANCE_TRANSFORM_ARRAY
// --------------------------------------------
//
// One instance whose `transform` is an array of matrices instantiates its
// group once per element: it must look like that many instances with one
// matrix each. The scene is a small asymmetric triangle in the plane z = 0
// placed by each matrix (a translation, a rotation about z and a uniform
// scale), seen by the screen camera, so where each instance lands is known
// without a camera projection.

// The triangle every instance shows, around the origin of the plane.
const Triangle kInstanceTriangle = {float3(0.f, 0.12f, 0.f),
    float3(-0.1f, -0.08f, 0.f),
    float3(0.12f, -0.06f, 0.f)};

// A translation by (x, y) of a rotation by `angle` about z and a uniform
// `scale`.
mat4 placement(float x, float y, float angle, float scale)
{
  const float c = scale * std::cos(angle), s = scale * std::sin(angle);
  return mat4(float4(c, s, 0.f, 0.f),
      float4(-s, c, 0.f, 0.f),
      float4(0.f, 0.f, 1.f, 0.f),
      float4(x, y, 0.f, 1.f));
}

// The four matrices of the Tests, one per quadrant.
std::vector<mat4> quadrantMatrices()
{
  return {placement(-0.5f, 0.5f, 0.f, 1.f),
      placement(0.5f, 0.5f, 0.5f * float(M_PI), 1.4f),
      placement(-0.5f, -0.5f, float(M_PI), 1.f),
      placement(0.5f, -0.5f, -0.25f * float(M_PI), 1.8f)};
}

// Their instance ids when a Case gives one per instance, and the one id a
// Case gives them all.
const std::vector<uint32_t> kInstanceIds = {7, 21, 3, 40};
constexpr uint32_t kSharedInstanceId = 42;

float3 transformPoint(const mat4 &m, const float3 &p)
{
  const float4 q = anari::math::mul(m, float4(p, 1.f));
  return float3(q.x, q.y, q.z);
}

// The group every instance of these Tests instantiates.
anari::Group instanceTriangleGroup(anari::Device d)
{
  const auto verts =
      std::vector<float3>(kInstanceTriangle.begin(), kInstanceTriangle.end());
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  anari::commitParameters(d, geom);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(
      d, group, "surface", anari::newArray1D(d, &surface, 1));
  anari::commitParameters(d, group);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return group;
}

void setMatrix(anari::Device d, anari::Instance inst, const mat4 &m)
{
  float values[16];
  std::memcpy(values, &m, sizeof(values));
  anari::setParameter(d, inst, "transform", values);
}

// How a Case gives its instances ids: none, one each, or one for all.
enum class InstanceIds
{
  None,
  PerInstance,
  Shared,
};

InstanceIds parseInstanceIds(const std::string &s)
{
  if (s == "none")
    return InstanceIds::None;
  if (s == "perInstance")
    return InstanceIds::PerInstance;
  if (s == "shared")
    return InstanceIds::Shared;
  throw std::invalid_argument("unknown instance ids " + s);
}

// The world of `matrices` instancing the triangle group, as one instance with
// a transform array ("array") or as one instance per matrix ("instances"),
// with the given ids.
anari::World transformArrayWorld(anari::Device d,
    const std::string &form,
    const std::vector<mat4> &matrices,
    InstanceIds ids)
{
  auto group = instanceTriangleGroup(d);
  std::vector<anari::Instance> instances;
  if (form == "array") {
    auto inst = anari::newObject<anari::Instance>(d, "transform");
    anari::setAndReleaseParameter(d,
        inst,
        "transform",
        anari::newArray1D(d, matrices.data(), matrices.size()));
    if (ids == InstanceIds::PerInstance)
      anari::setAndReleaseParameter(d,
          inst,
          "id",
          anari::newArray1D(d, kInstanceIds.data(), matrices.size()));
    else if (ids == InstanceIds::Shared)
      anari::setParameter(d, inst, "id", kSharedInstanceId);
    anari::setParameter(d, inst, "group", group);
    anari::commitParameters(d, inst);
    instances.push_back(inst);
  } else if (form == "instances") {
    for (size_t i = 0; i < matrices.size(); ++i) {
      auto inst = anari::newObject<anari::Instance>(d, "transform");
      setMatrix(d, inst, matrices[i]);
      if (ids == InstanceIds::PerInstance)
        anari::setParameter(d, inst, "id", kInstanceIds[i]);
      else if (ids == InstanceIds::Shared)
        anari::setParameter(d, inst, "id", kSharedInstanceId);
      anari::setParameter(d, inst, "group", group);
      anari::commitParameters(d, inst);
      instances.push_back(inst);
    }
  } else {
    anari::release(d, group);
    throw std::invalid_argument("unknown instance form " + form);
  }

  auto light = makeDirectionalLight(d, float3(0.f, 0.f, -1.f), 1.5f);
  WorldContents wc;
  wc.instances = instances;
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, light);
  anari::release(d, group);
  for (auto i : instances)
    anari::release(d, i);
  return world;
}

// What each instance's id must be in the frame, by pixel: at each instance's
// centroid, the instance's id; no id the instances were given anywhere
// outside every instance (grown by 1.5 pixels, so edge pixels, which may hold
// either, aren't judged). The spec (1.2, section 5.5.1) gives the id array
// and the single id's fallback, and no value for a pixel without a hit, so
// the background is checked only for not claiming an instance.
void instanceIdsAreAssigned(ExactCheck &x)
{
  const auto ids = x.ids(Channel::InstanceId);
  const auto mode = parseInstanceIds(x.values().getString("ids", "none"));
  const auto matrices = quadrantMatrices();
  auto idOf = [&](size_t i) {
    return mode == InstanceIds::PerInstance ? kInstanceIds[i]
                                            : kSharedInstanceId;
  };

  PixelMask centroids(ids.width, ids.height);
  PixelMask subject(ids.width, ids.height);
  std::vector<int64_t> expected(size_t(ids.width) * ids.height, -1);
  for (size_t i = 0; i < matrices.size(); ++i) {
    std::array<anari::math::float2, 3> p;
    for (size_t k = 0; k < 3; ++k)
      p[k] = planePixel(transformPoint(matrices[i], kInstanceTriangle[k]),
          ids.width,
          ids.height);
    const auto centroid = (p[0] + p[1] + p[2]) / 3.f;
    const auto cx = uint32_t(centroid.x), cy = uint32_t(centroid.y);
    centroids.set(cx, cy);
    expected[size_t(cy) * ids.width + cx] = idOf(i);
    for (uint32_t y = 0; y < ids.height; ++y)
      for (uint32_t px = 0; px < ids.width; ++px)
        if (nearTriangle(p[0],
                p[1],
                p[2],
                anari::math::float2(px + 0.5f, y + 0.5f),
                1.5f))
          subject.set(px, y);
  }

  x.matches(
      "the instanceId at each instance's centroid is the id it was given",
      ids,
      [&](uint32_t px, uint32_t y) {
        return uint32_t(expected[size_t(y) * ids.width + px]);
      },
      0.0,
      &centroids);
  std::vector<uint32_t> given;
  for (size_t i = 0; i < matrices.size(); ++i)
    given.push_back(idOf(i));
  x.backgroundOutside("pixels outside every instance hold none of their ids",
      ids,
      subject,
      "none of the ids the instances were given",
      [&](uint32_t id) {
        return std::find(given.begin(), given.end(), id) == given.end();
      });
}

// For the transform array's region Test: six matrices, of which the region
// names three; the others place the triangle where a device that ignores the
// region would draw it.
std::vector<mat4> regionMatrices()
{
  const auto q = quadrantMatrices();
  return {placement(0.5f, -0.5f, 0.f, 1.f), // left out, before
      q[0],
      q[1],
      q[2],
      placement(0.f, 0.f, 0.f, 1.f), // left out, after
      placement(-0.9f, 0.f, 0.f, 1.f)}; // left out, after
}

using V = std::vector<Any>;

V attributeValues()
{
  V out;
  for (const auto &a : attributeNames())
    out.emplace_back(a.c_str());
  return out;
}

// ---- instance attributes ----------------------------------------------------
//
// An instance's uniform `color` and `attribute0` to `attribute3` apply to
// every object of its group that carries no such attribute itself (1.2,
// section 5.5.1). Four instances of a triangle each get colours of their own
// from the instance (one shared group), from the geometry (a group each), or
// from both with the instance's colours decoys: all three must render alike.

// Whether, and with what, an object carries every attribute.
enum class Carries
{
  Nothing,
  Colors,
  Decoys
};

void setUniformAttributes(
    anari::Device d, anari::Object o, Carries carries, int item)
{
  if (carries == Carries::Nothing)
    return;
  const auto &names = attributeNames();
  for (size_t k = 0; k < names.size(); ++k)
    anari::setParameter(d,
        o,
        names[k].c_str(),
        carries == Carries::Colors ? setColor(int(k), item)
                                   : decoyColor(int(k), item));
}

anari::Group attributeGroup(
    anari::Device d, const std::string &attribute, Carries geometry, int item)
{
  const auto verts =
      std::vector<float3>(kInstanceTriangle.begin(), kInstanceTriangle.end());
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  setUniformAttributes(d, geom, geometry, item);
  anari::commitParameters(d, geom);
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", attribute.c_str());
  anari::commitParameters(d, mat);
  auto surface = makeSurface(d, geom, mat);
  auto group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(
      d, group, "surface", anari::newArray1D(d, &surface, 1));
  anari::commitParameters(d, group);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return group;
}

anari::World instanceAttributesWorld(
    anari::Device d, const std::string &attribute, const std::string &source)
{
  const Carries geometry =
      source == "instance" ? Carries::Nothing : Carries::Colors;
  const Carries instance = source == "instance" ? Carries::Colors
      : source == "geometryOverInstance"        ? Carries::Decoys
                                                : Carries::Nothing;
  const auto matrices = quadrantMatrices();
  std::vector<anari::Group> groups;
  std::vector<anari::Instance> instances;
  anari::Group shared = nullptr;
  if (source == "instance")
    shared = attributeGroup(d, attribute, Carries::Nothing, 0);
  for (size_t i = 0; i < matrices.size(); ++i) {
    auto group =
        shared ? shared : attributeGroup(d, attribute, geometry, int(i));
    if (!shared)
      groups.push_back(group);
    auto inst = anari::newObject<anari::Instance>(d, "transform");
    setMatrix(d, inst, matrices[i]);
    setUniformAttributes(d, inst, instance, int(i));
    anari::setParameter(d, inst, "group", group);
    anari::commitParameters(d, inst);
    instances.push_back(inst);
  }
  auto light = makeDirectionalLight(d, float3(0.f, 0.f, -1.f), 1.5f);
  WorldContents wc;
  wc.instances = instances;
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, light);
  if (shared)
    anari::release(d, shared);
  for (auto g : groups)
    anari::release(d, g);
  for (auto i : instances)
    anari::release(d, i);
  return world;
}

// ---- non-uniform scale
// -------------------------------------------------------
//
// An instance transform with a non-uniform scale and a rotation shears its
// group's normals unless they are transformed by the inverse transpose. A lit
// cube under such a transform must shade, and be as deep, as the cube with
// the transform applied to its vertices (whose faces then have their own
// flat normals).

const mat4 kSheared = anari::math::mul(mat4(float4(1.f, 0.f, 0.f, 0.f),
                                           float4(0.f, 1.f, 0.f, 0.f),
                                           float4(0.f, 0.f, 1.f, 0.f),
                                           float4(0.3f, -0.2f, 0.f, 1.f)),
    anari::math::mul(anari::math::rotation_matrix(anari::math::rotation_quat(
                         float3(0.f, 0.f, 1.f), 0.6f)),
        mat4(float4(1.8f, 0.f, 0.f, 0.f),
            float4(0.f, 0.5f, 0.f, 0.f),
            float4(0.f, 0.f, 1.2f, 0.f),
            float4(0.f, 0.f, 0.f, 1.f))));

anari::World nonUniformScaleWorld(anari::Device d, bool baked)
{
  const auto layout =
      makeTriangleLayout(TriangleShape::Cube, PrimitiveMode::Soup, 1);
  float3 center(0.f);
  for (const auto &p : layout.positions)
    center = center + p;
  center = center / float(layout.positions.size());

  // The cube about the origin, so the matrix scales it about its centre.
  std::vector<float3> verts;
  for (const auto &p : layout.positions) {
    auto q = p - center;
    if (baked) {
      const float4 t = anari::math::mul(kSheared, float4(q, 1.f));
      q = float3(t.x, t.y, t.z);
    }
    verts.push_back(q);
  }
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  anari::commitParameters(d, geom);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(
      d, group, "surface", anari::newArray1D(d, &surface, 1));
  anari::commitParameters(d, group);
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  setMatrix(d, inst, baked ? anari::math::identity : kSheared);
  anari::setParameter(d, inst, "group", group);
  anari::commitParameters(d, inst);
  auto light = makeDirectionalLight(d, float3(-0.4f, -0.7f, -1.f), 1.5f);
  WorldContents wc;
  wc.instances = {inst};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, light);
  anari::release(d, inst);
  anari::release(d, group);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

} // namespace

void registerInstanceTests(Catalog &catalog)
{
  makeTest("instance", "instance")
      .description("Checks multiple transformed instances sharing one group.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        TriangleSpec spec;
        auto geom = buildTriangleGeometry(d, spec);
        auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
        auto surface = makeSurface(d, geom, mat);

        // One group, shared by both instances.
        auto group = anari::newObject<anari::Group>(d);
        anari::setAndReleaseParameter(
            d, group, "surface", anari::newArray1D(d, &surface, 1));
        anari::commitParameters(d, group);

        auto makeInst = [&](const mat4 &xfm) {
          auto inst = anari::newObject<anari::Instance>(d, "transform");
          float m[16];
          std::memcpy(m, &xfm, sizeof(m));
          anari::setParameter(d, inst, "transform", m);
          anari::setParameter(d, inst, "group", group);
          anari::commitParameters(d, inst);
          return inst;
        };
        auto i0 = makeInst(anari::math::identity);
        auto i1 = makeInst(mat4(float4(-1.f, -1.f, 0.f, 0.f),
            float4(1.f, -1.f, 0.f, 0.f),
            float4(0.f, 0.f, -1.f, 0.f),
            float4(1.f, 1.f, 1.f, 1.f)));

        WorldContents wc;
        wc.instances = {i0, i1};
        auto world = assembleWorld(d, wc);

        anari::release(d, geom);
        anari::release(d, mat);
        anari::release(d, surface);
        anari::release(d, group);
        anari::release(d, i0);
        anari::release(d, i1);
        return world;
      })
      .channels({Channel::Color, Channel::Depth})
      .groundTruth(
          "where the two transformed instances land, in colour and depth, has "
          "no analytic answer in the Test")
      .covers("ANARI_KHR_INSTANCE_TRANSFORM")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("instance", "attributes")
      .description(
          "Checks that an instance's uniform color and attribute0 to "
          "attribute3 colour a group whose geometry carries none, as the "
          "geometry's own uniform attributes do, and that the geometry's win "
          "over the instance's.")
      .build([](BuildContext &ctx) {
        return instanceAttributesWorld(ctx.device(),
            ctx.getString("attribute", "color"),
            ctx.getString("source", "geometry"));
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .variant("source", V{"geometry", "instance", "geometryOverInstance"})
      .permute("attribute", attributeValues())
      .channels({Channel::Color})
      .noGroundTruth()
      .covers("ANARI_KHR_INSTANCE_TRANSFORM")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("instance", "non_uniform_scale")
      .description(
          "Checks that an instance transform with a rotation and a "
          "non-uniform scale shades and places a cube as the cube with the "
          "transform applied to its vertices.")
      .build([](BuildContext &ctx) {
        return nonUniformScaleWorld(
            ctx.device(), ctx.getString("form", "instance") == "baked");
      })
      // A fixed camera: the default one frames the world's bounds, and an
      // instance's bounds are its group's box transformed, looser than the
      // baked cube's.
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "perspective");
        anari::setParameter(d, c, "position", float3(0.f, 0.f, 3.f));
        anari::setParameter(d, c, "direction", float3(0.f, 0.f, -1.f));
        anari::setParameter(d, c, "up", float3(0.f, 1.f, 0.f));
        anari::setParameter(d, c, "fovy", 0.6f);
        anari::commitParameters(d, c);
        return c;
      })
      .variant("form", V{"baked", "instance"})
      .channels({Channel::Color})
      .expectExact("the cube shows", subjectIsShown)
      .noGroundTruth()
      .covers("ANARI_KHR_INSTANCE_TRANSFORM")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  // ---- transform array ------------------------------------------------------
  makeTest("instance", "transform_array")
      .description(
          "Checks that one instance with an array of transforms renders like "
          "one instance per transform, in colour, depth and instance id.")
      .build([](BuildContext &ctx) {
        return transformArrayWorld(ctx.device(),
            ctx.getString("form", ""),
            quadrantMatrices(),
            InstanceIds::None);
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      // The first value is the reference: one instance per transform.
      .variant("form", {"instances", "array"})
      .channels({Channel::Color, Channel::Depth, Channel::InstanceId})
      .rendererParam("resolutionDivider", "1")
      .noGroundTruth()
      .covers("ANARI_KHR_INSTANCE_TRANSFORM_ARRAY")
      .requireFeatures(kSupportMatteTriangle)
      .requireFeature("ANARI_KHR_INSTANCE_TRANSFORM")
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID")
      .registerInto(catalog);

  makeTest("instance", "transform_array_ids")
      .description(
          "Checks that each element of a transform array reports the id from "
          "the id array (or the instance's single id when there is no array) "
          "in the instanceId channel, as one instance per transform does.")
      .build([](BuildContext &ctx) {
        return transformArrayWorld(ctx.device(),
            ctx.getString("form", ""),
            quadrantMatrices(),
            parseInstanceIds(ctx.getString("ids", "")));
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .variant("form", {"instances", "array"})
      .permute("ids", {"perInstance", "shared"})
      .expectExact("instance ids are assigned", instanceIdsAreAssigned)
      .channels({Channel::Color, Channel::InstanceId})
      .rendererParam("resolutionDivider", "1")
      .noGroundTruth()
      .covers("ANARI_KHR_INSTANCE_TRANSFORM_ARRAY")
      .requireFeatures(kSupportMatteTriangle)
      .requireFeature("ANARI_KHR_INSTANCE_TRANSFORM")
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID")
      .registerInto(catalog);

  makeTest("instance", "transform_array_region")
      .description(
          "Checks that an instance's transform array, whose region names "
          "three of its six matrices, instantiates its group as an array of "
          "just those three does.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        const auto all = regionMatrices();
        const bool region = ctx.getString("array", "") == "region";
        // The reference holds only the matrices the region names.
        const std::vector<mat4> matrices =
            region ? all : std::vector<mat4>(all.begin() + 1, all.begin() + 4);

        auto group = instanceTriangleGroup(d);
        auto array = anari::newArray1D(d, matrices.data(), matrices.size());
        if (region) {
          const uint64_t r[2] = {1, 4};
          anariSetParameter(d, array, "region", ANARI_UINT64_REGION1, r);
          anari::commitParameters(d, array);
        }
        auto inst = anari::newObject<anari::Instance>(d, "transform");
        anari::setAndReleaseParameter(d, inst, "transform", array);
        anari::setParameter(d, inst, "group", group);
        anari::commitParameters(d, inst);
        auto light = makeDirectionalLight(d, float3(0.f, 0.f, -1.f), 1.5f);
        WorldContents wc;
        wc.instances = {inst};
        wc.lights = {light};
        auto world = assembleWorld(d, wc);
        anari::release(d, light);
        anari::release(d, inst);
        anari::release(d, group);
        return world;
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .variant("array", {"shortArray", "region"})
      .channels({Channel::Color, Channel::Depth})
      .noGroundTruth()
      .covers("ANARI_KHR_ARRAY1D_REGION")
      .requireFeatures(kSupportMatteTriangle)
      .requireFeature("ANARI_KHR_INSTANCE_TRANSFORM_ARRAY")
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
