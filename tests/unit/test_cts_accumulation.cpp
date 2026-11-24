// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The KHR_FRAME_ACCUMULATION behaviour check (`frame/progressive_rendering`)
// against stub frames whose images are generated in code: no device.

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/Filter.h"
#include "cts/FrameAccumulation.h"
#include "cts/Value.h"
// std
#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
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
  const cts_test::Rgba subject = changed ? cts_test::Rgba{60, 90, 200, 255}
                                         : cts_test::Rgba{200, 150, 90, 255};
  return rectangle(kSize, kSize, 4, 4, 28, 28, subject, {100, 100, 100, 255});
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
    last = sample();
    ++count;
    if (!accumulates)
      return;
    for (size_t i = 0; i < last.rgba.size(); ++i) {
      sum[i] += last.rgba[i];
      last.rgba[i] = static_cast<uint8_t>(sum[i] / count + 0.5);
    }
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

namespace {

// A stub renderer that samples each pixel at its centre (no antialiasing
// jitter) through a lens: the scene is a vertical edge, subject left and
// background right, and each render takes one lens sample per pixel, which
// moves the edge by up to `blur` pixels either way. With no blur (a pinhole)
// every render is the same; with blur the edge's band is noise that only
// accumulation removes, as a thin lens's is on any renderer.
struct LensStub : AccumulationTarget
{
  float blur{0.f};
  bool accumulates{true};

  bool changed{false};
  std::vector<double> sum = std::vector<double>(kSize * kSize * 4, 0.0);
  int count{0};
  Image last;
  std::mt19937 rng{11};

  Image sample()
  {
    const cts_test::Rgba subject = changed ? cts_test::Rgba{60, 90, 200, 255}
                                           : cts_test::Rgba{200, 150, 90, 255};
    const cts_test::Rgba background{100, 100, 100, 255};
    std::uniform_real_distribution<float> lens(-blur, blur);
    Image im = cts_test::solid(kSize, kSize, 100, 100, 100, 255);
    for (uint32_t y = 0; y < kSize; ++y)
      for (uint32_t x = 0; x < kSize; ++x) {
        const float shifted = x + 0.5f + (blur > 0.f ? lens(rng) : 0.f);
        cts_test::setPixel(
            im, x, y, shifted < 0.5f * kSize ? subject : background);
      }
    return im;
  }

  void render() override
  {
    last = sample();
    ++count;
    if (!accumulates)
      return;
    for (size_t i = 0; i < last.rgba.size(); ++i) {
      sum[i] += last.rgba[i];
      last.rgba[i] = static_cast<uint8_t>(sum[i] / count + 0.5);
    }
  }

  FrameReadbackResult color() override
  {
    return ok(last);
  }

  std::optional<float> refinementProgress() override
  {
    return std::nullopt;
  }

  void changeWorld() override
  {
    changed = true;
    std::fill(sum.begin(), sum.end(), 0.0);
    count = 0;
  }

  FrameReadbackResult renderFresh() override
  {
    return ok(sample());
  }
};

} // namespace

TEST_CASE(
    "frame accumulation: a renderer that never accumulates passes on a "
    "pinhole edge and fails under depth of field",
    "[cts]")
{
  // Why frame/progressive_rendering has a depth-of-field Case: a renderer
  // that samples pixel centres renders the edge Case the same every time, so
  // it reads as converged whether or not it accumulates.
  LensStub pinhole;
  pinhole.accumulates = false;
  const auto p = checkFrameAccumulation(pinhole);
  INFO(p.detail);
  CHECK(p.passed);
  CHECK(p.detail.find("converged in one render") != std::string::npos);

  // Through a lens, each render is one aperture sample: a renderer that
  // returns that one-sample frame instead of accumulating fails.
  LensStub lens;
  lens.blur = 6.f;
  lens.accumulates = false;
  const auto l = checkFrameAccumulation(lens);
  INFO(l.detail);
  CHECK_FALSE(l.passed);
  CHECK(l.detail.find("does not approach") != std::string::npos);

  // One that accumulates the aperture samples passes.
  LensStub accumulating;
  accumulating.blur = 6.f;
  const auto a = checkFrameAccumulation(accumulating);
  INFO(a.detail);
  CHECK(a.passed);
  CHECK(a.detail.find("converged in one render") == std::string::npos);
}

TEST_CASE(
    "frame/progressive_rendering: the depth-of-field Case needs the "
    "extension, the pinhole Case doesn't",
    "[cts]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  const auto found = catalog.filter(Filter{"frame/progressive_rendering"});
  REQUIRE(found.size() == 1);
  const TestDef &t = *found.front();
  REQUIRE(t.behaviorCheck);

  // Everything the Test needs, but not depth of field (helide-like, or
  // halcyon's preview renderer).
  std::set<std::string> device(
      t.requiredFeatures.begin(), t.requiredFeatures.end());
  const auto cases = expand(t);
  REQUIRE(cases.size() == 2);
  std::set<std::string> ids;
  for (const auto &c : cases) {
    INFO(c.id());
    ids.insert(c.id());
    const bool lens =
        std::any_of(c.values.begin(), c.values.end(), [](const CaseValue &v) {
          return v.axisName == "lens"
              && anyToString(v.value) == "depth_of_field";
        });
    CHECK(isSupported(t, c, device) == !lens);
    if (lens)
      CHECK(missingFeatures(t, c, device)
          == std::vector<std::string>{"ANARI_KHR_CAMERA_DEPTH_OF_FIELD"});
  }
  CHECK(ids == std::set<std::string>{"pinhole", "depth_of_field"});
  device.insert("ANARI_KHR_CAMERA_DEPTH_OF_FIELD");
  for (const auto &c : cases)
    CHECK(isSupported(t, c, device));
}
