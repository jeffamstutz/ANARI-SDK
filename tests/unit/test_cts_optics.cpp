// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The depth-of-field and stereo Tests, judged through the evaluation seam
// with images generated in code (ADR-0010): what a device implementing the
// behaviour renders passes, under each reading the spec leaves open; what one
// getting it wrong renders fails. No SDK device implements stereo, so this is
// its validation without a device.

#include "catch.hpp"
#include "cts_test_images.h"
// anari_test_scenes
#include "cts/BuiltinTests.h"
#include "cts/CameraProjection.h"
#include "cts/Catalog.h"
#include "cts/ExactCheck.h"
#include "cts/Expansion.h"
#include "cts/Filter.h"
#include "cts/Image.h"
#include "cts/Relations.h"
#include "cts/tests/OpticsScene.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace anari::cts;
using anari::math::float2;
using anari::math::float3;
using cts_test::Rgba;
using cts_test::solid;

namespace {

// The run's default size, which the Tests' pixel bounds are stated at.
constexpr uint32_t kSize = 256;

using Values = std::map<std::string, std::string>;

// One built-in Test, its Cases, and a way to judge what a device rendered for
// them through the Test's exact checks and relations, each Case seen through
// the Tests' camera.
struct Fixture
{
  Catalog catalog;
  const TestDef *test{nullptr};
  std::vector<Case> cases;

  explicit Fixture(const std::string &id)
  {
    registerBuiltinTests(catalog);
    for (const TestDef *t : catalog.filter(Filter{id}))
      if (t->id() == id)
        test = t;
    REQUIRE(test != nullptr);
    cases = expand(*test);
  }

  static Values valuesOf(const Case &c)
  {
    Values v;
    for (const auto &cv : c.values)
      v[cv.axisName] = anyToString(cv.value);
    return v;
  }

  size_t index(const Values &values) const
  {
    for (size_t i = 0; i < cases.size(); ++i) {
      bool all = true;
      for (const auto &cv : cases[i].values) {
        const auto it = values.find(cv.axisName);
        if (it != values.end() && anyToString(cv.value) != it->second)
          all = false;
      }
      if (all)
        return i;
    }
    FAIL("no such Case");
    return 0;
  }

  using Render = std::function<Image(const Values &)>;

  SelfCheckResults judge(const Render &render) const
  {
    std::vector<std::optional<CaseChannels>> inputs;
    for (const auto &c : cases) {
      CaseChannels in;
      ChannelValues cv;
      cv.channel = Channel::Color;
      cv.image = render(valuesOf(c));
      in.size = {cv.image.width, cv.image.height};
      in.channels.push_back(std::move(cv));
      in.camera = optics::camera();
      in.camera->aspect = float(in.size.width) / float(in.size.height);
      inputs.push_back(std::move(in));
    }
    return evaluateSelfChecks(*test, cases, inputs);
  }

  // Whether every assertion about the Case holding `values` passed (and there
  // is at least one).
  bool passes(const SelfCheckResults &r, const Values &values) const
  {
    const size_t i = index(values);
    bool any = false;
    for (const auto &o : r.exact[i]) {
      INFO(o.assertion.detail);
      if (!o.assertion.passed)
        return false;
      any = true;
    }
    for (const auto &o : r.relations)
      if (o.caseIndex == i && o.assertion.evaluated) {
        INFO(o.assertion.detail);
        if (!o.assertion.passed)
          return false;
        any = true;
      }
    return any;
  }

  // Whether every Case passes; the first failure's Case and assertion are
  // reported.
  bool allPass(const SelfCheckResults &r) const
  {
    for (size_t i = 0; i < cases.size(); ++i) {
      bool any = false;
      for (const auto &o : r.exact[i]) {
        any = true;
        if (!o.assertion.passed) {
          UNSCOPED_INFO(cases[i].id() << ": " << o.assertion.detail);
          return false;
        }
      }
      for (const auto &o : r.relations) {
        if (o.assertion.evaluated
            && std::find(o.partners.begin(), o.partners.end(), i)
                != o.partners.end())
          any = true;
        if (o.caseIndex == i && o.assertion.evaluated) {
          any = true;
          if (!o.assertion.passed) {
            UNSCOPED_INFO(cases[i].id() << ": " << o.assertion.detail);
            return false;
          }
        }
      }
      if (!any) {
        UNSCOPED_INFO(cases[i].id() << ": nothing evaluated");
        return false;
      }
    }
    return true;
  }
};

// The length of [a0, a1] inside [b0, b1].
float overlap(float a0, float a1, float b0, float b1)
{
  return std::max(0.f, std::min(a1, b1) - std::max(a0, b0));
}

// The share of pixel (px, py) a rectangle [x0, x1] x [y0, y1] covers once
// blurred by a box `blur` pixels in half extent (at least the pixel itself).
float coverage(uint32_t px,
    uint32_t py,
    float x0,
    float y0,
    float x1,
    float y1,
    float blur)
{
  const float r = std::max(blur, 0.5f);
  const float cx = px + 0.5f, cy = py + 0.5f;
  return overlap(cx - r, cx + r, x0, x1) / (2.f * r)
      * overlap(cy - r, cy + r, y0, y1) / (2.f * r);
}

// Depth of field ///////////////////////////////////////////////////////////

// How a synthetic device renders the depth-of-field Test.
struct ThinLens
{
  // The focus distance it uses when the Case leaves it unset.
  float defaultFocus{optics::kDefaultFocusDistance};
  bool ignoresAperture{false};
  std::optional<float> fixedFocus; // focuses here whatever the Case says
  float shift{0.f}; // pixels a blurred plate moves right
  bool gamma{false}; // writes sRGB-like (gamma 2.2) levels
};

// The radius in pixels a thin lens of `aperture` focused at `focus` blurs a
// point `depth` away by, in a frame `height` pixels tall at the default fovy.
float blurRadius(float aperture, float focus, float depth, uint32_t height)
{
  const float f = 0.5f * height / std::tan(0.5f * optics::camera().fovy);
  return f * aperture * std::fabs(1.f / focus - 1.f / depth);
}

Image focusFrame(const Values &v, const ThinLens &lens)
{
  const std::string name = v.at("lens");
  float aperture = 0.f, focus = lens.defaultFocus;
  if (name == "focus_near" || name == "focus_far") {
    aperture = optics::kApertureRadius;
    focus = name == "focus_near" ? optics::kNearDepth : optics::kFarDepth;
  } else if (name == "focus_one" || name == "focus_default") {
    aperture = optics::kDefaultFocusApertureRadius;
    if (name == "focus_one")
      focus = optics::kDefaultFocusDistance;
  }
  if (lens.ignoresAperture)
    aperture = 0.f;
  if (lens.fixedFocus)
    focus = *lens.fixedFocus;

  CameraProjection cam = optics::camera();
  const FrameSize size{kSize, kSize};
  Image im = solid(kSize, kSize, 0, 0, 0, 255);
  for (const auto &p : optics::kFocusPlates) {
    const auto lo =
        projectToPixel(cam, size, p.centre - float3(p.half, p.half, 0.f));
    const auto hi =
        projectToPixel(cam, size, p.centre + float3(p.half, p.half, 0.f));
    REQUIRE(lo);
    REQUIRE(hi);
    const float r =
        aperture > 0.f ? blurRadius(aperture, focus, -p.centre.z, kSize) : 0.f;
    const float dx = r > 0.f ? lens.shift : 0.f;
    for (uint32_t y = 0; y < kSize; ++y)
      for (uint32_t x = 0; x < kSize; ++x) {
        const float c = coverage(x, y, lo->x + dx, lo->y, hi->x + dx, hi->y, r);
        if (c <= 0.f)
          continue;
        float level = 0.6f * c;
        if (lens.gamma)
          level = std::pow(level, 1.f / 2.2f);
        const uint8_t l = uint8_t(std::lround(255.f * level));
        cts_test::setPixel(im, x, y, {l, l, l, 255});
      }
  }
  return im;
}

// Stereo /////////////////////////////////////////////////////////////////////

// How the eyes converge.
enum class Convergence
{
  Parallel,
  OffAxis, // the frustum shifted so the convergence plane has no disparity
  ToeIn, // the eye turned to look at the convergence point
};

// How an eye's image fills its half of a sideBySide or topBottom frame.
enum class HalfFit
{
  Squeeze, // the full frame's image, squeezed to the half
  Aspect, // rendered at the half's size and aspect
};

// How a synthetic device renders the stereo Tests.
struct StereoDevice
{
  Convergence convergence{Convergence::Parallel};
  float convergenceDistance{1.f};
  HalfFit fit{HalfFit::Aspect};
  bool rightEyeLeftHalf{false}; // sideBySide's other order
  // Mistakes.
  bool mono{false}; // ignores stereoMode
  bool swapEyes{false};
  bool vertical{false}; // the eyes apart along up, not right
  bool sameEyeBothHalves{false};
  bool rightEyeTop{false};
  bool topBottomAsSideBySide{false};
  std::string unsetMode{"none"};
  float defaultIpd{optics::kDefaultInterpupillaryDistance};
  bool zeroIpdIgnored{false}; // takes 0 as unset
};

const Rgba kNearColor{230, 38, 25, 255};
const Rgba kFarColor{25, 64, 230, 255};

// One eye's image, `eye` the signed offset along the camera's right vector
// (negative: the left eye), in a frame of `size`.
Image eyeImage(FrameSize size, float eye, const StereoDevice &dev)
{
  CameraProjection cam = optics::camera();
  cam.aspect = float(size.width) / float(size.height);
  float shift = 0.f; // pixels, the off-axis frustum's
  const float f = 0.5f * size.height / std::tan(0.5f * cam.fovy);
  if (dev.vertical) {
    cam.position.y += eye;
  } else {
    cam.position.x += eye;
    if (dev.convergence == Convergence::OffAxis)
      shift = -f * eye / dev.convergenceDistance;
    else if (dev.convergence == Convergence::ToeIn)
      cam.direction = anari::math::normalize(
          float3(0.f, 0.f, -dev.convergenceDistance) - cam.position);
  }
  Image im = solid(size.width, size.height, 0, 0, 0, 255);
  for (const auto *p : {&optics::kStereoFar, &optics::kStereoNear}) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (float sx : {-1.f, 1.f})
      for (float sy : {-1.f, 1.f}) {
        const auto q = projectToPixel(
            cam, size, p->centre + float3(sx * p->half, sy * p->half, 0.f));
        REQUIRE(q);
        x0 = std::min(x0, q->x + shift);
        x1 = std::max(x1, q->x + shift);
        y0 = std::min(y0, q->y);
        y1 = std::max(y1, q->y);
      }
    const Rgba c = p == &optics::kStereoNear ? kNearColor : kFarColor;
    for (uint32_t y = 0; y < size.height; ++y)
      for (uint32_t x = 0; x < size.width; ++x) {
        const float cov = coverage(x, y, x0, y0, x1, y1, 0.f);
        if (cov <= 0.f)
          continue;
        // Blended over what is there (antialiasing).
        uint8_t *px = &im.rgba[(size_t(y) * size.width + x) * 4];
        px[0] = uint8_t(std::lround(cov * c.r + (1.f - cov) * px[0]));
        px[1] = uint8_t(std::lround(cov * c.g + (1.f - cov) * px[1]));
        px[2] = uint8_t(std::lround(cov * c.b + (1.f - cov) * px[2]));
      }
  }
  return im;
}

// Every other column (or row) of `im`: its image squeezed to half the width
// (or height).
Image squeeze(const Image &im, bool columns)
{
  const uint32_t w = columns ? im.width / 2 : im.width;
  const uint32_t h = columns ? im.height : im.height / 2;
  Image out = solid(w, h, 0, 0, 0, 255);
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      const uint32_t sx = columns ? 2 * x : x, sy = columns ? y : 2 * y;
      std::copy_n(&im.rgba[(size_t(sy) * im.width + sx) * 4],
          4,
          &out.rgba[(size_t(y) * w + x) * 4]);
    }
  return out;
}

Image stereoFrame(const Values &v, const StereoDevice &dev)
{
  std::string mode = v.at("stereoMode");
  if (mode == "unset")
    mode = dev.unsetMode;
  if (dev.mono)
    mode = "none";
  float ipd = dev.defaultIpd;
  const auto it = v.find("interpupillaryDistance");
  if (it != v.end() && it->second != "none") {
    ipd = std::stof(it->second);
    if (ipd == 0.f && dev.zeroIpdIgnored)
      ipd = dev.defaultIpd;
  }
  float leftEye = -0.5f * ipd, rightEye = 0.5f * ipd;
  if (dev.swapEyes)
    std::swap(leftEye, rightEye);
  const FrameSize full{kSize, kSize};
  if (mode == "none")
    return eyeImage(full, 0.f, dev);
  if (mode == "left")
    return eyeImage(full, leftEye, dev);
  if (mode == "right")
    return eyeImage(full, rightEye, dev);
  const bool columns = mode == "sideBySide" || dev.topBottomAsSideBySide;
  auto half = [&](float eye) {
    if (dev.fit == HalfFit::Squeeze)
      return squeeze(eyeImage(full, eye, dev), columns);
    return eyeImage(
        columns ? FrameSize{kSize / 2, kSize} : FrameSize{kSize, kSize / 2},
        eye,
        dev);
  };
  float first = leftEye, second = rightEye;
  if (dev.sameEyeBothHalves)
    second = first;
  if (columns) {
    if (dev.rightEyeLeftHalf)
      std::swap(first, second);
    return hconcat(half(first), half(second));
  }
  if (dev.rightEyeTop)
    std::swap(first, second);
  return vconcat(half(first), half(second));
}

} // namespace

TEST_CASE("depth of field: a thin lens passes, under any output transfer",
    "[cts][optics]")
{
  Fixture f("camera/depth_of_field");
  CHECK(f.allPass(
      f.judge([](const Values &v) { return focusFrame(v, ThinLens{}); })));
  ThinLens gamma;
  gamma.gamma = true;
  CHECK(f.allPass(
      f.judge([&](const Values &v) { return focusFrame(v, gamma); })));
}

TEST_CASE(
    "depth of field: a lens that doesn't blur, or blurs the wrong plate, "
    "fails",
    "[cts][optics]")
{
  Fixture f("camera/depth_of_field");

  ThinLens pinhole;
  pinhole.ignoresAperture = true;
  const auto noBlur =
      f.judge([&](const Values &v) { return focusFrame(v, pinhole); });
  CHECK_FALSE(f.passes(noBlur, {{"lens", "focus_near"}}));
  CHECK_FALSE(f.passes(noBlur, {{"lens", "focus_far"}}));
  CHECK(f.passes(noBlur, {{"lens", "aperture_zero"}}));

  ThinLens farOnly;
  farOnly.fixedFocus = optics::kFarDepth;
  const auto stuck =
      f.judge([&](const Values &v) { return focusFrame(v, farOnly); });
  CHECK_FALSE(f.passes(stuck, {{"lens", "focus_near"}}));

  ThinLens shifted;
  shifted.shift = 3.f;
  const auto off =
      f.judge([&](const Values &v) { return focusFrame(v, shifted); });
  CHECK_FALSE(f.passes(off, {{"lens", "focus_near"}}));
}

TEST_CASE("depth of field: the unset focusDistance must be 1", "[cts][optics]")
{
  Fixture f("camera/depth_of_field");
  ThinLens farDefault;
  farDefault.defaultFocus = optics::kFarDepth;
  const auto r =
      f.judge([&](const Values &v) { return focusFrame(v, farDefault); });
  CHECK_FALSE(f.passes(r, {{"lens", "focus_default"}}));
  CHECK(f.passes(r, {{"lens", "focus_one"}}));
}

TEST_CASE("depth of field: an apertureRadius of 0 must be the pinhole",
    "[cts][optics]")
{
  Fixture f("camera/depth_of_field");
  // A device that blurs with the focus distance whatever the aperture.
  const auto r = f.judge([](const Values &v) {
    Values blurred = v;
    if (v.at("lens") == "aperture_zero")
      blurred["lens"] = "focus_far";
    return focusFrame(blurred, ThinLens{});
  });
  CHECK_FALSE(f.passes(r, {{"lens", "aperture_zero"}}));
}

TEST_CASE("stereo: every convergence and half fit passes", "[cts][optics]")
{
  for (const auto *id : {"camera/stereo",
           "camera/stereo_zero_ipd",
           "camera/stereo_default_ipd"}) {
    Fixture f(id);
    for (const auto convergence :
        {Convergence::Parallel, Convergence::OffAxis, Convergence::ToeIn})
      for (const float distance : {1.f, optics::kNearDepth, 4.f})
        for (const auto fit : {HalfFit::Squeeze, HalfFit::Aspect})
          for (const bool rightLeft : {false, true}) {
            StereoDevice dev;
            dev.convergence = convergence;
            dev.convergenceDistance = distance;
            dev.fit = fit;
            dev.rightEyeLeftHalf = rightLeft;
            INFO(id << " convergence " << int(convergence) << " at " << distance
                    << ", fit " << int(fit) << ", right eye left "
                    << rightLeft);
            CHECK(f.allPass(
                f.judge([&](const Values &v) { return stereoFrame(v, dev); })));
          }
  }
}

TEST_CASE("stereo: wrong eyes fail", "[cts][optics]")
{
  Fixture f("camera/stereo");
  auto judged = [&](const StereoDevice &dev) {
    return f.judge([&](const Values &v) { return stereoFrame(v, dev); });
  };
  const Values left{{"stereoMode", "left"}};
  const Values right{{"stereoMode", "right"}};
  const Values sideBySide{{"stereoMode", "sideBySide"}};
  const Values topBottom{{"stereoMode", "topBottom"}};

  StereoDevice mono;
  mono.mono = true;
  const auto m = judged(mono);
  CHECK_FALSE(f.passes(m, left));
  CHECK_FALSE(f.passes(m, right));
  CHECK_FALSE(f.passes(m, sideBySide));
  CHECK_FALSE(f.passes(m, topBottom));

  StereoDevice swapped;
  swapped.swapEyes = true;
  const auto s = judged(swapped);
  CHECK_FALSE(f.passes(s, left));
  CHECK_FALSE(f.passes(s, right));
  CHECK_FALSE(f.passes(s, topBottom));

  StereoDevice vertical;
  vertical.vertical = true;
  CHECK_FALSE(f.passes(judged(vertical), left));

  StereoDevice sameEye;
  sameEye.sameEyeBothHalves = true;
  CHECK_FALSE(f.passes(judged(sameEye), sideBySide));

  StereoDevice rightTop;
  rightTop.rightEyeTop = true;
  CHECK_FALSE(f.passes(judged(rightTop), topBottom));

  StereoDevice asSideBySide;
  asSideBySide.topBottomAsSideBySide = true;
  CHECK_FALSE(f.passes(judged(asSideBySide), topBottom));

  StereoDevice unsetSideBySide;
  unsetSideBySide.unsetMode = "sideBySide";
  CHECK_FALSE(f.passes(judged(unsetSideBySide), {{"stereoMode", "unset"}}));
}

TEST_CASE("stereo: an interpupillaryDistance of 0, or unset, must be honoured",
    "[cts][optics]")
{
  {
    Fixture f("camera/stereo_zero_ipd");
    StereoDevice dev;
    dev.zeroIpdIgnored = true;
    const auto r =
        f.judge([&](const Values &v) { return stereoFrame(v, dev); });
    CHECK_FALSE(f.passes(r, {{"stereoMode", "left"}}));
    CHECK_FALSE(f.passes(r, {{"stereoMode", "right"}}));
  }
  {
    Fixture f("camera/stereo_default_ipd");
    StereoDevice dev;
    dev.defaultIpd = 0.f;
    const auto r =
        f.judge([&](const Values &v) { return stereoFrame(v, dev); });
    CHECK_FALSE(f.passes(r, {{"interpupillaryDistance", "none"}}));
  }
}
