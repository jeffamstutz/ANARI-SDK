// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ConvergenceGuard.h"
#include "Metrics.h"
// std
#include <algorithm>
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

std::string notGuardedReason(
    bool rendererAccumulates, uint32_t accumulationFrames, bool hasColorChannel)
{
  if (!rendererAccumulates)
    return "the renderer lacks ANARI_KHR_FRAME_ACCUMULATION, so nothing shows "
           "its images are converged";
  if (accumulationFrames <= 1)
    return "the run renders one frame (--accumulation <= 1 or "
           "--no-accumulation), so nothing shows its images are converged";
  if (!hasColorChannel)
    return "the Test has no color Channel (depth and ID Channels are never "
           "accumulated)";
  return {};
}

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
    a.detail =
        "the convergence guard hit a render error after " + at + ": " + m.error;
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
      + "); raise `--accumulation` (a path tracer can need 256 frames or "
        "more; see cts/README.md)";
  if (m.refinementProgress && *m.refinementProgress >= 1.f)
    a.detail += "; the device reported refinementProgress "
        + progressText(*m.refinementProgress)
        + " and then still changed, which contradicts itself";
  return a;
}

std::vector<CaseGuard> guardRelations(std::vector<RelationOutcome> &outcomes,
    const std::vector<std::optional<ConvergenceMeasurement>> &measurements,
    const std::vector<std::string> &caseIds,
    const std::vector<bool> &selected,
    const SelfCheckDefaults &defaults)
{
  auto isSelected = [&](size_t k) {
    return k < selected.size() && selected[k];
  };
  // What is recorded: the evaluated assertions of selected Cases.
  auto recorded = [&](const RelationOutcome &o) {
    return o.assertion.evaluated && isSelected(o.caseIndex);
  };

  // Judge each Case a recorded assertion reads, once.
  struct Judged
  {
    CaseGuard guard;
    std::string reason; // the detail, without a "Case <id>: " prefix
  };
  std::vector<Judged> judged;
  std::vector<std::optional<size_t>> judgedOf(measurements.size());
  for (size_t k = 0; k < measurements.size(); ++k) {
    if (!measurements[k])
      continue;
    const RelationOutcome *reader = nullptr;
    for (const auto &o : outcomes) {
      const auto read = o.casesRead();
      if (recorded(o) && std::find(read.begin(), read.end(), k) != read.end()) {
        reader = &o;
        break;
      }
    }
    if (!reader)
      continue;
    Judged j;
    j.guard.caseIndex = k;
    j.guard.recordOn = k;
    j.guard.assertion = judgeConvergence(*measurements[k], defaults);
    j.reason = j.guard.assertion.detail;
    if (!isSelected(k)) {
      j.guard.recordOn = reader->caseIndex;
      j.guard.assertion.relatedCases = {caseIds[k]};
      j.guard.assertion.detail = "Case " + caseIds[k] + ": " + j.reason;
    }
    judgedOf[k] = judged.size();
    judged.push_back(std::move(j));
  }

  // Fail every recorded assertion reading a Case that failed the guard, on
  // the first such Case: its own, else its first partner's.
  for (auto &o : outcomes) {
    if (!recorded(o))
      continue;
    Assertion &a = o.assertion;
    for (size_t k : o.casesRead()) {
      if (k >= judgedOf.size() || !judgedOf[k])
        continue;
      const Judged &j = judged[*judgedOf[k]];
      if (j.guard.assertion.passed)
        continue;
      a.detail = (k == o.caseIndex ? "" : "Case " + caseIds[k] + ": ")
          + j.reason
          + (a.detail.empty() ? "" : " (the check itself: " + a.detail + ")");
      a.passed = false;
      break;
    }
  }

  std::vector<CaseGuard> guards;
  for (auto &j : judged)
    guards.push_back(std::move(j.guard));
  return guards;
}

} // namespace cts
} // namespace anari
