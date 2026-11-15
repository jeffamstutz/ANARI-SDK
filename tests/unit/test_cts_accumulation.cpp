// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The KHR_FRAME_ACCUMULATION behaviour check (`frame/progressive_rendering`)
// against stub frames whose images are generated in code: no device.

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/FrameAccumulation.h"
// std
#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace anari::cts;
using cts_test::rectangle;

namespace {

constexpr uint32_t kSize = 32;

// The noise-free image of the scene before and after the world change: a
// subject on a background, in mid-range values so noise doesn't clip.
Image scene(bool changed)
{
  return changed ? rectangle(kSize,
                       kSize,
                       4,
                       4,
                       28,
                       28,
                       {60, 90, 200, 255},
                       {100, 100, 100, 255})
                 : rectangle(kSize,
                       kSize,
                       4,
                       4,
                       28,
                       28,
                       {200, 150, 90, 255},
                       {100, 100, 100, 255});
}

FrameReadbackResult ok(Image im)
{
  FrameReadbackResult r;
  r.image = std::move(im);
  return r;
}

// A stub frame. Each render takes one sample: the scene plus uniform noise of
// `noise` levels per channel. How the samples become the frame's image, and
// what a world change does, is set by the flags.
struct StubFrame : AccumulationTarget
{
  int noise{40};
  bool accumulates{true}; // average the samples since the last restart
  bool restarts{true}; // a world change restarts accumulation
  bool reportsProgress{true};
  bool progressDecreases{false};

  bool changed{false};
  std::vector<double> sum = std::vector<double>(kSize * kSize * 4, 0.0);
  int count{0};
  Image last;
  std::mt19937 rng{7};

  Image sample()
  {
    Image im = scene(changed);
    std::uniform_int_distribution<int> n(-noise, noise);
    for (size_t i = 0; i < im.rgba.size(); ++i) {
      if (i % 4 == 3)
        continue;
      const int v = im.rgba[i] + (noise ? n(rng) : 0);
      im.rgba[i] = static_cast<uint8_t>(std::clamp(v, 0, 255));
    }
    return im;
  }

  void render() override
  {
    const Image s = sample();
    if (!accumulates) {
      last = s;
      ++count;
      return;
    }
    for (size_t i = 0; i < s.rgba.size(); ++i)
      sum[i] += s.rgba[i];
    ++count;
    last = s;
    for (size_t i = 0; i < s.rgba.size(); ++i)
      last.rgba[i] = static_cast<uint8_t>(sum[i] / count + 0.5);
  }

  FrameReadbackResult color() override
  {
    return ok(last);
  }

  std::optional<float> refinementProgress() override
  {
    if (!reportsProgress)
      return std::nullopt;
    const float p = std::min(1.f, count / 64.f);
    return progressDecreases ? 1.f - p : p;
  }

  void changeWorld() override
  {
    changed = true;
    if (restarts) {
      std::fill(sum.begin(), sum.end(), 0.0);
      count = 0;
    }
  }

  FrameReadbackResult renderFresh() override
  {
    return ok(sample());
  }
};

} // namespace

TEST_CASE("frame accumulation: an accumulating frame passes", "[cts]")
{
  StubFrame f;
  const auto r = checkFrameAccumulation(f);
  INFO(r.detail);
  CHECK(r.passed);
}

TEST_CASE(
    "frame accumulation: a frame that ignores accumulation fails", "[cts]")
{
  // Fresh noise every render: the image changes, but never improves.
  StubFrame f;
  f.accumulates = false;
  const auto r = checkFrameAccumulation(f);
  INFO(r.detail);
  CHECK_FALSE(r.passed);
  CHECK(r.detail.find("does not approach") != std::string::npos);
}

TEST_CASE("frame accumulation: a frame converged in one render passes", "[cts]")
{
  // A deterministic renderer, or a scene with nothing to sample: nothing
  // changes, and the check doesn't ask for a change. No refinementProgress.
  StubFrame f;
  f.noise = 0;
  f.accumulates = false;
  f.reportsProgress = false;
  const auto r = checkFrameAccumulation(f);
  INFO(r.detail);
  CHECK(r.passed);
  CHECK(r.detail.find("converged in one render") != std::string::npos);
  CHECK(r.detail.find("refinementProgress not reported") != std::string::npos);
}

TEST_CASE(
    "frame accumulation: a frame that doesn't restart after a world "
    "change fails",
    "[cts]")
{
  StubFrame f;
  f.restarts = false;
  f.reportsProgress = false;
  const auto r = checkFrameAccumulation(f);
  INFO(r.detail);
  CHECK_FALSE(r.passed);
  CHECK(r.detail.find("did not restart") != std::string::npos);
}

TEST_CASE(
    "frame accumulation: progress that keeps rising across a world "
    "change fails",
    "[cts]")
{
  // The image restarts, but refinementProgress doesn't drop: progress counts
  // samples since the stub frame was created, not since the change.
  struct NoProgressDrop : StubFrame
  {
    int total{0};
    void render() override
    {
      StubFrame::render();
      ++total;
    }
    std::optional<float> refinementProgress() override
    {
      return std::min(1.f, total / 64.f);
    }
  } f;
  const auto r = checkFrameAccumulation(f);
  INFO(r.detail);
  CHECK_FALSE(r.passed);
  CHECK(r.detail.find("did not drop") != std::string::npos);
}

TEST_CASE("frame accumulation: decreasing refinementProgress fails", "[cts]")
{
  StubFrame f;
  f.progressDecreases = true;
  const auto r = checkFrameAccumulation(f);
  INFO(r.detail);
  CHECK_FALSE(r.passed);
  CHECK(r.detail.find("decreased") != std::string::npos);
}

TEST_CASE("frame accumulation: a failed readback fails", "[cts]")
{
  struct BadReadback : StubFrame
  {
    FrameReadbackResult color() override
    {
      FrameReadbackResult r;
      r.error = FrameReadbackError::NullData;
      r.detail = "null";
      return r;
    }
  } f;
  const auto r = checkFrameAccumulation(f);
  CHECK_FALSE(r.passed);
  CHECK(r.detail.find("readback failed") != std::string::npos);
}

TEST_CASE("frame accumulation: mean absolute difference", "[cts]")
{
  const Image a = cts_test::solid(4, 4, 10, 20, 30, 255);
  const Image b = cts_test::solid(4, 4, 13, 20, 24, 0); // alpha ignored
  CHECK(meanAbsoluteDifference(a, a) == 0.0);
  CHECK(meanAbsoluteDifference(a, b) == Approx(3.0));
}
