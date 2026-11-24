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
// differ (effect, the meaning of a Permutation axis) and which must match an
// image derived from their siblings (equivalence: a derived expectation, of
// which a Variant axis is the identity case). Pure: no device, and no
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
    Skipped, // not rendered for a reason of its own (`error`), e.g. its size
    // The device lacks an extension this Case's values need (`error` names
    // it). The Case takes no part in any relation: its partners are not
    // failed for its absence.
    Unsupported,
  };
  State state{State::Absent};
  std::vector<Image> images; // in TestDef::channels order
  std::string error;
};

// Each Case's value index on every axis, in axis order. A Case value is
// matched to its axis's values by string form (anyToString()); a value that
// matches none of them, or more than one, or an axis the Case has no value
// for, throws std::invalid_argument naming the Test, the Case and the value
// (it would otherwise put the Case in the wrong group).
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
  std::vector<size_t> values; // each Case's value index on `axis`
};

std::vector<RelationGroup> relationGroups(
    const TestDef &test, const std::vector<Case> &cases);

// Whether value `valueIndex` of axis `axis` has a declared derived expectation
// (`.expectDerived`), which exempts it from the axis's effect pairs.
bool hasDerivedExpectation(const TestDef &test, size_t axis, size_t valueIndex);

// Whether a Test has any relational self-check (an axis with two values or
// more; an opted-out Permutation axis still records its opt-out).
bool hasRelations(const TestDef &test);

// The Cases that must be rendered for the relations attributed to the
// `selected` ones: the selected Cases plus their partners (for a derived
// expectation, every sibling on its axis; for a Variant, the first value's
// Case). A partner that isn't selected is a support render (its images are
// written; it is not scored).
std::vector<bool> renderSet(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<bool> &selected);

// One assertion, attributed to Case `caseIndex`, about its relation to the
// Cases `partners` (an effect's partner, or the siblings a derived expectation
// read). Image paths in `assertion` are left empty for the runner.
struct RelationOutcome
{
  size_t caseIndex{0};
  std::vector<size_t> partners;
  Assertion assertion;
  // The scored Channel's index into TestDef::channels, or -1 when nothing was
  // compared.
  int channelIndex{-1};
  // For a failed check whose images were comparable: where they changed.
  Image mask;
  // For a declared derived expectation (not a Variant's identity), the
  // expected image of the scored Channel and its mask, if it has one.
  Image expected;
  Image expectedMask;

  // The Cases the assertion reads: its own, then its partners.
  std::vector<size_t> casesRead() const
  {
    std::vector<size_t> read = {caseIndex};
    read.insert(read.end(), partners.begin(), partners.end());
    return read;
  }
};

// Evaluate every effect and equivalence of a Test (ADR-0010):
//  - effect, per Permutation axis not opted out: every pair within each group
//    must differ by changed fraction >= the minimum on at least one Channel;
//    values with a derived expectation take no part;
//  - equivalence, per derived expectation: the Case must be within the
//    maximum of the image its function derives from its siblings, on every
//    Channel (only the masked pixels, when it returns a mask); a Variant
//    axis's non-first values derive the first value's image unchanged;
//  - an opted-out axis yields an unevaluated entry carrying its reason.
// An effect pair is attributed to its later Case (axis value order) and names
// its partner; an equivalence to the derived Case, naming the siblings it
// read. A Case that is absent, failed or skipped carries nothing; a partner
// that is fails the assertion. Throws as caseValueIndices() does.
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
