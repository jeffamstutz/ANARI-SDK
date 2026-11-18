// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The Tests for the ANARI 1.2 changes, judged through the evaluation seam
// with images generated in code (ADR-0010): what a device implementing the
// behaviour renders passes, what one ignoring it renders fails. No SDK device
// implements all of them, so this is their validation without a device.

#include "catch.hpp"
#include "cts_test_images.h"
// anari_test_scenes
#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/Filter.h"
#include "cts/Image.h"
#include "cts/Relations.h"
#include "cts/ScreenQuad.h"
#include "cts/Value.h"
// std
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace anari::cts;
using cts_test::Rgba;
using cts_test::solid;

namespace {

constexpr uint32_t kSize = 64;

// One built-in Test, its Cases, and a way to judge images for them.
struct Fixture
{
  Catalog catalog;
  const TestDef *test{nullptr};
  std::vector<Case> cases;

  explicit Fixture(const std::string &id)
  {
    registerBuiltinTests(catalog);
    const auto found = catalog.filter(Filter{id});
    REQUIRE(found.size() == 1);
    test = found.front();
    cases = expand(*test);
  }

  // The Case holding `value` on the Test's only axis.
  size_t index(const std::string &value) const
  {
    for (size_t i = 0; i < cases.size(); ++i)
      if (anyToString(cases[i].values.front().value) == value)
        return i;
    FAIL("no Case " << value);
    return 0;
  }

  // Every relation attributed to the Case holding `value`, given each
  // value's colour image.
  std::vector<RelationOutcome> judge(
      const std::map<std::string, Image> &images, const std::string &value)
  {
    std::vector<CaseRender> renders(cases.size());
    for (size_t i = 0; i < cases.size(); ++i) {
      const auto it = images.find(anyToString(cases[i].values.front().value));
      REQUIRE(it != images.end());
      renders[i].state = CaseRender::State::Rendered;
      renders[i].images = {it->second};
    }
    std::vector<RelationOutcome> out;
    for (auto &o : evaluateRelations(*test, cases, renders))
      if (o.caseIndex == index(value))
        out.push_back(o);
    return out;
  }

  bool passes(
      const std::map<std::string, Image> &images, const std::string &value)
  {
    const auto outcomes = judge(images, value);
    if (outcomes.empty())
      return false;
    for (const auto &o : outcomes) {
      INFO(value << ": " << o.assertion.detail);
      if (!o.assertion.passed)
        return false;
    }
    return true;
  }
};

// `in` at pixels whose centre satisfies `region`, `out` elsewhere.
Image paint(const std::function<bool(float, float)> &region,
    const Image &in,
    const Image &out)
{
  Image im = out;
  for (uint32_t y = 0; y < im.height; ++y)
    for (uint32_t x = 0; x < im.width; ++x)
      if (region((x + 0.5f) / im.width, (y + 0.5f) / im.height))
        std::copy_n(&in.rgba[(size_t(y) * im.width + x) * 4],
            4,
            &im.rgba[(size_t(y) * im.width + x) * 4]);
  return im;
}

bool outsideImage(int dim, float x, float y)
{
  if (dim == 1)
    return x < 0.25f || x > 0.75f;
  if (dim == 2)
    return y < 0.25f || y > 0.75f;
  return y > 0.5f;
}

} // namespace

TEST_CASE("image2D_clamp_to_border tells clampToBorder from clampToEdge",
    "[cts][anari12]")
{
  Fixture f("sampler/image2D_clamp_to_border");
  CHECK_FALSE(f.test->usesGroundTruth());
  const Image edge = cts_test::gradient(kSize, kSize);
  const Image border = solid(kSize, kSize, 230, 38, 153, 255);
  const Image black = solid(kSize, kSize, 0, 0, 0, 255);
  auto bordered = [&](int dim, const Image &colour) {
    return paint([dim](float x, float y) { return outsideImage(dim, x, y); },
        colour,
        edge);
  };
  std::map<std::string, Image> images = {{"clampToEdge", edge},
      {"borderColorSurface", border},
      {"clampToBorder1", bordered(1, border)},
      {"clampToBorder2", bordered(2, border)},
      {"zeroBorderColor", bordered(1, black)},
      {"defaultBorderColor", bordered(1, black)}};

  SECTION("a device implementing it")
  {
    for (const char *v : {"clampToBorder1",
             "clampToBorder2",
             "zeroBorderColor",
             "defaultBorderColor"})
      CHECK(f.passes(images, v));
  }

  SECTION("a device that clamps to the edge instead")
  {
    for (const char *v : {"clampToBorder1",
             "clampToBorder2",
             "zeroBorderColor",
             "defaultBorderColor"})
      images[v] = edge;
    CHECK_FALSE(f.passes(images, "clampToBorder1"));
    CHECK_FALSE(f.passes(images, "clampToBorder2"));
    CHECK_FALSE(f.passes(images, "zeroBorderColor"));
  }

  SECTION("a device applying wrapMode2 to the first coordinate")
  {
    images["clampToBorder2"] = bordered(1, border);
    CHECK_FALSE(f.passes(images, "clampToBorder2"));
  }

  SECTION("a device whose default borderColor is opaque black")
  {
    // The explicit (0, 0, 0, 0) renders transparent; the default doesn't.
    images["zeroBorderColor"] = bordered(1, solid(kSize, kSize, 9, 9, 9, 255));
    CHECK_FALSE(f.passes(images, "defaultBorderColor"));
  }
}

TEST_CASE("image3D_clamp_to_border judges the third coordinate on its own",
    "[cts][anari12]")
{
  Fixture f("sampler/image3D_clamp_to_border");
  const Image edge = cts_test::gradient(kSize, kSize);
  const Image border = solid(kSize, kSize, 230, 38, 153, 255);
  auto bordered = [&](int dim) {
    return paint([dim](float x, float y) { return outsideImage(dim, x, y); },
        border,
        edge);
  };
  std::map<std::string, Image> images = {{"clampToEdge", edge},
      {"borderColorSurface", border},
      {"clampToBorder1", bordered(1)},
      {"clampToBorder2", bordered(2)},
      {"clampToBorder3", bordered(3)},
      {"zeroBorderColor", bordered(1)},
      {"defaultBorderColor", bordered(1)}};
  CHECK(f.passes(images, "clampToBorder3"));
  images["clampToBorder3"] = bordered(2);
  CHECK_FALSE(f.passes(images, "clampToBorder3"));
}

TEST_CASE(
    "matte_alpha_cutoff tells a binary cutoff from blending", "[cts][anari12]")
{
  Fixture f("material/matte_alpha_cutoff");
  CHECK_FALSE(f.test->usesGroundTruth());
  const Image opaque = solid(kSize, kSize, 230, 77, 51, 255);
  const Image none = solid(kSize, kSize, 0, 0, 0, 255);
  // Opacity at screen x, as the Test's ramp sets it.
  auto opacity = [](float x) {
    return (x + kScreenQuadOverhang) / (1.f + 2.f * kScreenQuadOverhang);
  };
  Image blended = opaque;
  for (uint32_t y = 0; y < kSize; ++y)
    for (uint32_t x = 0; x < kSize; ++x)
      for (int c = 0; c < 3; ++c) {
        auto &v = blended.rgba[(size_t(y) * kSize + x) * 4 + c];
        v = uint8_t(v * opacity((x + 0.5f) / kSize));
      }
  auto cut = [&](float c) {
    return paint([&](float x, float) { return opacity(x) >= c; }, opaque, none);
  };
  std::map<std::string, Image> images = {{"unset", blended},
      {"opaqueSurface", opaque},
      {"noSurface", none},
      {"0", opaque},
      {"0.25", cut(0.25f)},
      {"0.75", cut(0.75f)}};

  SECTION("a device implementing ANARI 1.2's alphaCutoff")
  {
    // `unset` is the first value: its effects are attributed to the others.
    for (const char *v : {"opaqueSurface", "noSurface", "0", "0.25", "0.75"})
      CHECK(f.passes(images, v));
  }

  SECTION("a device that blends whatever alphaCutoff says")
  {
    images["0.25"] = blended;
    images["0.75"] = blended;
    CHECK_FALSE(f.passes(images, "0.25"));
    CHECK_FALSE(f.passes(images, "0.75"));
  }

  SECTION("a device that ignores opacity (alphaMode opaque by default)")
  {
    for (const char *v : {"unset", "0.25", "0.75"})
      images[v] = opaque;
    CHECK_FALSE(f.passes(images, "opaqueSurface")); // no effect vs unset
    CHECK_FALSE(f.passes(images, "0.25"));
    CHECK_FALSE(f.passes(images, "0.75"));
  }

  SECTION("a device that cuts at the wrong threshold")
  {
    images["0.25"] = cut(0.75f);
    CHECK_FALSE(f.passes(images, "0.25"));
  }
}

TEST_CASE("the default-aspect Tests compare an unset aspect with 2:1",
    "[cts][anari12]")
{
  for (const char *id : {"camera/perspective_default_aspect",
           "camera/orthographic_default_aspect"}) {
    INFO(id);
    Fixture f(id);
    CHECK_FALSE(f.test->usesGroundTruth());
    // The frame is twice as wide as tall.
    const Image wide = cts_test::rectangle(
        2 * kSize, kSize, 40, 16, 88, 48, Rgba{200, 150, 100, 255});
    const Image squashed = cts_test::rectangle(
        2 * kSize, kSize, 52, 16, 76, 48, Rgba{200, 150, 100, 255});
    CHECK(f.passes({{"2", wide}, {"none", wide}}, "none"));
    CHECK_FALSE(f.passes({{"2", wide}, {"none", squashed}}, "none"));
  }
}

TEST_CASE("structured_regular_cell_centered compares with its vertex equal",
    "[cts][anari12]")
{
  // Two Tests, looking down z and down x: between them they check all three
  // axes of the field. Each compares only its own Cases.
  for (const char *id : {"volume/structured_regular_cell_centered_down_z",
           "volume/structured_regular_cell_centered_down_x"}) {
    INFO(id);
    Fixture f(id);
    CHECK_FALSE(f.test->usesGroundTruth());
    const Image cube = cts_test::rectangle(
        kSize, kSize, 16, 16, 48, 48, Rgba{90, 200, 90, 255});
    const Image smaller = cts_test::rectangle(
        kSize, kSize, 20, 20, 44, 44, Rgba{90, 200, 90, 255});
    CHECK(f.passes(
        {{"vertexCentered", cube}, {"cellCentered", cube}}, "cellCentered"));
    CHECK_FALSE(f.passes(
        {{"vertexCentered", cube}, {"cellCentered", smaller}}, "cellCentered"));
    // The default equivalence tolerance, not a loosened one: a silhouette one
    // pixel off (1.6 % of the frame here, which a 4 % tolerance would
    // pass) fails.
    const Image shifted = cts_test::rectangle(
        kSize, kSize, 17, 16, 49, 48, Rgba{90, 200, 90, 255});
    CHECK_FALSE(f.passes(
        {{"vertexCentered", cube}, {"cellCentered", shifted}}, "cellCentered"));
  }
}
