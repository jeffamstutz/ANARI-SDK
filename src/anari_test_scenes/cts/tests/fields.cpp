// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The spatial field Tests (ticket 11; ADR-0010 55-60), registered in the
// volume category: one field sampled into different representations
// (structuredRegular; unstructured hexahedra, tetrahedra and pyramids;
// nanovdb; data element types; the cubic filter) renders the same volume, and
// a step transfer function makes a field's extent and value mapping analytic.

#include "fields.h"
#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "NanoVdbGrid.h"
#include "RawArray.h"
#include "SubjectShown.h"
#include "VolumeScene.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace anari {
namespace cts {

using anari::math::float2;
using anari::math::float3;
using namespace volume_scene;

namespace fields {

// ---- unstructured cells ----

namespace {

float3 cornerOffset(int corner)
{
  // VTK's hexahedron order: the bottom face (z = 0) counterclockwise seen
  // from the top, then the top face the same way.
  static const float3 kCorners[8] = {{0, 0, 0},
      {1, 0, 0},
      {1, 1, 0},
      {0, 1, 0},
      {0, 0, 1},
      {1, 0, 1},
      {1, 1, 1},
      {0, 1, 1}};
  return kCorners[corner];
}

float det(float3 a, float3 b, float3 c)
{
  return math::dot(a, math::cross(b, c));
}

} // namespace

UnstructuredMesh unstructuredMesh(CellLayout layout)
{
  // The mix: each block in turn of the three kinds.
  static const CellLayout kMixedCycle[3] = {
      CellLayout::Hexahedra, CellLayout::Tetrahedra, CellLayout::Pyramids};
  UnstructuredMesh m;
  // A 3 x 3 x 3 lattice over [-1, 1]^3: 2 x 2 x 2 hexahedral blocks, each
  // made of cells of the layout's type.
  auto lattice = [](int i, int j, int k) {
    return uint32_t(i + 3 * j + 9 * k);
  };
  for (int k = 0; k < 3; ++k)
    for (int j = 0; j < 3; ++j)
      for (int i = 0; i < 3; ++i)
        m.positions.push_back(float3(-1.f + i, -1.f + j, -1.f + k));

  auto addCell = [&](uint8_t type, std::vector<uint32_t> ids) {
    m.cellIndex.push_back(uint32_t(m.index.size()));
    m.cellType.push_back(type);
    m.index.insert(m.index.end(), ids.begin(), ids.end());
  };

  int block = 0;
  for (int k = 0; k < 2; ++k)
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i, ++block) {
        uint32_t c[8];
        float3 p[8];
        for (int n = 0; n < 8; ++n) {
          const float3 o = cornerOffset(n);
          c[n] = lattice(i + int(o.x), j + int(o.y), k + int(o.z));
          p[n] = m.positions[c[n]];
        }
        CellLayout cells = layout;
        if (layout == CellLayout::Mixed)
          cells = kMixedCycle[block % 3];

        switch (cells) {
        case CellLayout::Hexahedra:
          addCell(
              kHexahedron, {c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]});
          break;
        case CellLayout::Tetrahedra: {
          // Six tetrahedra around the diagonal from corner 0 to corner 6,
          // one per order of the axes; each with its bottom triangle
          // counterclockwise seen from its top vertex.
          static const int kPaths[6][2] = {
              {1, 2}, {1, 5}, {3, 2}, {3, 7}, {4, 5}, {4, 7}};
          for (const auto &path : kPaths) {
            uint32_t a = c[path[0]], b = c[path[1]];
            float3 pa = p[path[0]], pb = p[path[1]];
            if (det(pa - p[0], pb - p[0], p[6] - p[0]) < 0.f) {
              std::swap(a, b);
              std::swap(pa, pb);
            }
            addCell(kTetrahedron, {c[0], a, b, c[6]});
          }
          break;
        }
        case CellLayout::Pyramids: {
          // Six pyramids, one per face, meeting at a vertex in the block's
          // centre; each base counterclockwise seen from the apex.
          const float3 centre =
              0.125f * (p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7]);
          const uint32_t apex = uint32_t(m.positions.size());
          m.positions.push_back(centre);
          static const int kFaces[6][4] = {{0, 1, 2, 3},
              {4, 5, 6, 7},
              {0, 1, 5, 4},
              {3, 2, 6, 7},
              {0, 3, 7, 4},
              {1, 2, 6, 5}};
          for (const auto &f : kFaces) {
            const float3 n = math::cross(p[f[1]] - p[f[0]], p[f[2]] - p[f[0]]);
            if (math::dot(n, centre - p[f[0]]) > 0.f)
              addCell(kPyramid, {c[f[0]], c[f[1]], c[f[2]], c[f[3]], apex});
            else
              addCell(kPyramid, {c[f[0]], c[f[3]], c[f[2]], c[f[1]], apex});
          }
          break;
        }
        default:
          break;
        }
      }
  return m;
}

anari::SpatialField newUnstructuredField(anari::Device d,
    const UnstructuredMesh &m,
    const std::vector<float> *vertexData,
    const std::vector<float> *cellData)
{
  auto field = anari::newObject<anari::SpatialField>(d, "unstructured");
  anari::setParameterArray1D(
      d, field, "vertex.position", m.positions.data(), m.positions.size());
  anari::setParameterArray1D(d, field, "index", m.index.data(), m.index.size());
  anari::setParameterArray1D(
      d, field, "cell.index", m.cellIndex.data(), m.cellIndex.size());
  anari::setParameterArray1D(
      d, field, "cell.type", m.cellType.data(), m.cellType.size());
  if (vertexData)
    anari::setParameterArray1D(
        d, field, "vertex.data", vertexData->data(), vertexData->size());
  if (cellData)
    anari::setParameterArray1D(
        d, field, "cell.data", cellData->data(), cellData->size());
  return field;
}

// ---- nanovdb ----

anari::SpatialField newNanoVdbField(
    anari::Device d, const NanoVdbGridSpec &spec, const char *filter)
{
  const auto bytes = makeNanoVdbFloatGrid(spec);
  auto field = anari::newObject<anari::SpatialField>(d, "nanovdb");
  anari::setAndReleaseParameter(d,
      field,
      "data",
      makeRawArray1D(d, ANARI_UINT8, bytes.size(), bytes.data()));
  if (filter)
    anari::setParameter(d, field, "filter", filter);
  return field;
}

NanoVdbGridSpec rampGrid(ViewAxis view)
{
  NanoVdbGridSpec spec;
  spec.voxels = {kRampVoxels, kRampVoxels, kRampVoxels};
  spec.voxelSize = kRampVoxelSize;
  spec.origin = float3(-1.f);
  // Each voxel holds the ramp at the centre of the box NanoVDB's world
  // bounds give it ([ijk, ijk + 1] in index space; visrtx samples there by
  // default). NanoVDB's own samplers read a value at its index point, the
  // box's corner: a device doing so shifts
  // the ramp by half a voxel (ADR-0010 57).
  spec.value = [view](uint32_t i, uint32_t j, uint32_t k) {
    const float h = kRampVoxelSize;
    return rampValue(view,
        float3(-1.f + (i + 0.5f) * h,
            -1.f + (j + 0.5f) * h,
            -1.f + (k + 0.5f) * h));
  };
  return spec;
}

// ---- structuredRegular ----

anari::SpatialField newRampStructuredField(
    anari::Device d, ViewAxis view, uint32_t n, const char *filter)
{
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  const float spacing = 2.f / float(n - 1);
  std::vector<float> data;
  data.reserve(size_t(n) * n * n);
  for (uint32_t k = 0; k < n; ++k)
    for (uint32_t j = 0; j < n; ++j)
      for (uint32_t i = 0; i < n; ++i)
        data.push_back(rampValue(view,
            float3(
                -1.f + i * spacing, -1.f + j * spacing, -1.f + k * spacing)));
  anari::setParameter(d, field, "origin", float3(-1.f));
  anari::setParameter(d, field, "spacing", float3(spacing));
  anari::setParameterArray3D(d, field, "data", data.data(), n, n, n);
  if (filter)
    anari::setParameter(d, field, "filter", filter);
  return field;
}

float zigzagValue(uint32_t a, uint32_t b)
{
  static const float kA[4] = {0.f, 1.f, 0.2f, 0.9f};
  static const float kB[4] = {0.8f, 0.1f, 0.6f, 0.f};
  return 0.6f * kA[a % 4] + 0.4f * kB[b % 4];
}

CameraProjection sphereCamera()
{
  CameraProjection c;
  c.type = CameraProjection::Type::Perspective;
  c.position = float3(0.f, 0.f, 3.f);
  c.direction = float3(0.f, 0.f, -1.f);
  c.up = float3(0.f, 1.f, 0.f);
  return c;
}

SphereRay sphereRay(const CameraProjection &camera,
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h,
    float radius)
{
  const float3 d = math::normalize(camera.direction);
  const float3 r = math::normalize(math::cross(d, camera.up));
  const float3 u = math::cross(r, d);
  const float t = std::tan(camera.fovy / 2.f);
  const float aspect = float(w) / float(h);
  const float sx = ((x + 0.5f) / w * 2.f - 1.f) * t * aspect;
  const float sy = ((y + 0.5f) / h * 2.f - 1.f) * t;
  const float3 dir = math::normalize(d + sx * r + sy * u);
  const float3 o = camera.position;
  const float b = math::dot(o, dir);
  const float c = math::dot(o, o) - b * b; // squared closest distance
  SphereRay ray;
  ray.closest = std::sqrt(std::max(0.f, c));
  if (ray.closest < radius)
    ray.hit = -b - std::sqrt(radius * radius - c);
  return ray;
}

} // namespace fields

namespace {

using namespace fields;
using V = std::vector<Any>;

// The volume the equivalences render a field with (ADR-0010's framing):
// a blue-green-red colour map over [0, 1], opaque at once.
anari::Volume fieldVolume(anari::Device d, anari::SpatialField field)
{
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setParameter(d, vol, "value", field);
  std::vector<float3> colors = {
      {0.1f, 0.2f, 0.9f}, {0.2f, 0.9f, 0.2f}, {0.9f, 0.2f, 0.1f}};
  anari::setAndReleaseParameter(
      d, vol, "color", anari::newArray1D(d, colors.data(), colors.size()));
  anari::setParameter(d, vol, "opacity", 1.f);
  anari::setParameter(d, vol, "unitDistance", 0.001f);
  const float range[2] = {0.f, 1.f};
  anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
  anari::commitParameters(d, vol);
  return vol;
}

// A world holding `field` (committed here and released) as fieldVolume().
anari::World fieldWorld(anari::Device d, anari::SpatialField field)
{
  anari::commitParameters(d, field);
  auto vol = fieldVolume(d, field);
  WorldContents wc;
  wc.volumes = {vol};
  auto world = assembleWorld(d, wc);
  anari::release(d, field);
  anari::release(d, vol);
  return world;
}

// The cells a field axis value of the unstructured Tests is made of (the
// hexahedra for the values that aren't a cell type).
CellLayout cellLayout(const std::string &value)
{
  if (value == "tetrahedra")
    return CellLayout::Tetrahedra;
  if (value == "pyramids")
    return CellLayout::Pyramids;
  if (value == "mixed")
    return CellLayout::Mixed;
  return CellLayout::Hexahedra;
}

// A camera hook building (and recording) the camera `projection` describes.
CameraFn cameraHook(CameraProjection projection)
{
  return [projection](BuildContext &ctx, const scenes::Bounds &) {
    auto c = ctx.newCamera(projection);
    anari::commitParameters(ctx.device(), c);
    return c;
  };
}

// A volume equivalence covers the axis along its view with a second view
// down another axis (ADR-0010, volume equivalence); nothing relates the two
// views' images, so they are two Tests.
struct View
{
  const char *suffix;
  ViewAxis axis;
  const char *direction;
};
const View kViews[2] = {
    {"_down_z", ViewAxis::Z, "-z"}, {"_down_x", ViewAxis::X, "-x"}};

// `expected`, compared only inside the bounds' silhouette shrunk by `band`
// world units (and two pixels).
ExpectedImage interiorOf(const Image &expected, float band)
{
  return maskedBy(
      expected, interiorMask(expected.width, expected.height, band));
}

// ---- the step transfer function's extent checks ----------------------------

// The extent Tests' view: orthographic down -z, 4 units tall, so world x and y
// in [-2, 2] fill a square frame and +-1, 0 and 0.5 fall on whole pixels when
// its size is a multiple of 8.
constexpr float kExtentViewHeight = 4.f;

CameraProjection extentCamera()
{
  CameraProjection c;
  c.type = CameraProjection::Type::Orthographic;
  c.position = float3(0.f, 0.f, 5.f);
  c.direction = float3(0.f, 0.f, -1.f);
  c.up = float3(0.f, 1.f, 0.f);
  c.height = kExtentViewHeight;
  return c;
}

// A transferFunction1D that is transparent below the middle of its value
// range and opaque above it: 256 opacities, the first half 0 and the second 1,
// so the opacity rises between 127/255 and 128/255 of the range. Opaque at
// once (unitDistance 0.001): a ray through a value above about 0.4985 of the
// range stops at the field's face. The colour is one bright orange.
anari::Volume stepVolume(anari::Device d, anari::SpatialField field)
{
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "color", float3(0.9f, 0.6f, 0.1f));
  std::vector<float> opacity(256, 0.f);
  std::fill(opacity.begin() + 128, opacity.end(), 1.f);
  anari::setAndReleaseParameter(
      d, vol, "opacity", anari::newArray1D(d, opacity.data(), opacity.size()));
  anari::setParameter(d, vol, "unitDistance", 0.001f);
  return vol;
}

// A world holding `field` (committed here and released) as stepVolume(),
// with `valueRange` set when it is.
anari::World stepWorld(
    anari::Device d, anari::SpatialField field, const Any &valueRange = none())
{
  anari::commitParameters(d, field);
  auto vol = stepVolume(d, field);
  applyParameterValue(d, vol, "valueRange", valueRange.raw());
  anari::commitParameters(d, vol);
  WorldContents wc;
  wc.volumes = {vol};
  auto world = assembleWorld(d, wc);
  anari::release(d, field);
  anari::release(d, vol);
  return world;
}

// Where in the range the step's opacity becomes enough to stop a ray: past
// 127/255 by 0.3 % of its width, where the opacity reaches about 0.003 and the
// slab is opaque within its depth.
constexpr float kStepFraction = 127.f / 255.f + 0.003f / 255.f;

// The analytic picture of a field ramping from 0 at x = x0 to 1 at x = x1
// over the box [x0, x1] x [y0, y1], seen down -z through a step transfer
// function: opaque where x is past the step (and inside the box),
// background elsewhere. `uncertain` widens the band around every edge that
// is not judged, beyond 1.5 pixels.
struct StepPicture
{
  float x0, x1, y0, y1;
  float valueLo{0.f}, valueHi{1.f}; // the volume's valueRange
  float uncertain{0.f};

  // The x at which the field's value reaches the step.
  float edge() const
  {
    const float v = valueLo + kStepFraction * (valueHi - valueLo);
    return x0 + v * (x1 - x0);
  }
};

void stepExtentChecks(ExactCheck &x, const StepPicture &s)
{
  const auto color = x.rgba(Channel::Color);
  const uint32_t w = color.width, h = color.height;
  const float pixel = kExtentViewHeight / float(h);
  const float m = 1.5f * pixel + s.uncertain;
  const float edge = s.edge();
  PixelMask inside(w, h), outside(w, h);
  for (uint32_t py = 0; py < h; ++py)
    for (uint32_t px = 0; px < w; ++px) {
      const float wx =
          ((px + 0.5f) / w - 0.5f) * kExtentViewHeight * float(w) / float(h);
      const float wy = ((py + 0.5f) / h - 0.5f) * kExtentViewHeight;
      if (wx > edge + m && wx < s.x1 - m && wy > s.y0 + m && wy < s.y1 - m)
        inside.set(px, py);
      else if (wx < edge - m || wx > s.x1 + m || wy < s.y0 - m || wy > s.y1 + m)
        outside.set(px, py);
    }
  // The frame's bottom-left corner is outside every Case's field.
  const Rgba8 background = color.at(0, 0);
  x.allPixels(
      "the field shows where its value is past the step",
      color,
      "unlike the background corner by more than 8 levels",
      [&](uint32_t, uint32_t, const Rgba8 &v) {
        return valueDistance(v, background) > 8.0;
      },
      &inside);
  x.allPixels(
      "background where the field is below the step or absent",
      color,
      "the background corner's colour, within 8 levels",
      [&](uint32_t, uint32_t, const Rgba8 &v) {
        return valueDistance(v, background) <= 8.0;
      },
      &outside);
}

// A valueRange (box1) axis value: stored with its ANARI type, FLOAT32_BOX1.
Any box1(float lower, float upper)
{
  const float v[2] = {lower, upper};
  return Any(ANARI_FLOAT32_BOX1, v);
}

// A float3 axis value of an exact check's Case, or the spec's default.
float3 axisFloat3(const ExactCheck &x, const char *name, float3 unset)
{
  const Any v = x.values().value(name);
  return v.valid() ? v.get<float3>() : unset;
}

// The isosurface's depth is the sphere's, where a ray meets it well inside
// its silhouette (a trilinear field of the distance puts the surface within
// about 0.001 of the sphere); a ray well outside it meets nothing, so its
// depth is no closer than the sphere's farthest point (the spec gives no
// value for a pixel without a hit).
void isosurfaceSphereChecks(ExactCheck &x)
{
  const auto depth = x.depth();
  const uint32_t w = depth.width, h = depth.height;
  const CameraProjection camera = x.camera().value_or(sphereCamera());
  constexpr float kMargin = 0.05f; // about 4 pixels at 256
  constexpr float kTolerance = 0.02f;
  PixelMask inside(w, h), near(w, h);
  std::vector<float> expected(size_t(w) * h, 0.f);
  for (uint32_t py = 0; py < h; ++py)
    for (uint32_t px = 0; px < w; ++px) {
      const SphereRay ray = sphereRay(camera, px, py, w, h, kSphereRadius);
      if (ray.closest < kSphereRadius - kMargin) {
        inside.set(px, py);
        expected[size_t(py) * w + px] = ray.hit;
      }
      if (ray.closest < kSphereRadius + kMargin)
        near.set(px, py);
    }
  // A device may take a pixel's depth at any point of its footprint (the
  // spec takes the minimum over its samples): allow what the sphere's depth
  // changes by over a pixel, beyond kTolerance.
  auto at = [&](uint32_t px, uint32_t py) {
    return sphereRay(camera, px, py, w, h, kSphereRadius).hit;
  };
  x.allPixels(
      "depth is the sphere's",
      depth,
      "the sphere's depth within the pixel, +/- " + valueText(kTolerance),
      [&](uint32_t px, uint32_t py, float v) {
        const float e = expected[size_t(py) * w + px];
        const float spread = std::max(
            std::abs(at(px + 1, py) - e), std::abs(at(px, py + 1) - e));
        return std::abs(v - e) <= kTolerance + spread;
      },
      &inside);
  const float farthest = math::length(camera.position) + kSphereRadius;
  x.backgroundOutside("depth outside the sphere",
      depth,
      near,
      "no closer than the sphere's farthest point",
      [farthest](float v) { return v >= farthest; });
}

} // namespace

void registerFieldTests(Catalog &catalog)
{
  // ---- unstructured ---------------------------------------------------------
  for (const View &v : kViews) {
    const ViewAxis view = v.axis;
    TestBuilder test =
        makeTest("volume", std::string("unstructured") + v.suffix);
    test.description(
            "Checks that an unstructured field of hexahedra, tetrahedra, "
            "pyramids or a mix renders as the structuredRegular "
            "field holding the same linear ramp, that vertex.data takes "
            "precedence over cell.data, and that uniform cell.data renders "
            "as the same uniform structuredRegular field, looking down "
            + std::string(v.direction) + ".")
        .build([view](BuildContext &ctx) {
          auto d = ctx.device();
          const std::string f = ctx.getString("field", "structuredRegular");
          if (f == "structuredRegular")
            return fieldWorld(d, newRampStructuredField(d, view, 5));
          if (f == "uniformStructuredRegular") {
            auto field =
                anari::newObject<anari::SpatialField>(d, "structuredRegular");
            const std::vector<float> data(8, kUniformValue);
            anari::setParameter(d, field, "origin", float3(-1.f));
            anari::setParameter(d, field, "spacing", float3(2.f));
            anari::setParameterArray3D(d, field, "data", data.data(), 2, 2, 2);
            return fieldWorld(d, field);
          }
          const UnstructuredMesh mesh = unstructuredMesh(cellLayout(f));
          std::vector<float> vertexData;
          for (const float3 &p : mesh.positions)
            vertexData.push_back(rampValue(view, p));
          // One value in every cell: uniform, unlike the ramp as a whole.
          const std::vector<float> cellData(
              mesh.cellType.size(), kUniformValue);
          if (f == "uniformCellData")
            return fieldWorld(
                d, newUnstructuredField(d, mesh, nullptr, &cellData));
          if (f == "vertexDataOverCellData")
            return fieldWorld(
                d, newUnstructuredField(d, mesh, &vertexData, &cellData));
          return fieldWorld(
              d, newUnstructuredField(d, mesh, &vertexData, nullptr));
        })
        .camera(cameraHook(viewCamera(view)))
        // Two references: the ramp and a uniform field. Every unstructured
        // value must render as one of them (a derived expectation, exempt
        // from the axis's effect check); the two references must differ.
        .permute("field",
            {"structuredRegular",
                "hexahedra",
                "tetrahedra",
                "pyramids",
                "mixed",
                "vertexDataOverCellData",
                "uniformStructuredRegular",
                "uniformCellData"});
    for (const char *ramp : {"hexahedra",
             "tetrahedra",
             "pyramids",
             "mixed",
             "vertexDataOverCellData"})
      test.expectDerived("field",
          ramp,
          std::string(ramp) == "vertexDataOverCellData"
              ? "the structuredRegular ramp: vertex.data takes precedence"
              : "the structuredRegular ramp",
          [](const Siblings &s) -> ExpectedImage {
            return s.at("structuredRegular");
          });
    test.expectDerived("field",
            "uniformCellData",
            "the uniform structuredRegular field",
            [](const Siblings &s) -> ExpectedImage {
              return s.at("uniformStructuredRegular");
            })
        .expectExact("the volume shows", subjectIsShown)
        .noGroundTruth()
        .covers("ANARI_KHR_SPATIAL_FIELD_UNSTRUCTURED")
        .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
            "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
            "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
        .registerInto(catalog);
  }

  // ---- nanovdb ----
  for (const View &v : kViews) {
    const ViewAxis view = v.axis;
    makeTest("volume", std::string("nanovdb") + v.suffix)
        .description(
            "Checks that a nanovdb field holding a linear ramp renders as the "
            "structuredRegular field holding it, with the linear (default) "
            "and the cubic filter, inside its bounds, looking down "
            + std::string(v.direction) + ".")
        .build([view](BuildContext &ctx) {
          auto d = ctx.device();
          const std::string f = ctx.getString("field", "structuredRegular");
          if (f == "structuredRegular")
            return fieldWorld(d, newRampStructuredField(d, view, 5));
          return fieldWorld(d,
              newNanoVdbField(
                  d, rampGrid(view), f == "nanovdbCubic" ? "cubic" : nullptr));
        })
        .camera(cameraHook(viewCamera(view)))
        .permute("field", {"structuredRegular", "nanovdb", "nanovdbCubic"})
        // The spec doesn't say where in a voxel its value sits, so the
        // grid's bounds and its ramp may be half a voxel off between
        // devices: compare inside a band of a voxel (the ramp moves less
        // than 3 levels by half a voxel), two for the cubic filter's
        // reach (ADR-0010 57).
        .expectDerived("field",
            "nanovdb",
            "the structuredRegular ramp, more than a voxel inside the bounds",
            [](const Siblings &s) -> ExpectedImage {
              return interiorOf(s.at("structuredRegular"), kRampVoxelSize);
            })
        .expectDerived("field",
            "nanovdbCubic",
            "the structuredRegular ramp, more than two voxels inside the "
            "bounds",
            [](const Siblings &s) -> ExpectedImage {
              return interiorOf(
                  s.at("structuredRegular"), 2.f * kRampVoxelSize);
            })
        .expectExact("the volume shows", subjectIsShown)
        .noGroundTruth()
        .covers("ANARI_KHR_SPATIAL_FIELD_NANOVDB")
        .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
            "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
            "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
        .registerInto(catalog);
  }

  makeTest("volume", "nanovdb_filter")
      .description(
          "Checks that a nanovdb field's nearest, linear and cubic filters "
          "render a coarse grid differently, and that linear is the "
          "default.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        NanoVdbGridSpec spec;
        spec.voxels = {4, 4, 4};
        spec.voxelSize = 0.5f;
        spec.origin = float3(-1.f);
        spec.value = [](uint32_t i, uint32_t j, uint32_t) {
          return zigzagValue(i, j);
        };
        auto field = newNanoVdbField(d, spec, nullptr);
        applyParameterValue(d, field, "filter", ctx.value("filter").raw());
        return fieldWorld(d, field);
      })
      .camera(cameraHook(viewCamera(ViewAxis::Z)))
      .permute("filter", V{Any("nearest"), Any("linear"), Any("cubic"), none()})
      .expectDerived("filter",
          "none",
          "the linear image (the default filter)",
          [](const Siblings &s) -> ExpectedImage { return s.at("linear"); })
      .expectExact("the volume shows", subjectIsShown)
      .noGroundTruth()
      .covers("ANARI_KHR_SPATIAL_FIELD_NANOVDB")
      .requireFeatures({"ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
          "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
      .registerInto(catalog);

  // ---- the cubic filter -----------------------------------------------------
  makeTest("volume", "structured_regular_filter_cubic")
      .description(
          "Checks that a structuredRegular field's nearest, linear and cubic "
          "filters render a coarse grid differently.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        // 4 x 4 x 4 vertices over [-1, 1]^3 zig-zagging across the view
        // (constant along it): each filter reconstructs it differently.
        auto field =
            anari::newObject<anari::SpatialField>(d, "structuredRegular");
        std::vector<float> data;
        for (uint32_t k = 0; k < 4; ++k)
          for (uint32_t j = 0; j < 4; ++j)
            for (uint32_t i = 0; i < 4; ++i)
              data.push_back(zigzagValue(i, j));
        anari::setParameter(d, field, "origin", float3(-1.f));
        anari::setParameter(d, field, "spacing", float3(2.f / 3.f));
        anari::setParameterArray3D(d, field, "data", data.data(), 4, 4, 4);
        applyParameterValue(d, field, "filter", ctx.value("filter").raw());
        return fieldWorld(d, field);
      })
      .camera(cameraHook(viewCamera(ViewAxis::Z)))
      .permute("filter", {"nearest", "linear", "cubic"})
      .expectExact("the volume shows", subjectIsShown)
      .noGroundTruth()
      .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_FILTER_CUBIC")
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
          "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
      .registerInto(catalog);

  for (const View &v : kViews) {
    const ViewAxis view = v.axis;
    makeTest("volume",
        std::string("structured_regular_filter_cubic_ramp") + v.suffix)
        .description(
            "Checks that the cubic filter reproduces a linear ramp as the "
            "linear filter does, where its reach stays inside the grid, "
            "looking down "
            + std::string(v.direction) + ".")
        .build([view](BuildContext &ctx) {
          auto d = ctx.device();
          return fieldWorld(d,
              newRampStructuredField(d,
                  view,
                  kCubicRampVertices,
                  ctx.getString("filter", "linear") == "cubic" ? "cubic"
                                                               : "linear"));
        })
        .camera(cameraHook(viewCamera(view)))
        .permute("filter", {"linear", "cubic"})
        // Every cubic that reproduces a linear function (B-spline,
        // Catmull-Rom, ...) agrees with linear where its four samples per
        // axis are inside the grid: a cell in from each boundary. Near the
        // boundary clamp-to-edge bends the cubic; the spec doesn't say
        // which cubic, so that band isn't compared.
        .expectDerived("filter",
            "cubic",
            "the linear image, a cell or more inside the bounds",
            [](const Siblings &s) -> ExpectedImage {
              return interiorOf(s.at("linear"), kCubicRampSpacing);
            })
        .expectExact("the volume shows", subjectIsShown)
        .noGroundTruth()
        .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_FILTER_CUBIC")
        .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
            "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
            "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
        .registerInto(catalog);
  }

  // ---- data element types ---------------------------------------------------
  for (const View &v : kViews) {
    const ViewAxis view = v.axis;
    makeTest("volume", std::string("structured_regular_data_types") + v.suffix)
        .description(
            "Checks that a structuredRegular field's data as UFIXED8, "
            "UFIXED16, FIXED16 or FLOAT64 renders as the same normalized "
            "values as FLOAT32, looking down "
            + std::string(v.direction) + ".")
        .build([view](BuildContext &ctx) {
          auto d = ctx.device();
          constexpr uint32_t n = 5;
          const float spacing = 2.f / float(n - 1);
          std::vector<float> values;
          for (uint32_t k = 0; k < n; ++k)
            for (uint32_t j = 0; j < n; ++j)
              for (uint32_t i = 0; i < n; ++i)
                values.push_back(rampValue(view,
                    float3(-1.f + i * spacing,
                        -1.f + j * spacing,
                        -1.f + k * spacing)));
          const std::string type = ctx.getString("dataType", "FLOAT32");
          ANARIDataType t = ANARI_FLOAT32;
          std::vector<uint8_t> bytes;
          auto append = [&bytes](const auto &value) {
            const auto *b = reinterpret_cast<const uint8_t *>(&value);
            bytes.insert(bytes.end(), b, b + sizeof(value));
          };
          for (float f : values) {
            if (type == "UFIXED8") {
              t = ANARI_UFIXED8;
              append(uint8_t(std::lround(f * 255.f)));
            } else if (type == "UFIXED16") {
              t = ANARI_UFIXED16;
              append(uint16_t(std::lround(f * 65535.f)));
            } else if (type == "FIXED16") {
              t = ANARI_FIXED16;
              append(int16_t(std::lround(f * 32767.f)));
            } else if (type == "FLOAT64") {
              t = ANARI_FLOAT64;
              append(double(f));
            } else {
              append(f);
            }
          }
          auto field =
              anari::newObject<anari::SpatialField>(d, "structuredRegular");
          anari::setParameter(d, field, "origin", float3(-1.f));
          anari::setParameter(d, field, "spacing", float3(spacing));
          anari::setAndReleaseParameter(
              d, field, "data", makeRawArray3D(d, t, n, n, n, bytes.data()));
          return fieldWorld(d, field);
        })
        .camera(cameraHook(viewCamera(view)))
        // The first value is the reference. UFIXED8 rounds a value by at
        // most 1/510, under a level of the colour map.
        .variant("dataType",
            {"FLOAT32", "UFIXED8", "UFIXED16", "FIXED16", "FLOAT64"})
        .expectExact("the volume shows", subjectIsShown)
        .noGroundTruth()
        .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
        .requireFeatures({"ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
            "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
        .registerInto(catalog);
  }

  // ---- extents through a step transfer function ----
  //
  // A field ramping along x seen down -z through a step transfer function is
  // opaque exactly where its value is past the step and background elsewhere:
  // its extent (origin, spacing) and its value mapping (valueRange) are where
  // the picture's edges are.
  makeTest("volume", "structured_regular_extent")
      .description(
          "Checks that a structuredRegular field's origin, spacing and its "
          "volume's valueRange put a step transfer function's edges where "
          "the spec puts them.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto field =
            anari::newObject<anari::SpatialField>(d, "structuredRegular");
        std::vector<float> data;
        for (uint32_t k = 0; k < 3; ++k)
          for (uint32_t j = 0; j < 3; ++j)
            for (uint32_t i = 0; i < 3; ++i)
              data.push_back(0.5f * i);
        anari::setParameterArray3D(d, field, "data", data.data(), 3, 3, 3);
        applyParameterValue(d, field, "origin", ctx.value("origin").raw());
        applyParameterValue(d, field, "spacing", ctx.value("spacing").raw());
        return stepWorld(d, field, ctx.value("valueRange"));
      })
      .camera(cameraHook(extentCamera()))
      .simplified()
      // Unset origin is (0, 0, 0) and unset spacing (1, 1, 1) (the spec).
      .permute("origin", V{Any(float3(-1.f)), none()})
      .permute("spacing", V{none(), Any(float3(0.5f, 0.75f, 1.f))})
      .permute("valueRange", V{none(), box1(0.5f, 1.f)})
      .expectExact("the picture's edges are the field's",
          [](ExactCheck &x) {
            const float3 o = axisFloat3(x, "origin", float3(0.f));
            const float3 sp = axisFloat3(x, "spacing", float3(1.f));
            StepPicture s{o.x, o.x + 2.f * sp.x, o.y, o.y + 2.f * sp.y};
            const Any range = x.values().value("valueRange");
            if (range.valid()) {
              float r[2];
              std::memcpy(r, range.data(), sizeof(r));
              s.valueLo = r[0];
              s.valueHi = r[1];
            }
            stepExtentChecks(x, s);
          })
      .channels({Channel::Color})
      .noGroundTruth()
      .covers("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
      .covers("ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D")
      .requireFeature("ANARI_KHR_CAMERA_ORTHOGRAPHIC")
      .registerInto(catalog);

  // The same picture from an unstructured and a nanovdb field, which need no
  // structuredRegular field to compare with: a device without one still has
  // their extent checked.
  makeTest("volume", "unstructured_extent")
      .description(
          "Checks that an unstructured field of hexahedra ramping along x "
          "puts a step transfer function's edges where its vertices put "
          "them.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        const UnstructuredMesh mesh = unstructuredMesh(CellLayout::Hexahedra);
        std::vector<float> data;
        for (const float3 &p : mesh.positions)
          data.push_back((p.x + 1.f) / 2.f);
        auto field = newUnstructuredField(d, mesh, &data, nullptr);
        return stepWorld(d, field);
      })
      .camera(cameraHook(extentCamera()))
      .expectExact("the picture's edges are the field's",
          [](ExactCheck &x) {
            stepExtentChecks(x, StepPicture{-1.f, 1.f, -1.f, 1.f});
          })
      .channels({Channel::Color})
      .noGroundTruth()
      .covers("ANARI_KHR_SPATIAL_FIELD_UNSTRUCTURED")
      .requireFeatures({"ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
          "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
      .registerInto(catalog);

  makeTest("volume", "nanovdb_extent")
      .description(
          "Checks that a nanovdb field ramping along x puts a step transfer "
          "function's edges where its grid puts them, to within half a "
          "voxel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        NanoVdbGridSpec spec;
        spec.voxels = {kRampVoxels, kRampVoxels, kRampVoxels};
        spec.voxelSize = kRampVoxelSize;
        spec.origin = float3(-1.f);
        spec.value = [](uint32_t i, uint32_t, uint32_t) {
          return (i + 0.5f) / kRampVoxels;
        };
        auto field = newNanoVdbField(d, spec, nullptr);
        return stepWorld(d, field);
      })
      .camera(cameraHook(extentCamera()))
      // Whether a voxel's value sits at its centre or its corner moves the
      // bounds and the ramp by up to a voxel (ADR-0010 57): edges within a
      // voxel aren't judged.
      .expectExact("the picture's edges are the field's",
          [](ExactCheck &x) {
            StepPicture s{-1.f, 1.f, -1.f, 1.f};
            s.uncertain = kRampVoxelSize;
            stepExtentChecks(x, s);
          })
      .channels({Channel::Color})
      .noGroundTruth()
      .covers("ANARI_KHR_SPATIAL_FIELD_NANOVDB")
      .requireFeatures({"ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
          "ANARI_KHR_CAMERA_ORTHOGRAPHIC"})
      .registerInto(catalog);

  // ---- isosurface -----------------------------------------------------------
  makeTest("geometry", "isosurface_sphere")
      .description(
          "Checks that an isosurface of a distance field is the sphere it "
          "describes (its depth), with the isovalue given as a FLOAT32 or as "
          "a one-element array.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        // 33^3 vertices over [-1, 1]^3, each holding its distance from the
        // origin.
        auto field =
            anari::newObject<anari::SpatialField>(d, "structuredRegular");
        constexpr uint32_t n = 33;
        const float spacing = 2.f / float(n - 1);
        std::vector<float> data;
        for (uint32_t k = 0; k < n; ++k)
          for (uint32_t j = 0; j < n; ++j)
            for (uint32_t i = 0; i < n; ++i)
              data.push_back(math::length(float3(
                  -1.f + i * spacing, -1.f + j * spacing, -1.f + k * spacing)));
        anari::setParameter(d, field, "origin", float3(-1.f));
        anari::setParameter(d, field, "spacing", float3(spacing));
        anari::setParameterArray3D(d, field, "data", data.data(), n, n, n);
        anari::commitParameters(d, field);
        auto geom = anari::newObject<anari::Geometry>(d, "isosurface");
        anari::setParameter(d, geom, "field", field);
        if (ctx.getString("isovalue", "array") == "scalar") {
          anari::setParameter(d, geom, "isovalue", kSphereRadius);
        } else {
          const float iso[1] = {kSphereRadius};
          anari::setAndReleaseParameter(
              d, geom, "isovalue", anari::newArray1D(d, iso, 1));
        }
        anari::commitParameters(d, geom);
        auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
        auto surface = makeSurface(d, geom, mat);
        auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
        WorldContents wc;
        wc.surfaces = {surface};
        wc.lights = {light};
        auto world = assembleWorld(d, wc);
        anari::release(d, field);
        anari::release(d, geom);
        anari::release(d, mat);
        anari::release(d, surface);
        anari::release(d, light);
        return world;
      })
      .camera(cameraHook(sphereCamera()))
      // The first value is the reference: the array form.
      .variant("isovalue", {"array", "scalar"})
      .expectExact("the isosurface is the sphere", isosurfaceSphereChecks)
      .channels({Channel::Color, Channel::Depth})
      .noGroundTruth()
      .covers("ANARI_KHR_GEOMETRY_ISOSURFACE")
      .requireFeature("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
      .requireFeatures(kSupportMatte)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
