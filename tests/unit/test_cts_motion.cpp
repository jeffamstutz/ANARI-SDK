// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The motion and shutter Tests (camera shutter and rolling shutter, camera
// and instance motion, triangle and quad motion deformation), judged through
// the evaluation seam with images generated in code (ADR-0010): what a device
// implementing the behaviour renders passes, what one getting it wrong
// renders fails. No SDK device implements these extensions, so this is their
// validation without a device.

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
#include "cts/tests/MotionScene.h"
// std
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace anari::cts;
using anari::math::float2;
using anari::math::float3;
using cts_test::Rgba;
using cts_test::setPixel;
using cts_test::solid;

namespace {

constexpr uint32_t kSize = 64;
const Rgba kSubject{230, 150, 80, 255};

using Values = std::map<std::string, std::string>;

// A shutter value's text, as in Case ids.
std::string shutter(float lo, float hi)
{
  const float v[2] = {lo, hi};
  return anyToString(Any(ANARI_FLOAT32_BOX1, v));
}

// One built-in Test, its Cases, and a way to judge what a device rendered for
// them through the Test's exact checks and relations, each Case seen through
// the rest camera.
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
      in.camera = motion::restCamera();
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

  // Whether every exact check of the Case holding `values` passed (and there
  // is at least one).
  bool exactPasses(const SelfCheckResults &r, const Values &values) const
  {
    const size_t i = index(values);
    for (const auto &o : r.exact[i]) {
      INFO(o.assertion.detail);
      if (!o.assertion.passed)
        return false;
    }
    return !r.exact[i].empty();
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
        // A partner of another Case's relation takes part in it too.
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

// A frame showing a rectangle per key index (0, 1, 2), or nothing (-1).
Image keyFrame(int key)
{
  Image im = solid(kSize, kSize, 0, 0, 0, 255);
  if (key < 0)
    return im;
  const uint32_t x0 = 6 + 18 * uint32_t(key);
  for (uint32_t y = 20; y < 44; ++y)
    for (uint32_t x = x0; x < x0 + 14; ++x)
      setPixel(im, x, y, kSubject);
  return im;
}

int keyAt(const std::string &shutterText)
{
  if (shutterText == shutter(0.f, 0.f))
    return 0;
  if (shutterText == shutter(0.5f, 0.5f))
    return 1;
  return 2;
}

// Where the open-shutter subject's rectangle lands at time t, through the
// rest camera at kSize.
struct PixelBox
{
  float x0, y0, x1, y1;
  bool contains(float x, float y) const
  {
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
  }
};

PixelBox projectedBox(const float3 &centre, float2 half)
{
  const auto cam = motion::restCamera();
  const auto lo = *projectToPixel(
      cam, {kSize, kSize}, centre - float3(half.x, half.y, 0.f));
  const auto hi = *projectToPixel(
      cam, {kSize, kSize}, centre + float3(half.x, half.y, 0.f));
  return {std::min(lo.x, hi.x),
      std::min(lo.y, hi.y),
      std::max(lo.x, hi.x),
      std::max(lo.y, hi.y)};
}

// A box shutter's image of a subject at `at(t)`: each pixel blends the
// subject by the share of [t0, t1] it covers the pixel (sampled).
template <typename Box>
Rgba blended(float t0, float t1, float px, float py, Box box)
{
  const int samples = t1 > t0 ? 65 : 1;
  int covered = 0;
  for (int i = 0; i < samples; ++i) {
    const float t = samples == 1 ? t0 : t0 + (t1 - t0) * i / (samples - 1);
    covered += box(t).contains(px, py) ? 1 : 0;
  }
  const float c = float(covered) / samples;
  return Rgba{uint8_t(kSubject.r * c),
      uint8_t(kSubject.g * c),
      uint8_t(kSubject.b * c),
      255};
}

// The open-shutter Tests' subject, seen with shutter [t0, t1].
Image openFrame(float t0, float t1)
{
  Image im = solid(kSize, kSize, 0, 0, 0, 255);
  auto box = [](float t) {
    return projectedBox(motion::openCentreAt(t),
        float2(motion::kOpenHalfWidth, motion::kOpenHalfHeight));
  };
  for (uint32_t y = 0; y < kSize; ++y)
    for (uint32_t x = 0; x < kSize; ++x)
      setPixel(im, x, y, blended(t0, t1, x + 0.5f, y + 0.5f, box));
  return im;
}

float2 shutterFrom(const std::string &text)
{
  if (text == "none")
    return float2(0.5f, 0.5f);
  for (float lo : {0.f, 0.5f, 1.f})
    for (float hi : {0.f, 0.5f, 1.f})
      if (shutter(lo, hi) == text)
        return float2(lo, hi);
  FAIL("unknown shutter " << text);
  return float2(0.f, 0.f);
}

// Two readings of the rolling shutter's line schedule, which the spec doesn't
// give (ADR-0010 48); the Tests must pass both. `s` is a line's place in scan
// order (0 for the first line, 1 for the last).
enum class Reading
{
  // The working-group proposal: lines open one after another over shutter
  // [0, 1], the first at its start and the last closing at its end, each
  // open for the duration; `down` starts at the top row, `up` at the bottom,
  // `right` at the left column, `left` at the right.
  Proposal,
  // anari-visionaray's (dco/Camera.h): a line opens `s` times the duration
  // after the shutter's start and stays open for the whole shutter (the bar
  // holds its last key after time 1); `down` starts at the bottom row.
  Visionaray,
};

// A line's place in scan order from its pixel centre (row or column, Image
// convention: row 0 at the bottom) in a frame `extent` lines long.
float scanOrder(
    Reading reading, const std::string &direction, float line, float extent)
{
  const float f = line / extent;
  const bool fromTopOrRight = direction == "left"
      || (direction == "down" && reading == Reading::Proposal)
      || (direction == "up" && reading == Reading::Visionaray);
  return fromTopOrRight ? 1.f - f : f;
}

// The rolling-shutter Tests' bar, scanned as `direction` with `duration`
// under `reading`; `none` is a global shutter.
Image rollingFrame(
    bool rows, Reading reading, const std::string &direction, float duration)
{
  Image im = solid(kSize, kSize, 0, 0, 0, 255);
  auto box = [rows](float t) {
    t = std::clamp(t, 0.f, 1.f);
    return projectedBox(motion::barFrom(rows)
            + t * (motion::barTo(rows) - motion::barFrom(rows)),
        motion::barHalfExtent(rows));
  };
  for (uint32_t y = 0; y < kSize; ++y)
    for (uint32_t x = 0; x < kSize; ++x) {
      float t0 = 0.f, t1 = 1.f;
      if (direction != "none") {
        const float line = rows ? y + 0.5f : x + 0.5f;
        const float s = scanOrder(reading, direction, line, float(kSize));
        t0 = reading == Reading::Proposal ? s * (1.f - duration) : s * duration;
        t1 = reading == Reading::Proposal ? t0 + duration : t0 + 1.f;
      }
      setPixel(im, x, y, blended(t0, t1, x + 0.5f, y + 0.5f, box));
    }
  return im;
}

const std::vector<std::string> kInstantTests = {"instance/motion_transform",
    "instance/motion_scale_rotation_translation",
    "geometry/triangle_motion_deformation",
    "geometry/quad_motion_deformation",
    "camera/motion_transformation"};

const std::vector<std::string> kOpenShutterTests = {"camera/shutter",
    "instance/motion_scale_rotation_translation_open_shutter",
    "geometry/triangle_motion_deformation_open_shutter",
    "geometry/quad_motion_deformation_open_shutter",
    "camera/motion_transformation_open_shutter"};

} // namespace

TEST_CASE("each motion and shutter extension is a Test's primary extension",
    "[cts][motion]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  std::set<std::string> covered;
  for (const TestDef *t : catalog.filter(Filter{""}))
    for (const auto &f : t->coveredFeatures)
      covered.insert(f);
  for (const char *ext : {"ANARI_KHR_CAMERA_SHUTTER",
           "ANARI_KHR_CAMERA_ROLLING_SHUTTER",
           "ANARI_KHR_CAMERA_MOTION_TRANSFORMATION",
           "ANARI_KHR_INSTANCE_MOTION_TRANSFORM",
           "ANARI_KHR_INSTANCE_MOTION_SCALE_ROTATION_TRANSLATION",
           "ANARI_KHR_GEOMETRY_TRIANGLE_MOTION_DEFORMATION",
           "ANARI_KHR_GEOMETRY_QUAD_MOTION_DEFORMATION"}) {
    INFO(ext);
    CHECK(covered.count(ext) == 1);
  }
}

TEST_CASE("an instant shutter on a key renders the static scene at that key",
    "[cts][motion]")
{
  for (const auto &id : kInstantTests) {
    INFO(id);
    Fixture f(id);
    CHECK_FALSE(f.test->usesGroundTruth());
    std::set<std::string> forms;
    for (const auto &c : f.cases)
      forms.insert(Fixture::valuesOf(c).at("subject"));
    REQUIRE(forms.count("static") == 1);
    REQUIRE(forms.size() >= 3);

    // A device implementing it: every form shows the key at its time.
    const auto good = f.judge(
        [](const Values &v) { return keyFrame(keyAt(v.at("shutter"))); });
    CHECK(f.allPass(good));

    // A device that gets one form wrong (ignores the motion and shows the
    // first key, or `time`, or the precedence over `transform`): that form
    // fails where its key isn't the first.
    for (const auto &form : forms) {
      if (form == "static")
        continue;
      INFO(form);
      const auto wrong = f.judge([&](const Values &v) {
        if (v.at("subject") == form)
          return keyFrame(form == "motion_time" ? -1 : 0);
        return keyFrame(keyAt(v.at("shutter")));
      });
      CHECK_FALSE(
          f.passes(wrong, {{"subject", form}, {"shutter", shutter(1.f, 1.f)}}));
      CHECK(f.passes(
          wrong, {{"subject", "static"}, {"shutter", shutter(1.f, 1.f)}}));
    }

    // A device that ignores the shutter time altogether: the static scene
    // differs between times but the motion form doesn't.
    const auto frozen = f.judge([](const Values &v) {
      return keyFrame(v.at("subject") == "static" ? keyAt(v.at("shutter")) : 1);
    });
    CHECK_FALSE(f.passes(
        frozen, {{"subject", "motion"}, {"shutter", shutter(0.f, 0.f)}}));
  }
}

TEST_CASE("identical motion keys render as no motion", "[cts][motion]")
{
  const float2 half(motion::kOpenHalfWidth, motion::kOpenHalfHeight);
  auto at = [&](const float3 &centre) {
    return [=](const Values &) {
      Image im = solid(kSize, kSize, 0, 0, 0, 255);
      const PixelBox box = projectedBox(centre, half);
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
          if (box.contains(x + 0.5f, y + 0.5f))
            setPixel(im, x, y, kSubject);
      return im;
    };
  };
  for (const auto &id : kInstantTests) {
    INFO(id);
    Fixture f(id + "_identical_keys");
    CHECK(f.allPass(f.judge(at(motion::kIdenticalAt))));
    // Both elsewhere: equivalent, but not where the keys put the subject.
    const auto bothWrong = f.judge(at(float3(-0.3f, 0.f, 0.f)));
    CHECK_FALSE(f.passes(bothWrong, {{"subject", "static"}}));
    CHECK_FALSE(f.passes(bothWrong, {{"subject", "identical_keys"}}));
    // The moving form shows something else (nothing, here).
    const auto wrong = f.judge([&](const Values &v) {
      if (v.at("subject") == "static")
        return at(motion::kIdenticalAt)(v);
      return solid(kSize, kSize, 0, 0, 0, 255);
    });
    CHECK(f.passes(wrong, {{"subject", "static"}}));
    CHECK_FALSE(f.passes(wrong, {{"subject", "identical_keys"}}));
  }
}

TEST_CASE(
    "an open shutter blurs the subject over where it went", "[cts][motion]")
{
  for (const auto &id : kOpenShutterTests) {
    INFO(id);
    Fixture f(id);
    auto render = [](const Values &v) {
      const float2 s = shutterFrom(v.at("shutter"));
      return openFrame(s.x, s.y);
    };
    CHECK(f.allPass(f.judge(render)));

    // One sample per pixel: each pixel shows the subject or the background,
    // with the chance of its cover (a fixed hash stands in for the noise).
    const auto oneSample = f.judge([&](const Values &v) {
      Image im = render(v);
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          const size_t i = (size_t(y) * kSize + x) * 4;
          const float cover = im.rgba[i] / float(kSubject.r);
          const float u =
              float((x * 73856093u ^ y * 19349663u) % 1000) / 1000.f;
          setPixel(im, x, y, u < cover ? kSubject : Rgba{0, 0, 0, 255});
        }
      return im;
    });
    CHECK(f.passes(oneSample, {{"shutter", shutter(0.f, 1.f)}}));

    // Shows the open shutter's start only: the end's part is empty.
    const auto instantOnly = f.judge([](const Values &v) {
      const float2 s = shutterFrom(v.at("shutter"));
      return openFrame(s.x, s.x);
    });
    CHECK_FALSE(f.passes(instantOnly, {{"shutter", shutter(0.f, 1.f)}}));
    CHECK(f.passes(instantOnly, {{"shutter", shutter(0.f, 0.f)}}));

    // Takes the keys in reverse order.
    const auto reversed = f.judge([](const Values &v) {
      const float2 s = shutterFrom(v.at("shutter"));
      return openFrame(1.f - s.y, 1.f - s.x);
    });
    CHECK_FALSE(f.passes(reversed, {{"shutter", shutter(0.f, 0.f)}}));
    CHECK_FALSE(f.passes(reversed, {{"shutter", shutter(1.f, 1.f)}}));

    // Ignores the motion: the subject stays at its first place.
    const auto still =
        f.judge([](const Values &) { return openFrame(0.f, 0.f); });
    CHECK_FALSE(f.passes(still, {{"shutter", shutter(1.f, 1.f)}}));
    CHECK_FALSE(f.passes(still, {{"shutter", shutter(0.f, 1.f)}}));

    // Draws the blur past where the subject went.
    const auto spread = f.judge([&](const Values &v) {
      Image im = render(v);
      for (uint32_t y = 28; y < 36; ++y)
        setPixel(im, 2, y, kSubject);
      return im;
    });
    CHECK_FALSE(f.passes(spread, {{"shutter", shutter(0.f, 1.f)}}));
  }
}

TEST_CASE("camera/shutter's default is [0.5, 0.5]", "[cts][motion]")
{
  Fixture f("camera/shutter");
  const auto atStart = f.judge([](const Values &v) {
    const float2 s = v.at("shutter") == "none" ? float2(0.f, 0.f)
                                               : shutterFrom(v.at("shutter"));
    return openFrame(s.x, s.y);
  });
  CHECK_FALSE(f.passes(atStart, {{"shutter", "none"}}));
}

TEST_CASE(
    "a rolling shutter shears the bar, the reversed scan the other "
    "way, under any line schedule",
    "[cts][motion]")
{
  for (const bool rows : {true, false}) {
    const std::string id =
        rows ? "camera/rolling_shutter_rows" : "camera/rolling_shutter_columns";
    INFO(id);
    Fixture f(id);
    CHECK_FALSE(f.test->usesGroundTruth());
    const std::string first = rows ? "down" : "right";
    const std::string reversed = rows ? "up" : "left";
    // Renders each Case's direction as `swap` says (as itself otherwise).
    auto render = [&](Reading reading,
                      const std::map<std::string, std::string> &swap) {
      return [&, reading, swap](const Values &v) {
        std::string direction = v.at("rollingShutterDirection");
        const auto it = swap.find(direction);
        if (it != swap.end())
          direction = it->second;
        return rollingFrame(rows,
            reading,
            direction,
            std::stof(v.at("rollingShutterDuration")));
      };
    };
    const Values atFirst{{"rollingShutterDirection", first}};
    const Values atReversed{{"rollingShutterDirection", reversed}};
    const Values atNone{{"rollingShutterDirection", "none"}};

    for (const Reading reading : {Reading::Proposal, Reading::Visionaray}) {
      INFO((reading == Reading::Proposal ? "proposal" : "visionaray"));
      CHECK(f.allPass(f.judge(render(reading, {}))));

      // Scans the reversed direction the same way as the first: the same
      // shear both ways.
      const auto sameSense = f.judge(render(reading, {{reversed, first}}));
      CHECK(f.passes(sameSense, atFirst));
      CHECK(f.exactPasses(sameSense, atReversed));
      CHECK_FALSE(f.passes(sameSense, atReversed));

      // Ignores the rolling shutter: a global shutter for every direction.
      const auto global =
          f.judge(render(reading, {{first, "none"}, {reversed, "none"}}));
      CHECK_FALSE(f.exactPasses(global, atFirst));
      CHECK_FALSE(f.exactPasses(global, atReversed));

      // Ignores it only for the reversed direction.
      const auto oneWay = f.judge(render(reading, {{reversed, "none"}}));
      CHECK_FALSE(f.exactPasses(oneWay, atReversed));
      CHECK_FALSE(f.passes(oneWay, atReversed));

      // Shears where it shouldn't: `none` scans like the first direction.
      const auto noneSheared = f.judge(render(reading, {{"none", first}}));
      CHECK_FALSE(f.exactPasses(noneSheared, atNone));
    }
  }
}
