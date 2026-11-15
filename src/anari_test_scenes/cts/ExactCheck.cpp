// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ExactCheck.h"
// std
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace anari {
namespace cts {

namespace {

std::string numberText(double value)
{
  std::ostringstream os;
  os << std::setprecision(6) << value;
  return os.str();
}

std::string percent(double fraction)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(2) << fraction * 100.0 << " %";
  return os.str();
}

const char *limitText(Limit limit)
{
  return limit == Limit::AtLeast ? ">=" : "<=";
}

ExactFinding failedFinding(std::string name, std::string detail)
{
  ExactFinding f;
  f.name = std::move(name);
  f.passed = false;
  f.detail = std::move(detail);
  return f;
}

ExactOutcome toOutcome(const std::string &label, ExactFinding f)
{
  ExactOutcome out;
  Assertion &a = out.assertion;
  a.kind = kExactAssertion;
  a.label = label;
  a.expectation = f.name;
  a.channel = f.channel;
  a.evaluated = true;
  a.passed = f.passed;
  a.detail = f.detail;
  a.score = std::numeric_limits<double>::quiet_NaN();
  a.threshold = std::numeric_limits<double>::quiet_NaN();
  if (f.measurement) {
    a.score = f.value;
    a.threshold = f.bound;
    a.measurement = Measurement{f.value, f.bound, limitText(f.limit)};
  } else if (f.checked > 0) {
    a.score = double(f.offending) / double(f.checked);
    a.threshold = 0.0;
    a.pixels = PixelCounts{f.offending, f.checked, std::move(f.first)};
  }
  out.mask = std::move(f.mask);
  return out;
}

} // namespace

// PixelMask //////////////////////////////////////////////////////////////////

size_t PixelMask::count() const
{
  return size_t(std::count_if(
      bits.begin(), bits.end(), [](uint8_t b) { return b != 0; }));
}

PixelMask PixelMask::inverted() const
{
  PixelMask out = *this;
  for (auto &b : out.bits)
    b = b ? 0 : 1;
  return out;
}

// Values /////////////////////////////////////////////////////////////////////

std::string valueText(uint32_t value)
{
  std::string text = std::to_string(value);
  if (value >= 0xffff0000u) {
    std::ostringstream hex;
    hex << " (0x" << std::hex << value << ")";
    text += hex.str();
  }
  return text;
}

std::string valueText(float value)
{
  return numberText(value);
}

std::string valueText(const Rgba8 &value)
{
  return "(" + std::to_string(value[0]) + ", " + std::to_string(value[1]) + ", "
      + std::to_string(value[2]) + ", " + std::to_string(value[3]) + ")";
}

double valueDistance(uint32_t a, uint32_t b)
{
  return a > b ? double(a - b) : double(b - a);
}

double valueDistance(float a, float b)
{
  if (a == b)
    return 0.0;
  if (std::isnan(a) || std::isnan(b))
    return std::numeric_limits<double>::infinity();
  return std::fabs(double(a) - double(b));
}

double valueDistance(const Rgba8 &a, const Rgba8 &b)
{
  int worst = 0;
  for (size_t i = 0; i < 4; ++i)
    worst = std::max(worst, std::abs(int(a[i]) - int(b[i])));
  return double(worst);
}

// ExactCheck /////////////////////////////////////////////////////////////////

ExactCheck::ExactCheck(const BuildContext &values,
    FrameSize size,
    const std::vector<ChannelValues> &channels,
    std::optional<CameraProjection> camera)
    : m_values(values),
      m_size(size),
      m_channels(channels),
      m_camera(std::move(camera))
{}

const ChannelValues &ExactCheck::channelValues(Channel channel) const
{
  for (const auto &c : m_channels)
    if (c.channel == channel)
      return c;
  throw std::invalid_argument(std::string("the Test does not render the ")
      + channelName(channel) + " channel");
}

ChannelView<uint32_t> ExactCheck::ids(Channel channel) const
{
  const auto &c = channelValues(channel);
  const size_t n = size_t(c.image.width) * c.image.height;
  if (c.ids.size() != n || n == 0)
    throw std::invalid_argument(std::string("the ") + channelName(channel)
        + " channel has no ID values");
  return {channel, c.image.width, c.image.height, c.ids.data()};
}

ChannelView<float> ExactCheck::depth() const
{
  const auto &c = channelValues(Channel::Depth);
  const size_t n = size_t(c.image.width) * c.image.height;
  if (c.depth.size() != n || n == 0)
    throw std::invalid_argument("the depth channel has no depth values");
  return {Channel::Depth, c.image.width, c.image.height, c.depth.data()};
}

ChannelView<Rgba8> ExactCheck::rgba(Channel channel) const
{
  const auto &c = channelValues(channel);
  if (!c.image.valid())
    throw std::invalid_argument(
        std::string("the ") + channelName(channel) + " channel has no image");
  return {channel, c.image.width, c.image.height, c.image.rgba.data()};
}

std::optional<math::float2> ExactCheck::project(const math::float3 &world) const
{
  if (!m_camera)
    throw std::runtime_error(
        "the Case's camera has no recorded projection (a Test's own camera "
        "hook records it with BuildContext::setCameraProjection())");
  return projectToPixel(*m_camera, m_size, world);
}

bool ExactCheck::recordPixels(std::string name,
    Channel channel,
    uint32_t width,
    uint32_t height,
    const PixelMask *where,
    const std::function<bool(uint32_t, uint32_t)> &holds,
    const std::function<FirstOffender(uint32_t, uint32_t)> &describe)
{
  ExactFinding f;
  f.name = std::move(name);
  f.channel = channelName(channel);
  if (where && (where->width != width || where->height != height)) {
    f.passed = false;
    f.detail = f.name + ": the mask is "
        + sizeText({where->width, where->height}) + " but the " + f.channel
        + " channel is " + sizeText({width, height});
    m_findings.push_back(std::move(f));
    return false;
  }

  // Mid grey: not looked at; black: held; white: offends.
  Image mask;
  mask.width = width;
  mask.height = height;
  mask.rgba.assign(size_t(width) * height * 4, 0);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      uint8_t shade = 128;
      if (!where || where->selects(x, y)) {
        ++f.checked;
        const bool ok = holds(x, y);
        shade = ok ? 0 : 255;
        if (!ok) {
          if (!f.first)
            f.first = describe(x, y);
          ++f.offending;
        }
      }
      uint8_t *p = &mask.rgba[(size_t(y) * width + x) * 4];
      p[0] = p[1] = p[2] = shade;
      p[3] = 255;
    }
  }

  f.passed = f.checked > 0 && f.offending == 0;
  if (f.checked == 0) {
    f.detail = f.name + ": no " + f.channel + " pixel was checked";
  } else {
    f.detail = f.name + ": " + std::to_string(f.offending) + " of "
        + std::to_string(f.checked) + " checked " + f.channel
        + " pixels offend";
    if (f.first)
      f.detail += "; first at (" + std::to_string(f.first->x) + ", "
          + std::to_string(f.first->y) + "): got " + f.first->got
          + ", expected " + f.first->expected;
  }
  if (!f.passed)
    f.mask = std::move(mask);
  m_findings.push_back(std::move(f));
  return m_findings.back().passed;
}

bool ExactCheck::measure(
    std::string name, double value, Limit limit, double bound)
{
  ExactFinding f;
  f.name = std::move(name);
  f.measurement = true;
  f.value = value;
  f.bound = bound;
  f.limit = limit;
  f.passed = !std::isnan(value)
      && (limit == Limit::AtLeast ? value >= bound : value <= bound);
  f.detail = f.name + " " + numberText(value) + ", required " + limitText(limit)
      + " " + numberText(bound);
  m_findings.push_back(std::move(f));
  return m_findings.back().passed;
}

// Evaluation /////////////////////////////////////////////////////////////////

std::vector<ExactOutcome> evaluateExactChecks(const TestDef &test,
    const Case &c,
    FrameSize size,
    const std::vector<ChannelValues> &channels,
    const std::optional<CameraProjection> &camera)
{
  std::vector<ExactOutcome> out;
  if (test.exactChecks.empty())
    return out;
  // Every channel must be the Case's size, so project() and the channel
  // views agree on what a pixel is.
  for (const auto &ch : channels) {
    if (ch.image.width == size.width && ch.image.height == size.height)
      continue;
    for (const auto &check : test.exactChecks)
      out.push_back(toOutcome(check.label,
          failedFinding("the check",
              std::string("the ") + channelName(ch.channel) + " channel is "
                  + sizeText({ch.image.width, ch.image.height})
                  + " but the Case is " + sizeText(size))));
    return out;
  }

  BuildContext values;
  for (const auto &cv : c.values)
    values.set(cv.axisName, cv.value);

  for (const auto &check : test.exactChecks) {
    ExactCheck ctx(values, size, channels, camera);
    std::optional<ExactFinding> problem;
    try {
      check.fn(ctx);
      if (ctx.findings().empty())
        problem = failedFinding(
            "the check", "the check recorded no finding (use its helpers)");
    } catch (const std::exception &e) {
      problem = failedFinding(
          "the check", std::string("the check failed: ") + e.what());
    } catch (...) {
      problem =
          failedFinding("the check", "the check failed: unknown exception");
    }
    for (auto f : ctx.findings())
      out.push_back(toOutcome(check.label, std::move(f)));
    if (problem)
      out.push_back(toOutcome(check.label, std::move(*problem)));
  }
  return out;
}

SelfCheckResults evaluateSelfChecks(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<std::optional<CaseChannels>> &inputs,
    const SelfCheckDefaults &defaults)
{
  if (inputs.size() != cases.size())
    throw std::invalid_argument(
        "evaluateSelfChecks: " + std::to_string(inputs.size()) + " inputs for "
        + std::to_string(cases.size()) + " Cases");
  SelfCheckResults results;
  results.exact.resize(cases.size());
  std::vector<CaseRender> renders(cases.size());
  for (size_t i = 0; i < cases.size(); ++i) {
    if (!inputs[i])
      continue;
    const CaseChannels &in = *inputs[i];
    FrameSize size = in.size;
    if ((size.width == 0 || size.height == 0) && !in.channels.empty())
      size = {
          in.channels.front().image.width, in.channels.front().image.height};
    results.exact[i] =
        evaluateExactChecks(test, cases[i], size, in.channels, in.camera);
    renders[i].state = CaseRender::State::Rendered;
    for (const auto &ch : in.channels)
      renders[i].images.push_back(ch.image);
  }
  results.relations = evaluateRelations(test, cases, renders, defaults);
  return results;
}

} // namespace cts
} // namespace anari
