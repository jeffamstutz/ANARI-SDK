// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Image.h"
#include "Relations.h"
#include "Sidecar.h"
// std
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The convergence guard of the self-checks (ADR-0010, "Convergence guard"):
// relational assertions (effect, equivalence, derived expectations) need the
// Candidate's image to be converged to within the per-pixel tolerance at the
// run's accumulation count. Changed fraction is asymmetric under noise: a
// noisy Candidate fails an equivalence, but it passes every effect check. The
// guard keeps accumulating each Case a relational assertion reads for as many
// frames again as it rendered, and requires the two images to agree within the
// equivalence maximum. Pure: no device. The runner owns the rendering (it
// measures each Case in its own render, Runner::renderChannels).
namespace anari {
namespace cts {

// Whether accumulating can stop: `rendered` frames are done, `cap` is the
// run's accumulation count, `progress` the frame's refinementProgress if the
// device reports it. refinementProgress is the stop condition, not the proof:
// it ends accumulation early at 1, the guard still checks the result.
bool accumulationDone(
    uint32_t rendered, uint32_t cap, std::optional<float> progress);

// Why the guard can't run, or empty when it can: the renderer lacks
// KHR_FRAME_ACCUMULATION (`rendererAccumulates`, which wins: no count helps),
// the run renders one frame (`accumulationFrames` <= 1, --no-accumulation), or
// the Test has no color Channel (depth and ID Channels are never accumulated).
// A Case whose relational assertions ran unguarded records it
// (CaseResult::notGuarded).
std::string notGuardedReason(bool rendererAccumulates,
    uint32_t accumulationFrames,
    bool hasColorChannel);

// What the runner measured on one Case.
struct ConvergenceMeasurement
{
  uint32_t frames{0}; // accumulated before `before` was read
  uint32_t extraFrames{0}; // accumulated again before `after`
  std::optional<float> refinementProgress; // as of `before`, if reported
  Image before;
  Image after;
  // Set when the guard's own frames could not be rendered or read: the guard
  // fails with this render error as the reason (it is never dropped).
  std::string error;
};

// The "convergence" Assertion for a measurement: score is the changed fraction
// between the two images, threshold the equivalence maximum, `frames` and
// `refinementProgress` as measured. Failed (never dropped) with the reason
// "not converged at N frames (changed fraction X %); raise `--accumulation`"
// when the images differ by more than the maximum; a device that reported
// refinementProgress 1 and then changed has contradicted itself, and the
// detail says so. A render error fails it with that error as the reason.
Assertion judgeConvergence(const ConvergenceMeasurement &measurement,
    const SelfCheckDefaults &defaults = {});

// One guarded Case: its convergence Assertion, and the selected Case whose
// Sidecar records it (`caseIndex` itself when selected, else the first
// selected Case with a relation reading it, the Assertion then naming it in
// `relatedCases`).
struct CaseGuard
{
  size_t caseIndex{0};
  size_t recordOn{0};
  Assertion assertion;
};

// Guard every Case an evaluated relational assertion of a selected Case in
// `outcomes` reads (its own Case and its partners), once each; an unselected
// Case's assertions are never recorded, so they need no guard, from
// `measurements` (by Case index; a Case without one, e.g. one that failed to
// render, is not guarded). Every evaluated assertion reading a Case that failed
// the guard is failed with the guard's detail as the reason ("Case <id>: ..."
// when it is a partner's), the check's own finding kept after it. Unevaluated
// entries (opted-out axes) are left alone. Returns the guarded Cases in index
// order.
std::vector<CaseGuard> guardRelations(std::vector<RelationOutcome> &outcomes,
    const std::vector<std::optional<ConvergenceMeasurement>> &measurements,
    const std::vector<std::string> &caseIds,
    const std::vector<bool> &selected,
    const SelfCheckDefaults &defaults = {});

} // namespace cts
} // namespace anari
