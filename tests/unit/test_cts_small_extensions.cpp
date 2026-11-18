// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The Tests for the small ANARI extensions (array region, transform arrays,
// light primary visibility, denoise, MPI), judged through the evaluation seam
// with images generated in code (ADR-0010): what a device implementing the
// behaviour renders passes, what one ignoring it renders fails. No SDK device
// implements array region or denoise, so this is their validation without a
// device.

#include "catch.hpp"
#include "cts_test_images.h"
// anari
#include "anari/anari_cpp.hpp"
// anari_test_scenes
#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/ExactCheck.h"
#include "cts/Expansion.h"
#include "cts/Filter.h"
#include "cts/Image.h"
#include "cts/Relations.h"
#include "cts/tests/ParameterQuery.h"
// std
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace anari::cts;
using anari::math::float2;
using anari::math::float3;
using cts_test::Rgba;
using cts_test::setPixel;
using cts_test::solid;

namespace {

constexpr uint32_t kSize = 64;

void statusFunc(const void *,
    anari::Device,
    anari::Object,
    anari::DataType,
    anari::StatusSeverity,
    anari::StatusCode,
    const char *)
{}

// One built-in Test, its Cases, and a way to judge what a device rendered for
// them: every Case's channels by the Case's axis values, through the Test's
// exact checks and relations.
struct Fixture
{
  Catalog catalog;
  const TestDef *test{nullptr};
  std::vector<Case> cases;

  explicit Fixture(const std::string &id)
  {
    registerBuiltinTests(catalog);
    // A pattern is a substring: take the Test whose id it is.
    for (const TestDef *t : catalog.filter(Filter{id}))
      if (t->id() == id)
        test = t;
    REQUIRE(test != nullptr);
    cases = expand(*test);
  }

  // The Case whose axis values are `values` (axis name -> value text).
  size_t index(const std::map<std::string, std::string> &values) const
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

  // The images of every Case, by `render(Case values)`: the channels of that
  // Case in the Test's Channel order.
  using Render = std::function<std::vector<ChannelValues>(
      const std::map<std::string, std::string> &)>;

  SelfCheckResults judge(const Render &render) const
  {
    std::vector<std::optional<CaseChannels>> inputs;
    for (const auto &c : cases) {
      std::map<std::string, std::string> values;
      for (const auto &cv : c.values)
        values[cv.axisName] = anyToString(cv.value);
      CaseChannels in;
      in.channels = render(values);
      in.size = {
          in.channels.front().image.width, in.channels.front().image.height};
      inputs.push_back(std::move(in));
    }
    return evaluateSelfChecks(*test, cases, inputs);
  }

  // Whether every assertion about the Case holding `values` passed (and there
  // is at least one).
  static bool passes(const SelfCheckResults &r,
      const Fixture &f,
      const std::map<std::string, std::string> &values)
  {
    const size_t i = f.index(values);
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
};

ChannelValues colorChannel(Image image)
{
  ChannelValues cv;
  cv.channel = Channel::Color;
  cv.image = std::move(image);
  return cv;
}

// A rectangle of normalized screen coordinates [x0, x1] x [y0, y1] of `fg`
// over `image`.
void paintScreenRect(
    Image &image, float x0, float y0, float x1, float y1, Rgba fg)
{
  for (uint32_t y = 0; y < image.height; ++y)
    for (uint32_t x = 0; x < image.width; ++x) {
      const float sx = (x + 0.5f) / image.width, sy = (y + 0.5f) / image.height;
      if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
        setPixel(image, x, y, fg);
    }
}

} // namespace

// Light primary visibility ///////////////////////////////////////////////////

TEST_CASE("quad_primary_visibility tells a hidden light from a visible one",
    "[cts][small-extensions]")
{
  Fixture f("light/quad_primary_visibility");
  CHECK_FALSE(f.test->usesGroundTruth());
  const Rgba lit{200, 200, 200, 255}, unlit{0, 0, 0, 255},
      light{255, 255, 255, 255};
  // The surface is [0.1, 0.9] x [0.05, 0.45] on screen, the light's footprint
  // [0.2, 0.8] x [0.7, 1].
  auto frame = [&](Rgba background, bool lightShown, Rgba surfaceColor) {
    Image im =
        solid(kSize, kSize, background.r, background.g, background.b, 255);
    paintScreenRect(im, 0.1f, 0.05f, 0.9f, 0.45f, surfaceColor);
    if (lightShown)
      paintScreenRect(im, 0.2f, 0.7f, 0.8f, 1.f, light);
    return im;
  };
  // What a device renders over `background`: whether it shows the light when
  // visible is true or unset, whether it shows it when visible is false, and
  // the surface's colour when visible is false. The scene without the light
  // (`absent`) leaves the surface unlit.
  auto render = [&](Rgba background,
                    bool visibleShown,
                    bool hiddenShown,
                    Rgba hiddenSurface) {
    return [=](const std::map<std::string, std::string> &v) {
      const auto &visible = v.at("visible");
      Image im = visible == "absent" ? frame(background, false, unlit)
          : visible == "false" ? frame(background, hiddenShown, hiddenSurface)
                               : frame(background, visibleShown, lit);
      return std::vector<ChannelValues>{colorChannel(im)};
    };
  };
  const Rgba black{0, 0, 0, 255}, grey{40, 60, 90, 255};

  SECTION("a device implementing it, whatever its background")
  {
    for (const Rgba background : {black, grey}) {
      const auto r = f.judge(render(background, true, false, lit));
      for (const char *v : {"none", "false", "true", "absent"})
        CHECK(Fixture::passes(r, f, {{"visible", v}}));
    }
  }

  SECTION("a device that ignores visible")
  {
    const auto r = f.judge(render(black, true, true, lit));
    CHECK(Fixture::passes(r, f, {{"visible", "none"}}));
    CHECK_FALSE(Fixture::passes(r, f, {{"visible", "false"}}));
    CHECK(Fixture::passes(r, f, {{"visible", "true"}}));
  }

  SECTION("a device whose lights are hidden by default")
  {
    const auto r = f.judge(render(black, false, false, lit));
    CHECK_FALSE(Fixture::passes(r, f, {{"visible", "none"}}));
    CHECK_FALSE(Fixture::passes(r, f, {{"visible", "true"}}));
  }

  SECTION("a device whose hidden light stops lighting the surface")
  {
    const auto r = f.judge(render(grey, true, false, unlit));
    CHECK_FALSE(Fixture::passes(r, f, {{"visible", "false"}}));
  }

  SECTION("a device that shows a hidden light's footprint as something else")
  {
    // Not what the scene without the light renders there.
    auto wrong = [&](const std::map<std::string, std::string> &v) {
      Image im = frame(
          grey, v.at("visible") != "false" && v.at("visible") != "absent", lit);
      if (v.at("visible") == "absent")
        im = frame(grey, false, unlit);
      if (v.at("visible") == "false")
        paintScreenRect(im, 0.2f, 0.7f, 0.8f, 1.f, Rgba{30, 30, 30, 255});
      return std::vector<ChannelValues>{colorChannel(im)};
    };
    CHECK_FALSE(Fixture::passes(f.judge(wrong), f, {{"visible", "false"}}));
  }
}

TEST_CASE("hdri_primary_visibility's footprint is everywhere but the surface",
    "[cts][small-extensions]")
{
  Fixture f("light/hdri_primary_visibility");
  const Rgba environment{255, 128, 26, 255};
  auto frame = [&](Rgba background, bool shown) {
    Image im =
        solid(kSize, kSize, background.r, background.g, background.b, 255);
    if (shown)
      paintScreenRect(im, 0.f, 0.f, 1.f, 1.f, environment);
    paintScreenRect(im, 0.1f, 0.05f, 0.9f, 0.45f, Rgba{200, 200, 160, 255});
    return im;
  };
  auto render = [&](Rgba background, bool hiddenShown) {
    return [=](const std::map<std::string, std::string> &v) {
      const bool shown =
          (v.at("visible") != "false" && v.at("visible") != "absent")
          || (v.at("visible") == "false" && hiddenShown);
      return std::vector<ChannelValues>{colorChannel(frame(background, shown))};
    };
  };
  // A hidden light leaves the renderer's background, whatever it is.
  for (const Rgba background : {Rgba{0, 0, 0, 255}, Rgba{40, 60, 90, 255}}) {
    const auto good = f.judge(render(background, false));
    for (const char *v : {"none", "false", "true", "absent"})
      CHECK(Fixture::passes(good, f, {{"visible", v}}));
  }
  CHECK_FALSE(Fixture::passes(
      f.judge(render(Rgba{0, 0, 0, 255}, true)), f, {{"visible", "false"}}));
}

// Transform arrays ///////////////////////////////////////////////////////////

namespace {

// The triangle and placements of the transform-array Tests, as the Tests
// define them.
using Tri = std::array<float2, 3>;

std::vector<Tri> quadrantTriangles()
{
  const float3 base[3] = {float3(0.f, 0.12f, 0.f),
      float3(-0.1f, -0.08f, 0.f),
      float3(0.12f, -0.06f, 0.f)};
  struct Placement
  {
    float x, y, angle, scale;
  };
  const float pi = 3.14159265f;
  const Placement placements[4] = {{-0.5f, 0.5f, 0.f, 1.f},
      {0.5f, 0.5f, 0.5f * pi, 1.4f},
      {-0.5f, -0.5f, pi, 1.f},
      {0.5f, -0.5f, -0.25f * pi, 1.8f}};
  std::vector<Tri> out;
  for (const auto &p : placements) {
    Tri t;
    for (int k = 0; k < 3; ++k) {
      const float c = p.scale * std::cos(p.angle),
                  s = p.scale * std::sin(p.angle);
      const float wx = c * base[k].x - s * base[k].y + p.x;
      const float wy = s * base[k].x + c * base[k].y + p.y;
      t[k] = float2((wx + 1.f) * 0.5f * kSize, (wy + 1.f) * 0.5f * kSize);
    }
    out.push_back(t);
  }
  return out;
}

float cross2(float2 a, float2 b)
{
  return a.x * b.y - a.y * b.x;
}

bool insideTriangle(const Tri &t, float2 p)
{
  const float d0 = cross2(t[1] - t[0], p - t[0]);
  const float d1 = cross2(t[2] - t[1], p - t[1]);
  const float d2 = cross2(t[0] - t[2], p - t[2]);
  return (d0 >= 0.f && d1 >= 0.f && d2 >= 0.f)
      || (d0 <= 0.f && d1 <= 0.f && d2 <= 0.f);
}

// An instanceId channel: instance i's pixels hold `idOf(i)`, the rest `none`.
ChannelValues instanceIds(
    const std::function<uint32_t(size_t)> &idOf, uint32_t none = ~0u)
{
  const auto tris = quadrantTriangles();
  return cts_test::idChannel(
      kSize,
      kSize,
      [&](uint32_t x, uint32_t y) {
        for (size_t i = 0; i < tris.size(); ++i)
          if (insideTriangle(tris[i], float2(x + 0.5f, y + 0.5f)))
            return idOf(i);
        return none;
      },
      Channel::InstanceId);
}

} // namespace

TEST_CASE(
    "transform_array_ids checks each instance's id", "[cts][small-extensions]")
{
  Fixture f("instance/transform_array_ids");
  CHECK_FALSE(f.test->usesGroundTruth());
  const uint32_t ids[4] = {7, 21, 3, 40};
  auto render = [&](std::function<uint32_t(size_t)> perInstance,
                    std::function<uint32_t(size_t)> shared,
                    uint32_t none = ~0u) {
    return [=](const std::map<std::string, std::string> &v) {
      auto idOf = v.at("ids") == "perInstance" ? perInstance : shared;
      return std::vector<ChannelValues>{
          colorChannel(solid(kSize, kSize, 90, 60, 30, 255)),
          instanceIds(idOf, none)};
    };
  };
  using IdOf = std::function<uint32_t(size_t)>;
  const IdOf correctPer = [&](size_t i) { return ids[i]; };
  const IdOf correctShared = [](size_t) { return 42u; };
  auto all = [&](const Fixture::Render &r) { return f.judge(r); };
  auto everyCasePasses = [&](const SelfCheckResults &r) {
    for (const char *form : {"instances", "array"})
      for (const char *mode : {"perInstance", "shared"})
        if (!Fixture::passes(r, f, {{"form", form}, {"ids", mode}}))
          return false;
    return true;
  };

  SECTION("a device implementing it")
  {
    CHECK(everyCasePasses(all(render(correctPer, correctShared))));
  }

  SECTION("a device that gives every element the first id")
  {
    const auto r = all(render([&](size_t) { return ids[0]; }, correctShared));
    CHECK_FALSE(
        Fixture::passes(r, f, {{"form", "array"}, {"ids", "perInstance"}}));
  }

  SECTION("a device that ignores the id array and uses the element index")
  {
    const auto r =
        all(render([](size_t i) { return uint32_t(i); }, correctShared));
    CHECK_FALSE(
        Fixture::passes(r, f, {{"form", "array"}, {"ids", "perInstance"}}));
  }

  SECTION("a device that ignores the single id of an array")
  {
    const auto r =
        all(render(correctPer, [](size_t i) { return uint32_t(i); }));
    CHECK_FALSE(Fixture::passes(r, f, {{"form", "array"}, {"ids", "shared"}}));
  }

  SECTION("a device whose background holds an instance's id")
  {
    const auto r = all(render(correctPer, correctShared, ids[1]));
    CHECK_FALSE(
        Fixture::passes(r, f, {{"form", "array"}, {"ids", "perInstance"}}));
  }

  SECTION("a device whose array form differs from separate instances")
  {
    // The array form's colour omits the last instance.
    auto wrong = [&](const std::map<std::string, std::string> &v) {
      Image im = solid(kSize, kSize, 90, 60, 30, 255);
      if (v.at("form") == "array")
        im = solid(kSize, kSize, 0, 0, 0, 255);
      return std::vector<ChannelValues>{colorChannel(im),
          instanceIds(
              v.at("ids") == "perInstance" ? correctPer : correctShared)};
    };
    CHECK_FALSE(Fixture::passes(
        f.judge(wrong), f, {{"form", "array"}, {"ids", "perInstance"}}));
  }
}

TEST_CASE("transform_array and transform_array_region compare with their equal",
    "[cts][small-extensions]")
{
  // One Test per equivalence: the array of matrices against what it must equal.
  struct Pair
  {
    const char *id, *axis, *reference, *other;
  };
  for (const Pair &p :
      {Pair{"instance/transform_array", "form", "instances", "array"},
          Pair{"instance/transform_array_region",
              "array",
              "shortArray",
              "region"},
          Pair{"geometry/array1d_region",
              "array",
              "shortArray",
              "vertexRegionAfterStart"}}) {
    INFO(p.id);
    Fixture f(p.id);
    CHECK_FALSE(f.test->usesGroundTruth());
    auto channels = [&](Image color) {
      std::vector<ChannelValues> out;
      for (Channel c : f.test->channels) {
        ChannelValues cv;
        cv.channel = c;
        cv.image = color;
        out.push_back(std::move(cv));
      }
      return out;
    };
    const Image subject = cts_test::rectangle(
        kSize, kSize, 8, 40, 56, 56, Rgba{200, 150, 100, 255});
    const Image extra = cts_test::rectangle(
        kSize, kSize, 8, 8, 56, 56, Rgba{200, 150, 100, 255});
    auto judge = [&](const Image &other) {
      return f.judge([&](const std::map<std::string, std::string> &v) {
        return channels(v.at(p.axis) == p.reference ? subject : other);
      });
    };
    CHECK(Fixture::passes(judge(subject), f, {{p.axis, p.other}}));
    // A device that draws more than the region (or array) names.
    CHECK_FALSE(Fixture::passes(judge(extra), f, {{p.axis, p.other}}));
  }
}

// Array region ///////////////////////////////////////////////////////////////

TEST_CASE("array1d_region_changes shows each region and the whole array",
    "[cts][small-extensions]")
{
  Fixture f("geometry/array1d_region_changes");
  CHECK_FALSE(f.test->usesGroundTruth());
  auto channels = [&](Image color) {
    std::vector<ChannelValues> out;
    for (Channel c : f.test->channels) {
      ChannelValues cv;
      cv.channel = c;
      cv.image = color;
      out.push_back(std::move(cv));
    }
    return out;
  };
  const Rgba fg{200, 150, 100, 255};
  const Image left = cts_test::rectangle(kSize, kSize, 8, 8, 24, 56, fg);
  const Image right = cts_test::rectangle(kSize, kSize, 40, 8, 56, 56, fg);
  Image both = left;
  for (uint32_t y = 8; y < 56; ++y)
    for (uint32_t x = 40; x < 56; ++x)
      setPixel(both, x, y, fg);

  auto judge = [&](const std::map<std::string, Image> &images) {
    return f.judge([&](const std::map<std::string, std::string> &v) {
      return channels(images.at(v.at("region")));
    });
  };

  SECTION("a device implementing it")
  {
    const auto r = judge({{"first", left}, {"second", right}, {"unset", both}});
    // `first` is only ever the earlier partner of a pair: no assertion of its
    // own.
    for (const char *v : {"unset", "second"})
      CHECK(Fixture::passes(r, f, {{"region", v}}));
  }

  SECTION("a device that ignores the region")
  {
    const auto r = judge({{"first", both}, {"second", both}, {"unset", both}});
    CHECK_FALSE(Fixture::passes(r, f, {{"region", "second"}}));
  }

  SECTION("a device whose unset region draws only the first element")
  {
    const auto r = judge({{"first", left}, {"second", right}, {"unset", left}});
    CHECK_FALSE(Fixture::passes(r, f, {{"region", "unset"}}));
  }
}

// Denoise ////////////////////////////////////////////////////////////////////

TEST_CASE("renderer_denoise requires a lit surface with denoise on and off",
    "[cts][small-extensions]")
{
  Fixture f("renderer/renderer_denoise");
  CHECK_FALSE(f.test->usesGroundTruth());
  auto render = [&](bool denoisedBlank) {
    return [=](const std::map<std::string, std::string> &v) {
      const bool blank = denoisedBlank && v.at("denoise") == "true";
      return std::vector<ChannelValues>{
          colorChannel(blank ? solid(kSize, kSize, 0, 0, 0, 255)
                             : solid(kSize, kSize, 160, 120, 80, 255))};
    };
  };
  const auto good = f.judge(render(false));
  CHECK(Fixture::passes(good, f, {{"denoise", "false"}}));
  CHECK(Fixture::passes(good, f, {{"denoise", "true"}}));
  const auto blank = f.judge(render(true));
  CHECK(Fixture::passes(blank, f, {{"denoise", "false"}}));
  CHECK_FALSE(Fixture::passes(blank, f, {{"denoise", "true"}}));
}

// Introspection checks ///////////////////////////////////////////////////////

TEST_CASE("the parameter-query Tests judge what a device lists",
    "[cts][small-extensions][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  // The helper finds a parameter the device lists, by name and type only.
  CHECK(listsParameter(d, ANARI_FRAME, nullptr, "size", ANARI_UINT32_VEC2));
  CHECK_FALSE(listsParameter(d, ANARI_FRAME, nullptr, "size", ANARI_BOOL));
  CHECK_FALSE(listsParameter(
      d, ANARI_FRAME, nullptr, "mpiCommunicator", ANARI_VOID_POINTER));

  // helide implements neither extension, so both Tests fail on it.
  Catalog catalog;
  registerBuiltinTests(catalog);
  for (const char *id : {"frame/frame_mpi_communicator_parameter",
           "renderer/renderer_denoise_parameter"}) {
    INFO(id);
    const auto found = catalog.filter(Filter{id});
    REQUIRE(found.size() == 1);
    REQUIRE(found.front()->behaviorCheck);
    const auto result = found.front()->behaviorCheck(
        d, nullptr, nullptr, nullptr, kSize, kSize);
    INFO(result.detail);
    CHECK_FALSE(result.passed);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("every small extension has a Test", "[cts][small-extensions]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  std::map<std::string, int> primary;
  for (const TestDef *t : catalog.filter(Filter{""}))
    for (const auto &e : t->coveredFeatures)
      ++primary[e];
  for (const char *e : {"ANARI_KHR_ARRAY1D_REGION",
           "ANARI_KHR_INSTANCE_TRANSFORM_ARRAY",
           "ANARI_KHR_LIGHT_PRIMARY_VISIBILITY",
           "ANARI_KHR_RENDERER_DENOISE",
           "ANARI_KHR_DATA_PARALLEL_MPI"}) {
    INFO(e);
    CHECK(primary[e] >= 1);
  }
}
