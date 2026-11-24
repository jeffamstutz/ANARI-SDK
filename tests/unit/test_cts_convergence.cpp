// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/Case.h"
#include "cts/ConvergenceGuard.h"
#include "cts/Expansion.h"
#include "cts/Relations.h"
#include "cts/Report.h"
#include "cts/Sidecar.h"
#include "cts/TestBuilder.h"
// std
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace anari::cts;
using cts_test::gradient;
using cts_test::solid;

namespace {

ConvergenceMeasurement measurement(Image before, Image after)
{
  ConvergenceMeasurement m;
  m.frames = 16;
  m.extraFrames = 16;
  m.before = std::move(before);
  m.after = std::move(after);
  return m;
}

// An outcome carrying an evaluated, passed relational assertion.
RelationOutcome passedRelation(const char *kind)
{
  RelationOutcome o;
  o.assertion.kind = kind;
  o.assertion.evaluated = true;
  o.assertion.passed = true;
  o.assertion.detail = "fine";
  return o;
}

} // namespace

// The stop condition /////////////////////////////////////////////////////////

TEST_CASE("accumulation stops at the cap without a reported progress",
    "[cts][convergence]")
{
  CHECK_FALSE(accumulationDone(0, 4, std::nullopt));
  CHECK_FALSE(accumulationDone(3, 4, std::nullopt));
  CHECK(accumulationDone(4, 4, std::nullopt));
}

TEST_CASE("a reported refinementProgress of 1 stops accumulation early",
    "[cts][convergence]")
{
  CHECK_FALSE(accumulationDone(2, 16, 0.5f));
  CHECK(accumulationDone(2, 16, 1.f));
  // The cap still wins when progress never gets there.
  CHECK(accumulationDone(16, 16, 0.5f));
}

// The guard decision /////////////////////////////////////////////////////////

TEST_CASE("the guard passes a stable image", "[cts][convergence]")
{
  const auto a =
      judgeConvergence(measurement(gradient(32, 32), gradient(32, 32)));
  CHECK(a.kind == kConvergenceAssertion);
  CHECK(a.evaluated);
  CHECK(a.passed);
  CHECK(a.score == 0.0);
  CHECK(a.threshold == SelfCheckDefaults{}.equivalenceMax);
  CHECK(a.framesRendered == 16);
  CHECK(a.detail.find("stable") != std::string::npos);
}

TEST_CASE("the guard tolerates differences within tau", "[cts][convergence]")
{
  // Every pixel moves by 4 levels, under tau = 8.
  CHECK(judgeConvergence(measurement(solid(32, 32, 100, 100, 100, 255),
                             solid(32, 32, 104, 100, 96, 255)))
            .passed);
}

TEST_CASE("the guard fails an image that is still refining with the reason",
    "[cts][convergence]")
{
  // Every pixel moves by 40 levels: a noisy, still-refining image.
  const auto a =
      judgeConvergence(measurement(gradient(32, 32), gradient(32, 32, 40)));
  CHECK_FALSE(a.passed);
  CHECK(a.score > 0.9);
  CHECK(a.detail.find("not converged at 16 frames") != std::string::npos);
  CHECK(a.detail.find("changed fraction") != std::string::npos);
  CHECK(a.detail.find("raise `--accumulation`") != std::string::npos);
  CHECK(a.detail.find("contradicts") == std::string::npos);
}

TEST_CASE(
    "a device that reports progress 1 and then changes contradicts itself",
    "[cts][convergence]")
{
  auto m = measurement(gradient(32, 32), gradient(32, 32, 40));
  m.refinementProgress = 1.f;
  const auto a = judgeConvergence(m);
  CHECK_FALSE(a.passed);
  REQUIRE(a.refinementProgress);
  CHECK(*a.refinementProgress == 1.0);
  CHECK(a.detail.find("contradicts itself") != std::string::npos);
}

TEST_CASE("a guard that could not render names the render error",
    "[cts][convergence]")
{
  ConvergenceMeasurement m;
  m.frames = 16;
  m.error = "frame readback failed: no mapped data";
  const auto a = judgeConvergence(m);
  // Never dropped, never passed; but it is no "not converged".
  CHECK_FALSE(a.passed);
  CHECK(a.evaluated);
  CHECK(a.detail.find("not converged") == std::string::npos);
  CHECK(a.detail.find("render error") != std::string::npos);
  CHECK(a.detail.find("frame readback failed: no mapped data")
      != std::string::npos);
}

TEST_CASE("the not-converged reason suggests a count for path tracers",
    "[cts][convergence]")
{
  const auto a =
      judgeConvergence(measurement(gradient(32, 32), gradient(32, 32, 40)));
  CHECK(a.detail.find("path tracer") != std::string::npos);
  CHECK(a.detail.find("256") != std::string::npos);
}

// When the guard can't run ///////////////////////////////////////////////////

TEST_CASE(
    "the guard runs when the renderer accumulates over several frames "
    "of a color Channel",
    "[cts][convergence]")
{
  CHECK(notGuardedReason(true, 16, true).empty());
  CHECK(notGuardedReason(true, 2, true).empty());
}

TEST_CASE("a renderer without KHR_FRAME_ACCUMULATION is not guarded",
    "[cts][convergence]")
{
  const auto reason = notGuardedReason(false, 16, true);
  CHECK(reason.find("ANARI_KHR_FRAME_ACCUMULATION") != std::string::npos);
  // It wins over the other reasons: raising the count can't help.
  CHECK(notGuardedReason(false, 1, false) == reason);
}

TEST_CASE("a run of one frame is not guarded", "[cts][convergence]")
{
  for (uint32_t frames : {0u, 1u}) {
    const auto reason = notGuardedReason(true, frames, true);
    CHECK(reason.find("--accumulation") != std::string::npos);
    CHECK(reason.find("--no-accumulation") != std::string::npos);
  }
}

TEST_CASE("a Test with no color Channel is not guarded", "[cts][convergence]")
{
  const auto reason = notGuardedReason(true, 16, false);
  CHECK(reason.find("no color Channel") != std::string::npos);
}

// Guarding the relational assertions /////////////////////////////////////////

namespace {

// An evaluated, passed relation of `kind` on Case `at` reading `partners`.
RelationOutcome relation(
    const char *kind, size_t at, std::vector<size_t> partners)
{
  RelationOutcome o = passedRelation(kind);
  o.caseIndex = at;
  o.partners = std::move(partners);
  return o;
}

ConvergenceMeasurement stable()
{
  return measurement(gradient(32, 32), gradient(32, 32));
}

ConvergenceMeasurement refining()
{
  return measurement(gradient(32, 32), gradient(32, 32, 40));
}

const std::vector<std::string> kIds = {"base", "a", "b", "c"};
const std::vector<bool> kAllSelected(4, true);

} // namespace

TEST_CASE("every Case a relation reads is guarded once", "[cts][convergence]")
{
  // a and b each read base; c reads nothing evaluated.
  std::vector<RelationOutcome> outcomes = {
      relation(kEffectAssertion, 1, {0}), relation(kEffectAssertion, 2, {0})};
  std::vector<std::optional<ConvergenceMeasurement>> m = {
      stable(), stable(), stable(), stable()};
  const auto guards = guardRelations(outcomes, m, kIds, kAllSelected);
  REQUIRE(guards.size() == 3);
  CHECK(guards[0].caseIndex == 0);
  CHECK(guards[1].caseIndex == 1);
  CHECK(guards[2].caseIndex == 2);
  for (const auto &g : guards) {
    CHECK(g.recordOn == g.caseIndex);
    CHECK(g.assertion.kind == kConvergenceAssertion);
    CHECK(g.assertion.passed);
  }
  CHECK(outcomes[0].assertion.passed);
  CHECK(outcomes[1].assertion.passed);
}

TEST_CASE(
    "a non-baseline Case that keeps changing fails the relations that "
    "read it",
    "[cts][convergence]")
{
  // The baseline converges; Case b, which only the second effect reads, keeps
  // changing: a device whose slow Case the baseline guard missed.
  std::vector<RelationOutcome> outcomes = {relation(kEffectAssertion, 1, {0}),
      relation(kEffectAssertion, 2, {0}),
      relation(kEquivalenceAssertion, 3, {2})};
  std::vector<std::optional<ConvergenceMeasurement>> m = {
      stable(), stable(), refining(), stable()};
  const auto guards = guardRelations(outcomes, m, kIds, kAllSelected);

  REQUIRE(guards.size() == 4);
  CHECK(guards[0].assertion.passed);
  CHECK(guards[1].assertion.passed);
  CHECK_FALSE(guards[2].assertion.passed);
  CHECK(guards[2].recordOn == 2);
  CHECK(guards[3].assertion.passed);

  // a reads only base and a: untouched.
  CHECK(outcomes[0].assertion.passed);
  CHECK(outcomes[0].assertion.detail == "fine");
  // b's own effect fails on b's guard, the check's finding kept.
  CHECK_FALSE(outcomes[1].assertion.passed);
  CHECK(outcomes[1].assertion.detail.find("not converged at 16 frames")
      != std::string::npos);
  CHECK(outcomes[1].assertion.detail.find("fine") != std::string::npos);
  // c's equivalence reads b: it fails and names b.
  CHECK_FALSE(outcomes[2].assertion.passed);
  CHECK(outcomes[2].assertion.detail.find("Case b") != std::string::npos);
  CHECK(
      outcomes[2].assertion.detail.find("not converged") != std::string::npos);
}

TEST_CASE("a guard's render error fails the relations with that error",
    "[cts][convergence]")
{
  std::vector<RelationOutcome> outcomes = {relation(kEffectAssertion, 1, {0})};
  ConvergenceMeasurement broken;
  broken.frames = 16;
  broken.error = "device lost";
  std::vector<std::optional<ConvergenceMeasurement>> m = {
      broken, stable(), std::nullopt, std::nullopt};
  guardRelations(outcomes, m, kIds, kAllSelected);
  CHECK_FALSE(outcomes[0].assertion.passed);
  CHECK(outcomes[0].assertion.detail.find("device lost") != std::string::npos);
  CHECK(
      outcomes[0].assertion.detail.find("not converged") == std::string::npos);
}

TEST_CASE("an unselected partner's guard is recorded on a selected reader",
    "[cts][convergence]")
{
  // Only a is selected; base is a support render.
  std::vector<RelationOutcome> outcomes = {relation(kEffectAssertion, 1, {0})};
  std::vector<std::optional<ConvergenceMeasurement>> m = {
      refining(), stable(), std::nullopt, std::nullopt};
  const std::vector<bool> selected = {false, true, false, false};
  const auto guards = guardRelations(outcomes, m, kIds, selected);
  REQUIRE(guards.size() == 2);
  CHECK(guards[0].caseIndex == 0);
  CHECK(guards[0].recordOn == 1);
  CHECK(guards[0].assertion.relatedCases == std::vector<std::string>{"base"});
  CHECK(guards[0].assertion.detail.find("Case base") != std::string::npos);
  CHECK(guards[1].recordOn == 1);
  CHECK(guards[1].assertion.relatedCases.empty());
  CHECK_FALSE(outcomes[0].assertion.passed);
}

TEST_CASE(
    "opted-out relations are neither guarded nor failed", "[cts][convergence]")
{
  RelationOutcome optedOut = relation(kEffectAssertion, 1, {0});
  optedOut.assertion.evaluated = false;
  optedOut.assertion.detail = "opted out";
  std::vector<RelationOutcome> outcomes = {optedOut};
  std::vector<std::optional<ConvergenceMeasurement>> m = {
      refining(), refining(), std::nullopt, std::nullopt};
  CHECK(guardRelations(outcomes, m, kIds, kAllSelected).empty());
  CHECK(outcomes[0].assertion.detail == "opted out");
}

TEST_CASE("a Case with no measurement is not guarded", "[cts][convergence]")
{
  // A partner that failed to render has no measurement; the relation already
  // fails for it.
  std::vector<RelationOutcome> outcomes = {relation(kEffectAssertion, 1, {0})};
  std::vector<std::optional<ConvergenceMeasurement>> m = {
      std::nullopt, stable(), std::nullopt, std::nullopt};
  const auto guards = guardRelations(outcomes, m, kIds, kAllSelected);
  REQUIRE(guards.size() == 1);
  CHECK(guards[0].caseIndex == 1);
  CHECK(outcomes[0].assertion.passed);
}

// The Sidecar and the report /////////////////////////////////////////////////

TEST_CASE("a convergence assertion round-trips through the Sidecar",
    "[cts][convergence]")
{
  CaseResult r;
  r.category = "demo";
  r.test = "t";
  r.caseId = "c";
  r.verdict = Verdict::Failed;
  auto m = measurement(gradient(16, 16), gradient(16, 16, 40));
  m.refinementProgress = 0.75f;
  r.assertions.push_back(judgeConvergence(m));

  CaseResult back;
  REQUIRE(fromJson(toJson(r), back));
  REQUIRE(back.assertions.size() == 1);
  const auto &a = back.assertions[0];
  CHECK(a.kind == kConvergenceAssertion);
  CHECK(a.framesRendered == 16);
  REQUIRE(a.refinementProgress);
  CHECK(*a.refinementProgress == Approx(0.75));
  CHECK_FALSE(a.passed);
  CHECK(a.score == Approx(r.assertions[0].score));
  // The report counts it as its own failure kind.
  CHECK(failureKinds(back) == std::vector<std::string>{"convergence"});
}

TEST_CASE("an older Sidecar assertion defaults the convergence fields",
    "[cts][convergence]")
{
  CaseResult r;
  r.category = "demo";
  r.test = "t";
  r.caseId = "c";
  Assertion e;
  e.kind = kEffectAssertion;
  e.passed = true;
  r.assertions.push_back(e);
  CaseResult back;
  REQUIRE(fromJson(toJson(r), back));
  CHECK(back.assertions[0].framesRendered == 0);
  CHECK_FALSE(back.assertions[0].refinementProgress);
}

TEST_CASE("'not guarded' round-trips through the Sidecar and is reported",
    "[cts][convergence]")
{
  CaseResult r;
  r.category = "demo";
  r.test = "t";
  r.caseId = "c";
  r.verdict = Verdict::Failed;
  r.notGuarded = notGuardedReason(false, 16, true);
  CaseResult back;
  REQUIRE(fromJson(toJson(r), back));
  CHECK(back.notGuarded == r.notGuarded);

  // A guarded Case records an empty reason; an older Sidecar records none,
  // which is not read as guarded.
  CaseResult plain;
  plain.category = "demo";
  plain.test = "t";
  plain.caseId = "d";
  plain.notGuarded = std::string();
  CaseResult plainBack;
  REQUIRE(fromJson(toJson(plain), plainBack));
  REQUIRE(plainBack.notGuarded);
  CHECK(plainBack.notGuarded->empty());
  CaseResult older = plain;
  older.notGuarded.reset();
  CaseResult olderBack;
  REQUIRE(fromJson(toJson(older), olderBack));
  CHECK_FALSE(olderBack.notGuarded);

  std::map<std::string, CaseResult> results = {
      {"demo/t/c", back}, {"demo/t/d", plainBack}};
  const Summary s = summarize(results);
  CHECK(s.notGuarded == std::map<std::string, int>{{*r.notGuarded, 1}});
  std::ostringstream text;
  writeTextSummary(text, "wd", results, true, {}, nullptr);
  CHECK(text.str().find("not guarded") != std::string::npos);
  CHECK(text.str().find(*r.notGuarded) != std::string::npos);
}
