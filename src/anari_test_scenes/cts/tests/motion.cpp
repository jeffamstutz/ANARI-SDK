// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The motion blur family: camera shutter and rolling shutter, camera motion,
// instance motion (matrix and decomposed keys) and triangle and quad motion
// deformation. Every check is a self-check (ADR-0010), so a device that
// advertises the extension is judged on its own output; no SDK device does.
//
// The spec gives motion keys "uniformly distributed in time" over `time`
// (default [0, 1]) and the shutter as an interval of that time, but not how
// keys are interpolated. So the main check of each motion extension renders
// with an instant shutter at the times its keys sit at (the start, middle and
// end of `time`), where any interpolation gives the key itself, and requires
// the static scene built at that key. Where a check has to know where the
// subject is between keys (the open and rolling shutters), the motion is a
// translation, which every interpolation agrees on.

#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "MotionScene.h"
#include "RawArray.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using anari::math::mat4;
using V = std::vector<Any>;

constexpr const char *kShutter = "ANARI_KHR_CAMERA_SHUTTER";
constexpr const char *kRollingShutter = "ANARI_KHR_CAMERA_ROLLING_SHUTTER";
constexpr const char *kCameraMotion = "ANARI_KHR_CAMERA_MOTION_TRANSFORMATION";
constexpr const char *kInstanceMotion = "ANARI_KHR_INSTANCE_MOTION_TRANSFORM";
constexpr const char *kInstanceSrt =
    "ANARI_KHR_INSTANCE_MOTION_SCALE_ROTATION_TRANSLATION";
constexpr const char *kTriangleMotion =
    "ANARI_KHR_GEOMETRY_TRIANGLE_MOTION_DEFORMATION";
constexpr const char *kQuadMotion =
    "ANARI_KHR_GEOMETRY_QUAD_MOTION_DEFORMATION";

// A shutter or `time` (box1) axis value, stored with its ANARI type.
Any box1(float lower, float upper)
{
  const float v[2] = {lower, upper};
  return Any(ANARI_FLOAT32_BOX1, v);
}

// The Case's shutter: the axis value, or the spec's default [0.5, 0.5].
float2 shutterOf(const BuildContext &ctx)
{
  const Any v = ctx.value("shutter");
  if (!v.valid() || v.type() != ANARI_FLOAT32_BOX1)
    return float2(0.5f, 0.5f);
  const auto *f = static_cast<const float *>(v.data());
  return float2(std::clamp(f[0], 0.f, 1.f), std::clamp(f[1], 0.f, 1.f));
}

// What moves ///////////////////////////////////////////////////////////////

enum class Kind
{
  InstanceMotion, // motionTransform instance
  InstanceSrt, // motionScaleRotationTranslation instance
  Triangle, // triangle motion deformation
  Quad, // quad motion deformation
  Camera, // camera motion transformation
};

bool movesGeometry(Kind k)
{
  return k == Kind::Triangle || k == Kind::Quad;
}

// A key: scale, then rotation (a quaternion, ijkw), then translation, the
// order the spec applies motionScaleRotationTranslation's components in.
struct Pose
{
  float3 scale{1.f, 1.f, 1.f};
  float4 rotation{0.f, 0.f, 0.f, 1.f};
  float3 translation{0.f, 0.f, 0.f};
};

mat4 matrixOf(const Pose &p)
{
  return math::mul(math::translation_matrix(p.translation),
      math::mul(
          math::rotation_matrix(p.rotation), math::scaling_matrix(p.scale)));
}

float3 transformPoint(const mat4 &m, const float3 &v)
{
  const float4 r = math::mul(m, float4(v, 1.f));
  return float3(r.x, r.y, r.z);
}

float4 rotationZ(float degrees)
{
  return math::rotation_quat(
      float3(0.f, 0.f, 1.f), degrees * 3.14159265f / 180.f);
}

Pose translated(const float3 &t)
{
  Pose p;
  p.translation = t;
  return p;
}

// The subject a Case renders: static (one key, built without motion) or one
// of the motion forms, with its keys, its per-key vertex normals (geometry
// motion only; empty when the Test sets none) and its `time`.
struct Subject
{
  std::string form{"static"};
  std::vector<Pose> keys;
  std::vector<float3> normals;
  std::optional<float2> time;
  float2 half{motion::kOpenHalfWidth, motion::kOpenHalfHeight};
};

// The instant Tests' keys, at times 0, 0.5 and 1: the rectangle placed,
// turned and stretched differently at each, so a device that takes another
// key (or none) renders something else. The camera's keys turn and shift the
// camera from the rest camera's place instead (a camera isn't scaled).
const std::array<Pose, 3> kKeys = {
    Pose{float3(1.f, 1.f, 1.f), rotationZ(0.f), float3(-0.6f, 0.3f, 0.f)},
    Pose{float3(1.3f, 0.7f, 1.f), rotationZ(30.f), float3(0.f, -0.4f, 0.f)},
    Pose{float3(0.8f, 1.2f, 1.f), rotationZ(-45.f), float3(0.6f, 0.3f, 0.f)}};

Pose cameraKey(size_t i)
{
  Pose p;
  p.rotation = kKeys[i].rotation;
  p.translation =
      float3(0.5f * kKeys[i].translation.x, 0.5f * kKeys[i].translation.y, 4.f);
  return p;
}

// A key that puts the subject out of view (or the camera looking away from
// it): what the keys before `time`'s start hold in the `motion_time` form, so
// a device that ignores `time` shows nothing at shutter 0.
Pose decoyKey(Kind kind)
{
  return translated(
      kind == Kind::Camera ? float3(0.f, 6.f, 4.f) : float3(0.f, 6.f, 0.f));
}

// The geometry Tests' per-key vertex normals: tilted towards the light, along
// the view, and away from it, so each key shades differently (the light
// comes from (0.6, 0, 0.8)).
const std::array<float3, 3> kNormals = {
    float3(0.6f, 0.f, 0.8f), float3(0.f, 0.f, 1.f), float3(-0.6f, 0.f, 0.8f)};
const float3 kFacing(0.f, 0.f, 1.f);
const float3 kLightDirection(-0.6f, 0.f, -0.8f);

// The instant Tests' subject: the static scene at the key the Case's
// (instant) shutter time sits on, or the motion form holding every key.
Subject instantSubject(const BuildContext &ctx, Kind kind)
{
  const float t = shutterOf(ctx).x;
  const size_t k = size_t(std::clamp(int(std::lround(t * 2.f)), 0, 2));
  auto key = [&](size_t i) {
    return kind == Kind::Camera ? cameraKey(i) : kKeys[i];
  };
  Subject s;
  s.form = ctx.getString("subject", "static");
  if (s.form == "static") {
    s.keys = {key(k)};
    s.normals = {kNormals[k]};
  } else if (s.form == "motion_time") {
    // time [-1, 1] over five keys: the last three sit at 0, 0.5 and 1.
    const Pose decoy = decoyKey(kind);
    s.keys = {decoy, decoy, key(0), key(1), key(2)};
    s.normals = {kFacing, kFacing, kNormals[0], kNormals[1], kNormals[2]};
    s.time = float2(-1.f, 1.f);
  } else {
    s.keys = {key(0), key(1), key(2)};
    s.normals = {kNormals[0], kNormals[1], kNormals[2]};
  }
  if (!movesGeometry(kind))
    s.normals.clear();
  return s;
}

// The open-shutter Tests' subject: moving along x from kOpenFrom to kOpenTo
// (the camera moving the other way, for camera motion).
Subject openSubject(Kind kind)
{
  Subject s;
  s.form = "motion";
  if (kind == Kind::Camera) {
    const float3 rest = motion::restCamera().position;
    s.keys = {translated(rest - motion::kOpenFrom),
        translated(rest - motion::kOpenTo)};
  } else {
    s.keys = {translated(motion::kOpenFrom), translated(motion::kOpenTo)};
  }
  if (movesGeometry(kind))
    s.normals = {kFacing, kFacing};
  return s;
}

// The identical-keys Tests' subject: the static scene at one place, or three
// motion keys all holding it.
Subject identicalSubject(const BuildContext &ctx, Kind kind)
{
  const float3 at = motion::kIdenticalAt;
  const Pose key = kind == Kind::Camera
      ? translated(motion::restCamera().position - at)
      : translated(at);
  Subject s;
  s.form = ctx.getString("subject", "static");
  s.keys = s.form == "static" ? std::vector<Pose>{key}
                              : std::vector<Pose>{key, key, key};
  if (movesGeometry(kind))
    s.normals.assign(s.keys.size(), kFacing);
  return s;
}

// The rolling-shutter Tests' subject: a bar instance moving across the frame.
Subject rollingSubject(bool rows)
{
  Subject s;
  s.form = "motion";
  s.keys = {translated(motion::barFrom(rows)), translated(motion::barTo(rows))};
  s.half = motion::barHalfExtent(rows);
  return s;
}

// Building it ////////////////////////////////////////////////////////////////

// The rectangle's vertices: one quad, or two triangles.
std::vector<float3> rectangleVertices(bool quad, float2 half)
{
  const float3 a(-half.x, -half.y, 0.f), b(half.x, -half.y, 0.f),
      c(half.x, half.y, 0.f), e(-half.x, half.y, 0.f);
  if (quad)
    return {a, b, c, e};
  return {a, b, c, a, c, e};
}

std::vector<float3> placed(const std::vector<float3> &vertices, const Pose &p)
{
  const mat4 m = matrixOf(p);
  std::vector<float3> out;
  out.reserve(vertices.size());
  for (const auto &v : vertices)
    out.push_back(transformPoint(m, v));
  return out;
}

void setTime(anari::Device d, anari::Object o, const std::optional<float2> &t)
{
  if (!t)
    return;
  const float v[2] = {t->x, t->y};
  anariSetParameter(d, o, "time", ANARI_FLOAT32_BOX1, v);
}

// An ARRAY1D of ARRAY1D, one inner array per key.
template <typename T>
anari::Array1D arrayOfArrays(
    anari::Device d, const std::vector<std::vector<T>> &perKey)
{
  std::vector<anari::Array1D> inner;
  for (const auto &values : perKey)
    inner.push_back(anari::newArray1D(d, values.data(), values.size()));
  auto outer = anari::newArray1D(d, inner.data(), inner.size());
  for (auto a : inner)
    anari::release(d, a);
  return outer;
}

// The rectangle as a triangle or quad geometry: static at the first key, or
// deformed through every key (`motion.vertex.*`). The static geometry and the
// moving one's required `vertex.position` hold the first key.
anari::Geometry rectangleGeometry(
    anari::Device d, bool quad, const Subject &s, bool moving)
{
  auto geom = anari::newObject<anari::Geometry>(d, quad ? "quad" : "triangle");
  const auto local = rectangleVertices(quad, s.half);
  const size_t n = local.size();
  const auto first = placed(local, s.keys.front());
  anari::setParameterArray1D(d, geom, "vertex.position", first.data(), n);
  const float4 tangent(1.f, 0.f, 0.f, 1.f);
  if (!moving) {
    if (!s.normals.empty()) {
      const std::vector<float3> normal(n, s.normals.front());
      const std::vector<float4> tangents(n, tangent);
      anari::setParameterArray1D(d, geom, "vertex.normal", normal.data(), n);
      anari::setParameterArray1D(d, geom, "vertex.tangent", tangents.data(), n);
    }
  } else {
    std::vector<std::vector<float3>> positions, normals;
    std::vector<std::vector<float4>> tangents;
    for (size_t i = 0; i < s.keys.size(); ++i) {
      positions.push_back(placed(local, s.keys[i]));
      if (!s.normals.empty()) {
        normals.emplace_back(n, s.normals[i]);
        tangents.emplace_back(n, tangent);
      }
    }
    anari::setAndReleaseParameter(
        d, geom, "motion.vertex.position", arrayOfArrays(d, positions));
    if (!normals.empty()) {
      anari::setAndReleaseParameter(
          d, geom, "motion.vertex.normal", arrayOfArrays(d, normals));
      anari::setAndReleaseParameter(
          d, geom, "motion.vertex.tangent", arrayOfArrays(d, tangents));
    }
    setTime(d, geom, s.time);
  }
  anari::commitParameters(d, geom);
  return geom;
}

void setMotionTransform(
    anari::Device d, anari::Object o, const std::vector<Pose> &keys)
{
  std::vector<mat4> m;
  for (const auto &k : keys)
    m.push_back(matrixOf(k));
  anari::setParameterArray1D(d, o, "motion.transform", m.data(), m.size());
}

void setMotionSrt(
    anari::Device d, anari::Object o, const std::vector<Pose> &keys)
{
  std::vector<float3> scale, translation;
  std::vector<float4> rotation;
  for (const auto &k : keys) {
    scale.push_back(k.scale);
    rotation.push_back(k.rotation);
    translation.push_back(k.translation);
  }
  anari::setParameterArray1D(d, o, "motion.scale", scale.data(), scale.size());
  anari::setAndReleaseParameter(d,
      o,
      "motion.rotation",
      makeRawArray1D(
          d, ANARI_FLOAT32_QUAT_IJKW, rotation.size(), rotation.data()));
  anari::setParameterArray1D(
      d, o, "motion.translation", translation.data(), translation.size());
}

// The moving instance of the instance Tests: `motionTransform` or
// `motionScaleRotationTranslation` by the form. `motion_over_transform` also
// sets a `transform` that would put the subject out of view, which the
// motion keys take precedence over.
anari::Instance motionInstance(
    anari::Device d, Kind kind, const Subject &s, anari::Surface surface)
{
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::commitParameters(d, group);

  const bool srt = kind == Kind::InstanceSrt && s.form != "motionTransform";
  auto instance = anari::newObject<anari::Instance>(
      d, srt ? "motionScaleRotationTranslation" : "motionTransform");
  if (srt)
    setMotionSrt(d, instance, s.keys);
  else
    setMotionTransform(d, instance, s.keys);
  if (s.form == "motion_over_transform") {
    const mat4 away = matrixOf(decoyKey(kind));
    anariSetParameter(d, instance, "transform", ANARI_FLOAT32_MAT4, &away);
  }
  setTime(d, instance, s.time);
  anari::setAndReleaseParameter(d, instance, "group", group);
  anari::commitParameters(d, instance);
  return instance;
}

// The Case's world: the lit rectangle, static or moving as the subject says
// (it stays put for camera motion).
anari::World motionWorld(BuildContext &ctx, Kind kind, const Subject &s)
{
  auto d = ctx.device();
  auto mat = makeMatteMaterial(d, float3(0.9f, 0.6f, 0.3f));
  auto light = makeDirectionalLight(d, kLightDirection);
  WorldContents wc;
  wc.lights = {light};
  const bool quad = kind == Kind::Quad;
  const bool moving = s.form != "static" && kind != Kind::Camera;
  anari::Geometry geom{nullptr};
  if (movesGeometry(kind)) {
    geom = rectangleGeometry(d, quad, s, moving);
  } else {
    // A rigid subject: static, the rectangle placed at its key (the camera
    // Tests' rectangle stays at the origin); moving, the rectangle at the
    // origin inside a motion instance.
    Subject rigid = s;
    if (moving || kind == Kind::Camera)
      rigid.keys = {Pose{}};
    rigid.normals.clear();
    geom = rectangleGeometry(d, false, rigid, false);
  }
  auto surface = makeSurface(d, geom, mat);
  anari::Instance instance{nullptr};
  if (moving && !movesGeometry(kind)) {
    instance = motionInstance(d, kind, s, surface);
    wc.instances = {instance};
  } else {
    wc.surfaces = {surface};
  }
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  if (instance)
    anari::release(d, instance);
  return world;
}

// The perspective camera at a key: placed at its translation, turned by its
// rotation (a camera key has no scale).
CameraProjection cameraAt(const Pose &p)
{
  CameraProjection c = motion::restCamera();
  c.position = p.translation;
  c.direction = math::qrot(p.rotation, float3(0.f, 0.f, -1.f));
  c.up = math::qrot(p.rotation, float3(0.f, 1.f, 0.f));
  return c;
}

void setCameraParam(BuildContext &ctx, anari::Camera c, const char *name)
{
  const Any v = ctx.value(name);
  if (v.valid())
    applyParameterValue(ctx.device(), c, name, v.raw());
}

// The Case's camera, with the Case's shutter (and rolling shutter). The
// rest camera, or for camera motion the camera at its key (static) or a
// camera moved by its keys. A moving camera leaves its frame at the spec's
// defaults (at the origin, looking down -z, +y up), so whether a device
// applies `motion.transform` to that frame or uses it as the camera's
// placement, the keys place it the same. A camera-motion Case records the
// rest camera's projection, not its own: its exact checks say where the
// subject appears as seen from the rest camera, as if the subject had moved
// the other way (its keys are translations wherever an exact check looks).
anari::Camera motionCamera(BuildContext &ctx, Kind kind, const Subject &s)
{
  auto d = ctx.device();
  anari::Camera camera{nullptr};
  if (kind != Kind::Camera) {
    camera = ctx.newCamera(motion::restCamera());
  } else {
    camera = ctx.newCamera(
        s.form == "static" ? cameraAt(s.keys.front()) : CameraProjection{});
    CameraProjection rest = motion::restCamera();
    rest.aspect = ctx.cameraProjection()->aspect;
    ctx.setCameraProjection(rest);
  }
  if (kind == Kind::Camera && s.form != "static") {
    if (s.form == "srt") {
      setMotionSrt(d, camera, s.keys);
    } else {
      setMotionTransform(d, camera, s.keys);
      // Decomposed keys that would look away from the subject, which
      // motion.transform overrides.
      if (s.form == "transform_over_srt")
        setMotionSrt(d,
            camera,
            std::vector<Pose>(s.keys.size(), decoyKey(Kind::Camera)));
    }
    setTime(d, camera, s.time);
  }
  setCameraParam(ctx, camera, "shutter");
  setCameraParam(ctx, camera, "rollingShutterDirection");
  setCameraParam(ctx, camera, "rollingShutterDuration");
  anari::commitParameters(d, camera);
  return camera;
}

// Exact checks ///////////////////////////////////////////////////////////////

// A pixel shows something other than the background when it is more than
// this many 8-bit levels from it (in any channel).
constexpr double kBackgroundLevels = 8.0;

// A screen rectangle in continuous pixel coordinates.
struct PixelRect
{
  float x0, y0, x1, y1;
};

// Where the rectangle of `half` extents centred at `centre` (in z = 0) lands.
std::optional<PixelRect> projectRect(
    const ExactCheck &x, const float3 &centre, float2 half)
{
  const auto lo = x.project(centre - float3(half.x, half.y, 0.f));
  const auto hi = x.project(centre + float3(half.x, half.y, 0.f));
  if (!lo || !hi)
    return std::nullopt;
  return PixelRect{std::min(lo->x, hi->x),
      std::min(lo->y, hi->y),
      std::max(lo->x, hi->x),
      std::max(lo->y, hi->y)};
}

// The pixels whose centres lie inside `r` grown by `grow` (shrunk when
// negative).
PixelMask maskOf(FrameSize size, const PixelRect &r, float grow)
{
  PixelMask m(size.width, size.height);
  for (uint32_t y = 0; y < size.height; ++y)
    for (uint32_t px = 0; px < size.width; ++px) {
      const float cx = px + 0.5f, cy = y + 0.5f;
      if (cx >= r.x0 - grow && cx <= r.x1 + grow && cy >= r.y0 - grow
          && cy <= r.y1 + grow)
        m.set(px, y);
    }
  return m;
}

// The open shutter (the open-shutter Tests): the subject moves along x from
// kOpenFrom to kOpenTo over time [0, 1]; a Case's shutter [a, b] sees it
// from its place at a to its place at b. The part both silhouettes share is
// covered all the time the shutter is open and shows the subject; nothing
// outside their hull is touched; and with a shutter open for a while, the
// part only one of them covers is covered for part of it and shows the
// subject (blended, but not background) in most of its pixels. Edges are
// given 2 pixels either way.
void openShutterCoverage(ExactCheck &x)
{
  const auto color = x.rgba(Channel::Color);
  const Rgba8 background = color.at(0, 0);
  const FrameSize size = x.frameSize();
  const float2 shutter = shutterOf(x.values());
  const float2 half(motion::kOpenHalfWidth, motion::kOpenHalfHeight);
  auto at = [&](float t) {
    return motion::kOpenFrom + t * (motion::kOpenTo - motion::kOpenFrom);
  };
  const auto start = projectRect(x, at(shutter.x), half);
  const auto end = projectRect(x, at(shutter.y), half);
  if (!start || !end) {
    x.measure("the subject projects into the frame", 0.0, Limit::AtLeast, 1.0);
    return;
  }
  auto shows = [&](const Rgba8 &v) {
    return valueDistance(v, background) > kBackgroundLevels;
  };
  const PixelRect always{std::max(start->x0, end->x0),
      std::max(start->y0, end->y0),
      std::min(start->x1, end->x1),
      std::min(start->y1, end->y1)};
  const PixelMask alwaysMask = maskOf(size, always, -2.f);
  x.allPixels(
      "the part covered all the time the shutter is open shows the "
      "subject",
      color,
      "not the background (the corner's colour)",
      [&](uint32_t, uint32_t, const Rgba8 &v) { return shows(v); },
      &alwaysMask);
  const PixelRect hull{std::min(start->x0, end->x0),
      std::min(start->y0, end->y0),
      std::max(start->x1, end->x1),
      std::max(start->y1, end->y1)};
  x.backgroundOutside("nothing outside where the subject was is touched",
      color,
      maskOf(size, hull, 2.f),
      "the background (the corner's colour)",
      [&](const Rgba8 &v) { return !shows(v); });
  if (!(shutter.y - shutter.x > 0.05f))
    return;
  auto fractionShown = [&](const PixelRect &r) {
    const PixelMask m = maskOf(size, r, -2.f);
    size_t n = 0, shown = 0;
    for (uint32_t y = 0; y < size.height; ++y)
      for (uint32_t px = 0; px < size.width; ++px)
        if (m.selects(px, y)) {
          ++n;
          shown += shows(color.at(px, y)) ? 1 : 0;
        }
    return n ? double(shown) / double(n) : 0.0;
  };
  x.measure(
      "fraction of the part only the start covers that shows the "
      "subject",
      fractionShown(PixelRect{start->x0, start->y0, end->x0, start->y1}),
      Limit::AtLeast,
      0.5);
  x.measure("fraction of the part only the end covers that shows the subject",
      fractionShown(PixelRect{start->x1, end->y0, end->x1, end->y1}),
      Limit::AtLeast,
      0.5);
}

// The identical-keys Tests: whether moving or static, the subject is the
// rectangle at kIdenticalAt (its inside shows it and nothing outside it is
// touched, edges given 2 pixels), so equivalent Cases can't pass by being
// equally wrong or empty.
void subjectAtIdenticalKeys(ExactCheck &x)
{
  const auto color = x.rgba(Channel::Color);
  const Rgba8 background = color.at(0, 0);
  const auto r = projectRect(x,
      motion::kIdenticalAt,
      float2(motion::kOpenHalfWidth, motion::kOpenHalfHeight));
  if (!r) {
    x.measure("the subject projects into the frame", 0.0, Limit::AtLeast, 1.0);
    return;
  }
  auto shows = [&](const Rgba8 &v) {
    return valueDistance(v, background) > kBackgroundLevels;
  };
  const PixelMask inside = maskOf(x.frameSize(), *r, -2.f);
  x.allPixels(
      "the subject shows where its keys put it",
      color,
      "not the background (the corner's colour)",
      [&](uint32_t, uint32_t, const Rgba8 &v) { return shows(v); },
      &inside);
  x.backgroundOutside("nothing outside the subject is touched",
      color,
      maskOf(x.frameSize(), *r, 2.f),
      "the background (the corner's colour)",
      [&](const Rgba8 &v) { return !shows(v); });
}

// The rolling shutter (the rolling-shutter Tests): the bar crosses the frame
// over shutter [0, 1], perpendicular to the scan, so each line shows it where
// it was while that line was open (motion::rollingLineMidTime). In three
// bands of lines (early, middle and late in the scan) the centre of the
// pixels showing the bar must be where the bar was at the band's time,
// within 6 % of the bar's travel across the frame plus 2 pixels. A global
// shutter (`none`) shows it blurred around its middle place in every band.
void rollingShear(ExactCheck &x, bool rows)
{
  const auto color = x.rgba(Channel::Color);
  const Rgba8 background = color.at(0, 0);
  const FrameSize size = x.frameSize();
  const std::string direction =
      x.values().getString("rollingShutterDirection", "none");
  const float duration = x.get<float>("rollingShutterDuration", 0.f);
  const auto from = x.project(motion::barFrom(rows));
  const auto to = x.project(motion::barTo(rows));
  if (!from || !to) {
    x.measure("the bar projects into the frame", 0.0, Limit::AtLeast, 1.0);
    return;
  }
  // Lines are rows (scanned along y) when the bar moves along x.
  const uint32_t lines = rows ? size.height : size.width;
  const uint32_t along = rows ? size.width : size.height;
  const float travel = rows ? to->x - from->x : to->y - from->y;
  const float tolerance = 0.06f * std::fabs(travel) + 2.f;
  // A global shutter has no scan order; its bands are taken top down.
  const std::string order = (direction == "none" || direction.empty())
      ? (rows ? "down" : "right")
      : direction;
  for (const float s0 : {0.1f, 0.5f, 0.9f}) {
    double sum = 0.0;
    size_t count = 0;
    for (uint32_t l = 0; l < lines; ++l) {
      const float s = motion::rollingScanOrder(order, l + 0.5f, float(lines));
      if (std::fabs(s - s0) > 0.025f)
        continue;
      for (uint32_t a = 0; a < along; ++a) {
        const Rgba8 v = rows ? color.at(a, l) : color.at(l, a);
        if (valueDistance(v, background) > kBackgroundLevels) {
          sum += a + 0.5;
          ++count;
        }
      }
    }
    const std::string band =
        "scan band at " + std::to_string(int(s0 * 100)) + " % of the scan";
    if (count == 0) {
      x.measure(band + ": pixels showing the bar", 0.0, Limit::AtLeast, 1.0);
      continue;
    }
    const float t = motion::rollingLineMidTime(direction, duration, s0);
    const float expected = rows ? from->x + t * (to->x - from->x)
                                : from->y + t * (to->y - from->y);
    x.measure(band + ": pixels between the bar's centre and its place at time "
            + std::to_string(t).substr(0, 4),
        std::fabs(sum / double(count) - expected),
        Limit::AtMost,
        tolerance);
  }
}

// Registration ///////////////////////////////////////////////////////////////

struct KindInfo
{
  Kind kind;
  const char *category;
  const char *name; // the Tests' name prefix
  const char *extension;
  const char *what; // what moves, for descriptions
  std::vector<std::string> forms; // the instant Test's subject values
  std::vector<std::string> support;
};

std::vector<std::string> plus(
    std::vector<std::string> a, const std::vector<std::string> &b)
{
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

V strings(const std::vector<std::string> &values)
{
  V out;
  for (const auto &v : values)
    out.emplace_back(Any(v.c_str()));
  return out;
}

// The instant shutter Test: at shutter [t, t] for t at each key, every
// motion form renders as the static scene built at that key.
void registerInstantTest(Catalog &catalog, const KindInfo &k)
{
  const Kind kind = k.kind;
  auto t = makeTest(k.category, k.name);
  t.description(std::string("Checks that with an instant shutter at the "
                            "start, middle and end of `time`, ")
       + k.what + " renders as the static scene at the key for that time.")
      .build([kind](BuildContext &ctx) {
        return motionWorld(ctx, kind, instantSubject(ctx, kind));
      })
      .camera([kind](BuildContext &ctx, const scenes::Bounds &) {
        return motionCamera(ctx, kind, instantSubject(ctx, kind));
      })
      .permute("shutter", V{box1(0.f, 0.f), box1(0.5f, 0.5f), box1(1.f, 1.f)})
      .variant("subject", strings(k.forms));
  if (kind == Kind::InstanceSrt)
    t.requireFeatureFor("subject", Any("motionTransform"), kInstanceMotion);
  t.noGroundTruth()
      .covers(k.extension)
      .requireFeatures(plus(k.support, {kShutter}))
      .registerInto(catalog);
}

// The open shutter: wide open, the image differs from both instants (the
// shutter axis's effect checks) and the subject covers where it went
// (openShutterCoverage). `camera/shutter` is the instance one, with the
// spec's default shutter too.
void registerOpenShutterTest(Catalog &catalog,
    const KindInfo &k,
    const std::string &category,
    const std::string &name,
    bool withDefault)
{
  const Kind kind = k.kind;
  V shutters{box1(0.f, 0.f), box1(1.f, 1.f), box1(0.f, 1.f)};
  if (withDefault) {
    shutters.push_back(box1(0.5f, 0.5f));
    shutters.push_back(none());
  }
  auto t = makeTest(category, name);
  t.description(std::string("Checks that an open shutter blurs ") + k.what
       + " over where it was while the shutter was open, and an instant one "
         "shows it where it was at that time.")
      .build([kind](BuildContext &ctx) {
        return motionWorld(ctx, kind, openSubject(kind));
      })
      .camera([kind](BuildContext &ctx, const scenes::Bounds &) {
        return motionCamera(ctx, kind, openSubject(kind));
      })
      .permute("shutter", shutters)
      .expectExact("the subject covers where it was while the shutter was "
                   "open",
          openShutterCoverage);
  if (withDefault) {
    // The spec's default shutter is [0.5, 0.5].
    t.expectDerived("shutter",
        "none",
        "the shutter [0.5, 0.5]",
        [](const Siblings &s) -> ExpectedImage {
          return s.at(anyToString(box1(0.5f, 0.5f)));
        });
  }
  t.noGroundTruth();
  if (withDefault)
    t.covers(kShutter).requireFeatures(plus(k.support, {k.extension}));
  else
    t.covers(k.extension).requireFeatures(plus(k.support, {kShutter}));
  t.registerInto(catalog);
}

// Identical keys: motion that doesn't move renders as no motion, with the
// shutter open.
void registerIdenticalKeysTest(Catalog &catalog, const KindInfo &k)
{
  const Kind kind = k.kind;
  makeTest(k.category, std::string(k.name) + "_identical_keys")
      .description(std::string("Checks that ") + k.what
          + " with identical keys renders as the static scene, with the "
            "shutter open.")
      .build([kind](BuildContext &ctx) {
        return motionWorld(ctx, kind, identicalSubject(ctx, kind));
      })
      .camera([kind](BuildContext &ctx, const scenes::Bounds &) {
        return motionCamera(ctx, kind, identicalSubject(ctx, kind));
      })
      .permute("shutter", V{box1(0.f, 1.f)})
      .variant("subject", V{Any("static"), Any("identical_keys")})
      .expectExact("the subject is where its keys hold it",
          subjectAtIdenticalKeys)
      .noGroundTruth()
      .covers(k.extension)
      .requireFeatures(plus(k.support, {kShutter}))
      .registerInto(catalog);
}

// The rolling shutter: a bar moving across the frame, scanned along its
// motion's perpendicular (rows: down and up; columns: right and left).
void registerRollingShutterTest(Catalog &catalog, bool rows)
{
  const V directions = rows ? V{Any("down"), Any("up"), Any("none")}
                            : V{Any("right"), Any("left"), Any("none")};
  makeTest("camera", rows ? "rolling_shutter_rows" : "rolling_shutter_columns")
      .description(std::string("Checks that a rolling shutter scanning ")
          + (rows ? "rows (down and up)" : "columns (right and left)")
          + " shows a bar moving across the scan where it was as each line "
            "was open, sheared one way or the other by the direction.")
      .build([rows](BuildContext &ctx) {
        return motionWorld(ctx, Kind::InstanceMotion, rollingSubject(rows));
      })
      .camera([rows](BuildContext &ctx, const scenes::Bounds &) {
        return motionCamera(ctx, Kind::InstanceMotion, rollingSubject(rows));
      })
      .simplified()
      .permute("rollingShutterDirection", directions)
      .permute("rollingShutterDuration", V{Any(0.f), Any(0.5f)})
      .permute("shutter", V{box1(0.f, 1.f)})
      .expectExact("the bar is where the scan saw it",
          [rows](ExactCheck &x) { rollingShear(x, rows); })
      .noGroundTruth()
      .covers(kRollingShutter)
      .requireFeatures(plus(kSupportMatteTriangle, {kShutter, kInstanceMotion}))
      .registerInto(catalog);
}

} // namespace

void registerMotionTests(Catalog &catalog)
{
  const std::vector<std::string> quadSupport = {"ANARI_KHR_GEOMETRY_QUAD",
      "ANARI_KHR_MATERIAL_MATTE",
      kSupportPerspective};
  const std::vector<KindInfo> kinds = {
      {Kind::InstanceMotion,
          "instance",
          "motion_transform",
          kInstanceMotion,
          "a motionTransform instance",
          {"static", "motion", "motion_time", "motion_over_transform"},
          kSupportMatteTriangle},
      {Kind::InstanceSrt,
          "instance",
          "motion_scale_rotation_translation",
          kInstanceSrt,
          "a motionScaleRotationTranslation instance",
          {"static",
              "motion",
              "motion_time",
              "motion_over_transform",
              "motionTransform"},
          kSupportMatteTriangle},
      {Kind::Triangle,
          "geometry",
          "triangle_motion_deformation",
          kTriangleMotion,
          "a triangle geometry deformed by motion.vertex.*",
          {"static", "motion", "motion_time"},
          kSupportMatteTriangle},
      {Kind::Quad,
          "geometry",
          "quad_motion_deformation",
          kQuadMotion,
          "a quad geometry deformed by motion.vertex.*",
          {"static", "motion", "motion_time"},
          quadSupport},
      {Kind::Camera,
          "camera",
          "motion_transformation",
          kCameraMotion,
          "a camera moved by motion.* keys",
          {"static", "motion", "motion_time", "srt", "transform_over_srt"},
          kSupportMatteTriangle},
  };
  for (const auto &k : kinds) {
    registerInstantTest(catalog, k);
    registerIdenticalKeysTest(catalog, k);
    if (k.kind == Kind::InstanceMotion)
      registerOpenShutterTest(catalog, k, "camera", "shutter", true);
    else
      registerOpenShutterTest(
          catalog, k, k.category, std::string(k.name) + "_open_shutter", false);
  }
  registerRollingShutterTest(catalog, true);
  registerRollingShutterTest(catalog, false);
}

} // namespace cts
} // namespace anari
