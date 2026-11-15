// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The relational pass of the self-checks (ADR-0010), with images generated in
// code: no device, no image files.

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/Expansion.h"
#include "cts/Image.h"
#include "cts/Relations.h"
#include "cts/TestBuilder.h"
#include "cts/Workdir.h"
// std
#include <cmath>
#include <filesystem>
#include <functional>
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
    CHECK(cases[o.partnerIndex].id() == "1");
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
    const Case &partner = cases[o.partnerIndex];
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
