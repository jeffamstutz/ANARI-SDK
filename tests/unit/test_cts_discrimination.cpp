// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The guard against indistinguishable Ground truth, with images generated in
// code: no device, no image files.

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/BuiltinTests.h"
#include "cts/Discrimination.h"
#include "cts/Expansion.h"
#include "cts/TestBuilder.h"
// std
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace anari::cts;
using cts_test::rectangle;

namespace {

constexpr uint32_t kSize = 64;
const GroundTruthThresholds kDefaults{0.70, 20.0};

// A Case's pairs as "<axis>:<first id>|<second id>", for readable checks.
std::set<std::string> pairNames(const TestDef &test)
{
  const auto cases = expand(test);
  std::set<std::string> out;
  for (const auto &p : discriminationPairs(test, cases))
    out.insert(test.axes[p.axis].name + ":" + cases[p.first].id() + "|"
        + cases[p.second].id());
  return out;
}

// A square whose position encodes `k`: distinct k give distinct images.
Image subject(int k)
{
  const uint32_t x0 = static_cast<uint32_t>(k) * 20;
  return rectangle(kSize, kSize, x0, 10, x0 + 20, 40, {200, 150, 100, 255});
}

// The value an axis holds in a Case, as a string.
std::string valueOf(const Case &c, const std::string &axis)
{
  for (const auto &cv : c.values)
    if (cv.axisName == axis)
      return anyToString(cv.value);
  return {};
}

} // namespace

// Pairing ////////////////////////////////////////////////////////////////////

TEST_CASE("cartesian Tests pair all values of an axis within each group",
    "[cts][discrimination]")
{
  const auto test = makeTest("demo", "cartesian")
                        .permute("a", {"a0", "a1"})
                        .permute("b", {"b0", "b1", "b2"})
                        .take();
  CHECK(pairNames(test)
      == std::set<std::string>{
          "a:a0_b0|a1_b0",
          "a:a0_b1|a1_b1",
          "a:a0_b2|a1_b2",
          "b:a0_b0|a0_b1",
          "b:a0_b0|a0_b2",
          "b:a0_b1|a0_b2",
          "b:a1_b0|a1_b1",
          "b:a1_b0|a1_b2",
          "b:a1_b1|a1_b2",
      });
}

TEST_CASE("simplified Tests pair each axis's values around the baseline",
    "[cts][discrimination]")
{
  const auto test = makeTest("demo", "simple")
                        .simplified()
                        .permute("a", {"a0", "a1", "a2"})
                        .permute("b", {"b0", "b1"})
                        .take();
  CHECK(pairNames(test)
      == std::set<std::string>{
          "a:a0_b0|a1_b0",
          "a:a0_b0|a2_b0",
          "a:a1_b0|a2_b0",
          "b:a0_b0|a0_b1",
      });
}

TEST_CASE(
    "Variant axes are not pairing axes, and a pair of Ground-truth "
    "files is listed once",
    "[cts][discrimination]")
{
  const auto test = makeTest("demo", "variants")
                        .permute("a", {"a0", "a1"})
                        .variant("v", {"soup", "indexed"})
                        .take();
  // Cartesian: axis a has a group per v value, but both compare a0 with a1.
  const auto pairs = pairNames(test);
  REQUIRE(pairs.size() == 1);
  CHECK(*pairs.begin() == "a:a0_soup|a1_soup");
}

TEST_CASE("frame type axes and opted-out axes are not pairing axes",
    "[cts][discrimination]")
{
  CHECK(isFrameTypeAxis("frame_color_type"));
  CHECK(isFrameTypeAxis("frame_depth_type"));
  CHECK_FALSE(isFrameTypeAxis("frame_color"));
  CHECK_FALSE(isFrameTypeAxis("color_type"));

  const auto test =
      makeTest("demo", "formats")
          .permute("frame_color_type", {"UFLOAT32_VEC4", "UFIXED8_VEC4"})
          .permute("filter", {"nearest", "linear"})
          .permute("a", {"a0", "a1"})
          .noEffectCheck("filter", "too subtle to judge")
          .take();
  // The opt-out's reason already says the image can't show `filter`, so only
  // `a` pairs remain: one per frame type x filter.
  for (const auto &p : pairNames(test))
    CHECK(p.rfind("a:", 0) == 0);
  CHECK(pairNames(test).size() == 4);
}

TEST_CASE("a Test with one Case has no pairs", "[cts][discrimination]")
{
  CHECK(pairNames(makeTest("demo", "single").take()).empty());
  CHECK(pairNames(makeTest("demo", "one").permute("a", {"a0"}).take()).empty());
}

// Scoring ////////////////////////////////////////////////////////////////////

TEST_CASE("a pair is indistinguishable when one passes as the other",
    "[cts][discrimination]")
{
  const auto test = makeTest("demo", "score").take();
  const auto same =
      scoreGroundTruthPair(test, {subject(0)}, {subject(0)}, kDefaults);
  CHECK(same.indistinguishable);
  REQUIRE(same.channels.size() == 1);
  CHECK(same.channels[0].passed);

  const auto moved =
      scoreGroundTruthPair(test, {subject(0)}, {subject(2)}, kDefaults);
  CHECK_FALSE(moved.indistinguishable);
}

TEST_CASE("scoring uses the Test's own thresholds", "[cts][discrimination]")
{
  // A small change, which a lax PSNR threshold lets through.
  const Image a = subject(0);
  Image b = a;
  for (size_t x = 0; x < 20; ++x) // a row inside the square
    b.rgba[(20 * kSize + x) * 4] = 0;
  const auto strict = makeTest("demo", "strict").threshold("psnr", 60.0).take();
  const auto lax = makeTest("demo", "lax").take();
  CHECK_FALSE(
      scoreGroundTruthPair(strict, {a}, {b}, kDefaults).indistinguishable);
  CHECK(scoreGroundTruthPair(lax, {a}, {b}, kDefaults).indistinguishable);
}

TEST_CASE("every Channel must pass for a pair to be indistinguishable",
    "[cts][discrimination]")
{
  const auto test = makeTest("demo", "channels")
                        .channels({Channel::Color, Channel::Depth})
                        .take();
  CHECK_FALSE(scoreGroundTruthPair(
      test, {subject(0), subject(0)}, {subject(0), subject(2)}, kDefaults)
          .indistinguishable);
  CHECK_FALSE(scoreGroundTruthPair(test, {subject(0)}, {subject(0)}, kDefaults)
          .indistinguishable);
}

// The check //////////////////////////////////////////////////////////////////

namespace {

// Ground truth that renders values of axis `a` by the table, the others alike.
GroundTruthLoader loader(
    std::map<std::string, int> position, std::set<std::string> missing = {})
{
  return [position, missing](const TestDef &test, const Case &c) {
    std::vector<Image> out;
    if (missing.count(c.id()))
      return out;
    for (size_t i = 0; i < test.channels.size(); ++i)
      out.push_back(subject(position.at(valueOf(c, "a"))));
    return out;
  };
}

} // namespace

TEST_CASE(
    "the check reports each indistinguishable value pair once, over "
    "its groups",
    "[cts][discrimination]")
{
  const auto test = makeTest("demo", "check")
                        .permute("a", {"a0", "a1", "a2"})
                        .permute("b", {"b0", "b1"})
                        .take();
  // a0 and a1 render alike; a2 differs; b changes nothing (both b values
  // render alike too).
  const auto result = checkDiscrimination(
      {&test}, {}, kDefaults, loader({{"a0", 0}, {"a1", 0}, {"a2", 2}}));
  CHECK(result.tests == 1);
  CHECK(result.pairsCompared == 9); // a: 3 pairs x 2 groups; b: 1 x 3
  CHECK(result.pairsIndistinguishable == 5); // a0|a1 twice, b0|b1 thrice
  CHECK(result.pairsUnchecked == 0);
  REQUIRE(result.findings.size() == 2);
  const auto &a = result.findings[0];
  CHECK(a.test == "demo/check");
  CHECK(a.axis == "a");
  CHECK(a.first == "a0");
  CHECK(a.second == "a1");
  CHECK(a.indistinguishable == 2);
  CHECK(a.compared == 2);
  CHECK(a.exampleFirst == "a0_b0");
  CHECK(a.exampleSecond == "a1_b0");
  CHECK_FALSE(a.allowedBecause);
  const auto &b = result.findings[1];
  CHECK(b.axis == "b");
  CHECK(b.indistinguishable == 3);
  CHECK(result.notAllowed() == 2);
}

TEST_CASE(
    "the allow-list allows a value pair in either order, and reports "
    "entries that no longer apply",
    "[cts][discrimination]")
{
  const auto test =
      makeTest("demo", "allow").permute("a", {"a0", "a1", "a2"}).take();
  const std::vector<Allowance> allow{
      {"demo/allow", "a", "a1", "a0", "renders alike by design"},
      {"demo/allow", "a", "a0", "a2", "used to render alike"},
      {"demo/other", "a", "a0", "a1", "a Test not checked here"},
  };
  const auto result = checkDiscrimination(
      {&test}, allow, kDefaults, loader({{"a0", 0}, {"a1", 0}, {"a2", 2}}));
  REQUIRE(result.findings.size() == 1);
  CHECK(result.findings[0].allowedBecause == "renders alike by design");
  CHECK(result.notAllowed() == 0);
  REQUIRE(result.stale.size() == 1);
  CHECK(result.stale[0].reason == "used to render alike");
}

TEST_CASE("pairs without Ground truth are counted, not compared",
    "[cts][discrimination]")
{
  const auto test =
      makeTest("demo", "missing").permute("a", {"a0", "a1", "a2"}).take();
  const auto result = checkDiscrimination({&test},
      {},
      kDefaults,
      loader({{"a0", 0}, {"a1", 0}, {"a2", 0}}, {"a2"}));
  CHECK(result.pairsCompared == 1);
  CHECK(result.pairsUnchecked == 2);
  CHECK(result.findings.size() == 1);
}

TEST_CASE("Tests without Ground truth are not checked", "[cts][discrimination]")
{
  const auto test = makeTest("demo", "selfChecked")
                        .permute("a", {"a0", "a1"})
                        .noGroundTruth()
                        .take();
  const auto result = checkDiscrimination(
      {&test}, {}, kDefaults, loader({{"a0", 0}, {"a1", 0}}));
  CHECK(result.tests == 0);
  CHECK(result.pairsCompared == 0);
  CHECK(result.findings.empty());
}

TEST_CASE("the report names the Test, axis and values of a finding",
    "[cts][discrimination]")
{
  const auto test = makeTest("demo", "text").permute("a", {"a0", "a1"}).take();
  const auto result = checkDiscrimination(
      {&test}, {}, kDefaults, loader({{"a0", 0}, {"a1", 0}}));
  std::ostringstream os;
  writeDiscriminationReport(os, result, false);
  const auto text = os.str();
  CHECK(text.find("demo/text") != std::string::npos);
  CHECK(text.find("`a0` vs `a1`") != std::string::npos);
  CHECK(text.find("1 not allowed") != std::string::npos);
}

// The built-in allow-list ////////////////////////////////////////////////////

TEST_CASE(
    "every built-in allow-list entry names a pairing axis and two of "
    "its values",
    "[cts][discrimination]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  std::set<std::pair<std::string, std::string>> seen;
  for (const auto &entry : builtinIndistinguishableAllowList()) {
    INFO(entry.test << " " << entry.axis << " " << entry.first << " "
                    << entry.second);
    CHECK_FALSE(entry.reason.empty());
    CHECK(entry.first != entry.second);
    const TestDef *test = nullptr;
    for (const auto &t : catalog.tests())
      if (t.id() == entry.test)
        test = &t;
    REQUIRE(test);
    const Axis *axis = nullptr;
    for (const auto &ax : test->axes)
      if (ax.name == entry.axis)
        axis = &ax;
    REQUIRE(axis);
    CHECK(axis->kind == AxisKind::Permutation);
    CHECK_FALSE(isFrameTypeAxis(axis->name));
    std::set<std::string> values;
    for (const auto &v : axis->values)
      values.insert(anyToString(v));
    CHECK(values.count(entry.first));
    CHECK(values.count(entry.second));
    // No duplicates, in either order.
    const auto key = entry.test + "\n" + entry.axis + "\n"
        + std::min(entry.first, entry.second) + "\n"
        + std::max(entry.first, entry.second);
    CHECK(seen.insert({key, ""}).second);
  }
}
