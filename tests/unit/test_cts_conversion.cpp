// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The checks the conversion to self-checks added to the existing Tests, judged
// through the evaluation seam with images generated in code (ADR-0010): what a
// device that follows the spec renders passes, what one that ignores it renders
// fails. No SDK device implements all of them.

#include "catch.hpp"
#include "cts_test_images.h"
// anari_test_scenes
#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/ExactCheck.h"
#include "cts/Expansion.h"
#include "cts/Image.h"
#include "cts/Relations.h"
#include "cts/tests/FrameTypeAxis.h"
// std
#include <map>
#include <string>
#include <vector>

using namespace anari::cts;
using cts_test::solid;

namespace {

constexpr uint32_t kSize = 64;

struct Fixture
{
  Catalog catalog;
  const TestDef *test{nullptr};
  std::vector<Case> cases;

  explicit Fixture(const std::string &id)
  {
    registerBuiltinTests(catalog);
    for (const auto &t : catalog.tests())
      if (t.id() == id)
        test = &t;
    REQUIRE(test != nullptr);
    cases = expand(*test);
  }

  const Case &caseWithId(const std::string &id) const
  {
    for (const auto &c : cases)
      if (c.id() == id)
        return c;
    FAIL("no Case " << id);
    return cases.front();
  }
};

ChannelValues colorChannel(const Image &image)
{
  ChannelValues v;
  v.channel = Channel::Color;
  v.image = image;
  return v;
}

// Whether every exact check of `c` holds on `image`.
bool exactPasses(
    const Fixture &f, const std::string &caseId, const Image &image)
{
  const auto outcomes = evaluateExactChecks(*f.test,
      f.caseWithId(caseId),
      {image.width, image.height},
      {colorChannel(image)},
      std::nullopt);
  REQUIRE_FALSE(outcomes.empty());
  for (const auto &o : outcomes) {
    INFO(o.assertion.detail);
    if (!o.assertion.passed)
      return false;
  }
  return true;
}

} // namespace

TEST_CASE(
    "the sRGB frame type is the linear colours, encoded", "[cts][conversion]")
{
  Fixture f("frame/frame_color_channel");
  CHECK_FALSE(f.test->usesGroundTruth());
  // A mid-grey subject on black: the encoding moves its bytes.
  const Image linear = cts_test::rectangle(
      kSize, kSize, 8, 8, 56, 56, {60, 80, 70, 255}, {0, 0, 0, 255});
  std::map<std::string, Image> images = {{"UFIXED8_VEC4", linear},
      {"FLOAT32_VEC4", linear},
      {"UFIXED8_RGBA_SRGB", srgbEncoded(linear)}};

  auto passes = [&](const std::string &value) {
    std::vector<CaseRender> renders(f.cases.size());
    for (size_t i = 0; i < f.cases.size(); ++i) {
      renders[i].state = CaseRender::State::Rendered;
      renders[i].images = {
          images.at(anyToString(f.cases[i].values.front().value))};
    }
    bool found = false;
    for (const auto &o : evaluateRelations(*f.test, f.cases, renders))
      if (anyToString(f.cases[o.caseIndex].values.front().value) == value) {
        found = true;
        INFO(value << ": " << o.assertion.detail);
        if (!o.assertion.passed)
          return false;
      }
    return found;
  };

  SECTION("a device that encodes the sRGB type")
  {
    CHECK(passes("UFIXED8_RGBA_SRGB"));
    CHECK(passes("FLOAT32_VEC4"));
  }
  SECTION("a device that writes the linear bytes to the sRGB type")
  {
    images["UFIXED8_RGBA_SRGB"] = linear;
    CHECK_FALSE(passes("UFIXED8_RGBA_SRGB"));
  }
  SECTION("a device that encodes the float type")
  {
    images["FLOAT32_VEC4"] = srgbEncoded(linear);
    CHECK_FALSE(passes("FLOAT32_VEC4"));
  }
}

TEST_CASE("clip planes that remove the subject leave only background",
    "[cts][conversion]")
{
  Fixture f("camera/perspective");
  CHECK_FALSE(f.test->usesGroundTruth());
  const Image background = solid(kSize, kSize, 0, 0, 0, 255);
  const Image subject = cts_test::rectangle(
      kSize, kSize, 8, 8, 56, 56, {180, 130, 80, 255}, {0, 0, 0, 255});

  // The `near` and `far` Cases are named by their four axis values.
  const std::string clippedNear = "none_none_50_100";
  const std::string clippedFar = "none_none_0.01_0.5";
  const std::string unclipped = "none_none_0.01_100";

  SECTION("a device that clips")
  {
    CHECK(exactPasses(f, clippedNear, background));
    CHECK(exactPasses(f, clippedFar, background));
    CHECK(exactPasses(f, unclipped, subject));
  }
  SECTION("a device that ignores the clip planes")
  {
    CHECK_FALSE(exactPasses(f, clippedNear, subject));
    CHECK_FALSE(exactPasses(f, clippedFar, subject));
  }
  SECTION("a device that shows nothing at all")
  {
    CHECK_FALSE(exactPasses(f, unclipped, background));
  }
}
