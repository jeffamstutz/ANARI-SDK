// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Attribute and element-type plumbing: geometry attributes (1.2, section 5.9):
// which slot of a geometry a material's `color` or `attributeN` reads, in which
// order the slots win, how face-varying data differs from vertex data, and
// which element types a colour and an index array may have. Each Test renders
// the same colours through different routes and asks the Candidate to agree
// with itself (ADR-0010): the slot, the index width and the colour type are
// Variants, the attribute read is a Permutation (each attribute holds different
// colours, so a device that reads the wrong one shows it).

#include "../AnariObject.h"
#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../FrameReadback.h"
#include "../GeometryBuilder.h"
#include "../GeometryLayout.h"
#include "../LightBuilder.h"
#include "../Metrics.h"
#include "../Relations.h"
#include "../ScreenQuad.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../VolumeBuilder.h"
#include "../WorldBuilder.h"
#include "AttributeColors.h"
#include "Categories.h"
#include "RawArray.h"
#include "ScreenScene.h"
#include "SubjectShown.h"

#include "generators/ColorPalette.h"
// std
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

constexpr int kPrimitives = 4;

const std::vector<std::string> &kAttributes = attributeNames();

V attributeValues()
{
  V out;
  for (const auto &a : kAttributes)
    out.emplace_back(a.c_str());
  return out;
}

// A geometry subtype and how its primitives own vertices.
struct Family
{
  const char *name; // test-name prefix and ANARI subtype
  const char *extension;
  int verticesPerPrimitive;
  int corners; // face-varying values per primitive; 0 when there are none
  // form: 0 soup, 1 indexed (32-bit indices), 2 indexed (64-bit)
  std::function<anari::Geometry(anari::Device, int form)> build;
};

// Every family lays its primitives out in a grid, four of them, each owning
// `verticesPerPrimitive` consecutive vertices (the curve is indexed, so that
// its primitives are the segments of those vertex pairs).
const std::vector<Family> &families()
{
  static const std::vector<Family> list = {
      {"triangle",
          "ANARI_KHR_GEOMETRY_TRIANGLE",
          3,
          3,
          [](anari::Device d, int form) {
            TriangleSpec s;
            s.primitiveCount = kPrimitives;
            s.wideIndices = form == 2;
            if (form)
              s.mode = PrimitiveMode::Indexed;
            return buildTriangleGeometry(d, s);
          }},
      {"quad",
          "ANARI_KHR_GEOMETRY_QUAD",
          4,
          4,
          [](anari::Device d, int form) {
            QuadSpec s;
            s.primitiveCount = kPrimitives;
            s.wideIndices = form == 2;
            if (form)
              s.mode = PrimitiveMode::Indexed;
            return buildQuadGeometry(d, s);
          }},
      {"sphere",
          "ANARI_KHR_GEOMETRY_SPHERE",
          1,
          0,
          [](anari::Device d, int form) {
            SphereSpec s;
            s.primitiveCount = kPrimitives;
            s.wideIndices = form == 2;
            if (form)
              s.mode = PrimitiveMode::Indexed;
            return buildSphereGeometry(d, s);
          }},
      {"cone",
          "ANARI_KHR_GEOMETRY_CONE",
          2,
          0,
          [](anari::Device d, int form) {
            ConeSpec s;
            s.primitiveCount = kPrimitives;
            s.wideIndices = form == 2;
            if (form)
              s.mode = PrimitiveMode::Indexed;
            return buildConeGeometry(d, s);
          }},
      {"cylinder",
          "ANARI_KHR_GEOMETRY_CYLINDER",
          2,
          0,
          [](anari::Device d, int form) {
            CylinderSpec s;
            s.primitiveCount = kPrimitives;
            s.wideIndices = form == 2;
            if (form)
              s.mode = PrimitiveMode::Indexed;
            return buildCylinderGeometry(d, s);
          }},
      {"curve",
          "ANARI_KHR_GEOMETRY_CURVE",
          2,
          0,
          [](anari::Device d, int form) {
            CurveSpec s;
            s.primitiveCount = kPrimitives;
            s.mode = PrimitiveMode::Indexed;
            s.wideIndices = form == 2;
            return buildCurveGeometry(d, s);
          }},
  };
  return list;
}

// The slots, from the one that wins to the one that loses (1.2, sections 5.5.1
// and 5.9: instance uniform < geometry uniform < primitive < vertex <
// faceVarying).
const std::vector<std::string> kSlotOrder = {
    "faceVarying", "vertex", "primitive", "uniform"};

std::string slotParameter(const std::string &slot, const std::string &attribute)
{
  return slot == "uniform" ? attribute : slot + "." + attribute;
}

std::vector<std::string> split(const std::string &s, char sep)
{
  std::vector<std::string> out;
  std::stringstream in(s);
  for (std::string part; std::getline(in, part, sep);)
    out.push_back(part);
  return out;
}

// What a slot holds for one attribute: a colour per primitive, per vertex, per
// face corner, or the one uniform colour (that of primitive 0, so that a
// Case with every primitive alike can compare a uniform against a primitive).
using ColorOf = std::function<float4(int set, int primitive)>;

void setSlot(anari::Device d,
    anari::Geometry g,
    const Family &f,
    const std::string &slot,
    const std::string &attribute,
    int set,
    const ColorOf &colorOf,
    bool constant)
{
  const auto name = slotParameter(slot, attribute);
  auto colorAt = [&](int p) { return colorOf(set, constant ? 0 : p); };
  if (slot == "uniform") {
    anari::setParameter(d, g, name.c_str(), colorAt(0));
    return;
  }
  std::vector<float4> values;
  for (int p = 0; p < kPrimitives; ++p) {
    const int per = slot == "primitive" ? 1
        : slot == "vertex"              ? f.verticesPerPrimitive
                                        : f.corners;
    for (int k = 0; k < per; ++k)
      values.push_back(colorAt(p));
  }
  anari::setAndReleaseParameter(
      d, g, name.c_str(), anari::newArray1D(d, values.data(), values.size()));
}

// The world: the family's geometry carrying, for every attribute, the colour
// set of its index in each slot of `slots`; `winner` names the slot whose sets
// are the real ones (every other slot carries the decoys). The matte
// material reads the Case's `attribute`.
anari::World slotsWorld(BuildContext &ctx,
    const Family &f,
    const std::vector<std::string> &slots,
    bool constant,
    int form = 0)
{
  auto d = ctx.device();
  auto geom = f.build(d, form);
  for (const auto &slot : slots) {
    const bool wins = slot == slots.front();
    for (size_t k = 0; k < kAttributes.size(); ++k)
      setSlot(d,
          geom,
          f,
          slot,
          kAttributes[k],
          int(k),
          wins ? ColorOf(setColor) : ColorOf(decoyColor),
          constant);
  }
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", ctx.getString("attribute", "color"));
  anari::commitParameters(d, mat);
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f), 1.f);
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

// A frame of `world` at w x h, its colour channel read back.
FrameReadbackResult renderColor(anari::Device d,
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
  anari::commitParameters(d, f);
  anari::render(d, f);
  anari::wait(d, f);
  return readFrameChannel(d, f, Channel::Color, w, h);
}

// An image of `like`'s size holding only its first pixel's value, to measure
// how much of `like` differs from its background corner.
Image solidLike(const Image &like, const uint8_t *pixel)
{
  Image out = like;
  for (size_t i = 0; i + 3 < out.rgba.size(); i += 4)
    for (int c = 0; c < 4; ++c)
      out.rgba[i + c] = pixel[c];
  return out;
}

std::vector<std::string> routeSlots(const Family &f)
{
  std::vector<std::string> slots = {"primitive", "vertex"};
  if (f.corners)
    slots.push_back("faceVarying");
  return slots;
}

void registerSlotTests(Catalog &catalog, const Family &f)
{
  const std::string name = f.name;

  // The same colours reach the material through the primitive, vertex or
  // face-varying slot; each of the five attributes holds other colours.
  {
    V slots;
    for (const auto &s : routeSlots(f))
      slots.emplace_back(s.c_str());
    makeTest("geometry", name + "_attribute_slots")
        .description("Checks that the colours of the primitive, vertex"
                     + std::string(f.corners ? " and face-varying" : "")
                     + " slots reach a matte material's `color` as the "
                       "attribute it names, for `color` and each of "
                       "`attribute0` to `attribute3`.")
        .build([f](BuildContext &ctx) {
          return slotsWorld(ctx, f, {ctx.getString("slot", "primitive")}, false);
        })
        .variant("slot", slots)
        .permute("attribute", attributeValues())
        .channels({Channel::Color})
        .noGroundTruth()
        .covers(f.extension)
        .requireFeatures(kSupportMatte)
        .registerInto(catalog);
  }

  // A uniform attribute is every primitive's: with all primitives alike, it
  // looks like the primitive slot.
  makeTest("geometry", name + "_uniform_attributes")
      .description(
          "Checks that a geometry's uniform `color` and "
          "`attribute0` to `attribute3` give every primitive the "
          "colour a per-primitive array of that colour does.")
      .build([f](BuildContext &ctx) {
        return slotsWorld(ctx, f, {ctx.getString("slot", "uniform")}, true);
      })
      .variant("slot", V{"uniform", "primitive"})
      .permute("attribute", attributeValues())
      .channels({Channel::Color})
      .noGroundTruth()
      .covers(f.extension)
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // primitive.index may hold 32- or 64-bit elements. A device that rejects
  // the wide form may take the process down with it, so the Test is a timed
  // behaviour Test, each Case in its own process (ADR-0009), and compares the
  // two renders itself.
  makeTest("geometry", name + "_index_types")
      .description(
          "Checks that an indexed geometry whose primitive.index has "
          "UINT64 elements renders as one whose index has UINT32 "
          "elements.")
      .build([f](BuildContext &ctx) {
        return slotsWorld(ctx, f, {"primitive"}, false, 1);
      })
      .behavior([f](anari::Device d,
                    anari::World narrow,
                    anari::Camera camera,
                    anari::Renderer renderer,
                    uint32_t w,
                    uint32_t h) {
        BuildContext ctx(d);
        UniqueAnariObject<anari::World> wide(
            d, slotsWorld(ctx, f, {"primitive"}, false, 2));
        const auto a = renderColor(d, narrow, camera, renderer, w, h);
        const auto b = renderColor(d, wide.get(), camera, renderer, w, h);
        if (!a || !b)
          return BehaviorResult{false,
              "a frame could not be read back: " + (a ? b.detail : a.detail)};
        const SelfCheckDefaults limits;
        const double shown = changedFraction(
            a.image, solidLike(a.image, a.image.rgba.data()), limits.tau);
        const double changed = changedFraction(a.image, b.image, limits.tau);
        std::ostringstream text;
        text.setf(std::ios::fixed);
        text.precision(2);
        if (shown < limits.effectMin) {
          text << "the geometry with UINT32 indices shows in only "
               << 100.0 * shown << " % of the frame";
          return BehaviorResult{false, text.str()};
        }
        text << "UINT64 indices changed " << 100.0 * changed
             << " % of the color pixels against UINT32 indices; allowed "
             << 100.0 * limits.equivalenceMax << " %";
        return BehaviorResult{changed <= limits.equivalenceMax, text.str()};
      })
      .timeout(std::chrono::seconds(30))
      .failOnDeviceErrors()
      .covers(f.extension)
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);

  // When two slots carry an attribute the higher one wins, whatever the lower
  // ones hold: every combination must look like the primitive slot alone.
  V combinations = {"primitive",
      "primitive+uniform",
      "vertex",
      "vertex+uniform",
      "vertex+primitive",
      "vertex+primitive+uniform"};
  if (f.corners)
    for (const char *c : {"faceVarying",
             "faceVarying+uniform",
             "faceVarying+primitive",
             "faceVarying+vertex",
             "faceVarying+vertex+primitive+uniform"})
      combinations.emplace_back(c);
  makeTest("geometry", name + "_attribute_precedence")
      .description(
          "Checks that where a geometry carries an attribute in "
          "several slots, the higher slot (face-varying, vertex, "
          "primitive, uniform) is the one a material reads.")
      .build([f](BuildContext &ctx) {
        return slotsWorld(
            ctx, f, split(ctx.getString("slots", "primitive"), '+'), false);
      })
      .variant("slots", combinations)
      .permute("attribute", V{"color", "attribute3"})
      .channels({Channel::Color})
      .noGroundTruth()
      .covers(f.extension)
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);
}

// ---- face-varying normals and tangents ------------------------------------
//
// A cube whose normals point away from its centre (a smooth shading a flat
// cube's own normals can't give), given as per-vertex normals of a soup, or
// as face-varying normals of the indexed cube (whose eight shared vertices
// can't carry a normal per face corner), or as face-varying normals over
// decoy vertex normals: all three shade alike.

float3 away(const float3 &p, const float3 &center)
{
  const auto v = p - center;
  return v / anari::math::length(v);
}

anari::World faceVaryingNormalsWorld(
    BuildContext &ctx, bool quads, const std::string &form)
{
  auto d = ctx.device();
  const bool soup = form == "soup";
  const int corners = quads ? 4 : 3;

  std::vector<float3> positions; // as a soup, one entry per corner
  std::vector<float3> unique; // the indexed cube's vertices
  std::vector<uint32_t> indices; // the indexed cube's corners
  if (quads) {
    const auto s = makeQuadLayout(QuadShape::Cube, PrimitiveMode::Soup, 1);
    const auto i = makeQuadLayout(QuadShape::Cube, PrimitiveMode::Indexed, 1);
    positions = s.positions;
    unique = i.positions;
    for (const auto &q : i.indices)
      indices.insert(indices.end(), {q.x, q.y, q.z, q.w});
  } else {
    const auto s =
        makeTriangleLayout(TriangleShape::Cube, PrimitiveMode::Soup, 1);
    const auto i =
        makeTriangleLayout(TriangleShape::Cube, PrimitiveMode::Indexed, 1);
    positions = s.positions;
    unique = i.positions;
    for (const auto &t : i.indices)
      indices.insert(indices.end(), {t.x, t.y, t.z});
  }
  float3 center(0.f);
  for (const auto &p : unique)
    center = center + p;
  center = center / float(unique.size());

  auto geom = anari::newObject<anari::Geometry>(d, quads ? "quad" : "triangle");
  auto publish = [&](const char *name, const std::vector<float3> &v) {
    anari::setAndReleaseParameter(
        d, geom, name, anari::newArray1D(d, v.data(), v.size()));
  };
  std::vector<float3> cornerNormals;
  for (uint32_t i : indices)
    cornerNormals.push_back(away(unique[i], center));

  if (soup) {
    publish("vertex.position", positions);
    std::vector<float3> n;
    for (const auto &p : positions)
      n.push_back(away(p, center));
    publish("vertex.normal", n);
  } else {
    publish("vertex.position", unique);
    // The index array's elements are 3 or 4 wide.
    if (quads) {
      std::vector<uint32_t> flat = indices;
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          makeRawArray1D(d, ANARI_UINT32_VEC4, flat.size() / 4, flat.data()));
    } else {
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          makeRawArray1D(
              d, ANARI_UINT32_VEC3, indices.size() / 3, indices.data()));
    }
    if (form != "flat")
      publish("faceVarying.normal", cornerNormals);
    if (form == "faceVaryingOverVertex") {
      // Vertex normals that would shade the cube dark, to be ignored.
      publish("vertex.normal",
          std::vector<float3>(unique.size(), float3(0.f, 0.f, -1.f)));
    }
    if (form == "faceVaryingTangents") {
      // The matte material has no use for tangents; a device must still
      // accept and ignore them.
      std::vector<float3> t(cornerNormals.size(), float3(1.f, 0.f, 0.f));
      publish("faceVarying.tangent", t);
    }
  }
  (void)corners;
  anari::commitParameters(d, geom);

  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(-0.4f, -1.f, -0.6f), 1.f);
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

void registerFaceVaryingNormals(Catalog &catalog, bool quads)
{
  const std::string name = quads ? "quad" : "triangle";
  auto test = makeTest("geometry", name + "_face_varying_normals");
  test.description(
          "Checks that face-varying normals shade an indexed cube "
          "as the same per-corner normals as vertex normals of the "
          "cube as a soup, that they take precedence over vertex "
          "normals, that face-varying tangents are accepted, and "
          "that the normals shade it differently from none.")
      .build([quads](BuildContext &ctx) {
        return faceVaryingNormalsWorld(ctx, quads, ctx.getString("form", ""));
      })
      // `soup` is the reference; `flat` has no normals (so the device shades
      // from the cube's own flat faces), which the smooth ones must change.
      .permute("form",
          V{"soup",
              "faceVarying",
              "faceVaryingOverVertex",
              "faceVaryingTangents",
              "flat"});
  for (const char *form :
      {"faceVarying", "faceVaryingOverVertex", "faceVaryingTangents"})
    test.expectDerived("form",
        form,
        "the soup cube with vertex normals",
        [](const Siblings &s) -> ExpectedImage { return s.at("soup"); });
  test.channels({Channel::Color})
      .expectExact("the cube shows", subjectIsShown)
      .noGroundTruth()
      .covers(quads ? "ANARI_KHR_GEOMETRY_QUAD" : "ANARI_KHR_GEOMETRY_TRIANGLE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);
}

// ---- colour element types ---------------------------------------------------
//
// The colour types a geometry's colour arrays must accept (1.2, section
// 3.6.1), written with the same colours: 8-bit-exact values, so every
// fixed-point type holds them exactly and the sRGB types hold their encoding.
// The one- and two-component types are left out: the spec doesn't say which
// colour they stand for.

struct ColorType
{
  const char *name;
  ANARIDataType type;
  int components;
  enum Kind
  {
    Float32,
    Fixed8,
    Fixed16,
    Fixed32,
    Srgb8
  } kind;
};

const std::vector<ColorType> &colorTypes()
{
  static const std::vector<ColorType> list = {
      {"float32_vec4", ANARI_FLOAT32_VEC4, 4, ColorType::Float32},
      {"float32_vec3", ANARI_FLOAT32_VEC3, 3, ColorType::Float32},
      {"ufixed8_vec4", ANARI_UFIXED8_VEC4, 4, ColorType::Fixed8},
      {"ufixed8_vec3", ANARI_UFIXED8_VEC3, 3, ColorType::Fixed8},
      {"ufixed16_vec4", ANARI_UFIXED16_VEC4, 4, ColorType::Fixed16},
      {"ufixed16_vec3", ANARI_UFIXED16_VEC3, 3, ColorType::Fixed16},
      {"ufixed32_vec4", ANARI_UFIXED32_VEC4, 4, ColorType::Fixed32},
      {"ufixed32_vec3", ANARI_UFIXED32_VEC3, 3, ColorType::Fixed32},
      {"ufixed8_rgba_srgb", ANARI_UFIXED8_RGBA_SRGB, 4, ColorType::Srgb8},
      {"ufixed8_rgb_srgb", ANARI_UFIXED8_RGB_SRGB, 3, ColorType::Srgb8},
  };
  return list;
}

float srgbEncode(float v)
{
  return v <= 0.0031308f ? 12.92f * v
                         : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

// `colors` (components in [0, 1]) as an array of `t`.
anari::Array1D colorArray(
    anari::Device d, const ColorType &t, const std::vector<float4> &colors)
{
  std::vector<uint8_t> bytes;
  auto put = [&](const void *p, size_t n) {
    const auto *b = static_cast<const uint8_t *>(p);
    bytes.insert(bytes.end(), b, b + n);
  };
  for (const auto &c : colors) {
    const float v[4] = {c.x, c.y, c.z, c.w};
    for (int k = 0; k < t.components; ++k) {
      switch (t.kind) {
      case ColorType::Float32:
        put(&v[k], sizeof(float));
        break;
      case ColorType::Fixed8: {
        const uint8_t x = uint8_t(std::lround(v[k] * 255.f));
        put(&x, sizeof(x));
        break;
      }
      case ColorType::Fixed16: {
        const uint16_t x = uint16_t(std::lround(v[k] * 65535.f));
        put(&x, sizeof(x));
        break;
      }
      case ColorType::Fixed32: {
        const uint32_t x = uint32_t(std::llround(double(v[k]) * 4294967295.0));
        put(&x, sizeof(x));
        break;
      }
      case ColorType::Srgb8: {
        // alpha stays linear
        const float e = k < 3 ? srgbEncode(v[k]) : v[k];
        const uint8_t x = uint8_t(std::lround(e * 255.f));
        put(&x, sizeof(x));
        break;
      }
      }
    }
  }
  return makeRawArray1D(d, t.type, colors.size(), bytes.data());
}

// The palette's colours snapped to the 8-bit grid.
float4 gridColor(int p)
{
  const auto c = scenes::colors::getColorFromPalette(p);
  auto snap = [](float x) { return std::round(x * 255.f) / 255.f; };
  return float4(snap(c.x), snap(c.y), snap(c.z), 1.f);
}

void registerColorTypes(Catalog &catalog)
{
  V types;
  for (const auto &t : colorTypes())
    types.emplace_back(t.name);
  makeTest("geometry", "triangle_color_types")
      .description(
          "Checks that colour arrays of each fixed-point, sRGB and "
          "floating-point colour type (three and four components) "
          "give a triangle geometry the colours the float array does.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        const std::string name = ctx.getString("colorType", "float32_vec4");
        const ColorType *type = nullptr;
        for (const auto &t : colorTypes())
          if (name == t.name)
            type = &t;
        if (!type)
          throw std::invalid_argument("unknown colour type " + name);
        const bool vertex = ctx.getString("slot", "primitive") == "vertex";

        TriangleSpec spec;
        spec.primitiveCount = kPrimitives;
        auto geom = buildTriangleGeometry(d, spec);
        std::vector<float4> colors;
        for (int p = 0; p < kPrimitives; ++p)
          for (int k = 0; k < (vertex ? 3 : 1); ++k)
            colors.push_back(gridColor(p));
        anari::setAndReleaseParameter(d,
            geom,
            vertex ? "vertex.color" : "primitive.color",
            colorArray(d, *type, colors));
        anari::commitParameters(d, geom);

        auto mat = anari::newObject<anari::Material>(d, "matte");
        anari::setParameter(d, mat, "color", "color");
        anari::commitParameters(d, mat);
        auto surface = makeSurface(d, geom, mat);
        auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f), 1.f);
        WorldContents wc;
        wc.surfaces = {surface};
        wc.lights = {light};
        auto world = assembleWorld(d, wc);
        anari::release(d, geom);
        anari::release(d, mat);
        anari::release(d, surface);
        anari::release(d, light);
        return world;
      })
      .variant("colorType", types)
      .variant("slot", V{"primitive", "vertex"})
      .channels({Channel::Color})
      .expectExact("the triangles show", subjectIsShown)
      .noGroundTruth()
      .covers("ANARI_KHR_GEOMETRY_TRIANGLE")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);
}

// ---- surface visibility -----------------------------------------------------
//
// `visible` false removes a surface from the picture: a world whose left
// triangle has visible false renders as one without it (`absent`), and a
// surface is visible unless said otherwise. The right triangle is always
// there, so a device can't pass by drawing nothing.

void registerSurfaceVisible(Catalog &catalog)
{
  makeTest("geometry", "surface_visible")
      .description(
          "Checks that a surface with visible false renders as a "
          "world without it, and that a surface is visible by "
          "default.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        const std::string visible = ctx.getString("visible", "none");
        auto triangleSurface = [&](float sx) {
          const auto soup = triangleSoup({screenTriangle(sx, 0.5f, 0.25f)});
          auto geom = anari::newObject<anari::Geometry>(d, "triangle");
          anari::setAndReleaseParameter(d,
              geom,
              "vertex.position",
              anari::newArray1D(d, soup.data(), soup.size()));
          anari::commitParameters(d, geom);
          auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
          auto surface = makeSurface(d, geom, mat);
          anari::release(d, geom);
          anari::release(d, mat);
          return surface;
        };
        auto always = triangleSurface(0.75f);
        WorldContents wc;
        wc.surfaces = {always};
        anari::Surface toggled = nullptr;
        if (visible != "absent") {
          toggled = triangleSurface(0.25f);
          if (visible == "true" || visible == "false")
            anari::setParameter(d, toggled, "visible", visible == "true");
          anari::commitParameters(d, toggled);
          wc.surfaces.push_back(toggled);
        }
        auto light = makeDirectionalLight(d, float3(0.f, 0.f, -1.f), 1.5f);
        wc.lights = {light};
        auto world = assembleWorld(d, wc);
        anari::release(d, light);
        anari::release(d, always);
        if (toggled)
          anari::release(d, toggled);
        return world;
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .permute("visible", V{"absent", "true", "false", "none"})
      .expectDerived("visible",
          "false",
          "the world without the surface",
          [](const Siblings &s) -> ExpectedImage { return s.at("absent"); })
      .expectDerived("visible",
          "none",
          "the visible surface: a surface is visible by default",
          [](const Siblings &s) -> ExpectedImage { return s.at("true"); })
      .channels({Channel::Color})
      .expectExact("the triangle that is always there shows", subjectIsShown)
      .noGroundTruth()
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
}

// ---- image sampler element types
// ----------------------------------------------
//
// The same texels in each colour type an image sampler must accept (1.2,
// section 3.6.1): a 4 x 4 image of 8-bit-exact colours, sampled with the
// nearest filter at screen positions, shows the same picture through any of
// them.

void registerSamplerElementTypes(Catalog &catalog)
{
  V types;
  for (const auto &t : colorTypes())
    types.emplace_back(t.name);
  makeTest("sampler", "image2D_element_types")
      .description(
          "Checks that an image2D sampler's image of each "
          "fixed-point, sRGB and floating-point colour type (three "
          "and four components) shows the picture the float image "
          "does.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        const std::string name = ctx.getString("elementType", "float32_vec4");
        const ColorType *type = nullptr;
        for (const auto &t : colorTypes())
          if (name == t.name)
            type = &t;
        if (!type)
          throw std::invalid_argument("unknown colour type " + name);

        constexpr int kTexels = 4;
        std::vector<float4> texels;
        for (int i = 0; i < kTexels * kTexels; ++i)
          texels.push_back(gridColor(i % 12));
        auto sampler = anari::newObject<anari::Sampler>(d, "image2D");
        auto image = colorArray(d, *type, texels);
        // The array is 2D: make it so from the same bytes.
        void *src = anariMapArray(d, image);
        auto image2D = makeRawArray2D(d, type->type, kTexels, kTexels, src);
        anariUnmapArray(d, image);
        anari::release(d, image);
        anari::setAndReleaseParameter(d, sampler, "image", image2D);
        anari::setParameter(d, sampler, "inAttribute", "attribute1");
        anari::setParameter(d, sampler, "filter", "nearest");
        anari::commitParameters(d, sampler);

        auto geom = makeScreenQuad(d, {{"attribute1", 2, [](float2 s) {
                                          return float4(s.x, s.y, 0.f, 0.f);
                                        }}});
        auto mat = anari::newObject<anari::Material>(d, "matte");
        anari::setAndReleaseParameter(d, mat, "color", sampler);
        anari::commitParameters(d, mat);
        auto surface = makeSurface(d, geom, mat);
        auto world = makeScreenQuadWorld(d, surface);
        anari::release(d, geom);
        anari::release(d, mat);
        anari::release(d, surface);
        return world;
      })
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return makeScreenQuadCamera(ctx.device());
      })
      .variant("elementType", types)
      .channels({Channel::Color})
      .expectExact("the image shows", subjectIsShown)
      .noGroundTruth()
      .covers("ANARI_KHR_SAMPLER_IMAGE2D")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
}

// ---- isosurface attributes
// ------------------------------------------------------
//
// An isosurface's primitives are its surfaces, one per isovalue, so it has
// uniform and per-primitive attributes (no vertices to carry others). With
// every surface alike, a uniform colour looks like a per-primitive array of
// it.

void registerIsosurfaceAttributes(Catalog &catalog)
{
  static const Family family = {"isosurface",
      "ANARI_KHR_GEOMETRY_ISOSURFACE",
      1,
      0,
      [](anari::Device d, int) {
        auto field = makeStructuredRegularField(d, {16, 16, 16});
        auto geom = anari::newObject<anari::Geometry>(d, "isosurface");
        anari::setParameter(d, geom, "field", field);
        const std::vector<float> iso = {0.25f, 0.5f, 0.75f};
        anari::setAndReleaseParameter(
            d, geom, "isovalue", anari::newArray1D(d, iso.data(), iso.size()));
        anari::commitParameters(d, geom);
        anari::release(d, field);
        return geom;
      }};
  makeTest("geometry", "isosurface_uniform_attributes")
      .description(
          "Checks that an isosurface geometry's uniform `color` and "
          "`attribute0` to `attribute3` give every surface the "
          "colour a per-primitive array of that colour does.")
      .build([](BuildContext &ctx) {
        return slotsWorld(
            ctx, family, {ctx.getString("slot", "uniform")}, true);
      })
      .variant("slot", V{"uniform", "primitive"})
      .permute("attribute", attributeValues())
      .channels({Channel::Color})
      .expectExact("the isosurface shows", subjectIsShown)
      .noGroundTruth()
      .covers(family.extension)
      .requireFeatures(kSupportMatte)
      .requireFeature("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
      .registerInto(catalog);
}

} // namespace

void registerAttributeTests(Catalog &catalog)
{
  for (const auto &f : families())
    registerSlotTests(catalog, f);
  registerFaceVaryingNormals(catalog, false);
  registerFaceVaryingNormals(catalog, true);
  registerColorTypes(catalog);
  registerSurfaceVisible(catalog);
  registerSamplerElementTypes(catalog);
  registerIsosurfaceAttributes(catalog);
}

} // namespace cts
} // namespace anari
