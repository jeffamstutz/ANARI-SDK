// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "Image.h"
#include "Sidecar.h"
#include "TestDef.h"
#include "Workdir.h"
// std
#include <cstddef>
#include <string>
#include <vector>

// The relational pass of the self-checks (ADR-0010): given a Test, its Cases
// and the Candidate's own 8-bit comparison images of them, which Cases must
// differ (effect, the meaning of a Permutation axis) and which must match
// (equivalence, the meaning of a Variant axis). Pure: no device, and no
// filesystem except loadCaseRender(), which reads a Case's images back from
// the results tree so a Case rendered in another process can take part.
namespace anari {
namespace cts {

// The default self-check tolerances (ADR-0010 records the calibration). The
// gap between the two fractions is deliberate: a pair between them fails both
// kinds of check.
struct SelfCheckDefaults
{
  int tau{8}; // per-pixel tolerance in 8-bit levels; ID channels use 0
  double equivalenceMax{0.002}; // equivalence: changed fraction <= 0.2 %
  double effectMin{0.01}; // effect: changed fraction >= 1 %
};

// The per-pixel tolerance of a Channel: 0 for the ID channels, whose values
// either match or don't, else `tau`.
int channelTau(Channel channel, int tau);

// What one Case contributes to the relational pass.
struct CaseRender
{
  enum class State
  {
    Absent, // not rendered in this run (not selected, and no one's partner)
    Rendered,
    Failed, // build or render failed; `error` says why
  };
  State state{State::Absent};
  std::vector<Image> images; // in TestDef::channels order
  std::string error;
};

// Each Case's value index on every axis, in axis order.
std::vector<std::vector<size_t>> caseValueIndices(
    const TestDef &test, const std::vector<Case> &cases);

// A group: the Cases (indices into `cases`) that differ only in one axis,
// ordered by that axis's value order. Only groups of two or more Cases.
// Cartesian expansion gives one group per combination of the other axes;
// simplified expansion gives one per axis (its baseline and that axis's other
// Cases).
struct RelationGroup
{
  size_t axis{0};
  std::vector<size_t> cases;
};

std::vector<RelationGroup> relationGroups(
    const TestDef &test, const std::vector<Case> &cases);

// Whether a Test has any relational self-check (an axis with two values or
// more; an opted-out Permutation axis still records its opt-out).
bool hasRelations(const TestDef &test);

// The Cases that must be rendered for the relations attributed to the
// `selected` ones: the selected Cases plus their partners. A partner that
// isn't selected is a support render (its images are written; it is not
// scored).
std::vector<bool> renderSet(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<bool> &selected);

// One assertion, attributed to Case `caseIndex`, about its relation to Case
// `partnerIndex`. Image paths in `assertion` are left empty for the runner.
struct RelationOutcome
{
  size_t caseIndex{0};
  size_t partnerIndex{0};
  Assertion assertion;
  // The scored Channel's index into TestDef::channels, or -1 when nothing was
  // compared.
  int channelIndex{-1};
  // For a failed check whose images were comparable: where they changed.
  Image mask;
};

// Evaluate every effect and equivalence of a Test (ADR-0010):
//  - effect, per Permutation axis not opted out: every pair within each group
//    must differ by changed fraction >= the minimum on at least one Channel;
//  - equivalence, per Variant axis: every Case is compared with the group's
//    first-value Case, and must be within the maximum on every Channel;
//  - an opted-out axis yields an unevaluated entry carrying its reason.
// A pair is attributed to its later Case (axis value order) and names its
// partner. Pairs whose later Case is absent or failed are not evaluated; a
// partner that is absent or failed fails the pair.
std::vector<RelationOutcome> evaluateRelations(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<CaseRender> &renders,
    const SelfCheckDefaults &defaults = {});

// The skipReason of a fully self-checked Case that had nothing to evaluate.
inline constexpr const char *kNothingEvaluatedReason =
    "nothing to evaluate: no Ground truth comparison and no self-check";

// Settle a Case's verdict once its assertions are merged (ADR-0010): the
// conjunction of the Ground-truth comparison, when it ran, and every evaluated
// assertion. On entry `result.verdict` is the per-Case phase's: Failed (the
// render or the Ground-truth comparison failed) and Skipped are kept; Passed
// is provisional. A provisionally passed Case with nothing evaluated (no
// Ground truth, no evaluated assertion of its own, and not the partner of an
// evaluated pair, `participated`) is skipped.
void settleVerdict(CaseResult &result, bool participated);

// A Case's images read back from the results tree (its result PNGs, in
// TestDef::channels order): Rendered when every one loads, else Absent.
CaseRender loadCaseRender(
    const Workdir &workdir, const TestDef &test, const Case &c);

} // namespace cts
} // namespace anari
