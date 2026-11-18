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

TEST_CASE("a guard that could not render fails, it is never dropped",
    "[cts][convergence]")
{
  ConvergenceMeasurement m;
  m.frames = 1;
  m.error = "world/camera/renderer build failed";
  const auto a = judgeConvergence(m);
  CHECK_FALSE(a.passed);
  CHECK(a.evaluated);
  CHECK(a.detail.find("not converged at 1 frame:") != std::string::npos);
  CHECK(a.detail.find("build failed") != std::string::npos);
}

// Failing the relational assertions //////////////////////////////////////////

TEST_CASE("a failed guard fails every evaluated relational assertion",
    "[cts][convergence]")
{
  std::vector<RelationOutcome> outcomes = {
      passedRelation(kEffectAssertion), passedRelation(kEquivalenceAssertion)};
  RelationOutcome optedOut;
  optedOut.assertion.kind = kEffectAssertion;
  optedOut.assertion.evaluated = false;
  optedOut.assertion.detail = "opted out";
  outcomes.push_back(optedOut);

  const auto failed =
      judgeConvergence(measurement(gradient(32, 32), gradient(32, 32, 40)));
  failRelationsNotConverged(outcomes, failed);

  for (size_t i = 0; i < 2; ++i) {
    CHECK_FALSE(outcomes[i].assertion.passed);
    CHECK(outcomes[i].assertion.detail.find("not converged at 16 frames")
        != std::string::npos);
    // The check's own finding is kept.
    CHECK(outcomes[i].assertion.detail.find("fine") != std::string::npos);
  }
  CHECK_FALSE(outcomes[2].assertion.evaluated);
  CHECK(outcomes[2].assertion.detail == "opted out");
  CHECK(hasEvaluatedRelation(outcomes));
}

TEST_CASE("a passed guard leaves the relational assertions alone",
    "[cts][convergence]")
{
  std::vector<RelationOutcome> outcomes = {passedRelation(kEffectAssertion)};
  failRelationsNotConverged(
      outcomes, judgeConvergence(measurement(gradient(8, 8), gradient(8, 8))));
  CHECK(outcomes[0].assertion.passed);
  CHECK(outcomes[0].assertion.detail == "fine");
}

TEST_CASE("only unevaluated outcomes need no guard", "[cts][convergence]")
{
  RelationOutcome optedOut;
  optedOut.assertion.evaluated = false;
  CHECK_FALSE(hasEvaluatedRelation({optedOut}));
  CHECK_FALSE(hasEvaluatedRelation({}));
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
