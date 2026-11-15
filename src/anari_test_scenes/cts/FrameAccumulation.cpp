// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "FrameAccumulation.h"
#include "Metrics.h"
// std
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

namespace {

std::string formatNumber(double v, const char *format)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), format, v);
  return buf;
}

// A mean difference in 8-bit levels.
std::string formatLevels(double v)
{
  return formatNumber(v, "%.2f");
}

// A refinementProgress value or a ratio.
std::string formatFraction(double v)
{
  return formatNumber(v, "%.3g");
}

// What the phases found: failures decide the verdict, notes describe a pass.
struct Findings
{
  std::vector<std::string> failures;
  std::vector<std::string> notes;

  BehaviorResult result() const
  {
    const auto &parts = failures.empty() ? notes : failures;
    std::string s;
    for (const auto &p : parts)
      s += (s.empty() ? "" : "; ") + p;
    return {failures.empty(), s};
  }
};

// The checkpoint is clearly closer to the many-frame result than the first
// render is, unless the first render is already converged.
void judgeConvergence(const Image &first,
    const Image &checkpoint,
    const Image &accumulated,
    Findings &out)
{
  const auto N = std::to_string(kAccumulationRenders);
  const double dFirst = meanAbsoluteDifference(first, accumulated);
  const double dCheckpoint = meanAbsoluteDifference(checkpoint, accumulated);
  if (dFirst <= kConvergedLevels) {
    out.notes.push_back("converged in one render (mean difference "
        + formatLevels(dFirst) + " levels to the " + N + "-render image)");
    return;
  }
  const std::string measured = "mean difference to the " + N
      + "-render image: " + formatLevels(dFirst) + " levels after 1 render, "
      + formatLevels(dCheckpoint) + " after "
      + std::to_string(kAccumulationCheckpoint);
  if (dCheckpoint <= kConvergenceRatio * dFirst)
    out.notes.push_back(measured);
  else
    out.failures.push_back("the image does not approach the " + N
        + "-render result as frames accumulate (" + measured
        + "; expected at most " + formatFraction(kConvergenceRatio) + "x)");
}

// After a committed world change, the next render is nearer a fresh frame's
// render of the changed world than the image accumulated before the change.
void judgeRestart(const Image &accumulated,
    const Image &afterChange,
    const Image &fresh,
    Findings &out)
{
  const double dChange = meanAbsoluteDifference(accumulated, fresh);
  const double dToFresh = meanAbsoluteDifference(afterChange, fresh);
  const double dToOld = meanAbsoluteDifference(afterChange, accumulated);
  if (!(dChange >= kVisibleChangeLevels)) {
    out.failures.push_back(
        "the world change is not visible in a fresh frame (mean difference "
        + formatLevels(dChange) + " levels), so a restart can't be told");
  } else if (dToFresh < dToOld) {
    out.notes.push_back("restarted after a world change (mean difference "
        + formatLevels(dToFresh) + " levels to a fresh frame, "
        + formatLevels(dToOld) + " to the image before the change)");
  } else {
    out.failures.push_back(
        "accumulation did not restart after a world change: the next render "
        "is "
        + formatLevels(dToOld) + " levels from the image before the change and "
        + formatLevels(dToFresh) + " from a fresh frame of the changed world");
  }
}

// refinementProgress, where reported: in [0, 1], not decreasing across the
// renders without changes, and lower after the change if it rose before it.
void judgeProgress(const std::vector<std::optional<float>> &beforeChange,
    std::optional<float> afterChange,
    Findings &out)
{
  std::vector<float> reported;
  for (const auto &p : beforeChange)
    if (p)
      reported.push_back(*p);
  if (reported.empty() && !afterChange) {
    out.notes.push_back("refinementProgress not reported");
    return;
  }

  auto inRange = [](float p) { return p >= 0.f && p <= 1.f; };
  bool allInRange = !afterChange || inRange(*afterChange);
  bool decreased = false;
  for (size_t i = 0; i < reported.size(); ++i) {
    allInRange &= inRange(reported[i]);
    decreased |= i > 0 && reported[i] < reported[i - 1];
  }
  if (!allInRange)
    out.failures.push_back("refinementProgress outside [0, 1]");
  if (decreased)
    out.failures.push_back(
        "refinementProgress decreased across renders without changes");
  if (reported.empty())
    return;

  const float first = reported.front();
  const float last = reported.back();
  if (afterChange && last > first && !(*afterChange < last))
    out.failures.push_back(
        "refinementProgress did not drop after a world change ("
        + formatFraction(last) + " before, " + formatFraction(*afterChange)
        + " after)");
  out.notes.push_back("refinementProgress " + formatFraction(first) + " -> "
      + formatFraction(last) + ", "
      + (afterChange ? formatFraction(*afterChange)
                     : std::string("not reported"))
      + " after the change");
}

} // namespace

BehaviorResult checkFrameAccumulation(AccumulationTarget &target)
{
  // Renders without changes, keeping the first and checkpoint images and the
  // progress after each render.
  std::vector<std::optional<float>> progress;
  FrameReadbackResult first, checkpoint;
  for (int i = 1; i <= kAccumulationRenders; ++i) {
    target.render();
    progress.push_back(target.refinementProgress());
    if (i == 1)
      first = target.color();
    else if (i == kAccumulationCheckpoint)
      checkpoint = target.color();
  }
  const auto accumulated = target.color();

  target.changeWorld();
  target.render();
  const auto afterChangeProgress = target.refinementProgress();
  const auto afterChange = target.color();
  const auto fresh = target.renderFresh();

  const std::pair<const FrameReadbackResult *, std::string> readbacks[] = {
      {&first, "the first render"},
      {&checkpoint, std::to_string(kAccumulationCheckpoint) + " renders"},
      {&accumulated, std::to_string(kAccumulationRenders) + " renders"},
      {&afterChange, "the world change"},
      {&fresh, "a fresh frame's render"},
  };
  for (const auto &[r, when] : readbacks)
    if (!*r)
      return {false, "color readback failed after " + when + ": " + r->detail};

  Findings findings;
  judgeConvergence(first.image, checkpoint.image, accumulated.image, findings);
  judgeRestart(accumulated.image, afterChange.image, fresh.image, findings);
  judgeProgress(progress, afterChangeProgress, findings);
  return findings.result();
}

} // namespace cts
} // namespace anari
