// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FrameReadback.h"
#include "TestDef.h"
// std
#include <optional>

// The behaviour check for KHR_FRAME_ACCUMULATION
// (`frame/progressive_rendering`). The 1.2 draft (section 5.1) promises that
// with `accumulation` enabled, "multiple subsequent calls to anariRenderFrame
// without changes to Frame improve the image quality", and lists the frame
// property `refinementProgress` ("progress of frame refinement, in [0..1]")
// under the extension. The check asserts what follows from that, and nothing a
// correct device can't do:
//   - accumulated frames approach the many-frame result: the difference to it
//     after a few renders is clearly smaller than after one. A device whose
//     first render is already converged (a deterministic renderer, or a scene
//     with nothing to sample) passes; a device that renders fresh noise each
//     time without accumulating fails;
//   - a committed change to the world restarts accumulation: the first render
//     after it is closer to a fresh frame's render of the changed world than
//     to the image accumulated before the change;
//   - `refinementProgress`, when reported, is in [0, 1], doesn't decrease
//     across renders without changes, and drops after the change if it rose
//     before it. It is optional in the API definitions (not tagged required),
//     so a device that doesn't report it isn't failed for that.
namespace anari {
namespace cts {

// What the check drives: one frame with `accumulation` enabled, over a scene
// whose world the check can change. Implemented over an ANARI device in
// tests/frame.cpp, and by stubs in the unit tests.
class AccumulationTarget
{
 public:
  virtual ~AccumulationTarget() = default;
  // Render the frame once and wait for it.
  virtual void render() = 0;
  // The frame's colour channel as of the last render.
  virtual FrameReadbackResult color() = 0;
  // The frame's `refinementProgress` as of the last render, if reported.
  virtual std::optional<float> refinementProgress() = 0;
  // Commit a visible change to the world the frame renders.
  virtual void changeWorld() = 0;
  // Render the current world once in a new frame (accumulation enabled) and
  // return its colour channel: the reference for a restarted accumulation.
  virtual FrameReadbackResult renderFresh() = 0;
};

// Renders without changes before the many-frame result is read, and the
// early checkpoint compared with the first render. For Monte Carlo
// accumulation the expected difference to the many-frame result after k
// renders scales with sqrt(1/k - 1/N): 0.45 of the first render's at k = 4,
// N = 16, well inside kConvergenceRatio.
inline constexpr int kAccumulationRenders = 16;
inline constexpr int kAccumulationCheckpoint = 4;
// The early checkpoint's difference to the many-frame result must be at most
// this fraction of the first render's.
inline constexpr double kConvergenceRatio = 0.8;
// A first render within this mean difference (8-bit levels) of the many-frame
// result is already converged; there is nothing to refine. Above zero so a
// device that is deterministic in intent but varies by a level here and there
// (threaded float reductions, GPU atomics) isn't asked to converge noise it
// doesn't have.
inline constexpr double kConvergedLevels = 0.25;
// The world change must move the image by at least this mean difference.
inline constexpr double kVisibleChangeLevels = 1.0;

// Renders the target kAccumulationRenders times, changes the world, renders
// once more, and judges the results as described above.
BehaviorResult checkFrameAccumulation(AccumulationTarget &target);

} // namespace cts
} // namespace anari
