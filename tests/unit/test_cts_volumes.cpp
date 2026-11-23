// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The spatial field and volume Tests (ticket 11; ADR-0010 55-60): the NanoVDB
// grid they build, read back through NanoVDB's own accessors, and their
// self-checks judged through the evaluation seam with images generated in
// code. No SDK device implements nanovdb, unstructured or cubic filtering, so
// the synthetic positives and negatives are those Tests' validation without a
// device.

#include "catch.hpp"
#include "cts_test_images.h"
// anari_test_scenes
#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/ExactCheck.h"
#include "cts/Expansion.h"
#include "cts/Filter.h"
#include "cts/Image.h"
#include "cts/Relations.h"
#include "cts/tests/NanoVdbGrid.h"
#include "cts/tests/VolumeScene.h"
#include "cts/tests/fields.h"
// nanovdb
#include <nanovdb/NanoVDB.h>
// std
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

using namespace anari::cts;
using anari::math::float2;
using anari::math::float3;
using namespace anari::cts::volume_scene;

namespace {

// A copy of `bytes` at NanoVDB's alignment, as a device holds the array.
struct AlignedGrid
{
  struct Free
  {
    void operator()(uint8_t *p) const
    {
      ::operator delete[](p, std::align_val_t(NANOVDB_DATA_ALIGNMENT));
    }
  };
  std::unique_ptr<uint8_t[], Free> bytes;

  explicit AlignedGrid(const std::vector<uint8_t> &in)
      : bytes(static_cast<uint8_t *>(::operator new[](
            in.size(), std::align_val_t(NANOVDB_DATA_ALIGNMENT))))
  {
    std::memcpy(bytes.get(), in.data(), in.size());
  }
  const nanovdb::NanoGrid<float> *grid() const
  {
    return reinterpret_cast<const nanovdb::NanoGrid<float> *>(bytes.get());
  }
};

} // namespace

TEST_CASE("a built NanoVDB grid reads back its voxels", "[cts][volumes]")
{
  NanoVdbGridSpec spec;
  spec.voxels = {20, 9, 3}; // more than one leaf along x and y
  spec.voxelSize = 0.25f;
  spec.origin = {-1.f, 2.f, 0.5f};
  spec.value = [](uint32_t i, uint32_t j, uint32_t k) {
    return float(i) + 100.f * j + 10000.f * k;
  };
  const auto bytes = makeNanoVdbFloatGrid(spec);
  AlignedGrid aligned(bytes);
  const auto *grid = aligned.grid();

  REQUIRE(grid->isValid());
  CHECK(grid->gridType() == nanovdb::GridType::Float);
  CHECK(grid->gridCount() == 1);
  CHECK(grid->gridSize() == bytes.size());
  CHECK(grid->version().getMajor() == 32);
  CHECK(grid->activeVoxelCount() == 20u * 9u * 3u);
  CHECK(grid->tree().nodeCount(0) == 3u * 2u * 1u);
  CHECK(grid->indexBBox()
      == nanovdb::CoordBBox(nanovdb::Coord(0), nanovdb::Coord(19, 8, 2)));
  CHECK(grid->voxelSize()[0] == Approx(0.25));
  CHECK(grid->worldBBox().min()[0] == Approx(-1.0));
  CHECK(grid->worldBBox().max()[0] == Approx(-1.0 + 20 * 0.25));
  CHECK(grid->worldBBox().max()[1] == Approx(2.0 + 9 * 0.25));
  // Index (0, 0, 0) sits at the origin.
  const auto w = grid->indexToWorld(nanovdb::Vec3d(2, 1, 0));
  CHECK(w[0] == Approx(-0.5));
  CHECK(w[1] == Approx(2.25));
  CHECK(w[2] == Approx(0.5));

  auto acc = grid->getAccessor();
  bool all = true;
  for (int i = 0; i < 20; ++i)
    for (int j = 0; j < 9; ++j)
      for (int k = 0; k < 3; ++k) {
        const nanovdb::Coord ijk(i, j, k);
        all = all && acc.isActive(ijk)
            && acc.getValue(ijk) == spec.value(i, j, k);
      }
  CHECK(all);
  // Outside the box: inactive, the background (0).
  CHECK_FALSE(acc.isActive(nanovdb::Coord(20, 0, 0)));
  CHECK(acc.getValue(nanovdb::Coord(20, 0, 0)) == 0.f);
  CHECK(grid->tree().root().minimum() == 0.f);
  CHECK(grid->tree().root().maximum() == spec.value(19, 8, 2));
}

TEST_CASE("the NanoVDB grid builder rejects an empty or too large box",
    "[cts][volumes]")
{
  NanoVdbGridSpec spec;
  spec.value = [](uint32_t, uint32_t, uint32_t) { return 0.f; };
  spec.voxels = {0, 1, 1};
  CHECK_THROWS_AS(makeNanoVdbFloatGrid(spec), std::invalid_argument);
  spec.voxels = {129, 1, 1};
  CHECK_THROWS_AS(makeNanoVdbFloatGrid(spec), std::invalid_argument);
}

// ---- the Tests, judged on images generated in code
// ----------------------------

namespace {

constexpr uint32_t kSize = 64;

using Values = std::map<std::string, std::string>;

// One built-in Test, its Cases, and a way to judge channels supplied for
// them through its exact checks and relations.
struct Fixture
{
  Catalog catalog;
  const TestDef *test{nullptr};
  std::vector<Case> cases;

  explicit Fixture(const std::string &id)
  {
    registerBuiltinTests(catalog);
    for (const TestDef *t : catalog.filter(Filter{id}))
      if (t->id() == id)
        test = t;
    REQUIRE(test != nullptr);
    cases = expand(*test);
  }

  static Values valuesOf(const Case &c)
  {
    Values v;
    for (const auto &cv : c.values)
      v[cv.axisName] = anyToString(cv.value);
    return v;
  }

  size_t index(const Values &values) const
  {
    for (size_t i = 0; i < cases.size(); ++i) {
      bool all = true;
      for (const auto &cv : cases[i].values) {
        const auto it = values.find(cv.axisName);
        if (it != values.end() && anyToString(cv.value) != it->second)
          all = false;
      }
      if (all)
        return i;
    }
    FAIL("no such Case");
    return 0;
  }

  // The Case's channels, in the Test's Channel order.
  using Render = std::function<std::vector<ChannelValues>(const Values &)>;

  SelfCheckResults judge(const Render &render,
      std::optional<CameraProjection> camera = std::nullopt) const
  {
    std::vector<std::optional<CaseChannels>> inputs;
    for (const auto &c : cases) {
      CaseChannels in;
      in.channels = render(valuesOf(c));
      in.size = {
          in.channels.front().image.width, in.channels.front().image.height};
      if (camera) {
        in.camera = camera;
        in.camera->aspect = float(in.size.width) / float(in.size.height);
      }
      inputs.push_back(std::move(in));
    }
    return evaluateSelfChecks(*test, cases, inputs);
  }

  // Whether every assertion about the Case holding `values` passed (and
  // there is at least one).
  bool passes(const SelfCheckResults &r, const Values &values) const
  {
    const size_t i = index(values);
    bool any = false;
    for (const auto &o : r.exact[i]) {
      INFO(o.assertion.detail);
      if (!o.assertion.passed)
        return false;
      any = true;
    }
    for (const auto &o : r.relations)
      if (o.caseIndex == i && o.assertion.evaluated) {
        INFO(o.assertion.detail);
        if (!o.assertion.passed)
          return false;
        any = true;
      }
    return any;
  }

  bool allPass(const SelfCheckResults &r) const
  {
    for (const auto &c : cases)
      if (!passes(r, valuesOf(c))) {
        UNSCOPED_INFO("fails: " << c.id());
        return false;
      }
    return true;
  }
};

std::vector<ChannelValues> colorOnly(Image image)
{
  ChannelValues cv;
  cv.channel = Channel::Color;
  cv.image = std::move(image);
  return {cv};
}

// The Tests' colour map (blue, green, red over [0, 1]) as 8 bits.
cts_test::Rgba colorOf(float v)
{
  const float c[3][3] = {
      {0.1f, 0.2f, 0.9f}, {0.2f, 0.9f, 0.2f}, {0.9f, 0.2f, 0.1f}};
  v = std::min(std::max(v, 0.f), 1.f) * 2.f;
  const int i = std::min(int(v), 1);
  const float f = v - i;
  uint8_t out[3];
  for (int k = 0; k < 3; ++k)
    out[k] =
        uint8_t(std::lround(255.f * (c[i][k] * (1.f - f) + c[i + 1][k] * f)));
  return cts_test::Rgba{out[0], out[1], out[2], 255};
}

// What a volume equivalence Test's view shows of a field over [-1, 1]^3
// constant along the view: `value(a, b)` inside the bounds' silhouette
// [-1, 1] + `grow` (world units), black outside.
Image fieldView(const std::function<float(float, float)> &value,
    float grow = 0.f,
    uint32_t size = kSize)
{
  Image im = cts_test::solid(size, size, 0, 0, 0, 255);
  for (uint32_t y = 0; y < size; ++y)
    for (uint32_t x = 0; x < size; ++x) {
      const float2 ab = pixelAcrossView(x, y, size, size);
      if (std::abs(ab.x) <= 1.f + grow && std::abs(ab.y) <= 1.f + grow)
        cts_test::setPixel(im, x, y, colorOf(value(ab.x, ab.y)));
    }
  return im;
}

// The ramp across the view (rampValue() in across-view coordinates).
float ramp(float a, float b)
{
  return rampValue(ViewAxis::Z, float3(a, b, 0.f));
}

} // namespace

TEST_CASE(
    "the unstructured meshes fill [-1, 1]^3 with cells ordered as the "
    "spec and VTK order them",
    "[cts][volumes]")
{
  using namespace fields;
  // Signed volumes: a cell ordered as the spec says (bottom counterclockwise
  // seen from the top or apex, the right-hand normal pointing at it) has a
  // positive one.
  auto tet = [](float3 a, float3 b, float3 c, float3 d) {
    return anari::math::dot(b - a, anari::math::cross(c - a, d - a)) / 6.f;
  };
  for (CellLayout layout : {CellLayout::Hexahedra,
           CellLayout::Tetrahedra,
           CellLayout::Pyramids,
           CellLayout::Mixed}) {
    const UnstructuredMesh m = unstructuredMesh(layout);
    REQUIRE(m.cellIndex.size() == m.cellType.size());
    float total = 0.f;
    bool positive = true;
    for (size_t c = 0; c < m.cellType.size(); ++c) {
      const uint32_t *i = &m.index[m.cellIndex[c]];
      auto p = [&](int n) { return m.positions[i[n]]; };
      float v = 0.f;
      if (m.cellType[c] == kTetrahedron)
        v = tet(p(0), p(1), p(2), p(3));
      else if (m.cellType[c] == kPyramid)
        v = tet(p(0), p(1), p(2), p(4)) + tet(p(0), p(2), p(3), p(4));
      else if (m.cellType[c] == kHexahedron)
        // An axis-aligned box: its bottom face's normal times its height.
        v = anari::math::dot(
            anari::math::cross(p(1) - p(0), p(3) - p(0)), p(4) - p(0));
      else
        FAIL("unexpected cell type " << int(m.cellType[c]));
      positive = positive && v > 0.f;
      total += v;
    }
    CHECK(positive);
    CHECK(total == Approx(8.f));
    for (uint32_t n : m.index)
      CHECK(n < m.positions.size());
  }
  CHECK(unstructuredMesh(CellLayout::Hexahedra).cellType.size() == 8);
  CHECK(unstructuredMesh(CellLayout::Tetrahedra).cellType.size() == 48);
  CHECK(unstructuredMesh(CellLayout::Pyramids).cellType.size() == 48);
}

TEST_CASE("unstructured cells must render as the structuredRegular ramp",
    "[cts][volumes]")
{
  for (const char *id :
      {"volume/unstructured_down_z", "volume/unstructured_down_x"}) {
    INFO(id);
    Fixture f(id);
    CHECK_FALSE(f.test->usesGroundTruth());
    const Image rampImage = fieldView(ramp);
    const Image uniform =
        fieldView([](float, float) { return fields::kUniformValue; });
    auto correct = [&](const Values &v) {
      const std::string field = v.at("field");
      return colorOnly(field.rfind("uniform", 0) == 0 ? uniform : rampImage);
    };
    CHECK(f.allPass(f.judge(correct)));

    // Pyramids read with the other winding render nothing; vertex.data losing
    // to cell.data renders the uniform value; cell.data ignored renders
    // nothing; a ramp mirrored across the view.
    const Image empty = cts_test::solid(kSize, kSize, 0, 0, 0, 255);
    const Image mirrored =
        fieldView([](float a, float b) { return ramp(-a, b); });
    for (const auto &[value, wrong] :
        std::vector<std::pair<std::string, Image>>{{"pyramids", empty},
            {"vertexDataOverCellData", uniform},
            {"uniformCellData", empty},
            {"tetrahedra", mirrored}}) {
      INFO(value);
      auto render = [&](const Values &v) {
        return v.at("field") == value ? colorOnly(wrong) : correct(v);
      };
      CHECK_FALSE(f.passes(f.judge(render), {{"field", value}}));
    }
  }
}

TEST_CASE(
    "nanovdb must render as the structuredRegular ramp inside its "
    "bounds, whichever voxel centering",
    "[cts][volumes]")
{
  for (const char *id : {"volume/nanovdb_down_z", "volume/nanovdb_down_x"}) {
    INFO(id);
    Fixture f(id);
    const Image rampImage = fieldView(ramp);
    // A device reading a voxel's value at its corner: the ramp half a voxel
    // over, the bounds a voxel short on one side.
    const float h = fields::kRampVoxelSize;
    Image cornerImage = fieldView(
        [h](float a, float b) { return ramp(a - h / 2.f, b - h / 2.f); });
    for (uint32_t y = 0; y < kSize; ++y)
      for (uint32_t x = 0; x < kSize; ++x) {
        const float2 ab = pixelAcrossView(x, y, kSize, kSize);
        if (ab.x > 1.f - h || ab.y > 1.f - h)
          cts_test::setPixel(cornerImage, x, y, cts_test::Rgba{0, 0, 0, 255});
      }
    const Image corner = cornerImage;
    for (const Image *nano : {&rampImage, &corner}) {
      auto render = [&](const Values &v) {
        return colorOnly(
            v.at("field") == "structuredRegular" ? rampImage : *nano);
      };
      CHECK(f.allPass(f.judge(render)));
    }
    // A grid read with its rows transposed, or as a quarter of the bounds.
    const Image transposed =
        fieldView([](float a, float b) { return ramp(b, a); });
    const Image quarter =
        fieldView([](float a, float b) { return ramp(a, b); }, -0.5f);
    for (const Image *wrong : {&transposed, &quarter}) {
      auto render = [&](const Values &v) {
        return colorOnly(
            v.at("field") == "structuredRegular" ? rampImage : *wrong);
      };
      const auto r = f.judge(render);
      CHECK_FALSE(f.passes(r, {{"field", "nanovdb"}}));
      CHECK_FALSE(f.passes(r, {{"field", "nanovdbCubic"}}));
    }
  }
}

TEST_CASE("the filters must differ on a coarse grid, linear by default",
    "[cts][volumes]")
{
  // Three distinct reconstructions of the zig-zag.
  const Image nearest = fieldView([](float a, float b) {
    return fields::zigzagValue(
        uint32_t((a + 1.f) * 1.999f), uint32_t((b + 1.f) * 1.999f));
  });
  const Image linear =
      fieldView([](float a, float) { return (a + 1.f) / 2.f; });
  const Image cubic =
      fieldView([](float a, float) { return 0.25f + (a + 1.f) / 4.f; });
  auto images = [&](const Image &forCubic, const Image &forUnset) {
    return [&, forCubic, forUnset](const Values &v) {
      const std::string filter = v.at("filter");
      if (filter == "nearest")
        return colorOnly(nearest);
      if (filter == "cubic")
        return colorOnly(forCubic);
      if (filter == "none")
        return colorOnly(forUnset);
      return colorOnly(linear);
    };
  };
  {
    Fixture f("volume/nanovdb_filter");
    CHECK(f.allPass(f.judge(images(cubic, linear))));
    // Cubic read as linear (as visrtx does), and an unset filter as nearest.
    CHECK_FALSE(
        f.passes(f.judge(images(linear, linear)), {{"filter", "cubic"}}));
    CHECK_FALSE(
        f.passes(f.judge(images(cubic, nearest)), {{"filter", "none"}}));
  }
  {
    Fixture f("volume/structured_regular_filter_cubic");
    CHECK(f.allPass(f.judge(images(cubic, linear))));
    CHECK_FALSE(
        f.passes(f.judge(images(linear, linear)), {{"filter", "cubic"}}));
  }
}

TEST_CASE("cubic must reproduce a linear ramp away from the grid's boundary",
    "[cts][volumes]")
{
  for (const char *id : {"volume/structured_regular_filter_cubic_ramp_down_z",
           "volume/structured_regular_filter_cubic_ramp_down_x"}) {
    INFO(id);
    Fixture f(id);
    const Image linear = fieldView(ramp);
    // Clamp-to-edge bends a cubic within a cell of the boundary.
    const float cell = fields::kCubicRampSpacing;
    const Image bent = fieldView([cell](float a, float b) {
      const bool border = std::abs(a) > 1.f - cell || std::abs(b) > 1.f - cell;
      return border ? ramp(a, b) + 0.2f : ramp(a, b);
    });
    // A cubic that doesn't reproduce linear functions (a smoothstep).
    const Image smooth = fieldView([](float a, float b) {
      const float t = (a + 1.f) / 2.f;
      return ramp(2.f * t * t * (3.f - 2.f * t) - 1.f, b);
    });
    for (const auto &[forCubic, ok] : std::vector<std::pair<Image, bool>>{
             {linear, true}, {bent, true}, {smooth, false}}) {
      auto render = [&](const Values &v) {
        return colorOnly(v.at("filter") == "cubic" ? forCubic : linear);
      };
      CHECK(f.passes(f.judge(render), {{"filter", "cubic"}}) == ok);
    }
  }
}

TEST_CASE("data element types must render the same normalized values",
    "[cts][volumes]")
{
  for (const char *id : {"volume/structured_regular_data_types_down_z",
           "volume/structured_regular_data_types_down_x"}) {
    INFO(id);
    Fixture f(id);
    const Image rampImage = fieldView(ramp);
    CHECK(f.allPass(
        f.judge([&](const Values &) { return colorOnly(rampImage); })));
    // FIXED16 read as unsigned (values doubled) or FLOAT64 read as nothing.
    const Image doubled =
        fieldView([](float a, float b) { return 2.f * ramp(a, b); });
    const Image empty = cts_test::solid(kSize, kSize, 0, 0, 0, 255);
    auto render = [&](const Values &v) {
      const std::string t = v.at("dataType");
      return colorOnly(t == "FIXED16" ? doubled
              : t == "FLOAT64"        ? empty
                                      : rampImage);
    };
    const auto r = f.judge(render);
    CHECK_FALSE(f.passes(r, {{"dataType", "FIXED16"}}));
    CHECK_FALSE(f.passes(r, {{"dataType", "FLOAT64"}}));
    CHECK(f.passes(r, {{"dataType", "UFIXED8"}}));
  }
}

namespace {

// What the extent Tests' camera (orthographic down -z, 4 tall) shows of an
// opaque box [x0, x1] x [y0, y1]: orange inside, black outside.
Image boxView(float x0, float x1, float y0, float y1)
{
  Image im = cts_test::solid(kSize, kSize, 0, 0, 0, 255);
  for (uint32_t y = 0; y < kSize; ++y)
    for (uint32_t x = 0; x < kSize; ++x) {
      const float wx = ((x + 0.5f) / kSize - 0.5f) * 4.f;
      const float wy = ((y + 0.5f) / kSize - 0.5f) * 4.f;
      if (wx > x0 && wx < x1 && wy > y0 && wy < y1)
        cts_test::setPixel(im, x, y, cts_test::Rgba{230, 153, 26, 255});
    }
  return im;
}

} // namespace

TEST_CASE(
    "a step transfer function puts the field's edges where the spec "
    "does",
    "[cts][volumes]")
{
  Fixture f("volume/structured_regular_extent");
  // Per Case (origin, spacing, valueRange): where the field is past the
  // step. The base Case: origin -1, spacing 1, the step at x = 0.
  auto correct = [](const Values &v) {
    if (v.at("origin") == "none")
      return colorOnly(boxView(1.f, 2.f, 0.f, 2.f));
    if (v.at("spacing") != "none")
      return colorOnly(boxView(-0.5f, 0.f, -1.f, 0.5f));
    if (v.at("valueRange") != "none")
      return colorOnly(boxView(0.5f, 1.f, -1.f, 1.f));
    return colorOnly(boxView(0.f, 1.f, -1.f, 1.f));
  };
  CHECK(f.allPass(f.judge(correct)));
  // A device ignoring valueRange (the step stays at x = 0), or reading the
  // bounds as data.size x spacing (a cell wider).
  auto noRange = [&](const Values &v) {
    return v.at("valueRange") != "none"
        ? colorOnly(boxView(0.f, 1.f, -1.f, 1.f))
        : correct(v);
  };
  CHECK_FALSE(f.passes(f.judge(noRange), {{"valueRange", "0.5-1"}}));
  auto wide = [&](const Values &v) {
    return v.at("spacing") != "none"
        ? colorOnly(boxView(-0.25f, 0.5f, -1.f, 1.25f))
        : correct(v);
  };
  CHECK_FALSE(f.passes(f.judge(wide), {{"spacing", "0.5-0.75-1"}}));

  for (const char *id :
      {"volume/unstructured_extent", "volume/nanovdb_extent"}) {
    INFO(id);
    Fixture g(id);
    CHECK(g.allPass(g.judge([](const Values &) {
      return colorOnly(boxView(0.f, 1.f, -1.f, 1.f));
    })));
    // Mirrored: the ramp read backwards.
    CHECK_FALSE(g.allPass(g.judge([](const Values &) {
      return colorOnly(boxView(-1.f, 0.f, -1.f, 1.f));
    })));
  }
  // nanovdb's edges may be up to a voxel off (voxel centering); a quarter of
  // the field off is wrong for both.
  const float h = fields::kRampVoxelSize;
  Fixture nano("volume/nanovdb_extent");
  CHECK(nano.allPass(nano.judge([h](const Values &) {
    return colorOnly(boxView(0.5f * h, 1.f - h, -1.f, 1.f - h));
  })));
  auto quarterOff = [](const Values &) {
    return colorOnly(boxView(0.25f, 1.25f, -1.f, 1.f));
  };
  CHECK_FALSE(nano.allPass(nano.judge(quarterOff)));
  Fixture unstructured("volume/unstructured_extent");
  CHECK_FALSE(unstructured.allPass(unstructured.judge(quarterOff)));
}

TEST_CASE(
    "an isosurface of a distance field must be its sphere", "[cts][volumes]")
{
  Fixture f("geometry/isosurface_sphere");
  const CameraProjection camera = fields::sphereCamera();
  constexpr float inf = std::numeric_limits<float>::infinity();
  auto render = [&](float radius, float offset) {
    return [&, radius, offset](const Values &) {
      ChannelValues color;
      color.channel = Channel::Color;
      color.image = cts_test::disc(kSize,
          kSize,
          kSize / 2,
          kSize / 2,
          kSize / 4,
          cts_test::Rgba{180, 130, 80, 255});
      auto depth =
          cts_test::depthChannel(kSize, kSize, [&](uint32_t x, uint32_t y) {
            const auto ray =
                fields::sphereRay(camera, x, y, kSize, kSize, radius);
            return ray.closest < radius ? ray.hit + offset : inf;
          });
      return std::vector<ChannelValues>{color, depth};
    };
  };
  CHECK(f.allPass(f.judge(render(fields::kSphereRadius, 0.f), camera)));
  // A surface 0.09 deeper (as visionaray's), or a sphere of the wrong radius.
  CHECK_FALSE(f.allPass(f.judge(render(fields::kSphereRadius, 0.09f), camera)));
  CHECK_FALSE(f.allPass(f.judge(render(0.7f, 0.f), camera)));
}
