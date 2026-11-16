// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The relational pass of the self-checks (ADR-0010), with images generated in
// code: no device, no image files.

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/FrameSize.h"
#include "cts/Image.h"
#include "cts/Metrics.h"
#include "cts/Relations.h"
#include "cts/TestBuilder.h"
#include "cts/Workdir.h"
// std
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace anari::cts;
using cts_test::rectangle;
using cts_test::solid;

namespace {

constexpr uint32_t kSize = 64;

// A square subject whose position encodes `k`: distinct k render distinct
// images (each square is 10 % of the frame, so a move changes ~20 %).
Image subject(int k, uint32_t size = kSize)
{
  const uint32_t x0 = static_cast<uint32_t>(k) * 4;
  return rectangle(size, size, x0, 10, x0 + 20, 30, {200, 150, 100, 255});
}

// The value an axis holds in a Case, as a string.
std::string valueOf(const Case &c, const std::string &axis)
{
  for (const auto &cv : c.values)
    if (cv.axisName == axis)
      return anyToString(cv.value);
  return {};
}

using ImageFn = std::function<Image(const Case &)>;

std::vector<CaseRender> renderAll(
    const TestDef &test, const std::vector<Case> &cases, const ImageFn &fn)
{
  std::vector<CaseRender> out(cases.size());
  for (size_t i = 0; i < cases.size(); ++i) {
    out[i].state = CaseRender::State::Rendered;
    for (size_t ch = 0; ch < test.channels.size(); ++ch)
      out[i].images.push_back(fn(cases[i]));
  }
  return out;
}

// The outcomes attributed to the Case with id `id`.
std::vector<RelationOutcome> attributedTo(
    const std::vector<RelationOutcome> &outcomes,
    const std::vector<Case> &cases,
    const std::string &id)
{
  std::vector<RelationOutcome> out;
  for (const auto &o : outcomes)
    if (cases[o.caseIndex].id() == id)
      out.push_back(o);
  return out;
}

size_t indexOf(const std::vector<Case> &cases, const std::string &id)
{
  for (size_t i = 0; i < cases.size(); ++i)
    if (cases[i].id() == id)
      return i;
  FAIL("no Case " << id);
  return 0;
}

} // namespace

// Grouping ///////////////////////////////////////////////////////////////////

TEST_CASE(
    "simplified Tests group each axis with the baseline", "[cts][relations]")
{
  const auto test = makeTest("demo", "simple")
                        .simplified()
                        .permute("a", {"a0", "a1", "a2"})
                        .permute("b", {"b0", "b1"})
                        .take();
  const auto cases = expand(test);
  REQUIRE(cases.size() == 4); // baseline + a1 + a2 + b1
  const auto groups = relationGroups(test, cases);
  REQUIRE(groups.size() == 2);
  CHECK(groups[0].axis == 0);
  CHECK(groups[0].cases == std::vector<size_t>{0, 1, 2});
  CHECK(groups[1].axis == 1);
  CHECK(groups[1].cases == std::vector<size_t>{0, 3});
}

TEST_CASE("cartesian Tests group each axis per combination of the others",
    "[cts][relations]")
{
  const auto test = makeTest("demo", "cartesian")
                        .permute("a", {"a0", "a1"})
                        .permute("b", {"b0", "b1", "b2"})
                        .take();
  const auto cases = expand(test);
  REQUIRE(cases.size() == 6);
  const auto groups = relationGroups(test, cases);
  // Axis a: one group of 2 per b value; axis b: one group of 3 per a value.
  REQUIRE(groups.size() == 5);
  int aGroups = 0, bGroups = 0;
  for (const auto &g : groups) {
    if (g.axis == 0) {
      ++aGroups;
      REQUIRE(g.cases.size() == 2);
      CHECK(valueOf(cases[g.cases[0]], "a") == "a0");
      CHECK(valueOf(cases[g.cases[1]], "a") == "a1");
      CHECK(valueOf(cases[g.cases[0]], "b") == valueOf(cases[g.cases[1]], "b"));
    } else {
      ++bGroups;
      REQUIRE(g.cases.size() == 3);
      CHECK(valueOf(cases[g.cases[2]], "b") == "b2");
    }
  }
  CHECK(aGroups == 3);
  CHECK(bGroups == 2);
}

// Effect /////////////////////////////////////////////////////////////////////

TEST_CASE(
    "an effect passes when each value renders differently", "[cts][relations]")
{
  const auto test =
      makeTest("demo", "effect").permute("k", {"0", "1", "2"}).take();
  const auto cases = expand(test);
  const auto renders = renderAll(test, cases, [](const Case &c) {
    return subject(std::stoi(valueOf(c, "k")));
  });
  const auto outcomes = evaluateRelations(test, cases, renders);
  // All pairs: (0,1), (0,2), (1,2).
  REQUIRE(outcomes.size() == 3);
  for (const auto &o : outcomes) {
    CHECK(o.assertion.kind == kEffectAssertion);
    CHECK(o.assertion.axis == "k");
    CHECK(o.assertion.evaluated);
    CHECK(o.assertion.passed);
    CHECK(o.assertion.score >= 0.01);
    CHECK(o.assertion.threshold == 0.01);
    CHECK(o.assertion.channel == "color");
    CHECK_FALSE(o.mask.valid()); // masks only for failures
  }
}

TEST_CASE("a Permutation axis fed identical images fails its effect check",
    "[cts][relations]")
{
  const auto test = makeTest("demo", "ignored").permute("k", {"0", "1"}).take();
  const auto cases = expand(test);
  const auto renders =
      renderAll(test, cases, [](const Case &) { return subject(0); });
  const auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 1);
  const auto &o = outcomes.front();
  CHECK_FALSE(o.assertion.passed);
  CHECK(o.assertion.score == 0.0);
  CHECK(o.mask.valid()); // all black: nothing changed
  CHECK(o.assertion.detail.find("`0`") != std::string::npos);
}

TEST_CASE("effect checks every pair and attributes it to the later Case",
    "[cts][relations]")
{
  // Values 1 and 2 render alike: set vs unset differs, but two set values
  // don't. Only the pair (1, 2) fails, on Case 2, naming Case 1.
  const auto test =
      makeTest("demo", "pairs").permute("k", {"0", "1", "2"}).take();
  const auto cases = expand(test);
  const auto renders = renderAll(test, cases, [](const Case &c) {
    return subject(valueOf(c, "k") == "0" ? 0 : 5);
  });
  const auto outcomes = evaluateRelations(test, cases, renders);

  CHECK(
      attributedTo(outcomes, cases, "0").empty()); // the baseline carries none
  const auto one = attributedTo(outcomes, cases, "1");
  REQUIRE(one.size() == 1);
  CHECK(one[0].assertion.passed);

  const auto two = attributedTo(outcomes, cases, "2");
  REQUIRE(two.size() == 2);
  int failed = 0;
  for (const auto &o : two) {
    if (o.assertion.passed)
      continue;
    ++failed;
    CHECK(o.assertion.relatedCases == std::vector<std::string>{"1"});
    CHECK(cases[o.partners.front()].id() == "1");
  }
  CHECK(failed == 1);
}

TEST_CASE("an effect passes when any Channel shows it", "[cts][relations]")
{
  const auto test = makeTest("demo", "channels")
                        .permute("k", {"0", "1"})
                        .channels({Channel::Color, Channel::Depth})
                        .take();
  const auto cases = expand(test);
  // Color unchanged, depth changed.
  std::vector<CaseRender> renders(cases.size());
  for (size_t i = 0; i < cases.size(); ++i) {
    renders[i].state = CaseRender::State::Rendered;
    renders[i].images = {subject(0), subject(static_cast<int>(i) * 5)};
  }
  const auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 1);
  CHECK(outcomes[0].assertion.passed);
  CHECK(outcomes[0].assertion.channel == "depth");
  CHECK(outcomes[0].channelIndex == 1);
}

TEST_CASE("effectMargin overrides the minimum", "[cts][relations]")
{
  // The subject moves one pixel: 20 rows x 2 columns of 64x64 change, ~1 %.
  auto test = makeTest("demo", "margin").permute("k", {"0", "1"}).take();
  const auto cases = expand(test);
  const auto renders = renderAll(test, cases, [](const Case &c) {
    const uint32_t x0 = valueOf(c, "k") == "0" ? 0 : 1;
    return rectangle(kSize, kSize, x0, 10, x0 + 20, 30, {200, 150, 100, 255});
  });
  const double fraction = 40.0 / (kSize * kSize);
  auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 1);
  CHECK(outcomes[0].assertion.score == Approx(fraction));
  CHECK_FALSE(outcomes[0].assertion.passed); // < 1 %

  test.effectMargins["k"] = 0.005;
  outcomes = evaluateRelations(test, cases, renders);
  CHECK(outcomes[0].assertion.passed);
  CHECK(outcomes[0].assertion.threshold == 0.005);
}

TEST_CASE("an opted-out axis is recorded with its reason, unevaluated",
    "[cts][relations]")
{
  const auto test = makeTest("demo", "optout")
                        .permute("k", {"0", "1", "2"})
                        .noEffectCheck("k", "only a timing changes")
                        .take();
  const auto cases = expand(test);
  const auto renders =
      renderAll(test, cases, [](const Case &) { return subject(0); });
  const auto outcomes = evaluateRelations(test, cases, renders);
  // One entry per later Case, not per pair.
  REQUIRE(outcomes.size() == 2);
  for (const auto &o : outcomes) {
    CHECK(o.assertion.kind == kEffectAssertion);
    CHECK_FALSE(o.assertion.evaluated);
    CHECK(
        o.assertion.detail.find("only a timing changes") != std::string::npos);
  }
  // An opted-out axis needs no partner renders.
  CHECK(renderSet(test, cases, {false, false, true})
      == std::vector<bool>{false, false, true});
}

// Equivalence ////////////////////////////////////////////////////////////////

TEST_CASE("a Variant axis is checked for equivalence with its first value",
    "[cts][relations]")
{
  const auto test = makeTest("demo", "variant")
                        .permute("p", {"0", "1"})
                        .variant("mode", {"soup", "indexed", "strip"})
                        .take();
  const auto cases = expand(test);
  // "strip" renders differently: its equivalence fails, the others pass.
  const auto renders = renderAll(test, cases, [](const Case &c) {
    const int p = std::stoi(valueOf(c, "p"));
    return subject(valueOf(c, "mode") == "strip" ? p + 3 : p);
  });
  const auto outcomes = evaluateRelations(test, cases, renders);
  int equivalences = 0, failedEquivalences = 0;
  for (const auto &o : outcomes) {
    if (o.assertion.kind != kEquivalenceAssertion)
      continue;
    ++equivalences;
    CHECK(o.assertion.axis == "mode");
    // Compared with the first value's Case, other axes equal.
    const Case &partner = cases[o.partners.front()];
    CHECK(valueOf(partner, "mode") == "soup");
    CHECK(valueOf(partner, "p") == valueOf(cases[o.caseIndex], "p"));
    CHECK(o.assertion.threshold == 0.002);
    if (!o.assertion.passed) {
      ++failedEquivalences;
      CHECK(valueOf(cases[o.caseIndex], "mode") == "strip");
      CHECK(o.mask.valid());
    }
  }
  CHECK(equivalences == 4); // indexed and strip, per p value
  CHECK(failedEquivalences == 2);
  // The Permutation axis still gets its effect checks.
  int effects = 0;
  for (const auto &o : outcomes)
    effects += o.assertion.kind == kEffectAssertion ? 1 : 0;
  CHECK(effects == 3); // one per mode value
}

TEST_CASE("an equivalence requires every Channel to match", "[cts][relations]")
{
  const auto test = makeTest("demo", "allchannels")
                        .variant("mode", {"soup", "indexed"})
                        .channels({Channel::Color, Channel::Depth})
                        .take();
  const auto cases = expand(test);
  std::vector<CaseRender> renders(cases.size());
  for (size_t i = 0; i < cases.size(); ++i) {
    renders[i].state = CaseRender::State::Rendered;
    renders[i].images = {subject(0), subject(static_cast<int>(i) * 5)};
  }
  const auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 1);
  CHECK_FALSE(outcomes[0].assertion.passed);
  CHECK(outcomes[0].assertion.channel == "depth");
}

TEST_CASE(
    "equivalenceTolerance overrides the maximum and tau", "[cts][relations]")
{
  auto test =
      makeTest("demo", "tolerance").variant("mode", {"soup", "indexed"}).take();
  const auto cases = expand(test);
  // Every pixel differs by 10 levels.
  const auto renders = renderAll(test, cases, [](const Case &c) {
    const uint8_t v = valueOf(c, "mode") == "soup" ? 100 : 110;
    return solid(kSize, kSize, v, v, v, 255);
  });
  CHECK_FALSE(evaluateRelations(test, cases, renders)[0].assertion.passed);
  test.equivalenceTolerances["mode"] = EquivalenceTolerance{0.0, 10};
  const auto o = evaluateRelations(test, cases, renders)[0];
  CHECK(o.assertion.passed);
  CHECK(o.assertion.tau == 10);
}

TEST_CASE("ID channels compare with tau 0", "[cts][relations]")
{
  CHECK(channelTau(Channel::PrimitiveId, 8) == 0);
  CHECK(channelTau(Channel::ObjectId, 8) == 0);
  CHECK(channelTau(Channel::InstanceId, 8) == 0);
  CHECK(channelTau(Channel::Color, 8) == 8);
  CHECK(channelTau(Channel::Depth, 8) == 8);

  const auto test = makeTest("demo", "ids")
                        .variant("mode", {"soup", "indexed"})
                        .channels({Channel::PrimitiveId})
                        .take();
  const auto cases = expand(test);
  // One level apart everywhere: a different ID.
  const auto renders = renderAll(test, cases, [](const Case &c) {
    const uint8_t v = valueOf(c, "mode") == "soup" ? 100 : 101;
    return solid(8, 8, v, v, v, 255);
  });
  const auto o = evaluateRelations(test, cases, renders)[0];
  CHECK_FALSE(o.assertion.passed);
  CHECK(o.assertion.tau == 0);
}

// What can't be evaluated ////////////////////////////////////////////////////

TEST_CASE("images of different sizes fail with that detail", "[cts][relations]")
{
  const auto test = makeTest("demo", "sizes").permute("k", {"0", "1"}).take();
  const auto cases = expand(test);
  const auto renders = renderAll(test, cases, [](const Case &c) {
    return subject(0, valueOf(c, "k") == "0" ? 64 : 32);
  });
  const auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 1);
  CHECK_FALSE(outcomes[0].assertion.passed);
  CHECK(std::isnan(outcomes[0].assertion.score));
  CHECK(
      outcomes[0].assertion.detail.find("32x32 vs 64x64") != std::string::npos);
}

TEST_CASE(
    "a partner that did not render fails the relation", "[cts][relations]")
{
  const auto test = makeTest("demo", "broken").permute("k", {"0", "1"}).take();
  const auto cases = expand(test);
  auto renders = renderAll(test, cases, [](const Case &c) {
    return subject(std::stoi(valueOf(c, "k")));
  });
  renders[0] = {};
  renders[0].state = CaseRender::State::Failed;
  renders[0].error = "boom";
  const auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 1);
  CHECK(outcomes[0].caseIndex == 1);
  CHECK_FALSE(outcomes[0].assertion.passed);
  CHECK(outcomes[0].assertion.detail == "partner `0` did not render: boom");
  CHECK(outcomes[0].channelIndex == -1);

  // A later Case that failed itself carries no relation.
  renders = renderAll(test, cases, [](const Case &) { return subject(0); });
  renders[1].state = CaseRender::State::Failed;
  CHECK(evaluateRelations(test, cases, renders).empty());
}

TEST_CASE(
    "renderSet adds the partners of the selected Cases", "[cts][relations]")
{
  const auto test = makeTest("demo", "partners")
                        .simplified()
                        .permute("a", {"a0", "a1", "a2"})
                        .variant("v", {"v0", "v1"})
                        .take();
  const auto cases = expand(test); // base, a1, a2, v1
  REQUIRE(cases.size() == 4);
  // Selecting a2 needs the baseline and a1 (all pairs), not v1.
  CHECK(renderSet(test, cases, {false, false, true, false})
      == std::vector<bool>{true, true, true, false});
  // Selecting v1 needs the baseline (its first value).
  CHECK(renderSet(test, cases, {false, false, false, true})
      == std::vector<bool>{true, false, false, true});
  // Selecting the baseline needs nothing more.
  CHECK(renderSet(test, cases, {true, false, false, false})
      == std::vector<bool>{true, false, false, false});
}

TEST_CASE("hasRelations needs an axis with two values", "[cts][relations]")
{
  CHECK_FALSE(hasRelations(makeTest("demo", "none").take()));
  CHECK_FALSE(hasRelations(makeTest("demo", "one").permute("k", {"0"}).take()));
  CHECK(hasRelations(makeTest("demo", "two").permute("k", {"0", "1"}).take()));
}

// Verdict ////////////////////////////////////////////////////////////////////

TEST_CASE("settleVerdict is the conjunction of everything evaluated",
    "[cts][relations]")
{
  Assertion passing;
  passing.kind = kEffectAssertion;
  passing.passed = true;
  Assertion failing = passing;
  failing.passed = false;
  Assertion optedOut = failing;
  optedOut.evaluated = false;

  CaseResult r;
  r.verdict = Verdict::Passed;

  SECTION("a failed assertion fails a passing Ground-truth comparison")
  {
    r.groundTruthEvaluated = true;
    r.assertions = {passing, failing};
    settleVerdict(r, false);
    CHECK(r.verdict == Verdict::Failed);
  }
  SECTION("passed self-checks without Ground truth pass")
  {
    r.assertions = {passing};
    settleVerdict(r, false);
    CHECK(r.verdict == Verdict::Passed);
    CHECK(r.passedWithoutGroundTruth());
  }
  SECTION("an opted-out entry neither passes nor fails")
  {
    r.assertions = {optedOut};
    settleVerdict(r, false);
    CHECK(r.verdict == Verdict::Skipped);
    CHECK(r.skipReason == kNoGroundTruthReason);
  }
  SECTION("the earlier partner of an evaluated pair counts as evaluated")
  {
    settleVerdict(r, true);
    CHECK(r.verdict == Verdict::Passed);
  }
  SECTION("a self-checked Test with nothing evaluated is skipped")
  {
    r.usesGroundTruth = false;
    settleVerdict(r, false);
    CHECK(r.verdict == Verdict::Skipped);
    CHECK(r.skipReason == kNothingEvaluatedReason);
  }
  SECTION("a failed render stays failed")
  {
    r.verdict = Verdict::Failed;
    r.assertions = {passing};
    settleVerdict(r, true);
    CHECK(r.verdict == Verdict::Failed);
  }
}

// Images from the results tree ///////////////////////////////////////////////

TEST_CASE(
    "loadCaseRender reads a Case's result images back", "[cts][relations]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_relations_load_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const Workdir wd(root);

  const auto test = makeTest("demo", "load")
                        .permute("k", {"0", "1"})
                        .channels({Channel::Color, Channel::Depth})
                        .take();
  const auto cases = expand(test);
  for (size_t i = 0; i < cases.size(); ++i)
    for (Channel ch : test.channels)
      REQUIRE(savePNG(wd.resultImagePath(cases[i], ch).string(),
          subject(static_cast<int>(i) * 5)));

  std::vector<CaseRender> renders;
  for (const auto &c : cases)
    renders.push_back(loadCaseRender(wd, test, c));
  REQUIRE(renders[1].state == CaseRender::State::Rendered);
  REQUIRE(renders[1].images.size() == 2);
  CHECK(renders[1].images[0].rgba == subject(5).rgba);
  // The pass runs the same on images read back from PNGs.
  const auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 1);
  CHECK(outcomes[0].assertion.passed);

  // A Case missing an image reads as absent.
  std::filesystem::remove(wd.resultImagePath(cases[0], Channel::Depth));
  CHECK(loadCaseRender(wd, test, cases[0]).state == CaseRender::State::Absent);

  std::filesystem::remove_all(root, ec);
}

// Derived expectations (ticket 16) ///////////////////////////////////////////

namespace {

// A gradient image with `k` shifting every pixel: distinct k, distinct image.
Image textured(uint32_t w, uint32_t h, int k = 0)
{
  return cts_test::gradient(w, h, k * 20);
}

// The CaseRender of a Case rendered as `image` on every Channel.
CaseRender rendered(const TestDef &test, const Image &image)
{
  CaseRender r;
  r.state = CaseRender::State::Rendered;
  r.images.assign(test.channels.size(), image);
  return r;
}

const RelationOutcome *equivalenceOf(
    const std::vector<RelationOutcome> &outcomes,
    const std::vector<Case> &cases,
    const std::string &id)
{
  for (const auto &o : outcomes)
    if (o.assertion.kind == kEquivalenceAssertion
        && cases[o.caseIndex].id() == id)
      return &o;
  return nullptr;
}

// A stereo-like Test: sideBySide must be left and right concatenated.
TestDef stereoTest()
{
  return makeTest("demo", "stereo")
      .permute("mode", {"left", "right", "sideBySide"})
      .expectDerived("mode",
          "sideBySide",
          "left and right, side by side",
          [](const Siblings &s) {
            return hconcat(s.at("left"), s.at("right"));
          })
      .take();
}

} // namespace

TEST_CASE(
    "crop, hconcat and vconcat keep Image coordinates", "[cts][relations]")
{
  const Image full = textured(8, 6);
  const Image c = crop(full, PixelRect{2, 1, 3, 4});
  REQUIRE(c.valid());
  CHECK(c.width == 3);
  CHECK(c.height == 4);
  // Pixel (0, 0) of the crop is pixel (2, 1) of the image.
  for (uint32_t y = 0; y < 4; ++y)
    for (uint32_t x = 0; x < 3; ++x)
      for (int ch = 0; ch < 4; ++ch)
        CHECK(c.rgba[(y * 3 + x) * 4 + ch]
            == full.rgba[((y + 1) * 8 + x + 2) * 4 + ch]);
  CHECK_FALSE(crop(full, PixelRect{6, 0, 3, 1}).valid()); // leaves the image
  CHECK_FALSE(crop(full, PixelRect{0, 0, 0, 1}).valid()); // empty

  // Splitting and rejoining gives the image back.
  const Image left = crop(full, PixelRect{0, 0, 5, 6});
  const Image right = crop(full, PixelRect{5, 0, 3, 6});
  CHECK(hconcat(left, right).rgba == full.rgba);
  const Image bottom = crop(full, PixelRect{0, 0, 8, 2});
  const Image top = crop(full, PixelRect{0, 2, 8, 4});
  const Image joined = vconcat(top, bottom);
  CHECK(joined.width == 8);
  CHECK(joined.height == 6);
  CHECK(joined.rgba == full.rgba);
  // Mismatched sides are not joined.
  CHECK_FALSE(hconcat(left, bottom).valid());
  CHECK_FALSE(vconcat(left, bottom).valid());
}

TEST_CASE(
    "regionComposite takes each side from its own image", "[cts][relations]")
{
  const Image inside = solid(8, 4, 200, 0, 0, 255);
  const Image outside = solid(8, 4, 0, 0, 200, 255);
  // The left quarter of the frame: its edge is the line x = 0.25 (2 px).
  const auto left = [](float x, float) { return x < 0.25f; };

  SECTION("without a margin every pixel is compared")
  {
    const ExpectedImage e = regionComposite(inside, outside, left, 0.f);
    REQUIRE(e.image.valid());
    REQUIRE(e.masked());
    for (uint32_t y = 0; y < 4; ++y) {
      for (uint32_t x = 0; x < 8; ++x) {
        const size_t i = size_t(y) * 8 + x;
        INFO("pixel " << x << ", " << y);
        CHECK(maskSelects(e.mask, i));
        CHECK(e.image.rgba[i * 4] == (x < 2 ? 200 : 0));
        CHECK(e.image.rgba[i * 4 + 2] == (x < 2 ? 0 : 200));
      }
    }
  }

  SECTION("pixels within the margin of the edge are not compared")
  {
    // One pixel of margin: columns 1 and 2 touch the edge within it.
    const ExpectedImage e = regionComposite(inside, outside, left, 1.f);
    REQUIRE(e.masked());
    for (uint32_t x = 0; x < 8; ++x) {
      INFO("column " << x);
      CHECK(maskSelects(e.mask, x) == (x != 1 && x != 2));
    }
  }

  SECTION("images of different sizes give no expectation")
  {
    CHECK_FALSE(regionComposite(inside, solid(4, 4, 0, 0, 0, 255), left, 0.f)
            .image.valid());
    CHECK_FALSE(regionComposite(Image{}, outside, left, 0.f).image.valid());
  }
}

TEST_CASE("regionPixels needs whole pixels", "[cts][relations]")
{
  const auto r = regionPixels(256, 256, 0.25f, 0.25f, 0.75f, 0.75f);
  REQUIRE(r);
  CHECK(r->x == 64);
  CHECK(r->y == 64);
  CHECK(r->width == 128);
  CHECK(r->height == 128);
  // The region camera_general used before: 0.3 * 256 is 76.8.
  CHECK_FALSE(regionPixels(256, 256, 0.25f, 0.3f, 0.75f, 0.8f));
  CHECK_FALSE(regionPixels(66, 66, 0.25f, 0.25f, 0.75f, 0.75f));
  CHECK_FALSE(regionPixels(64, 64, 0.5f, 0.f, 0.5f, 1.f)); // empty
  CHECK_FALSE(regionPixels(64, 64, -0.25f, 0.f, 0.5f, 1.f)); // outside
}

TEST_CASE("a derived expectation that holds passes", "[cts][relations]")
{
  const auto test = stereoTest();
  const auto cases = expand(test);
  REQUIRE(cases.size() == 3);
  std::vector<CaseRender> renders(3);
  renders[indexOf(cases, "left")] = rendered(test, textured(16, 8, 1));
  renders[indexOf(cases, "right")] = rendered(test, textured(16, 8, 2));
  renders[indexOf(cases, "sideBySide")] =
      rendered(test, hconcat(textured(16, 8, 1), textured(16, 8, 2)));

  const auto outcomes = evaluateRelations(test, cases, renders);
  const auto *o = equivalenceOf(outcomes, cases, "sideBySide");
  REQUIRE(o);
  CHECK(o->assertion.axis == "mode");
  CHECK(o->assertion.evaluated);
  CHECK(o->assertion.passed);
  CHECK(o->assertion.score == 0.0);
  CHECK(o->assertion.threshold == 0.002);
  CHECK(o->assertion.expectation == "left and right, side by side");
  // It names the siblings it read, in value order.
  CHECK(o->assertion.relatedCases == std::vector<std::string>{"left", "right"});
  CHECK(o->partners
      == std::vector<size_t>{indexOf(cases, "left"), indexOf(cases, "right")});
  // The expected image is handed to the runner to write.
  CHECK(o->expected.width == 32);
  CHECK(o->expected.height == 8);
  CHECK_FALSE(o->expectedMask.valid());
  CHECK_FALSE(o->mask.valid());
}

TEST_CASE("a derived expectation that doesn't hold fails", "[cts][relations]")
{
  const auto test = stereoTest();
  const auto cases = expand(test);
  std::vector<CaseRender> renders(3);
  renders[indexOf(cases, "left")] = rendered(test, textured(16, 8, 1));
  renders[indexOf(cases, "right")] = rendered(test, textured(16, 8, 2));

  SECTION("the halves swapped")
  {
    renders[indexOf(cases, "sideBySide")] =
        rendered(test, hconcat(textured(16, 8, 2), textured(16, 8, 1)));
    const auto outcomes = evaluateRelations(test, cases, renders);
    const auto *o = equivalenceOf(outcomes, cases, "sideBySide");
    REQUIRE(o);
    CHECK_FALSE(o->assertion.passed);
    CHECK(o->assertion.score > 0.5);
    CHECK(o->mask.valid());
    CHECK(o->assertion.detail.find("the expected image (left and right, side "
                                   "by side)")
        != std::string::npos);
  }
  SECTION("the wrong size")
  {
    renders[indexOf(cases, "sideBySide")] = rendered(test, textured(16, 8, 1));
    const auto outcomes = evaluateRelations(test, cases, renders);
    const auto *o = equivalenceOf(outcomes, cases, "sideBySide");
    REQUIRE(o);
    CHECK_FALSE(o->assertion.passed);
    CHECK(o->assertion.detail.find("16x8 vs 32x8") != std::string::npos);
  }
  SECTION("a sibling that did not render")
  {
    renders[indexOf(cases, "sideBySide")] =
        rendered(test, hconcat(textured(16, 8, 1), textured(16, 8, 2)));
    renders[indexOf(cases, "right")] = {};
    renders[indexOf(cases, "right")].state = CaseRender::State::Failed;
    renders[indexOf(cases, "right")].error = "boom";
    const auto outcomes = evaluateRelations(test, cases, renders);
    const auto *o = equivalenceOf(outcomes, cases, "sideBySide");
    REQUIRE(o);
    CHECK_FALSE(o->assertion.passed);
    CHECK(o->assertion.detail == "partner `right` did not render: boom");
    // It names the sibling that failed it.
    const auto &related = o->assertion.relatedCases;
    CHECK(std::find(related.begin(), related.end(), "right") != related.end());
  }
  SECTION("a sibling that was skipped")
  {
    renders[indexOf(cases, "sideBySide")] =
        rendered(test, hconcat(textured(16, 8, 1), textured(16, 8, 2)));
    renders[indexOf(cases, "left")] = {};
    renders[indexOf(cases, "left")].state = CaseRender::State::Skipped;
    renders[indexOf(cases, "left")].error = "not whole pixels";
    const auto outcomes = evaluateRelations(test, cases, renders);
    const auto *o = equivalenceOf(outcomes, cases, "sideBySide");
    REQUIRE(o);
    CHECK_FALSE(o->assertion.passed);
    CHECK(
        o->assertion.detail == "partner `left` was skipped: not whole pixels");
  }
}

TEST_CASE(
    "a masked expectation counts only the masked pixels", "[cts][relations]")
{
  // "off" must match "on" except inside the footprint (the left half), which
  // the mask leaves out.
  const uint32_t w = 20, h = 10;
  const Image footprint = rectangle(w, h, 0, 0, 10, 10, {255, 255, 255, 255});
  Image notFootprint = footprint;
  for (size_t i = 0; i < notFootprint.pixelCount(); ++i)
    for (int c = 0; c < 3; ++c)
      notFootprint.rgba[i * 4 + c] = 255 - notFootprint.rgba[i * 4 + c];
  const auto test = makeTest("demo", "visible")
                        .permute("visible", {"on", "off"})
                        .expectDerived("visible",
                            "off",
                            "`on` outside the light's footprint",
                            [notFootprint](const Siblings &s) {
                              return maskedBy(s.at("on"), notFootprint);
                            })
                        .take();
  const auto cases = expand(test);
  const Image on = rectangle(w, h, 0, 0, 20, 10, {200, 100, 50, 255});
  // In "off" the footprint went dark: only masked-out pixels changed.
  Image off = on;
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < 10; ++x)
      cts_test::setPixel(off, x, y, {0, 0, 0, 255});
  std::vector<CaseRender> renders = {rendered(test, on), rendered(test, off)};

  auto o = evaluateRelations(test, cases, renders);
  REQUIRE(o.size() == 1); // no effect pair: "off" is derived
  CHECK(o[0].assertion.passed);
  CHECK(o[0].assertion.score == 0.0);
  CHECK(o[0].assertion.detail.find("the 100 masked color pixels")
      != std::string::npos);
  CHECK(o[0].expectedMask.valid());

  // A change outside the footprint is counted, as a fraction of the 100
  // masked pixels.
  for (uint32_t x = 10; x < 20; ++x)
    cts_test::setPixel(renders[1].images[0], x, 0, {0, 0, 0, 255});
  o = evaluateRelations(test, cases, renders);
  CHECK_FALSE(o[0].assertion.passed);
  CHECK(o[0].assertion.score == Approx(0.1));
  // Masked-out pixels show grey in the changed-pixel mask.
  REQUIRE(o[0].mask.valid());
  CHECK(o[0].mask.rgba[0] == 128);

  CHECK(changedFraction(on, off, 8, notFootprint) == 0.0);
  CHECK(std::isnan(changedFraction(on, off, 8, solid(w, h, 0, 0, 0, 255))));
  CHECK(
      std::isnan(changedFraction(on, off, 8, solid(4, 4, 255, 255, 255, 255))));
  CHECK(selectedPixels(notFootprint) == 100);
}

TEST_CASE(
    "a mask that selects nothing fails the expectation", "[cts][relations]")
{
  const auto test =
      makeTest("demo", "emptymask")
          .permute("k", {"a", "b"})
          .expectDerived("k",
              "b",
              "nothing",
              [](const Siblings &s) {
                const Image &a = s.at("a");
                return maskedBy(a, solid(a.width, a.height, 0, 0, 0, 255));
              })
          .take();
  const auto cases = expand(test);
  std::vector<CaseRender> renders = {
      rendered(test, textured(8, 8)), rendered(test, textured(8, 8))};
  const auto o = evaluateRelations(test, cases, renders);
  REQUIRE(o.size() == 1);
  CHECK_FALSE(o[0].assertion.passed);
  CHECK(o[0].assertion.detail == "the mask of color selects no pixel");
}

TEST_CASE("a Variant is the identity derived expectation", "[cts][relations]")
{
  const auto test =
      makeTest("demo", "identity").variant("mode", {"soup", "indexed"}).take();
  const auto cases = expand(test);
  std::vector<CaseRender> renders = {
      rendered(test, textured(8, 8)), rendered(test, textured(8, 8))};
  const auto o = evaluateRelations(test, cases, renders);
  REQUIRE(o.size() == 1);
  CHECK(o[0].assertion.kind == kEquivalenceAssertion);
  CHECK(o[0].assertion.passed);
  CHECK(o[0].assertion.expectation == "identical to `soup`");
  CHECK(o[0].assertion.relatedCases == std::vector<std::string>{"soup"});
  // The expected image is the partner's own: nothing new to write.
  CHECK_FALSE(o[0].expected.valid());
  CHECK(o[0].assertion.detail.find("vs `soup`") != std::string::npos);
}

TEST_CASE("a derived value takes no part in its axis's effect pairs",
    "[cts][relations]")
{
  const auto test = stereoTest();
  const auto cases = expand(test);
  // left and right differ; sideBySide is a different size.
  std::vector<CaseRender> renders(3);
  renders[indexOf(cases, "left")] = rendered(test, textured(16, 8, 1));
  renders[indexOf(cases, "right")] = rendered(test, textured(16, 8, 2));
  renders[indexOf(cases, "sideBySide")] =
      rendered(test, hconcat(textured(16, 8, 1), textured(16, 8, 2)));
  const auto outcomes = evaluateRelations(test, cases, renders);
  REQUIRE(outcomes.size() == 2);
  int effects = 0;
  for (const auto &o : outcomes) {
    CHECK(o.assertion.passed);
    if (o.assertion.kind != kEffectAssertion)
      continue;
    ++effects;
    // Only left vs right.
    CHECK(cases[o.caseIndex].id() == "right");
    CHECK(o.assertion.relatedCases == std::vector<std::string>{"left"});
  }
  CHECK(effects == 1);

  // Selecting the derived Case renders every sibling; selecting right
  // renders left only.
  CHECK(renderSet(test, cases, {false, false, true})
      == std::vector<bool>{true, true, true});
  CHECK(renderSet(test, cases, {false, true, false})
      == std::vector<bool>{true, true, false});
}

TEST_CASE("expectDerived rejects what it can't attach to", "[cts][relations]")
{
  auto fn = [](const Siblings &s) { return ExpectedImage(s.at("a")); };
  auto builder = makeTest("demo", "bad").permute("k", {"a", "b"});
  CHECK_THROWS_AS(
      builder.expectDerived("nope", "b", "x", fn), std::invalid_argument);
  CHECK_THROWS_AS(
      builder.expectDerived("k", "c", "x", fn), std::invalid_argument);
  CHECK_THROWS_AS(
      builder.expectDerived("k", "b", "", fn), std::invalid_argument);
  builder.expectDerived("k", "b", "x", fn);
  CHECK_THROWS_AS(
      builder.expectDerived("k", "b", "y", fn), std::invalid_argument);
}

// Frame size /////////////////////////////////////////////////////////////////

TEST_CASE("a Case's frame size is its Test's hook applied to the run's",
    "[cts][relations]")
{
  const auto test =
      makeTest("demo", "sizes")
          .permute("region", {"full", "half"})
          .frameSize([](BuildContext &ctx, FrameSize run) -> CaseFrameSize {
            if (ctx.getString("region", "") == "full")
              return run;
            return regionFrameSize(run, 0.25f, 0.25f, 0.75f, 0.75f);
          })
          .take();
  const auto cases = expand(test);
  const Case &full = cases[indexOf(cases, "full")];
  const Case &half = cases[indexOf(cases, "half")];

  auto r = resolveFrameSize(test, full, {256, 128});
  REQUIRE(r.resolved());
  CHECK(r.size == FrameSize{256, 128});
  r = resolveFrameSize(test, half, {256, 128});
  REQUIRE(r.resolved());
  CHECK(r.size == FrameSize{128, 64});

  // Not whole pixels at the run's size: skipped, with the reason.
  r = resolveFrameSize(test, half, {66, 66});
  CHECK_FALSE(r.resolved());
  CHECK(r.skipReason.find("not cover whole pixels at 66x66")
      != std::string::npos);

  // No hook: the run's size.
  const auto plain = makeTest("demo", "plain").permute("k", {"a"}).take();
  r = resolveFrameSize(plain, expand(plain).front(), {40, 30});
  REQUIRE(r.resolved());
  CHECK(r.size == FrameSize{40, 30});

  // A hook giving an empty frame skips the Case.
  const auto empty = makeTest("demo", "empty")
                         .frameSize([](BuildContext &, FrameSize) {
                           return CaseFrameSize(FrameSize{0, 10});
                         })
                         .take();
  CHECK_FALSE(
      resolveFrameSize(empty, expand(empty).front(), {8, 8}).resolved());
}

// Unmatched axis values //////////////////////////////////////////////////////

TEST_CASE("an axis value a Case holds but the axis doesn't is an error",
    "[cts][relations]")
{
  const auto test = makeTest("demo", "mismatch")
                        .permute("a", {"a0", "a1"})
                        .permute("b", {"b0", "b1"})
                        .take();
  auto cases = expand(test);
  // A Case whose `b` value isn't one of the axis's: not grouped as index 0.
  cases[1].values[1].value = Any("b7");
  try {
    caseValueIndices(test, cases);
    FAIL("no error for an unmatched value");
  } catch (const std::invalid_argument &e) {
    const std::string what = e.what();
    CHECK(what.find("demo/mismatch") != std::string::npos);
    CHECK(what.find("`" + cases[1].id() + "`") != std::string::npos);
    CHECK(what.find("`b7`") != std::string::npos);
  }
  CHECK_THROWS_AS(relationGroups(test, cases), std::invalid_argument);
  CHECK_THROWS_AS(
      evaluateRelations(test, cases, std::vector<CaseRender>(cases.size())),
      std::invalid_argument);

  // An axis the Case has no value for, too.
  cases = expand(test);
  cases[0].values.pop_back();
  CHECK_THROWS_AS(caseValueIndices(test, cases), std::invalid_argument);
}

TEST_CASE(
    "every built-in Test's Cases match their axis values", "[cts][relations]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  for (const TestDef *test : catalog.filter(Filter{}))
    CHECK_NOTHROW(caseValueIndices(*test, expand(*test)));
}
