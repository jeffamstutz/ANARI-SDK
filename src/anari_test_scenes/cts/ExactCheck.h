// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BuildContext.h"
#include "CameraProjection.h"
#include "Case.h"
#include "Channel.h"
#include "FrameReadback.h"
#include "FrameSize.h"
#include "Image.h"
#include "Relations.h"
#include "Sidecar.h"
#include "TestDef.h"
// std
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// Exact checks (ADR-0010): per-Case assertions over a Case's raw channel
// values (the ID at a pixel, the depth of a known surface, "pixels outside
// this region are background"), for answers that are analytic. A Test
// declares one with `.expectExact(label, fn)`; `fn` receives an ExactCheck,
// reads the Case's axis values, frame size and channels from it, and reports
// through its helpers, each of which records a finding (counts, the first
// offender, a mask of where it failed) that the report shows without
// understanding the predicate.
namespace anari {
namespace cts {

// An 8-bit RGBA pixel of a comparison image.
using Rgba8 = std::array<uint8_t, 4>;

// A read-only typed view of one Channel: `uint32_t` for the ID Channels,
// `float` for depth, Rgba8 for the comparison image of any Channel. Pixel
// (x, y) in Image convention (row 0 at the bottom).
template <typename T>
struct ChannelView
{
  Channel channel{Channel::Color};
  uint32_t width{0};
  uint32_t height{0};
  const void *data{nullptr};

  T at(uint32_t x, uint32_t y) const
  {
    return static_cast<const T *>(data)[size_t(y) * width + x];
  }
};

template <>
inline Rgba8 ChannelView<Rgba8>::at(uint32_t x, uint32_t y) const
{
  const auto *p =
      static_cast<const uint8_t *>(data) + (size_t(y) * width + x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

// A set of pixels of a frame: where a helper looks, or a subject's footprint.
struct PixelMask
{
  uint32_t width{0};
  uint32_t height{0};
  std::vector<uint8_t> bits;

  PixelMask() = default;
  PixelMask(uint32_t w, uint32_t h, bool value = false)
      : width(w), height(h), bits(size_t(w) * h, value ? 1 : 0)
  {}

  void set(uint32_t x, uint32_t y, bool on = true)
  {
    bits[size_t(y) * width + x] = on ? 1 : 0;
  }
  bool selects(uint32_t x, uint32_t y) const
  {
    return bits[size_t(y) * width + x] != 0;
  }
  size_t count() const;
  PixelMask inverted() const;
};

// Text of a pixel value, for a finding's first offender.
std::string valueText(uint32_t value);
std::string valueText(float value);
std::string valueText(const Rgba8 &value);

// How far apart two pixel values are: the absolute difference, or for Rgba8
// the largest per-channel one. Equal values (including equal infinities) are
// 0 apart; a NaN is infinitely far from anything.
double valueDistance(uint32_t a, uint32_t b);
double valueDistance(float a, float b);
double valueDistance(const Rgba8 &a, const Rgba8 &b);

// What a scalar measurement must be: at least, or at most, its bound.
enum class Limit
{
  AtLeast,
  AtMost,
};

// One result an exact check recorded: a pixel finding (how many of the
// pixels it looked at offend, the first offender, and a mask) or a scalar
// measurement (a named number with its bound). Both have a name and pass or
// fail.
struct ExactFinding
{
  std::string name;
  bool passed{false};
  std::string channel; // empty for a measurement
  // A pixel finding.
  size_t offending{0};
  size_t checked{0};
  std::optional<FirstOffender> first;
  // Where it failed: opaque white for an offending pixel, black for a
  // checked one that held, mid grey for one not looked at. Only on failure.
  Image mask;
  // A scalar measurement.
  bool measurement{false};
  double value{std::numeric_limits<double>::quiet_NaN()};
  double bound{std::numeric_limits<double>::quiet_NaN()};
  Limit limit{Limit::AtLeast};
  // One line, as the report shows it.
  std::string detail;
};

// The context an exact check runs in: the Case's axis values, frame size,
// channels and camera, and the helpers it reports through. A helper records
// a finding and returns whether it held; the check passes when every finding
// does (and it records at least one).
class ExactCheck
{
 public:
  ExactCheck(const BuildContext &values,
      FrameSize size,
      const std::vector<ChannelValues> &channels,
      std::optional<CameraProjection> camera);
  // It keeps references to the axis values and channels: no temporaries.
  ExactCheck(BuildContext &&,
      FrameSize,
      const std::vector<ChannelValues> &,
      std::optional<CameraProjection>) = delete;
  ExactCheck(const BuildContext &,
      FrameSize,
      std::vector<ChannelValues> &&,
      std::optional<CameraProjection>) = delete;

  // The Case's axis values (as a build hook reads them).
  const BuildContext &values() const
  {
    return m_values;
  }
  template <typename T>
  T get(const std::string &name, T valIfNotFound) const
  {
    return m_values.get<T>(name, valIfNotFound);
  }

  FrameSize frameSize() const
  {
    return m_size;
  }

  // Typed views of the Test's declared Channels. Throw std::invalid_argument
  // for a Channel the Test doesn't declare (or, for ids()/depth(), one whose
  // raw values weren't kept).
  ChannelView<uint32_t> ids(Channel channel) const;
  ChannelView<float> depth() const;
  ChannelView<Rgba8> rgba(Channel channel) const;

  // Where a world-space point lands in the frame (projectToPixel()) through
  // the Case's camera. Empty behind the camera. Throws std::runtime_error
  // when the Case's camera has no recorded projection (a Test's own camera
  // hook that didn't call BuildContext::setCameraProjection()).
  std::optional<anari::math::float2> project(
      const anari::math::float3 &world) const;
  bool canProject() const
  {
    return m_camera.has_value();
  }
  // The Case's camera, when its projection is recorded (e.g. its position,
  // which depth is measured from).
  const std::optional<CameraProjection> &camera() const
  {
    return m_camera;
  }

  // Helpers ////////////////////////////////////////////////////////////////

  // Every pixel of `view` (inside `where`, when given) satisfies
  // `pred(x, y, value)`. `expected` says in words what a pixel must hold
  // (the first offender's "expected").
  template <typename T, typename Pred>
  bool allPixels(std::string name,
      const ChannelView<T> &view,
      std::string expected,
      Pred pred,
      const PixelMask *where = nullptr)
  {
    return recordPixels(
        std::move(name),
        view.channel,
        view.width,
        view.height,
        where,
        [&](uint32_t x, uint32_t y) { return bool(pred(x, y, view.at(x, y))); },
        [&](uint32_t x, uint32_t y) {
          return FirstOffender{x, y, valueText(view.at(x, y)), expected};
        });
  }

  // Every pixel of `view` (inside `where`, when given) is within `tolerance`
  // of `expected(x, y)` (valueDistance()).
  template <typename T, typename Expected>
  bool matches(std::string name,
      const ChannelView<T> &view,
      Expected expected,
      double tolerance = 0.0,
      const PixelMask *where = nullptr)
  {
    return recordPixels(
        std::move(name),
        view.channel,
        view.width,
        view.height,
        where,
        [&](uint32_t x, uint32_t y) {
          return valueDistance(view.at(x, y), T(expected(x, y))) <= tolerance;
        },
        [&](uint32_t x, uint32_t y) {
          std::string want = valueText(T(expected(x, y)));
          if (tolerance > 0.0)
            want += " +/- " + valueText(float(tolerance));
          return FirstOffender{x, y, valueText(view.at(x, y)), want};
        });
  }

  // Every pixel of `view` outside `subject` satisfies `isBackground(value)`;
  // `background` says in words what background holds.
  template <typename T, typename Pred>
  bool backgroundOutside(std::string name,
      const ChannelView<T> &view,
      const PixelMask &subject,
      std::string background,
      Pred isBackground)
  {
    const PixelMask outside = subject.inverted();
    return allPixels(
        std::move(name),
        view,
        std::move(background),
        [&](uint32_t, uint32_t, const T &v) { return bool(isBackground(v)); },
        &outside);
  }

  // A scalar measurement: `value` must be at least (or at most) `bound`.
  // This is where a magnitude is checked ("edge contrast 0.31, required
  // >= 0.6").
  bool measure(std::string name, double value, Limit limit, double bound);

  const std::vector<ExactFinding> &findings() const
  {
    return m_findings;
  }

 private:
  const ChannelValues &channelValues(Channel channel) const;
  bool recordPixels(std::string name,
      Channel channel,
      uint32_t width,
      uint32_t height,
      const PixelMask *where,
      const std::function<bool(uint32_t, uint32_t)> &holds,
      const std::function<FirstOffender(uint32_t, uint32_t)> &describe);

  const BuildContext &m_values;
  FrameSize m_size;
  const std::vector<ChannelValues> &m_channels;
  std::optional<CameraProjection> m_camera;
  std::vector<ExactFinding> m_findings;
};

// One finding of an exact check as a Sidecar assertion (kind "exact"), and
// its failure mask (written by the runner, which fills the mask path).
struct ExactOutcome
{
  Assertion assertion;
  Image mask;
};

// Run every exact check of `test` on Case `c` (ADR-0010): one assertion per
// finding, labelled with its check. A check that throws, or records nothing,
// yields a failed assertion saying so. `channels` are in TestDef::channels
// order. Pure: no device.
std::vector<ExactOutcome> evaluateExactChecks(const TestDef &test,
    const Case &c,
    FrameSize size,
    const std::vector<ChannelValues> &channels,
    const std::optional<CameraProjection> &camera);

// The evaluation seam for Test authors (ADR-0010) ////////////////////////////

// What one Case supplies to evaluateSelfChecks(): its channels (TestDef::
// channels order), the size it rendered at (0x0: the first channel image's
// size) and its camera, if exact checks project.
struct CaseChannels
{
  std::vector<ChannelValues> channels;
  FrameSize size;
  std::optional<CameraProjection> camera;
};

struct SelfCheckResults
{
  // Per Case (indices into `cases`): its exact checks' outcomes.
  std::vector<std::vector<ExactOutcome>> exact;
  // The relational pass (evaluateRelations()) over the Cases supplied.
  std::vector<RelationOutcome> relations;
};

// Evaluate all of a Test's self-checks, its exact checks and its relations
// (effect, equivalence and derived expectations), against supplied channel
// data, with no device: a Test author builds a correct and a wrong image in
// code and shows the checks accept the first and reject the second. An empty
// optional is a Case not rendered. Throws as evaluateRelations() does.
SelfCheckResults evaluateSelfChecks(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<std::optional<CaseChannels>> &inputs,
    const SelfCheckDefaults &defaults = {});

} // namespace cts
} // namespace anari
