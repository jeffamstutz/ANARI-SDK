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
// guard renders a Test's baseline Case, keeps accumulating for as many frames
// again, and requires the two images to agree within the equivalence maximum.
// Pure: no device. The runner owns the rendering (Runner::measureConvergence).
namespace anari {
namespace cts {

// Whether accumulating can stop: `rendered` frames are done, `cap` is the
// run's accumulation count, `progress` the frame's refinementProgress if the
// device reports it. refinementProgress is the stop condition, not the proof:
// it ends accumulation early at 1, the guard still checks the result.
bool accumulationDone(
    uint32_t rendered, uint32_t cap, std::optional<float> progress);

// What the runner measured on the baseline Case.
struct ConvergenceMeasurement
{
  uint32_t frames{0}; // accumulated before `before` was read
  uint32_t extraFrames{0}; // accumulated again before `after`
  std::optional<float> refinementProgress; // as of `before`, if reported
  Image before;
  Image after;
  // Set when the baseline could not be rendered for the guard: the guard
  // fails with this as the reason (it is never dropped).
  std::string error;
};

// The "convergence" Assertion for a measurement: score is the changed fraction
// between the two images, threshold the equivalence maximum, `frames` and
// `refinementProgress` as measured. Failed (never dropped) with the reason
// "not converged at N frames (changed fraction X %); raise `--accumulation`"
// when the images differ by more than the maximum; a device that reported
// refinementProgress 1 and then changed has contradicted itself, and the
// detail says so.
Assertion judgeConvergence(const ConvergenceMeasurement &measurement,
    const SelfCheckDefaults &defaults = {});

// Mark every evaluated relational assertion in `outcomes` failed, with the
// failed convergence Assertion's detail as the reason (the earlier result is
// kept after it). Unevaluated entries (opted-out axes) are left alone. Does
// nothing if `convergence` passed.
void failRelationsNotConverged(
    std::vector<RelationOutcome> &outcomes, const Assertion &convergence);

// Whether any outcome is an evaluated relational assertion, i.e. whether the
// guard has anything to protect.
bool hasEvaluatedRelation(const std::vector<RelationOutcome> &outcomes);

} // namespace cts
} // namespace anari
