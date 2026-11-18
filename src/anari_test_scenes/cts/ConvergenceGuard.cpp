// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ConvergenceGuard.h"
#include "Metrics.h"
// std
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace anari {
namespace cts {

namespace {

std::string percent(double fraction)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(2) << fraction * 100.0 << " %";
  return os.str();
}

std::string progressText(float p)
{
  std::ostringstream os;
  os << std::setprecision(3) << p;
  return os.str();
}

} // namespace

bool accumulationDone(
    uint32_t rendered, uint32_t cap, std::optional<float> progress)
{
  return rendered >= cap || (progress && *progress >= 1.f);
}

Assertion judgeConvergence(
    const ConvergenceMeasurement &m, const SelfCheckDefaults &defaults)
{
  Assertion a;
  a.kind = kConvergenceAssertion;
  a.channel = "color";
  a.tau = channelTau(Channel::Color, defaults.tau);
  a.threshold = defaults.equivalenceMax;
  a.framesRendered = m.frames;
  if (m.refinementProgress)
    a.refinementProgress = *m.refinementProgress;
  a.expectation = "the image no longer changes as accumulation continues";
  a.score = std::numeric_limits<double>::quiet_NaN();

  const std::string at =
      "at " + std::to_string(m.frames) + " frame" + (m.frames == 1 ? "" : "s");
  if (!m.error.empty()) {
    a.passed = false;
    a.detail = "not converged " + at + ": the guard could not render the "
        "baseline Case (" + m.error + ")";
    return a;
  }
  if (!m.before.valid() || m.before.width != m.after.width
      || m.before.height != m.after.height) {
    a.passed = false;
    a.detail = "not converged " + at + ": the guard's two renders are not "
        "comparable images";
    return a;
  }

  a.score = changedFraction(m.before, m.after, a.tau);
  a.passed = a.score <= a.threshold;
  if (a.passed) {
    a.detail = "stable: " + percent(a.score) + " of pixels changed over "
        + std::to_string(m.extraFrames) + " more frame"
        + (m.extraFrames == 1 ? "" : "s") + " (at most " + percent(a.threshold)
        + ") " + at;
    if (m.refinementProgress)
      a.detail += "; refinementProgress " + progressText(*m.refinementProgress);
    return a;
  }

  a.detail = "not converged " + at + " (changed fraction " + percent(a.score)
      + " over " + std::to_string(m.extraFrames) + " more frame"
      + (m.extraFrames == 1 ? "" : "s") + ", at most " + percent(a.threshold)
      + "); raise `--accumulation`";
  if (m.refinementProgress && *m.refinementProgress >= 1.f)
    a.detail += "; the device reported refinementProgress "
        + progressText(*m.refinementProgress)
        + " and then still changed, which contradicts itself";
  return a;
}

void failRelationsNotConverged(
    std::vector<RelationOutcome> &outcomes, const Assertion &convergence)
{
  if (convergence.passed)
    return;
  for (auto &o : outcomes) {
    Assertion &a = o.assertion;
    if (!a.evaluated)
      continue;
    a.detail = convergence.detail
        + (a.detail.empty() ? "" : " (the check itself: " + a.detail + ")");
    a.passed = false;
  }
}

bool hasEvaluatedRelation(const std::vector<RelationOutcome> &outcomes)
{
  for (const auto &o : outcomes)
    if (o.assertion.evaluated)
      return true;
  return false;
}

} // namespace cts
} // namespace anari
