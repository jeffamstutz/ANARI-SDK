// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Exact checks (ADR-0010): the helpers, project(), the Sidecar and report
// records, and the evaluation seam, with channel data generated in code: no
// device, no image files.

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/BuiltinTests.h"
#include "cts/CameraProjection.h"
#include "cts/Catalog.h"
#include "cts/ExactCheck.h"
#include "cts/Expansion.h"
#include "cts/GeometryLayout.h"
#include "cts/Report.h"
#include "cts/Sidecar.h"
#include "cts/TestBuilder.h"
#include "cts/ViewBuilder.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace anari::cts;
using anari::math::float2;
using anari::math::float3;
using cts_test::depthChannel;
using cts_test::idChannel;

namespace {

constexpr uint32_t kW = 16;
constexpr uint32_t kH = 8;

// The single finding a context recorded.
const ExactFinding &onlyFinding(const ExactCheck &x)
{
  REQUIRE(x.findings().size() == 1);
  return x.findings().front();
}

// The shade of pixel (x, y) of a failure mask.
uint8_t shade(const Image &mask, uint32_t x, uint32_t y)
{
  return mask.rgba[(size_t(y) * mask.width + x) * 4];
}

float cross2(float2 a, float2 b)
{
  return a.x * b.y - a.y * b.x;
}

bool insideTriangle(float2 a, float2 b, float2 c, float2 p)
{
  const float d0 = cross2(b - a, p - a);
  const float d1 = cross2(c - b, p - b);
  const float d2 = cross2(a - c, p - c);
  return (d0 >= 0.f && d1 >= 0.f && d2 >= 0.f)
      || (d0 <= 0.f && d1 <= 0.f && d2 <= 0.f);
}

} // namespace

// Helpers ////////////////////////////////////////////////////////////////////

TEST_CASE("allPixels counts offenders and records the first", "[cts][exact]")
{
  BuildContext values;
  // IDs 0..15 along x; pixel (12, 3) and every x >= 12 hold >= 12.
  const std::vector<ChannelValues> channels = {
      idChannel(kW, kH, [](uint32_t x, uint32_t) { return x; })};
  ExactCheck x(values, {kW, kH}, channels, std::nullopt);
  const auto ids = x.ids(Channel::PrimitiveId);

  SECTION("passing")
  {
    CHECK(x.allPixels(
        "ids below 16", ids, "< 16", [](uint32_t, uint32_t, uint32_t v) {
          return v < 16;
        }));
    const auto &f = onlyFinding(x);
    CHECK(f.passed);
    CHECK(f.channel == "primitiveId");
    CHECK(f.checked == kW * kH);
    CHECK(f.offending == 0);
    CHECK_FALSE(f.first);
    CHECK_FALSE(f.mask.valid()); // only on failure
  }

  SECTION("failing")
  {
    CHECK_FALSE(x.allPixels(
        "ids below 12", ids, "< 12", [](uint32_t, uint32_t, uint32_t v) {
          return v < 12;
        }));
    const auto &f = onlyFinding(x);
    CHECK_FALSE(f.passed);
    CHECK(f.checked == kW * kH);
    CHECK(f.offending == 4 * kH);
    REQUIRE(f.first);
    // Scanned from row 0 (the bottom), left to right.
    CHECK(f.first->x == 12);
    CHECK(f.first->y == 0);
    CHECK(f.first->got == "12");
    CHECK(f.first->expected == "< 12");
    CHECK(f.detail.find("32 of 128 checked primitiveId pixels offend")
        != std::string::npos);
    REQUIRE(f.mask.valid());
    CHECK(shade(f.mask, 12, 3) == 255); // offends
    CHECK(shade(f.mask, 2, 3) == 0); // held
  }

  SECTION("inside a mask only")
  {
    PixelMask where(kW, kH);
    where.set(13, 5);
    where.set(1, 1);
    CHECK_FALSE(x.allPixels(
        "ids below 12",
        ids,
        "< 12",
        [](uint32_t, uint32_t, uint32_t v) { return v < 12; },
        &where));
    const auto &f = onlyFinding(x);
    CHECK(f.checked == 2);
    CHECK(f.offending == 1);
    REQUIRE(f.first);
    CHECK(f.first->x == 13);
    CHECK(f.first->y == 5);
    CHECK(shade(f.mask, 0, 0) == 128); // not looked at
    CHECK(shade(f.mask, 1, 1) == 0);
    CHECK(shade(f.mask, 13, 5) == 255);
  }

  SECTION("a mask of the wrong size fails the finding")
  {
    PixelMask where(kW + 1, kH, true);
    CHECK_FALSE(x.allPixels(
        "ids",
        ids,
        "anything",
        [](uint32_t, uint32_t, uint32_t) { return true; },
        &where));
    CHECK(onlyFinding(x).detail.find("the mask is 17x8") != std::string::npos);
  }

  SECTION("an empty mask checks nothing, and fails")
  {
    PixelMask where(kW, kH);
    CHECK_FALSE(x.allPixels(
        "ids",
        ids,
        "anything",
        [](uint32_t, uint32_t, uint32_t) { return true; },
        &where));
    CHECK(onlyFinding(x).checked == 0);
  }
}

TEST_CASE("matches compares float depth within a tolerance", "[cts][exact]")
{
  BuildContext values;
  const float inf = std::numeric_limits<float>::infinity();
  // A plane at depth 2 on the left half, nothing (infinity) on the right.
  auto plane = [&](uint32_t x, uint32_t) { return x < kW / 2 ? 2.f : inf; };
  PixelMask left(kW, kH);
  for (uint32_t y = 0; y < kH; ++y)
    for (uint32_t x = 0; x < kW / 2; ++x)
      left.set(x, y);

  SECTION("within the tolerance")
  {
    const std::vector<ChannelValues> channels = {depthChannel(
        kW, kH, [&](uint32_t x, uint32_t y) { return plane(x, y) + 0.004f; })};
    ExactCheck x(values, {kW, kH}, channels, std::nullopt);
    CHECK(x.matches(
        "depth of the plane",
        x.depth(),
        [](uint32_t, uint32_t) { return 2.f; },
        0.01,
        &left));
    CHECK(onlyFinding(x).checked == kW * kH / 2);
    // The raw value survives the 8-bit image: 2.004 is not a gray level.
    CHECK(x.depth().at(0, 0) == Approx(2.004f));
  }

  SECTION("outside the tolerance")
  {
    const std::vector<ChannelValues> channels = {
        depthChannel(kW, kH, [&](uint32_t x, uint32_t y) {
          return x == 3 && y == 2 ? 2.5f : plane(x, y);
        })};
    ExactCheck x(values, {kW, kH}, channels, std::nullopt);
    CHECK_FALSE(x.matches(
        "depth of the plane",
        x.depth(),
        [](uint32_t, uint32_t) { return 2.f; },
        0.01,
        &left));
    const auto &f = onlyFinding(x);
    CHECK(f.offending == 1);
    REQUIRE(f.first);
    CHECK(f.first->x == 3);
    CHECK(f.first->y == 2);
    CHECK(f.first->got == "2.5");
    CHECK(f.first->expected == "2 +/- 0.01");
  }

  SECTION("infinity matches infinity; NaN matches nothing")
  {
    PixelMask right = left.inverted();
    const std::vector<ChannelValues> channels = {
        depthChannel(kW, kH, [&](uint32_t x, uint32_t y) {
          return x == kW - 1 && y == 0 ? std::nanf("") : plane(x, y);
        })};
    ExactCheck x(values, {kW, kH}, channels, std::nullopt);
    CHECK_FALSE(x.matches(
        "no hit",
        x.depth(),
        [&](uint32_t, uint32_t) { return inf; },
        0.0,
        &right));
    CHECK(onlyFinding(x).offending == 1);
  }
}

TEST_CASE(
    "backgroundOutside judges the pixels outside a subject", "[cts][exact]")
{
  BuildContext values;
  // A subject (ID 3) in [4, 8) x [2, 6); background ~0u elsewhere, except one
  // stray pixel.
  auto ids = [](uint32_t x, uint32_t y) -> uint32_t {
    if (x >= 4 && x < 8 && y >= 2 && y < 6)
      return 3;
    return x == 15 && y == 7 ? 0u : ~0u;
  };
  const std::vector<ChannelValues> channels = {idChannel(kW, kH, ids)};
  ExactCheck x(values, {kW, kH}, channels, std::nullopt);
  PixelMask subject(kW, kH);
  for (uint32_t y = 2; y < 6; ++y)
    for (uint32_t px = 4; px < 8; ++px)
      subject.set(px, y);

  CHECK_FALSE(x.backgroundOutside("background holds no ID",
      x.ids(Channel::PrimitiveId),
      subject,
      "0xffffffff",
      [](uint32_t v) { return v == ~0u; }));
  const auto &f = onlyFinding(x);
  CHECK(f.checked == kW * kH - 16);
  CHECK(f.offending == 1);
  REQUIRE(f.first);
  CHECK(f.first->x == 15);
  CHECK(f.first->y == 7);
  CHECK(f.first->got == "0");
  CHECK(shade(f.mask, 5, 3) == 128); // the subject is not looked at
}

TEST_CASE("a scalar measurement records its value and bound", "[cts][exact]")
{
  BuildContext values;
  const std::vector<ChannelValues> channels;
  ExactCheck x(values, {kW, kH}, channels, std::nullopt);

  CHECK_FALSE(x.measure("edge contrast", 0.31, Limit::AtLeast, 0.6));
  CHECK(x.measure("penumbra width", 3.0, Limit::AtMost, 4.0));
  CHECK_FALSE(x.measure("nan", std::nan(""), Limit::AtMost, 4.0));
  REQUIRE(x.findings().size() == 3);
  const auto &f = x.findings().front();
  CHECK(f.measurement);
  CHECK_FALSE(f.passed);
  CHECK(f.value == 0.31);
  CHECK(f.bound == 0.6);
  CHECK(f.detail == "edge contrast 0.31, required >= 0.6");
  CHECK(x.findings()[1].passed);
}

TEST_CASE("an exact check reads only the Test's channels", "[cts][exact]")
{
  BuildContext values;
  values.setValue("count", 3);
  const std::vector<ChannelValues> channels = {
      idChannel(kW, kH, [](uint32_t, uint32_t) { return 1u; })};
  ExactCheck x(values, {kW, kH}, channels, std::nullopt);
  CHECK(x.get<int>("count", 0) == 3);
  CHECK(x.frameSize() == FrameSize{kW, kH});
  CHECK_THROWS_AS(x.depth(), std::invalid_argument);
  CHECK_THROWS_AS(x.ids(Channel::ObjectId), std::invalid_argument);
  CHECK(x.rgba(Channel::PrimitiveId).at(0, 0)[3] == 255);
  CHECK_FALSE(x.canProject());
  CHECK_THROWS_AS(x.project(float3(0.f)), std::runtime_error);
}

// project() ///////////////////////////////////////////////////////////////////

TEST_CASE(
    "project maps world points through a perspective camera", "[cts][exact]")
{
  CameraProjection cam; // at the origin, looking down -z, y up
  cam.fovy = float(M_PI / 2.0); // tan(fovy / 2) = 1
  const FrameSize size{100, 100};

  auto at = [&](float3 p, FrameSize s = {100, 100}) {
    const auto px = projectToPixel(cam, s, p);
    REQUIRE(px);
    return *px;
  };
  CHECK(at({0.f, 0.f, -1.f}).x == Approx(50.f));
  CHECK(at({0.f, 0.f, -1.f}).y == Approx(50.f));
  CHECK(at({1.f, 0.f, -1.f}).x == Approx(100.f)); // the right edge
  CHECK(at({0.f, 0.5f, -1.f}).y == Approx(75.f)); // up is row 0's opposite
  CHECK(at({0.f, 1.f, -2.f}).y == Approx(75.f)); // farther, smaller
  CHECK_FALSE(projectToPixel(cam, size, {0.f, 0.f, 1.f})); // behind

  // aspect 2: a frame twice as wide covers twice the width.
  cam.aspect = 2.f;
  CHECK(at({1.f, 0.f, -1.f}, {200, 100}).x == Approx(150.f));

  // The frame shows the top-right quarter of the sensor.
  cam.aspect = 1.f;
  cam.regionLo = float2(0.5f, 0.5f);
  CHECK(at({0.f, 0.f, -1.f}).x == Approx(0.f));
  CHECK(at({1.f, 1.f, -1.f}).y == Approx(100.f));

  // A camera looking down +x with z up.
  CameraProjection side;
  side.direction = float3(1.f, 0.f, 0.f);
  side.up = float3(0.f, 0.f, 1.f);
  side.fovy = float(M_PI / 2.0);
  const auto p = projectToPixel(side, size, {2.f, 0.f, 1.f});
  REQUIRE(p);
  CHECK(p->x == Approx(50.f));
  CHECK(p->y == Approx(75.f));
}

TEST_CASE(
    "project maps world points through an orthographic camera", "[cts][exact]")
{
  CameraProjection cam;
  cam.type = CameraProjection::Type::Orthographic;
  cam.height = 2.f;
  const auto p = projectToPixel(cam, {100, 100}, {0.5f, -1.f, 5.f});
  REQUIRE(p); // no near side: behind the camera still projects
  CHECK(p->x == Approx(75.f));
  CHECK(p->y == Approx(0.f));
}

// Records ///////////////////////////////////////////////////////////////////

TEST_CASE("an exact assertion round-trips through the Sidecar and the report",
    "[cts][exact]")
{
  CaseResult r;
  r.category = "frame";
  r.test = "ids";
  r.caseId = "default";
  r.verdict = Verdict::Failed;
  Assertion pixels;
  pixels.kind = kExactAssertion;
  pixels.label = "IDs";
  pixels.expectation = "background holds no ID";
  pixels.channel = "primitiveId";
  pixels.passed = false;
  pixels.score = 0.25;
  pixels.threshold = 0.0;
  pixels.detail =
      "background holds no ID: 1 of 4 checked primitiveId pixels "
      "offend; first at (1, 2): got 0, expected 0xffffffff";
  pixels.maskImage = "results/frame/ids/default.exact-0.mask.png";
  pixels.pixels = PixelCounts{1, 4, FirstOffender{1, 2, "0", "0xffffffff"}};
  Assertion measured;
  measured.kind = kExactAssertion;
  measured.label = "edges";
  measured.expectation = "edge contrast";
  measured.passed = true;
  measured.score = 0.7;
  measured.threshold = 0.6;
  measured.measurement = Measurement{0.7, 0.6, ">="};
  r.assertions = {pixels, measured};

  CaseResult back;
  REQUIRE(fromJson(toJson(r), back));
  REQUIRE(back.assertions.size() == 2);
  const auto &a = back.assertions[0];
  CHECK(a.kind == "exact");
  CHECK(a.label == "IDs");
  CHECK(a.maskImage == pixels.maskImage);
  REQUIRE(a.pixels);
  CHECK(a.pixels->offending == 1);
  CHECK(a.pixels->checked == 4);
  REQUIRE(a.pixels->first);
  CHECK(a.pixels->first->x == 1);
  CHECK(a.pixels->first->y == 2);
  CHECK(a.pixels->first->got == "0");
  CHECK(a.pixels->first->expected == "0xffffffff");
  CHECK_FALSE(a.measurement);
  const auto &m = back.assertions[1];
  REQUIRE(m.measurement);
  CHECK(m.measurement->value == 0.7);
  CHECK(m.measurement->bound == 0.6);
  CHECK(m.measurement->limit == ">=");
  CHECK_FALSE(m.pixels);

  CHECK(failureKinds(back) == std::vector<std::string>{"exact"});
  std::map<std::string, CaseResult> results{{"frame/ids/default", back}};
  std::ostringstream text;
  writeTextSummary(text, "run", results, false);
  CHECK(text.str().find("exact failed (check \"IDs\") [mask "
                        "results/frame/ids/default.exact-0.mask.png]: "
                        "background holds no ID: 1 of 4")
      != std::string::npos);
}

// Declaring and evaluating ///////////////////////////////////////////////////

TEST_CASE("expectExact rejects bad declarations", "[cts][exact]")
{
  auto fn = [](ExactCheck &) {};
  CHECK_THROWS_AS(
      makeTest("t", "a").expectExact("", fn), std::invalid_argument);
  CHECK_THROWS_AS(makeTest("t", "a").expectExact("x", fn).expectExact("x", fn),
      std::invalid_argument);
  auto behavior = [](anari::Device,
                      anari::World,
                      anari::Camera,
                      anari::Renderer,
                      uint32_t,
                      uint32_t) { return BehaviorResult{true, ""}; };
  CHECK_THROWS_AS(makeTest("t", "a").behavior(behavior).expectExact("x", fn),
      std::invalid_argument);
  CHECK_THROWS_AS(makeTest("t", "a").expectExact("x", fn).behavior(behavior),
      std::invalid_argument);
}

TEST_CASE(
    "evaluateExactChecks turns findings into exact assertions", "[cts][exact]")
{
  const std::vector<ChannelValues> channels = {
      idChannel(kW, kH, [](uint32_t x, uint32_t) { return x; })};
  const TestDef test =
      makeTest("t", "ids")
          .channels({Channel::PrimitiveId})
          .expectExact("ok",
              [](ExactCheck &x) {
                x.allPixels("ids below 16",
                    x.ids(Channel::PrimitiveId),
                    "< 16",
                    [](uint32_t, uint32_t, uint32_t v) { return v < 16; });
                x.measure("count", x.get<int>("count", 0), Limit::AtLeast, 2);
              })
          .expectExact("throws",
              [](ExactCheck &x) {
                x.measure("before", 1.0, Limit::AtLeast, 0.0);
                x.depth(); // not a channel of the Test
              })
          .expectExact("silent", [](ExactCheck &) {})
          .take();
  Case c;
  c.category = "t";
  c.testName = "ids";
  c.values = {{"count", Any(3), AxisKind::Permutation}};

  const auto out =
      evaluateExactChecks(test, c, {kW, kH}, channels, std::nullopt);
  REQUIRE(out.size() == 5);
  CHECK(out[0].assertion.kind == "exact");
  CHECK(out[0].assertion.label == "ok");
  CHECK(out[0].assertion.expectation == "ids below 16");
  CHECK(out[0].assertion.passed);
  REQUIRE(out[0].assertion.pixels);
  CHECK(out[0].assertion.pixels->checked == kW * kH);
  CHECK(out[1].assertion.passed); // the axis value reached the check
  REQUIRE(out[1].assertion.measurement);
  CHECK(out[1].assertion.measurement->value == 3.0);
  // A throwing check keeps what it recorded and fails with why.
  CHECK(out[2].assertion.label == "throws");
  CHECK(out[2].assertion.passed);
  CHECK_FALSE(out[3].assertion.passed);
  CHECK(out[3].assertion.detail.find(
            "the check failed: the Test does not render the depth")
      != std::string::npos);
  // A check that records nothing fails.
  CHECK(out[4].assertion.label == "silent");
  CHECK_FALSE(out[4].assertion.passed);
  CHECK(
      out[4].assertion.detail.find("recorded no finding") != std::string::npos);

  // A verdict is the conjunction: one failed exact assertion fails the Case.
  CaseResult r;
  r.verdict = Verdict::Passed;
  r.usesGroundTruth = false;
  for (const auto &o : out)
    r.assertions.push_back(o.assertion);
  settleVerdict(r, false);
  CHECK(r.verdict == Verdict::Failed);
}

// The evaluation seam on the catalog's own proof Test: a correct primitive ID
// buffer passes and wrong ones fail. It validates the check, not the scene.
TEST_CASE("the evaluation seam judges frame_primitiveID_channel's ID buffers",
    "[cts][exact]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  const auto found = catalog.filter(Filter{"frame/frame_primitiveID_channel"});
  REQUIRE(found.size() == 1);
  const TestDef &test = *found.front();
  REQUIRE(test.exactChecks.size() == 1);
  CHECK_FALSE(test.usesGroundTruth());
  const auto cases = expand(test);
  REQUIRE(cases.size() == 1);

  // The runner's camera for the eight triangles: framed from their bounds.
  const auto positions = layoutTriangleSoup(8);
  anari::scenes::Bounds bounds{positions.front(), positions.front()};
  for (const auto &p : positions) {
    bounds[0] = anari::math::min(bounds[0], p);
    bounds[1] = anari::math::max(bounds[1], p);
  }
  const auto desc = cameraFromBounds(bounds);
  CameraProjection camera;
  camera.position = desc.position;
  camera.direction = desc.direction;
  camera.up = desc.up;
  const FrameSize size{128, 128};

  // Rasterize: pixel centres inside triangle i hold i; the rest `background`.
  auto render = [&](std::map<uint32_t, uint32_t> relabel = {},
                    uint32_t background = ~0u) {
    std::vector<std::array<float2, 3>> tris;
    for (size_t i = 0; i < positions.size(); i += 3) {
      std::array<float2, 3> t;
      for (size_t k = 0; k < 3; ++k)
        t[k] = *projectToPixel(camera, size, positions[i + k]);
      tris.push_back(t);
    }
    return idChannel(size.width, size.height, [&](uint32_t x, uint32_t y) {
      const float2 p(x + 0.5f, y + 0.5f);
      for (uint32_t i = 0; i < tris.size(); ++i)
        if (insideTriangle(tris[i][0], tris[i][1], tris[i][2], p))
          return relabel.count(i) ? relabel[i] : i;
      return background;
    });
  };
  auto evaluate = [&](ChannelValues ids, std::optional<CameraProjection> cam) {
    std::vector<std::optional<CaseChannels>> inputs(1);
    inputs[0] = CaseChannels{{std::move(ids)}, size, cam};
    const auto results = evaluateSelfChecks(test, cases, inputs);
    CHECK(results.relations.empty());
    return results.exact.front();
  };

  SECTION("correct")
  {
    const auto out = evaluate(render(), camera);
    REQUIRE(out.size() == 2);
    for (const auto &o : out) {
      INFO(o.assertion.detail);
      CHECK(o.assertion.passed);
    }
    CHECK(out[0].assertion.pixels->checked == 8);
  }

  SECTION("two triangles' IDs swapped")
  {
    const auto out = evaluate(render({{2, 5}, {5, 2}}), camera);
    REQUIRE(out.size() == 2);
    CHECK_FALSE(out[0].assertion.passed);
    CHECK(out[0].assertion.pixels->offending == 2);
    CHECK(out[1].assertion.passed);
    CHECK(out[0].mask.valid());
  }

  SECTION("background claims triangle 0")
  {
    const auto out = evaluate(render({}, 0u), camera);
    REQUIRE(out.size() == 2);
    CHECK(out[0].assertion.passed);
    CHECK_FALSE(out[1].assertion.passed);
    REQUIRE(out[1].assertion.pixels->first);
    CHECK(out[1].assertion.pixels->first->got == "0");
  }

  SECTION("the image upside down")
  {
    auto ids = render();
    std::vector<uint32_t> flipped(ids.ids.size());
    for (uint32_t y = 0; y < size.height; ++y)
      for (uint32_t x = 0; x < size.width; ++x)
        flipped[size_t(size.height - 1 - y) * size.width + x] =
            ids.ids[size_t(y) * size.width + x];
    const auto out = evaluate(cts_test::decodedChannel(Channel::PrimitiveId,
                                  ANARI_UINT32,
                                  flipped,
                                  size.width,
                                  size.height),
        camera);
    CHECK_FALSE(out[0].assertion.passed);
  }

  SECTION("no camera projection")
  {
    const auto out = evaluate(render(), std::nullopt);
    REQUIRE(out.size() == 1);
    CHECK_FALSE(out[0].assertion.passed);
    CHECK(out[0].assertion.detail.find("no recorded projection")
        != std::string::npos);
  }
}

TEST_CASE("frame readback keeps raw depth and ID values", "[cts][exact]")
{
  const std::vector<float> depth = {0.25f, 7.5f};
  const MappedFrameDescriptor d{depth.data(), 2, 1, ANARI_FLOAT32};
  const auto rd = decodeFrameChannel(Channel::Depth, d, 2, 1, 10.f);
  REQUIRE(rd);
  CHECK(rd.depth == depth);
  CHECK(rd.ids.empty());

  const std::vector<uint32_t> ids = {7u, ~0u};
  const MappedFrameDescriptor i{ids.data(), 2, 1, ANARI_UINT32};
  const auto ri = decodeFrameChannel(Channel::InstanceId, i, 2, 1);
  REQUIRE(ri);
  CHECK(ri.ids == ids);
  CHECK(ri.depth.empty());
}

TEST_CASE(
    "an exact check fails when a channel isn't the Case's size", "[cts][exact]")
{
  const std::vector<ChannelValues> channels = {
      idChannel(kW, kH, [](uint32_t, uint32_t) { return 1u; })};
  const TestDef test =
      makeTest("t", "ids")
          .channels({Channel::PrimitiveId})
          .expectExact("never runs",
              [](ExactCheck &x) { x.measure("ran", 1.0, Limit::AtLeast, 0.0); })
          .take();
  Case c;
  const auto out =
      evaluateExactChecks(test, c, {kW, kH + 1}, channels, std::nullopt);
  REQUIRE(out.size() == 1);
  CHECK_FALSE(out[0].assertion.passed);
  CHECK(out[0].assertion.detail
      == "the primitiveId channel is 16x8 but the Case is 16x9");
}
