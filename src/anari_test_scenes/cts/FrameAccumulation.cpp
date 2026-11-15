// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "FrameAccumulation.h"
// std
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

std::string levels(double v)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", v);
  return buf;
}

std::string fraction(float v)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.3g", v);
  return buf;
}

std::string join(const std::vector<std::string> &parts)
{
  std::string s;
  for (const auto &p : parts)
    s += (s.empty() ? "" : "; ") + p;
  return s;
}

} // namespace

double meanAbsoluteDifference(const Image &a, const Image &b)
{
  if (a.width != b.width || a.height != b.height || !a.valid() || !b.valid())
    return std::numeric_limits<double>::infinity();
  double sum = 0.0;
  for (size_t i = 0; i < a.rgba.size(); i += 4)
    for (size_t c = 0; c < 3; ++c)
      sum += std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c]));
  return sum / (double(a.pixelCount()) * 3.0);
}

BehaviorResult checkFrameAccumulation(AccumulationTarget &target)
{
  std::vector<std::string> failures;
  std::vector<std::string> notes;

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

  if (!first)
    return {false, "first render's color readback failed: " + first.detail};
  if (!checkpoint)
    return {false,
        "color readback failed after " + std::to_string(kAccumulationCheckpoint)
            + " renders: " + checkpoint.detail};
  if (!accumulated)
    return {false,
        "color readback failed after " + std::to_string(kAccumulationRenders)
            + " renders: " + accumulated.detail};

  // Convergence: the checkpoint is clearly closer to the many-frame result
  // than the first render is.
  const auto N = std::to_string(kAccumulationRenders);
  const double dFirst = meanAbsoluteDifference(first.image, accumulated.image);
  const double dCheckpoint =
      meanAbsoluteDifference(checkpoint.image, accumulated.image);
  if (dFirst <= kConvergedLevels) {
    notes.push_back("converged in one render (mean difference " + levels(dFirst)
        + " levels to the " + N + "-render image)");
  } else {
    const std::string measured = "mean difference to the " + N
        + "-render image: " + levels(dFirst) + " levels after 1 render, "
        + levels(dCheckpoint) + " after "
        + std::to_string(kAccumulationCheckpoint);
    if (dCheckpoint <= kConvergenceRatio * dFirst)
      notes.push_back(measured);
    else
      failures.push_back("the image does not approach the " + N
          + "-render result as frames accumulate (" + measured
          + "; expected at most " + levels(kConvergenceRatio) + "x)");
  }

  // Restart: after a committed world change, the first render is nearer a
  // fresh frame's render of the changed world than the image accumulated
  // before the change.
  target.changeWorld();
  target.render();
  const auto afterChangeProgress = target.refinementProgress();
  const auto afterChange = target.color();
  const auto fresh = target.renderFresh();
  if (!afterChange)
    return {false,
        "color readback failed after the world change: " + afterChange.detail};
  if (!fresh)
    return {false, "fresh frame's color readback failed: " + fresh.detail};

  const double dChange = meanAbsoluteDifference(accumulated.image, fresh.image);
  const double dToFresh =
      meanAbsoluteDifference(afterChange.image, fresh.image);
  const double dToOld =
      meanAbsoluteDifference(afterChange.image, accumulated.image);
  if (dChange < kVisibleChangeLevels) {
    failures.push_back(
        "the world change is not visible in a fresh frame (mean "
        "difference "
        + levels(dChange) + " levels), so a restart can't be told");
  } else if (dToFresh < dToOld) {
    notes.push_back("restarted after a world change (mean difference "
        + levels(dToFresh) + " levels to a fresh frame, " + levels(dToOld)
        + " to the image before the change)");
  } else {
    failures.push_back(
        "accumulation did not restart after a world change: "
        "the next render is "
        + levels(dToOld) + " levels from the image before the change and "
        + levels(dToFresh) + " from a fresh frame of the changed world");
  }

  // refinementProgress, where reported.
  bool reported = afterChangeProgress.has_value();
  std::optional<float> previous, firstReported, lastReported;
  bool inRange = true, decreased = false;
  auto checkRange = [&](float p) { inRange &= p >= 0.f && p <= 1.f; };
  for (const auto &p : progress) {
    if (!p)
      continue;
    reported = true;
    checkRange(*p);
    if (previous && *p < *previous)
      decreased = true;
    previous = p;
    if (!firstReported)
      firstReported = p;
    lastReported = p;
  }
  if (afterChangeProgress)
    checkRange(*afterChangeProgress);

  if (!reported) {
    notes.push_back("refinementProgress not reported");
  } else {
    if (!inRange)
      failures.push_back("refinementProgress outside [0, 1]");
    if (decreased)
      failures.push_back(
          "refinementProgress decreased across renders without changes");
    if (firstReported && lastReported && afterChangeProgress
        && *lastReported > *firstReported
        && !(*afterChangeProgress < *lastReported))
      failures.push_back(
          "refinementProgress did not drop after a world change ("
          + fraction(*lastReported) + " before, "
          + fraction(*afterChangeProgress) + " after)");
    if (inRange && !decreased && firstReported && lastReported)
      notes.push_back("refinementProgress " + fraction(*firstReported) + " -> "
          + fraction(*lastReported) + ", "
          + (afterChangeProgress ? fraction(*afterChangeProgress)
                                 : std::string("not reported"))
          + " after the change");
  }

  if (!failures.empty())
    return {false, join(failures)};
  return {true, join(notes)};
}

} // namespace cts
} // namespace anari
