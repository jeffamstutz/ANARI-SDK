// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <functional>
#include <string>

namespace anari {
namespace cts {

struct BuildContext;
struct Case;
struct TestDef;

// A frame's size in pixels.
struct FrameSize
{
  uint32_t width{0};
  uint32_t height{0};

  bool operator==(const FrameSize &o) const
  {
    return width == o.width && height == o.height;
  }
  bool operator!=(const FrameSize &o) const
  {
    return !(*this == o);
  }
};

// "WxH".
std::string sizeText(FrameSize size);

// A Case's frame size, or why it can't have one (ADR-0010: a Case whose size
// isn't whole pixels at the run's size is skipped with that reason).
struct CaseFrameSize
{
  FrameSize size;
  std::string skipReason; // empty when `size` holds

  CaseFrameSize() = default;
  CaseFrameSize(FrameSize s) : size(s) {}

  static CaseFrameSize skip(std::string reason)
  {
    CaseFrameSize out;
    out.skipReason = std::move(reason);
    return out;
  }

  bool resolved() const
  {
    return skipReason.empty();
  }
};

// A Test's per-Case frame-size hook: the Case's size given the run's
// (`--width`/`--height`), from the Case's axis values. A Test without one
// renders every Case at the run's size.
using FrameSizeFn = std::function<CaseFrameSize(BuildContext &, FrameSize run)>;

// The frame of the normalized region [lo, hi] of a `run`-sized frame (e.g. a
// camera's imageRegion), or a skip when its edges aren't whole pixels there.
CaseFrameSize regionFrameSize(
    FrameSize run, float loX, float loY, float hiX, float hiY);

// The size Case `c` renders at: its Test's frame-size hook applied to `run`,
// or `run` without one. A hook giving an empty size skips the Case. `device`
// is handed to the hook's BuildContext (may be null). Pure otherwise.
CaseFrameSize resolveFrameSize(const TestDef &test,
    const Case &c,
    FrameSize run,
    anari::Device device = nullptr);

} // namespace cts
} // namespace anari
