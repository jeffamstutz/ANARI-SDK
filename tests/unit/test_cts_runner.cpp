// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "helium_test_device.h"
// cts
#include "cts/BuiltinTests.h"
#include "cts/Case.h"
#include "cts/Catalog.h"
#include "cts/DeviceErrorLog.h"
#include "cts/DeviceFailure.h"
#include "cts/DeviceProbe.h"
#include "cts/ExactCheck.h"
#include "cts/Expansion.h"
#include "cts/FrameFormats.h"
#include "cts/FrameReadback.h"
#include "cts/GeometryBuilder.h"
#include "cts/GeometryLayout.h"
#include "cts/Isolation.h"
#include "cts/LightBuilder.h"
#include "cts/Report.h"
#include "cts/Runner.h"
#include "cts/SurfaceBuilder.h"
#include "cts/TestBuilder.h"
#include "cts/ViewBuilder.h"
#include "cts/WorldBuilder.h"
// helium
#include "helium/BaseFrame.h"
// std
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace anari::cts;
using anari::math::float3;

namespace {

void statusFunc(const void *,
    anari::Device,
    anari::Object,
    anari::DataType,
    anari::StatusSeverity,
    anari::StatusCode,
    const char *)
{}

// A Test build function: read axis values from the context and author a world.
anari::World buildTriangleWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = ctx.get<int>("primitiveCount", 4);
  spec.mode = parsePrimitiveMode(ctx.getString("primitiveMode", "soup"));
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.8f, 0.4f, 0.2f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

anari::World buildSphereWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  SphereSpec spec;
  spec.primitiveCount = 6;
  auto geom = buildSphereGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.3f, 0.7f, 0.5f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

// A matte triangle surface with no explicit lights. Tests can make the
// renderer's ambient term the only source of illumination.
anari::World buildAmbientWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = 4;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
  auto surface = makeSurface(d, geom, mat);
  WorldContents wc;
  wc.surfaces = {surface};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

Catalog makeCatalog()
{
  Catalog cat;
  // 2 permutations x 2 variants = 4 cases, 2 ground-truth keys.
  makeTest("geometry", "triangle")
      .description("Checks triangle primitive layouts.")
      .build(buildTriangleWorld)
      .permute("primitiveCount", {4, 8})
      .variant("primitiveMode", {"soup", "indexed"})
      .registerInto(cat);
  // Single case, two channels.
  makeTest("geometry", "sphere")
      .description("Checks sphere color and depth rendering.")
      .build(buildSphereWorld)
      .channels({Channel::Color, Channel::Depth})
      .registerInto(cat);
  // Requires a feature no device has -> always skipped.
  makeTest("demo", "needs_bogus")
      .description("Checks feature-gated Test reporting.")
      .build(buildSphereWorld)
      .covers("ANARI_KHR_BOGUS_FEATURE")
      .registerInto(cat);
  return cat;
}

std::string readFile(const std::filesystem::path &p)
{
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool hasPublicationTemporary(const std::filesystem::path &root)
{
  if (!std::filesystem::exists(root))
    return false;
  for (const auto &entry :
      std::filesystem::recursive_directory_iterator(root)) {
    const auto name = entry.path().filename().string();
    if (name.find(".stage.") != std::string::npos
        || name.find(".backup.") != std::string::npos)
      return true;
  }
  return false;
}

uint64_t imageEnergy(const Image &image)
{
  uint64_t energy = 0;
  for (size_t i = 0; i < image.rgba.size(); i += 4)
    energy += image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2];
  return energy;
}

struct FailingArtifactWriter : public ArtifactWriter
{
  int failImageWrite{-1};
  bool failSidecarWrite{false};

  bool writeImage(
      const std::filesystem::path &path, const Image &image) override
  {
    if (m_imageWrites++ == failImageWrite)
      return false;
    return savePNG(path.string(), image);
  }

  bool writeSidecar(
      const std::filesystem::path &path, const CaseResult &result) override
  {
    return !failSidecarWrite && anari::cts::writeSidecar(path, result);
  }

 private:
  int m_imageWrites{0};
};

// A Case carrying a single output-format axis value.
Case caseWithFormat(const std::string &axis, anari::cts::Any value)
{
  Case c;
  c.category = "frame";
  c.testName = "fmt";
  c.values = {{axis, std::move(value), AxisKind::Permutation}};
  return c;
}

} // namespace

// Frame output-format resolution (pure; no device) ////////////////////////////

TEST_CASE("frame readback rejects null mapped data", "[cts][framereadback]")
{
  const MappedFrameDescriptor mapped{nullptr, 2, 2, ANARI_UFIXED8_RGBA_SRGB};

  const auto result = decodeFrameChannel(Channel::Color, mapped, 2, 2);

  CHECK_FALSE(result);
  CHECK(result.error == FrameReadbackError::NullData);
  CHECK(result.detail.find("channel.color") != std::string::npos);
  CHECK(result.detail.find("null") != std::string::npos);
}

TEST_CASE(
    "frame readback requires exact mapped dimensions", "[cts][framereadback]")
{
  const uint32_t pixel = 0;

  SECTION("short dimensions")
  {
    const MappedFrameDescriptor mapped{&pixel, 1, 2, ANARI_UFIXED8_RGBA_SRGB};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 2, 2);

    CHECK_FALSE(result);
    CHECK(result.error == FrameReadbackError::DimensionMismatch);
    CHECK(result.detail.find("expected 2x2") != std::string::npos);
    CHECK(result.detail.find("got 1x2") != std::string::npos);
  }

  SECTION("oversized dimensions")
  {
    const MappedFrameDescriptor mapped{&pixel, 3, 2, ANARI_UFIXED8_RGBA_SRGB};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 2, 2);

    CHECK_FALSE(result);
    CHECK(result.error == FrameReadbackError::DimensionMismatch);
    CHECK(result.detail.find("expected 2x2") != std::string::npos);
    CHECK(result.detail.find("got 3x2") != std::string::npos);
  }
}

TEST_CASE("frame readback rejects unsupported types for every channel",
    "[cts][framereadback]")
{
  const uint32_t pixel = 0;
  const std::pair<Channel, ANARIDataType> cases[] = {
      {Channel::Color, ANARI_FLOAT32},
      {Channel::Depth, ANARI_UINT32},
      {Channel::Albedo, ANARI_FIXED16_VEC3},
      {Channel::Normal, ANARI_UFIXED8_VEC3},
      {Channel::PrimitiveId, ANARI_FLOAT32},
      {Channel::ObjectId, ANARI_FLOAT32},
      {Channel::InstanceId, ANARI_FLOAT32},
  };

  for (const auto &[channel, pixelType] : cases) {
    INFO("channel: " << channelName(channel));
    const MappedFrameDescriptor mapped{&pixel, 1, 1, pixelType};
    const auto result = decodeFrameChannel(channel, mapped, 1, 1);

    CHECK_FALSE(result);
    CHECK(result.error == FrameReadbackError::UnsupportedType);
    CHECK(result.detail.find(channelName(channel)) != std::string::npos);
    CHECK(result.detail.find(anari::toString(pixelType)) != std::string::npos);
  }
}

TEST_CASE(
    "frame readback rejects overflow-sized dimensions", "[cts][framereadback]")
{
  const uint32_t pixel = 0;
  constexpr uint32_t huge = std::numeric_limits<uint32_t>::max();
  const MappedFrameDescriptor mapped{
      &pixel, huge, huge, ANARI_UFIXED8_RGBA_SRGB};

  const auto result = decodeFrameChannel(Channel::Color, mapped, huge, huge);

  CHECK_FALSE(result);
  CHECK(result.error == FrameReadbackError::SizeOverflow);
  CHECK(result.detail.find("overflow") != std::string::npos);
}

TEST_CASE("frame readback decodes every supported color format",
    "[cts][framereadback]")
{
  SECTION("UFIXED8_VEC4")
  {
    const uint8_t pixel[] = {10, 20, 30, 40};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_VEC4};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{10, 20, 30, 40});
  }

  SECTION("UFIXED8_RGBA_SRGB")
  {
    const uint8_t pixel[] = {50, 60, 70, 80};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_RGBA_SRGB};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{50, 60, 70, 80});
  }

  SECTION("FLOAT32_VEC4")
  {
    const float pixel[] = {0.f, 0.25f, 0.5f, 1.f};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FLOAT32_VEC4};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{0, 63, 127, 255});
  }
}

TEST_CASE(
    "frame readback decodes depth with the scene scale", "[cts][framereadback]")
{
  const float pixels[] = {0.f, 2.f, std::numeric_limits<float>::infinity()};
  const MappedFrameDescriptor mapped{pixels, 3, 1, ANARI_FLOAT32};

  const auto result = decodeFrameChannel(Channel::Depth, mapped, 3, 1, 4.f);

  REQUIRE(result);
  CHECK(result.image.rgba
      == std::vector<uint8_t>{
          0, 0, 0, 255, 127, 127, 127, 255, 255, 255, 255, 255});
}

TEST_CASE("frame readback decodes every supported albedo format",
    "[cts][framereadback]")
{
  SECTION("UFIXED8_VEC3")
  {
    const uint8_t pixel[] = {10, 20, 30};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_VEC3};
    const auto result = decodeFrameChannel(Channel::Albedo, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{10, 20, 30, 255});
  }

  SECTION("UFIXED8_RGB_SRGB")
  {
    const uint8_t pixel[] = {40, 50, 60};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_RGB_SRGB};
    const auto result = decodeFrameChannel(Channel::Albedo, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{40, 50, 60, 255});
  }

  SECTION("FLOAT32_VEC3")
  {
    const float pixel[] = {0.25f, 0.5f, 1.f};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FLOAT32_VEC3};
    const auto result = decodeFrameChannel(Channel::Albedo, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{63, 127, 255, 255});
  }
}

TEST_CASE("frame readback decodes every supported normal format",
    "[cts][framereadback]")
{
  SECTION("FIXED16_VEC3")
  {
    const int16_t pixel[] = {-32768, 0, 32767};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FIXED16_VEC3};
    const auto result = decodeFrameChannel(Channel::Normal, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{0, 127, 255, 255});
  }

  SECTION("FLOAT32_VEC3")
  {
    const float pixel[] = {-1.f, 0.f, 1.f};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FLOAT32_VEC3};
    const auto result = decodeFrameChannel(Channel::Normal, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{0, 127, 255, 255});
  }
}

TEST_CASE("frame readback decodes every ID channel", "[cts][framereadback]")
{
  const uint32_t pixel = 0;
  const Channel channels[] = {
      Channel::PrimitiveId, Channel::ObjectId, Channel::InstanceId};

  for (const Channel channel : channels) {
    INFO("channel: " << channelName(channel));
    const MappedFrameDescriptor mapped{&pixel, 1, 1, ANARI_UINT32};
    const auto result = decodeFrameChannel(channel, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{140, 209, 198, 255});
  }
}

TEST_CASE("output-format strings map to ANARIDataType", "[cts][frameformat]")
{
  CHECK(colorFormatFromString("UFIXED8_RGBA_SRGB") == ANARI_UFIXED8_RGBA_SRGB);
  CHECK(colorFormatFromString("UFIXED8_VEC4") == ANARI_UFIXED8_VEC4);
  CHECK(colorFormatFromString("FLOAT32_VEC4") == ANARI_FLOAT32_VEC4);
  CHECK(colorFormatFromString("nonsense") == ANARI_UNKNOWN);

  CHECK(albedoFormatFromString("UFIXED8_VEC3") == ANARI_UFIXED8_VEC3);
  CHECK(albedoFormatFromString("UFIXED8_RGB_SRGB") == ANARI_UFIXED8_RGB_SRGB);
  CHECK(albedoFormatFromString("FLOAT32_VEC3") == ANARI_FLOAT32_VEC3);
  CHECK(
      albedoFormatFromString("FIXED16_VEC3") == ANARI_UNKNOWN); // wrong channel

  CHECK(normalFormatFromString("FIXED16_VEC3") == ANARI_FIXED16_VEC3);
  CHECK(normalFormatFromString("FLOAT32_VEC3") == ANARI_FLOAT32_VEC3);
  CHECK(
      normalFormatFromString("UFIXED8_VEC3") == ANARI_UNKNOWN); // wrong channel
}

TEST_CASE("channels have fixed default formats", "[cts][frameformat]")
{
  CHECK(channelDefaultFormat(Channel::Color) == ANARI_UFIXED8_RGBA_SRGB);
  CHECK(channelDefaultFormat(Channel::Depth) == ANARI_FLOAT32);
  CHECK(channelDefaultFormat(Channel::Albedo) == ANARI_FLOAT32_VEC3);
  CHECK(channelDefaultFormat(Channel::Normal) == ANARI_FLOAT32_VEC3);
  CHECK(channelDefaultFormat(Channel::PrimitiveId) == ANARI_UINT32);
}

TEST_CASE("caseChannelFormat honors the output-format axis per channel",
    "[cts][frameformat]")
{
  // The color axis overrides the color channel.
  auto colorCase = caseWithFormat("frame_color_type", Any("FLOAT32_VEC4"));
  CHECK(caseChannelFormat(colorCase, Channel::Color) == ANARI_FLOAT32_VEC4);
  // ...but does not bleed into other channels.
  CHECK(caseChannelFormat(colorCase, Channel::Depth) == ANARI_FLOAT32);

  // Albedo and normal axes drive their own channels (D2).
  auto albedoCase = caseWithFormat("frame_albedo_type", Any("UFIXED8_VEC3"));
  CHECK(caseChannelFormat(albedoCase, Channel::Albedo) == ANARI_UFIXED8_VEC3);

  auto normalCase = caseWithFormat("frame_normal_type", Any("FIXED16_VEC3"));
  CHECK(caseChannelFormat(normalCase, Channel::Normal) == ANARI_FIXED16_VEC3);

  // No axis -> the channel default.
  Case bare;
  CHECK(caseChannelFormat(bare, Channel::Albedo) == ANARI_FLOAT32_VEC3);
  CHECK(caseChannelFormat(bare, Channel::Color) == ANARI_UFIXED8_RGBA_SRGB);

  // An unrecognized axis value falls back to the channel default.
  auto bogus = caseWithFormat("frame_color_type", Any("BOGUS"));
  CHECK(caseChannelFormat(bogus, Channel::Color) == ANARI_UFIXED8_RGBA_SRGB);
}

TEST_CASE(
    "Runner generates ground truth then scores a run", "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping runner integration test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root = std::filesystem::temp_directory_path() / "cts_runner_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  auto catalog = makeCatalog();
  const std::set<std::string>
      features; // no real features required by the tests
  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};

  Runner runner(d, Workdir(root), opts);

  SECTION("run before generate self-checks, or skips for lack of ground truth")
  {
    auto summary = runner.run(catalog, Filter{"geometry"}, features);
    CHECK(summary.total == 5); // 4 triangle + 1 sphere
    // The triangle Cases pass their effect and equivalence checks with the
    // Ground truth not evaluated (ADR-0010); the sphere, one Case with no
    // axis, has nothing to evaluate.
    CHECK(summary.passed == 4);
    CHECK(summary.passedWithoutGroundTruth == 4);
    CHECK(summary.skipped == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const auto sidecar = Workdir(root).sidecarPath(sph);
    REQUIRE(std::filesystem::exists(sidecar));
    const auto text = readFile(sidecar);
    CHECK(text.find("\"verdict\": \"skipped\"") != std::string::npos);
    CHECK(text.find("ground truth") != std::string::npos);
  }

  SECTION("generate then run passes against its own output")
  {
    auto genSummary = runner.generate(catalog, Filter{""}, features);
    CHECK(genSummary.total == 6); // 4 + 1 + 1(bogus)
    CHECK(genSummary.passed == 5); // bogus is skipped
    CHECK(genSummary.skipped == 1);

    // ADR-0005: the generated workdir ignores itself.
    CHECK(std::filesystem::exists(root / ".gitignore"));

    // Ground-truth images exist, keyed by the shared ground-truth key.
    Workdir wd(root);
    Case tri;
    tri.category = "geometry";
    tri.testName = "triangle";
    tri.values = {{"primitiveCount", Any(4), AxisKind::Permutation},
        {"primitiveMode", Any("soup"), AxisKind::Variant}};
    CHECK(
        std::filesystem::exists(wd.groundTruthImagePath(tri, Channel::Color)));

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    CHECK(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Color)));
    CHECK(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Depth)));

    // A candidate identical to the reference (both helide) must pass.
    auto runSummary = runner.run(catalog, Filter{""}, features);
    CHECK(runSummary.total == 6);
    CHECK(runSummary.passed == 5);
    CHECK(runSummary.skipped == 1); // the bogus-feature test

    // Sidecar + result image for a triangle case.
    const auto sidecar = wd.sidecarPath(tri);
    REQUIRE(std::filesystem::exists(sidecar));
    const auto text = readFile(sidecar);
    CHECK(text.find("\"verdict\": \"passed\"") != std::string::npos);
    CHECK(text.find("\"description\": \"Checks triangle primitive layouts.\"")
        != std::string::npos);
    CHECK(text.find("ssim") != std::string::npos);
    CHECK(std::filesystem::exists(wd.resultImagePath(tri, Channel::Color)));
    // The run records its device identity (schema v2) and emits debug images.
    CHECK(text.find("\"library\": \"helide\"") != std::string::npos);
    CHECK(std::filesystem::exists(wd.diffImagePath(tri, Channel::Color)));
    CHECK(std::filesystem::exists(wd.thresholdImagePath(tri, Channel::Color)));

    // The bogus-feature test is skipped with a feature reason.
    Case bogus;
    bogus.category = "demo";
    bogus.testName = "needs_bogus";
    const auto bogusText = readFile(wd.sidecarPath(bogus));
    CHECK(bogusText.find("\"verdict\": \"skipped\"") != std::string::npos);
    CHECK(bogusText.find(
              "\"description\": \"Checks feature-gated Test reporting.\"")
        != std::string::npos);
    CaseResult bogusResult;
    REQUIRE(readSidecar(wd.sidecarPath(bogus), bogusResult));
    CHECK(bogusResult.missingFeatures
        == std::vector<std::string>{"ANARI_KHR_BOGUS_FEATURE"});
    CHECK(bogusResult.requiredFeatures
        == std::vector<std::string>{"ANARI_KHR_BOGUS_FEATURE"});
    CHECK(bogusResult.skipReason.find("ANARI_KHR_BOGUS_FEATURE")
        != std::string::npos);
    // Every sidecar records which extensions the Catalog has a Test for.
    CHECK(bogusResult.catalogCoveredFeatures
        == std::vector<std::string>{"ANARI_KHR_BOGUS_FEATURE"});
  }

  SECTION("a filtered run records the whole Catalog's covered extensions")
  {
    REQUIRE(runner.generate(catalog, Filter{"geometry"}, features).passed == 5);
    REQUIRE(runner.run(catalog, Filter{"geometry"}, features).passed == 5);
    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    CaseResult result;
    REQUIRE(readSidecar(Workdir(root).sidecarPath(sph), result));
    CHECK(result.coveredFeatures.empty());
    CHECK(result.catalogCoveredFeatures
        == std::vector<std::string>{"ANARI_KHR_BOGUS_FEATURE"});
  }

  SECTION("a result directory creation failure fails the Case")
  {
    auto genSummary = runner.generate(catalog, Filter{"geometry"}, features);
    REQUIRE(genSummary.passed == 5);

    const auto blockedDirectory = root / "results" / "geometry" / "sphere";
    std::filesystem::create_directories(blockedDirectory.parent_path());
    {
      std::ofstream blocker(blockedDirectory);
      REQUIRE(blocker);
      blocker << "not a directory\n";
    }

    const auto runSummary = runner.run(catalog, Filter{"geometry"}, features);
    CHECK(runSummary.total == 5);
    CHECK(runSummary.passed == 4);
    CHECK(runSummary.failed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    CHECK_FALSE(std::filesystem::exists(Workdir(root).sidecarPath(sph)));
  }

  SECTION("a workdir setup failure fails every selected Case")
  {
    {
      std::ofstream blocker(root);
      REQUIRE(blocker);
      blocker << "not a directory\n";
    }

    const auto summary = runner.generate(catalog, Filter{"demo"}, features);
    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK(summary.skipped == 0);
  }

  SECTION("an image write failure cannot leave a stale passing sidecar")
  {
    REQUIRE(runner.generate(catalog, Filter{"sphere"}, features).passed == 1);
    REQUIRE(runner.run(catalog, Filter{"sphere"}, features).passed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    REQUIRE(std::filesystem::exists(wd.sidecarPath(sph)));
    const auto previousImage =
        readFile(wd.resultImagePath(sph, Channel::Color));

    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failImageWrite = 0;
    Runner failingRunner(d, wd, opts, writer);
    const auto summary = failingRunner.run(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK_FALSE(std::filesystem::exists(wd.sidecarPath(sph)));
    CHECK(readFile(wd.resultImagePath(sph, Channel::Color)) == previousImage);
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  SECTION("a sidecar write failure fails the Case and preserves its images")
  {
    REQUIRE(runner.generate(catalog, Filter{"sphere"}, features).passed == 1);
    REQUIRE(runner.run(catalog, Filter{"sphere"}, features).passed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    const auto previousImage =
        readFile(wd.resultImagePath(sph, Channel::Color));

    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failSidecarWrite = true;
    Runner failingRunner(d, wd, opts, writer);
    const auto summary = failingRunner.run(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK_FALSE(std::filesystem::exists(wd.sidecarPath(sph)));
    CHECK(readFile(wd.resultImagePath(sph, Channel::Color)) == previousImage);
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  SECTION("partial multi-channel ground truth is not published")
  {
    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failImageWrite = 1;
    Runner failingRunner(d, Workdir(root), opts, writer);
    const auto summary =
        failingRunner.generate(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    CHECK_FALSE(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Color)));
    CHECK_FALSE(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Depth)));
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  SECTION("failed ground-truth replacement preserves the complete old set")
  {
    REQUIRE(runner.generate(catalog, Filter{"sphere"}, features).passed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    const auto previousColor =
        readFile(wd.groundTruthImagePath(sph, Channel::Color));
    const auto previousDepth =
        readFile(wd.groundTruthImagePath(sph, Channel::Depth));

    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failImageWrite = 1;
    Runner failingRunner(d, wd, opts, writer);
    const auto summary =
        failingRunner.generate(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK(readFile(wd.groundTruthImagePath(sph, Channel::Color))
        == previousColor);
    CHECK(readFile(wd.groundTruthImagePath(sph, Channel::Depth))
        == previousDepth);
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

// Self-checks through the real runner (ADR-0010) /////////////////////////////

namespace {

// Counts the Ground-truth images an ArtifactWriter is asked to write.
struct CountingArtifactWriter : public ArtifactWriter
{
  int groundTruthWrites{0};

  bool writeImage(
      const std::filesystem::path &path, const Image &image) override
  {
    if (path.generic_string().find("/ground_truth/") != std::string::npos)
      ++groundTruthWrites;
    return savePNG(path.string(), image);
  }

  bool writeSidecar(
      const std::filesystem::path &path, const CaseResult &result) override
  {
    return anari::cts::writeSidecar(path, result);
  }
};

Case caseOf(const std::string &category,
    const std::string &test,
    std::vector<CaseValue> values)
{
  Case c;
  c.category = category;
  c.testName = test;
  c.values = std::move(values);
  return c;
}

} // namespace

TEST_CASE("Runner fails the effect of an axis the build ignores",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping self-check test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_selfcheck_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog catalog;
  // A test-only Test whose build ignores its Permutation axis: every Case
  // renders the same image, so its effect checks must fail.
  makeTest("demo", "ignores_axis")
      .description("A Permutation axis the build never reads.")
      .build(buildTriangleWorld)
      .permute("ignored", {"a", "b", "c"})
      .noGroundTruth()
      .registerInto(catalog);
  // A Variant axis the build does read: the Reference device disagrees with
  // itself.
  makeTest("demo", "bad_variant")
      .build(buildTriangleWorld)
      .variant("primitiveCount", {4, 9})
      .registerInto(catalog);

  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};
  const std::set<std::string> features;
  const Workdir wd(root);
  const Case a = caseOf(
      "demo", "ignores_axis", {{"ignored", Any("a"), AxisKind::Permutation}});
  const Case b = caseOf(
      "demo", "ignores_axis", {{"ignored", Any("b"), AxisKind::Permutation}});
  const Case c = caseOf(
      "demo", "ignores_axis", {{"ignored", Any("c"), AxisKind::Permutation}});

  SECTION("a full run records the effect failures in the Sidecars")
  {
    Runner runner(d, wd, opts);
    const auto summary = runner.run(catalog, Filter{"ignores_axis"}, features);
    CHECK(summary.total == 3);
    CHECK(summary.failed == 2); // b and c
    CHECK(summary.passed == 1); // a, the earlier partner of every pair

    CaseResult rb, rc, ra;
    REQUIRE(readSidecar(wd.sidecarPath(a), ra));
    REQUIRE(readSidecar(wd.sidecarPath(b), rb));
    REQUIRE(readSidecar(wd.sidecarPath(c), rc));
    CHECK(ra.verdict == Verdict::Passed);
    CHECK(ra.assertions.empty());
    CHECK_FALSE(ra.usesGroundTruth);
    CHECK(ra.groundTruthKey.empty());

    CHECK(rb.verdict == Verdict::Failed);
    REQUIRE(rb.assertions.size() == 1);
    CHECK(rb.assertions[0].kind == "effect");
    CHECK(rb.assertions[0].axis == "ignored");
    CHECK(rb.assertions[0].relatedCases == std::vector<std::string>{"a"});
    CHECK(rb.assertions[0].score == 0.0);
    CHECK_FALSE(rb.assertions[0].passed);
    // The partner's image and the (empty) changed-pixel mask are on disk.
    CHECK(std::filesystem::exists(root / rb.assertions[0].partnerImage));
    CHECK(std::filesystem::exists(root / rb.assertions[0].maskImage));
    CHECK(failureKinds(rb) == std::vector<std::string>{"effect"});

    // c fails against both a and b: every pair is checked.
    REQUIRE(rc.assertions.size() == 2);
    CHECK(rc.verdict == Verdict::Failed);
  }

  SECTION("a filtered run renders partners as support renders, same verdict")
  {
    Runner full(d, wd, opts);
    full.run(catalog, Filter{"ignores_axis"}, features);
    CaseResult fullC;
    REQUIRE(readSidecar(wd.sidecarPath(c), fullC));

    std::filesystem::remove_all(root, ec);
    Runner filtered(d, wd, opts);
    const auto summary =
        filtered.run(catalog, Filter{"demo/ignores_axis/c"}, features);
    // Only c is selected and counted; a and b are rendered for it.
    CHECK(summary.total == 1);
    CHECK(summary.failed == 1);
    CHECK_FALSE(std::filesystem::exists(wd.sidecarPath(a)));
    CHECK_FALSE(std::filesystem::exists(wd.sidecarPath(b)));
    CHECK(std::filesystem::exists(wd.resultImagePath(a, Channel::Color)));
    CHECK(std::filesystem::exists(wd.resultImagePath(b, Channel::Color)));

    CaseResult filteredC;
    REQUIRE(readSidecar(wd.sidecarPath(c), filteredC));
    CHECK(filteredC.verdict == fullC.verdict);
    REQUIRE(filteredC.assertions.size() == fullC.assertions.size());
    for (size_t i = 0; i < fullC.assertions.size(); ++i) {
      CHECK(filteredC.assertions[i].passed == fullC.assertions[i].passed);
      CHECK(filteredC.assertions[i].relatedCases
          == fullC.assertions[i].relatedCases);
    }
  }

  SECTION("generate skips a self-checked Test and reports bad Variants")
  {
    auto writer = std::make_shared<CountingArtifactWriter>();
    Runner runner(d, wd, opts, writer);
    const auto summary = runner.generate(catalog, Filter{"demo/"}, features);
    CHECK(summary.total == 5);
    CHECK(summary.skipped == 3); // ignores_axis has no Ground truth
    CHECK(summary.passed == 2);
    // One shared file, written once, from the first Variant value's Case.
    CHECK(writer->groundTruthWrites == 1);
    CHECK(summary.variantDisagreements == 1);
    CHECK_FALSE(std::filesystem::exists(
        root / "ground_truth" / "demo" / "ignores_axis"));
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE(
    "Runner renders each Case at its frame size and checks a derived crop",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping frame-size test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_framesize_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  // The middle of the sensor, rendered at its own pixel size, must equal the
  // crop of the full frame (camera/camera_general's imageRegion in small).
  const float region[4] = {0.25f, 0.25f, 0.75f, 0.75f};
  const Any box(ANARI_FLOAT32_BOX2, region);
  Catalog catalog;
  makeTest("demo", "region")
      .build(buildTriangleWorld)
      .camera([](BuildContext &ctx, const anari::scenes::Bounds &bounds) {
        auto dev = ctx.device();
        PerspectiveCameraOptions opts;
        opts.aspect = 1.f;
        auto c = makePerspectiveCamera(dev, cameraFromBounds(bounds), opts);
        const Any r = ctx.value("imageRegion");
        if (r.valid()) {
          anariSetParameter(
              dev, c, "imageRegion", ANARI_FLOAT32_BOX2, r.raw().data());
          anari::commitParameters(dev, c);
        }
        return c;
      })
      .frameSize([](BuildContext &ctx, FrameSize run) -> CaseFrameSize {
        if (!ctx.value("imageRegion").valid())
          return run;
        return regionFrameSize(run, 0.25f, 0.25f, 0.75f, 0.75f);
      })
      .permute("imageRegion", std::vector<Any>{none(), box})
      .expectDerived("imageRegion",
          anyToString(box),
          "the region's crop of the full frame",
          [](const Siblings &s) -> ExpectedImage {
            const Image &full = s.at("none");
            const auto rect = regionPixels(
                full.width, full.height, 0.25f, 0.25f, 0.75f, 0.75f);
            return rect ? crop(full, *rect) : Image{};
          })
      .noGroundTruth()
      .registerInto(catalog);

  const auto cases = expand(*catalog.filter(Filter{"demo/region"}).front());
  REQUIRE(cases.size() == 2);
  const Case &full = cases[0];
  const Case &part = cases[1];

  RunOptions opts;
  opts.device = {"helide", "default", "default"};
  const Workdir wd(root);

  SECTION("whole pixels: the region Case renders the crop's size and matches")
  {
    opts.width = 64;
    opts.height = 64;
    Runner runner(d, wd, opts);
    const auto summary = runner.run(catalog, Filter{"demo/region"}, {});
    CHECK(summary.total == 2);
    CHECK(summary.failed == 0);
    CHECK(summary.passed == 2);

    CaseResult rf, rp;
    REQUIRE(readSidecar(wd.sidecarPath(full), rf));
    REQUIRE(readSidecar(wd.sidecarPath(part), rp));
    CHECK(rf.frameWidth == 64);
    CHECK(rf.frameHeight == 64);
    CHECK(rp.frameWidth == 32);
    CHECK(rp.frameHeight == 32);
    CHECK(
        loadPNG(wd.resultImagePath(part, Channel::Color).string()).width == 32);

    // One equivalence, no effect pair: the region value is derived.
    REQUIRE(rp.assertions.size() == 1);
    const auto &a = rp.assertions[0];
    CHECK(a.kind == kEquivalenceAssertion);
    CHECK(a.axis == "imageRegion");
    CHECK(a.relatedCases == std::vector<std::string>{"none"});
    CHECK(a.expectation == "the region's crop of the full frame");
    CHECK(a.passed);
    REQUIRE_FALSE(a.expectedImage.empty());
    const Image expected = loadPNG((root / a.expectedImage).string());
    CHECK(expected.width == 32);
    CHECK(expected.height == 32);
    // The full-frame Case is the sibling of an evaluated expectation.
    CHECK(rf.verdict == Verdict::Passed);
  }

  SECTION("not whole pixels: the region Case is skipped with the reason")
  {
    opts.width = 66;
    opts.height = 66;
    Runner runner(d, wd, opts);
    runner.run(catalog, Filter{"demo/region"}, {});
    CaseResult rp;
    REQUIRE(readSidecar(wd.sidecarPath(part), rp));
    CHECK(rp.verdict == Verdict::Skipped);
    CHECK(rp.skipReason.find("whole pixels at 66x66") != std::string::npos);
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("Runner runs exact checks on raw values and records them",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping exact-check test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_exact_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog catalog;
  // Ground truth is left undeclared (compared by default) and never
  // generated: the exact checks alone decide.
  makeTest("demo", "exact")
      .build(buildTriangleWorld)
      .channels({Channel::Color, Channel::Depth})
      .expectExact("depth at the first triangle",
          [](ExactCheck &x) {
            // Depth is the distance from the camera: project the centroid of
            // triangle 0 and compare there.
            const auto tri = layoutTriangleSoup(4);
            const float3 centroid = (tri[0] + tri[1] + tri[2]) / 3.f;
            const auto at = x.project(centroid);
            if (!at)
              throw std::runtime_error("the centroid is behind the camera");
            PixelMask where(x.frameSize().width, x.frameSize().height);
            where.set(uint32_t(at->x), uint32_t(at->y));
            const float distance =
                anari::math::length(centroid - x.camera()->position);
            x.matches(
                "depth is the distance to the camera",
                x.depth(),
                [&](uint32_t, uint32_t) { return distance; },
                0.01 * distance,
                &where);
          })
      .expectExact("an impossible depth",
          [](ExactCheck &x) {
            x.allPixels("every depth is negative",
                x.depth(),
                "< 0",
                [](uint32_t, uint32_t, float v) { return v < 0.f; });
          })
      .registerInto(catalog);

  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};
  const Workdir wd(root);
  Runner runner(d, wd, opts);
  const auto summary = runner.run(catalog, Filter{"demo/exact"}, {});
  CHECK(summary.total == 1);
  CHECK(summary.failed == 1);

  const Case c = caseOf("demo", "exact", {});
  CaseResult r;
  REQUIRE(readSidecar(wd.sidecarPath(c), r));
  CHECK(r.verdict == Verdict::Failed);
  CHECK_FALSE(r.groundTruthEvaluated);
  REQUIRE(r.assertions.size() == 2);
  const auto &good = r.assertions[0];
  INFO(good.detail);
  CHECK(good.kind == "exact");
  CHECK(good.label == "depth at the first triangle");
  CHECK(good.passed);
  REQUIRE(good.pixels);
  CHECK(good.pixels->checked == 1);
  CHECK(good.maskImage.empty());

  const auto &bad = r.assertions[1];
  CHECK(bad.label == "an impossible depth");
  CHECK(bad.expectation == "every depth is negative");
  CHECK(bad.channel == "depth");
  CHECK_FALSE(bad.passed);
  REQUIRE(bad.pixels);
  CHECK(bad.pixels->checked == 64 * 64);
  CHECK(bad.pixels->offending == 64 * 64);
  REQUIRE(bad.pixels->first);
  CHECK(bad.pixels->first->x == 0);
  CHECK(bad.pixels->first->y == 0);
  CHECK(bad.pixels->first->expected == "< 0");
  CHECK(std::filesystem::exists(root / bad.maskImage));
  CHECK(failureKinds(r) == std::vector<std::string>{"exact"});

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("A support render never overwrites a scored Case's images",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping support-render test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_support_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog catalog;
  makeTest("demo", "ignores_axis")
      .build(buildTriangleWorld)
      .permute("ignored", {"a", "b", "c"})
      .noGroundTruth()
      .registerInto(catalog);

  struct RecordingWriter : public ArtifactWriter
  {
    std::vector<std::string> images;
    bool writeImage(
        const std::filesystem::path &path, const Image &image) override
    {
      images.push_back(path.generic_string());
      return savePNG(path.string(), image);
    }
    bool writeSidecar(
        const std::filesystem::path &path, const CaseResult &result) override
    {
      return anari::cts::writeSidecar(path, result);
    }
  };

  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  opts.device = {"helide", "default", "default"};
  const Workdir wd(root);
  const Case a = caseOf(
      "demo", "ignores_axis", {{"ignored", Any("a"), AxisKind::Permutation}});
  const Case b = caseOf(
      "demo", "ignores_axis", {{"ignored", Any("b"), AxisKind::Permutation}});
  auto writer = std::make_shared<RecordingWriter>();
  Runner runner(d, wd, opts, writer);
  const std::set<std::string> features;

  // --stdin runs one Filter per line through the same Runner: line 1 scores
  // a, line 2 scores c and so renders a and b as support renders.
  runner.run(catalog, Filter{"demo/ignores_axis/a"}, features);
  // The writer sees staged paths (<dir>/.<file>.stage.N): match on those.
  auto wroteImageOf = [&](const Case &c) {
    const auto final = wd.resultImagePath(c, Channel::Color);
    const auto staged =
        (final.parent_path() / ("." + final.filename().string() + ".stage."))
            .generic_string();
    return std::count_if(writer->images.begin(),
        writer->images.end(),
        [&](const std::string &w) { return w.find(staged) == 0; });
  };
  REQUIRE(wroteImageOf(a) == 1);
  const auto scoredBytes = readFile(wd.resultImagePath(a, Channel::Color));

  runner.run(catalog, Filter{"demo/ignores_axis/c"}, features);
  CHECK(wroteImageOf(a) == 1); // not rewritten as a support render
  CHECK(readFile(wd.resultImagePath(a, Channel::Color)) == scoredBytes);
  CHECK(wroteImageOf(b) == 1); // never scored: written as a support render
  CaseResult ra;
  CHECK(readSidecar(wd.sidecarPath(a), ra));

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("generate writes a shared Ground truth from the first Variant only",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping generate test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_gt_writer_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  auto catalog = makeCatalog();
  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};
  auto writer = std::make_shared<CountingArtifactWriter>();
  Runner runner(d, Workdir(root), opts, writer);
  // geometry/triangle: 2 primitiveCount x 2 primitiveMode Cases, 2 files.
  const auto summary =
      runner.generate(catalog, Filter{"geometry/triangle"}, {});
  CHECK(summary.total == 4);
  CHECK(summary.passed == 4);
  CHECK(writer->groundTruthWrites == 2);
  CHECK(summary.variantDisagreements == 0);

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("Runner applies ambientRadiance before Test renderer overrides",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping renderer defaults test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  auto rendererTestBuilder = makeTest("renderer", "defaults");
  rendererTestBuilder.build(buildAmbientWorld)
      .renderer([](BuildContext &ctx, anari::Renderer renderer) {
        // Helide's implementation-specific switch allows the standard ambient
        // parameters to affect this focused integration test.
        anari::setParameter(
            ctx.device(), renderer, "ignoreAmbientLighting", false);
      });
  const TestDef rendererTest = rendererTestBuilder.take();
  Case c;
  c.category = rendererTest.category;
  c.testName = rendererTest.name;

  RunOptions darkOptions;
  darkOptions.width = 32;
  darkOptions.height = 32;
  darkOptions.ambientRadiance = 0.f;
  Runner darkRunner(d, Workdir(std::filesystem::path{}), darkOptions);
  const auto dark = darkRunner.renderCase(rendererTest, c);

  RunOptions brightOptions = darkOptions;
  brightOptions.ambientRadiance = 4.f;
  Runner brightRunner(d, Workdir(std::filesystem::path{}), brightOptions);
  const auto bright = brightRunner.renderCase(rendererTest, c);

  REQUIRE(dark.size() == 1);
  REQUIRE(bright.size() == 1);
  CHECK(imageEnergy(bright.front()) > imageEnergy(dark.front()));

  auto overrideTestBuilder = makeTest("renderer", "ambient_override");
  overrideTestBuilder.build(buildAmbientWorld)
      .renderer([](BuildContext &ctx, anari::Renderer renderer) {
        auto d = ctx.device();
        anari::setParameter(d, renderer, "ignoreAmbientLighting", false);
        anari::setParameter(d, renderer, "ambientRadiance", 0.5f);
      });
  const TestDef overrideTest = overrideTestBuilder.take();
  c.testName = overrideTest.name;

  const auto overriddenDark = darkRunner.renderCase(overrideTest, c);
  const auto overriddenBright = brightRunner.renderCase(overrideTest, c);
  REQUIRE(overriddenDark.size() == 1);
  REQUIRE(overriddenBright.size() == 1);
  CHECK(overriddenDark.front().rgba == overriddenBright.front().rgba);

  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE(
    "accumulation gates off when unsupported but denoise is applied "
    "unconditionally; the sidecar records the effective settings",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping accumulation/denoise test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_accum_denoise_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog cat;
  makeTest("geometry", "sphere").build(buildSphereWorld).registerInto(cat);

  // Request accumulation + denoise, but helide advertises neither extension.
  const std::set<std::string> features; // helide: no accumulation, no denoise
  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  opts.accumulationFrames = 16; // CLI default; gated off here (no extension)
  // Applied even though helide lacks the extension.
  opts.denoise = true;
  opts.device = {"helide", "default", "default"};

  Runner runner(d, Workdir(root), opts);

  // Ground truth then a run. Accumulation gates off (-> 1). Denoise is set on
  // the renderer regardless, but helide ignores the unknown param, so a
  // candidate identical to its own ground truth still passes.
  auto gen = runner.generate(cat, Filter{""}, features);
  CHECK(gen.passed == 1);
  auto s = runner.run(cat, Filter{""}, features);
  CHECK(s.total == 1);
  CHECK(s.passed == 1);
  CHECK(s.failed == 0);

  // The sidecar detail records the effective settings: accumulation resolved to
  // 1 (gated off, no extension) but denoise stays on (applied unconditionally).
  Case sph;
  sph.category = "geometry";
  sph.testName = "sphere";
  const auto text = readFile(Workdir(root).sidecarPath(sph));
  CHECK(text.find("accumulation=1, denoise=on") != std::string::npos);

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("Runner isolates a throwing case and completes the rest",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping crash-isolation test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root = std::filesystem::temp_directory_path() / "cts_crash_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  // A build hook that throws only when the shared flag is set, so we can
  // generate clean ground truth first and then make the same Case explode.
  auto shouldThrow = std::make_shared<bool>(false);
  Catalog cat;
  makeTest("geometry", "triangle").build(buildTriangleWorld).registerInto(cat);
  makeTest("geometry", "explodes")
      .build([shouldThrow](BuildContext &ctx) -> anari::World {
        if (*shouldThrow)
          throw std::runtime_error("boom during build");
        return buildSphereWorld(ctx);
      })
      .registerInto(cat);
  makeTest("geometry", "sphere").build(buildSphereWorld).registerInto(cat);

  const std::set<std::string> features;
  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  Runner runner(d, Workdir(root), opts);

  Case explodes;
  explodes.category = "geometry";
  explodes.testName = "explodes";

  SECTION("a throw during run() yields one failed sidecar; the run continues")
  {
    // Ground truth for every Case (nothing throws yet).
    auto gen = runner.generate(cat, Filter{""}, features);
    CHECK(gen.total == 3);
    CHECK(gen.passed == 3);

    // Now make the middle Case throw during its build.
    *shouldThrow = true;
    auto s = runner.run(cat, Filter{""}, features);
    CHECK(s.total == 3);
    CHECK(s.passed == 2); // triangle + sphere still scored
    CHECK(s.failed == 1); // explodes
    CHECK(s.skipped == 0);

    // The throwing Case is recorded as failed with the message in detail.
    const auto text = readFile(Workdir(root).sidecarPath(explodes));
    CHECK(text.find("\"verdict\": \"failed\"") != std::string::npos);
    CHECK(text.find("boom during build") != std::string::npos);

    // The cases after the throwing one still produced their sidecars.
    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const auto sphText = readFile(Workdir(root).sidecarPath(sph));
    CHECK(sphText.find("\"verdict\": \"passed\"") != std::string::npos);
  }

  SECTION("a throw during generate() loses only that case")
  {
    *shouldThrow = true;
    auto gen = runner.generate(cat, Filter{""}, features);
    CHECK(gen.total == 3);
    CHECK(gen.passed == 2); // triangle + sphere ground truth produced
    CHECK(gen.failed == 1); // explodes

    // The surrounding cases' ground truth exists despite the throw.
    Case tri;
    tri.category = "geometry";
    tri.testName = "triangle";
    CHECK(std::filesystem::exists(
        Workdir(root).groundTruthImagePath(tri, Channel::Color)));
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("Runner verifies behavioral tests via the behavior hook",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping behavior hook test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_behavior_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog cat;
  // A behavior check that inspects what the runner built and passes.
  makeTest("frame", "behaves")
      .build(buildTriangleWorld)
      .behavior([](anari::Device dev,
                    anari::World w,
                    anari::Camera cam,
                    anari::Renderer r,
                    uint32_t width,
                    uint32_t height) -> BehaviorResult {
        const bool ok = dev != nullptr && w != nullptr && cam != nullptr
            && r != nullptr && width > 0 && height > 0;
        return {ok, ok ? "scene objects present" : "missing scene objects"};
      })
      .registerInto(cat);
  // A behavior check gated on a feature no device has -> skipped, never called.
  makeTest("frame", "behaves_bogus")
      .build(buildTriangleWorld)
      .behavior(
          [](anari::Device,
              anari::World,
              anari::Camera,
              anari::Renderer,
              uint32_t,
              uint32_t) -> BehaviorResult { return {false, "should not run"}; })
      .requireFeature("ANARI_KHR_BOGUS_FEATURE")
      .registerInto(cat);

  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  Runner runner(d, Workdir(root), opts);

  SECTION("generate skips behavioral tests (no ground truth)")
  {
    auto g = runner.generate(cat, Filter{""}, {});
    CHECK(g.total == 2);
    CHECK(g.skipped == 2);
    CHECK(g.passed == 0);
    // No ground truth produced for a behavioral test.
    Case behaves;
    behaves.category = "frame";
    behaves.testName = "behaves";
    CHECK_FALSE(std::filesystem::exists(
        Workdir(root).groundTruthImagePath(behaves, Channel::Color)));
  }

  SECTION("run invokes the hook and records verdict + detail")
  {
    auto s = runner.run(cat, Filter{""}, {});
    CHECK(s.total == 2);
    CHECK(s.passed == 1);
    CHECK(s.skipped == 1);
    CHECK(s.failed == 0);

    Case behaves;
    behaves.category = "frame";
    behaves.testName = "behaves";
    const auto text = readFile(Workdir(root).sidecarPath(behaves));
    CHECK(text.find("\"verdict\": \"passed\"") != std::string::npos);
    CHECK(text.find("scene objects present") != std::string::npos);
    // No images are rendered for a behavioral case.
    CHECK_FALSE(std::filesystem::exists(
        Workdir(root).resultImagePath(behaves, Channel::Color)));

    Case bogus;
    bogus.category = "frame";
    bogus.testName = "behaves_bogus";
    const auto bogusText = readFile(Workdir(root).sidecarPath(bogus));
    CHECK(bogusText.find("\"verdict\": \"skipped\"") != std::string::npos);
    CHECK(bogusText.find("feature") != std::string::npos);
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

// Timed behavior Tests (ADR-0009) /////////////////////////////////////////////

namespace {

// Stands in for the child process an isolated Case runs in: 'body' plays
// the process (it may write the Case's sidecar) and says how it ended.
struct FakeIsolation : public CaseIsolation
{
  std::function<ProcessResult(const Case &)> body;
  std::vector<std::string> ran;
  std::chrono::milliseconds lastTimeout{0};

  ProcessResult run(const Case &c, std::chrono::milliseconds timeout) override
  {
    ran.push_back(c.qualifiedId());
    lastTimeout = timeout;
    return body(c);
  }
};

ProcessResult exited(int code)
{
  ProcessResult r;
  r.status = ProcessResult::Status::Exited;
  r.exitCode = code;
  if (code != 0)
    r.detail = "exited with code " + std::to_string(code);
  return r;
}

BehaviorResult passingCheck(anari::Device,
    anari::World,
    anari::Camera,
    anari::Renderer,
    uint32_t,
    uint32_t)
{
  return {true, "fine"};
}

} // namespace

TEST_CASE("Runner records how an isolated timed behavior Case ended",
    "[cts][runner][isolation]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_isolation_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog cat;
  makeTest("synchronization", "timed")
      .description("A timed check.")
      .behavior(passingCheck)
      .timeout(std::chrono::seconds(5))
      .registerInto(cat);
  makeTest("synchronization", "timed_bogus")
      .behavior(passingCheck)
      .timeout(std::chrono::seconds(5))
      .requireFeature("ANARI_KHR_BOGUS_FEATURE")
      .registerInto(cat);

  Case timed;
  timed.category = "synchronization";
  timed.testName = "timed";
  const Workdir workdir(root);
  const auto sidecar = workdir.sidecarPath(timed);

  // The child writes a sidecar with this verdict, as `anariCts run
  // --isolated-case` does.
  auto writeResult = [&](Verdict verdict, const std::string &detail) {
    CaseResult r;
    r.category = timed.category;
    r.test = timed.testName;
    r.caseId = timed.id();
    r.verdict = verdict;
    r.detail = detail;
    REQUIRE(writeSidecar(sidecar, r));
  };

  auto isolation = std::make_shared<FakeIsolation>();
  // No device: the Cases run (or are skipped) without the runner touching one.
  Runner runner(nullptr, workdir, RunOptions{});
  runner.setCaseIsolation(isolation);

  auto verdictOf = [&]() {
    CaseResult r;
    REQUIRE(readSidecar(sidecar, r));
    return r;
  };

  SECTION("a clean exit reports the child's sidecar")
  {
    isolation->body = [&](const Case &) {
      writeResult(Verdict::Passed, "fine");
      return exited(0);
    };
    const auto s = runner.run(cat, Filter{""}, {});
    CHECK(s.total == 2);
    CHECK(s.passed == 1);
    CHECK(s.skipped == 1);
    CHECK(s.failed == 0);
    // Only the runnable Case was isolated, with its Test's timeout.
    REQUIRE(isolation->ran.size() == 1);
    CHECK(isolation->ran[0] == "synchronization/timed/default");
    CHECK(isolation->lastTimeout == std::chrono::seconds(5));
    CHECK(verdictOf().verdict == Verdict::Passed);
  }

  SECTION("a timeout is a failure that says the device hung")
  {
    isolation->body = [&](const Case &) {
      ProcessResult r;
      r.status = ProcessResult::Status::TimedOut;
      return r;
    };
    const auto s = runner.run(cat, Filter{"timed"}, {});
    CHECK(s.failed == 1);
    const auto r = verdictOf();
    CHECK(r.verdict == Verdict::Failed);
    CHECK(r.detail.find("timed out after 5 s") != std::string::npos);
    CHECK(r.description == "A timed check.");
  }

  SECTION("a hang after the check keeps the check's own result in the detail")
  {
    isolation->body = [&](const Case &) {
      writeResult(Verdict::Passed, "fine");
      ProcessResult r;
      r.status = ProcessResult::Status::TimedOut;
      return r;
    };
    runner.run(cat, Filter{"timed"}, {});
    const auto r = verdictOf();
    CHECK(r.verdict == Verdict::Failed);
    CHECK(r.detail.find("timed out") != std::string::npos);
    CHECK(r.detail.find("passed: fine") != std::string::npos);
  }

  SECTION("a crash is a failure with the crash")
  {
    isolation->body = [&](const Case &) {
      ProcessResult r;
      r.status = ProcessResult::Status::Crashed;
      r.detail = "killed by signal 11 (Segmentation fault)";
      return r;
    };
    const auto s = runner.run(cat, Filter{"timed"}, {});
    CHECK(s.failed == 1);
    CHECK(verdictOf().detail.find("signal 11") != std::string::npos);
  }

  SECTION("a child that writes no result fails, even over a stale sidecar")
  {
    writeResult(Verdict::Passed, "left by an earlier run");
    isolation->body = [&](const Case &) { return exited(0); };
    const auto s = runner.run(cat, Filter{"timed"}, {});
    CHECK(s.failed == 1);
    const auto r = verdictOf();
    CHECK(r.verdict == Verdict::Failed);
    CHECK(r.detail.find("without writing a result") != std::string::npos);
  }

  SECTION("a child that can't start fails")
  {
    isolation->body = [&](const Case &) {
      ProcessResult r;
      r.status = ProcessResult::Status::NotStarted;
      r.detail = "no such file";
      return r;
    };
    runner.run(cat, Filter{"timed"}, {});
    CHECK(verdictOf().detail.find("no such file") != std::string::npos);
  }

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("Runner runs a timed behavior Case in-process without isolation",
    "[cts][runner][helide]")
{
  DeviceErrorLog errors;
  auto recordStatus = [](const void *userData,
                          anari::Device,
                          anari::Object,
                          anari::DataType,
                          anari::StatusSeverity severity,
                          anari::StatusCode,
                          const char *message) {
    static_cast<DeviceErrorLog *>(const_cast<void *>(userData))
        ->record(severity, message);
  };
  anari::Library lib = anari::loadLibrary("helide", recordStatus, &errors);
  if (!lib) {
    WARN("helide library not available; skipping timed behavior test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_timed_inprocess_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  // A check that makes the device report an ERROR: waiting on a frame whose
  // render waits for an array this thread has mapped.
  auto provokeError = [](anari::Device dev,
                          anari::World w,
                          anari::Camera cam,
                          anari::Renderer r,
                          uint32_t width,
                          uint32_t height) -> BehaviorResult {
    auto f = anari::newObject<anari::Frame>(dev);
    anari::setParameter(
        dev, f, "size", anari::math::vec<uint32_t, 2>(width, height));
    anari::setParameter(dev, f, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
    anari::setParameter(dev, f, "world", w);
    anari::setParameter(dev, f, "camera", cam);
    anari::setParameter(dev, f, "renderer", r);
    anari::commitParameters(dev, f);
    auto a = anari::newArray1D(dev, ANARI_FLOAT32, 4);
    anari::map<float>(dev, a);
    anari::render(dev, f);
    anariFrameReady(dev, f, ANARI_WAIT);
    anari::unmap(dev, a);
    anari::wait(dev, f);
    anari::release(dev, a);
    anari::release(dev, f);
    return {true, "made an error"};
  };

  Catalog cat;
  makeTest("synchronization", "strict")
      .build(buildTriangleWorld)
      .behavior(provokeError)
      .timeout(std::chrono::seconds(30))
      .failOnDeviceErrors()
      .registerInto(cat);
  makeTest("synchronization", "lenient")
      .build(buildTriangleWorld)
      .behavior(provokeError)
      .timeout(std::chrono::seconds(30))
      .registerInto(cat);

  RunOptions opts;
  opts.width = 16;
  opts.height = 16;
  opts.deviceErrors = &errors;
  const Workdir workdir(root);

  auto resultOf = [&](const std::string &test) {
    Case c;
    c.category = "synchronization";
    c.testName = test;
    CaseResult r;
    REQUIRE(readSidecar(workdir.sidecarPath(c), r));
    return r;
  };

  SECTION("a device ERROR fails only a Test that asks for it")
  {
    Runner runner(d, workdir, opts);
    const auto s = runner.run(cat, Filter{""}, {});
    CHECK(s.total == 2);
    CHECK(s.passed == 1);
    CHECK(s.failed == 1);
    const auto strict = resultOf("strict");
    CHECK(strict.verdict == Verdict::Failed);
    CHECK(strict.detail.find("the device reported") != std::string::npos);
    CHECK(resultOf("lenient").verdict == Verdict::Passed);
  }

  SECTION("onlyCase runs just that Case")
  {
    opts.onlyCase = "synchronization/lenient/default";
    Runner runner(d, workdir, opts);
    const auto s = runner.run(cat, Filter{""}, {});
    CHECK(s.total == 1);
    CHECK(s.passed == 1);
    Case strict;
    strict.category = "synchronization";
    strict.testName = "strict";
    CHECK_FALSE(std::filesystem::exists(workdir.sidecarPath(strict)));
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("The built-in single-threaded synchronization Tests pass on helide",
    "[cts][runner][helide]")
{
  DeviceErrorLog errors;
  auto recordStatus = [](const void *userData,
                          anari::Device,
                          anari::Object,
                          anari::DataType,
                          anari::StatusSeverity severity,
                          anari::StatusCode,
                          const char *message) {
    static_cast<DeviceErrorLog *>(const_cast<void *>(userData))
        ->record(severity, message);
  };
  anari::Library lib = anari::loadLibrary("helide", recordStatus, &errors);
  if (!lib) {
    WARN("helide library not available; skipping synchronization Tests");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);
  std::set<std::string> features;
  for (auto **e = anariGetDeviceExtensions(lib, "default"); e && *e; ++e)
    features.insert(*e);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_synchronization_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  // Only the single-threaded rules: the concurrent ones (those that require
  // KHR_DEVICE_SYNCHRONIZATION) depend on timing, so they run only isolated,
  // one child process per Case (unit_test::cts::isolated_run), so a flake or a
  // crash there costs one Case rather than this binary.
  Catalog builtin;
  registerBuiltinTests(builtin);
  Catalog cat;
  for (const auto *test : builtin.filter(Filter{"synchronization/"})) {
    if (std::count(test->requiredFeatures.begin(),
            test->requiredFeatures.end(),
            "ANARI_KHR_DEVICE_SYNCHRONIZATION")
        == 0)
      cat.add(*test);
  }
  REQUIRE(cat.size() == 2);

  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  opts.deviceErrors = &errors;
  // In-process (no isolation): the ctest TIMEOUT catches a hang.
  Runner runner(d, Workdir(root), opts);
  const auto s = runner.run(cat, Filter{"synchronization/"}, features);
  CHECK(s.total == 2);
  CHECK(s.passed == 2);

  for (const auto *test : cat.filter(Filter{"synchronization/"})) {
    for (const auto &c : expand(*test)) {
      CaseResult r;
      REQUIRE(readSidecar(Workdir(root).sidecarPath(c), r));
      INFO(c.qualifiedId() << ": " << r.detail);
      CHECK(r.verdict == Verdict::Passed);
    }
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

// The probe frame: a device that can't render ////////////////////////////////

namespace {

// A frame whose render reports an ERROR and whose color channel maps null
// (as helide_gpu's does when SDL3 can't initialise), or, with 'renders' set,
// maps a black probe-sized image.
struct StubFrame : public helium::BaseFrame
{
  StubFrame(helium::BaseGlobalDeviceState *s, bool renders)
      : BaseFrame(s), m_renders(renders)
  {}

  bool isValid() const override
  {
    return true;
  }
  bool getProperty(const std::string_view &,
      ANARIDataType,
      void *,
      uint64_t,
      uint32_t) override
  {
    return false;
  }
  void commitParameters() override {}
  void finalize() override {}
  void renderFrame() override
  {
    if (!m_renders)
      reportMessage(ANARI_SEVERITY_ERROR,
          "[stub] SDL_InitSubSystem(SDL_INIT_VIDEO) failed: libSDL3.so.0: "
          "cannot open shared object file");
  }
  void *map(std::string_view,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override
  {
    if (!m_renders) {
      *width = 0;
      *height = 0;
      *pixelType = ANARI_UNKNOWN;
      return nullptr;
    }
    *width = kProbeFrameSize;
    *height = kProbeFrameSize;
    *pixelType = ANARI_UFIXED8_RGBA_SRGB;
    return m_pixels.data();
  }
  void unmap(std::string_view) override {}
  int frameReady(ANARIWaitMask) override
  {
    return 1;
  }
  void discard() override {}

 private:
  bool m_renders{false};
  std::vector<uint32_t> m_pixels =
      std::vector<uint32_t>(kProbeFrameSize * kProbeFrameSize, 0);
};

// A device whose objects do nothing; its frames are StubFrames. It reports
// through a status callback that records into 'errors', as anariCts's does.
struct StubRenderDevice : public helium_test::TestDevice
{
  StubRenderDevice(DeviceErrorLog &errors, bool renders) : m_renders(renders)
  {
    state()->statusCB = recordError;
    state()->statusCBUserPtr = &errors;
  }

  ANARIWorld newWorld() override
  {
    return (ANARIWorld) new helium_test::StubObject(ANARI_WORLD, state());
  }
  ANARICamera newCamera(const char *subtype) override
  {
    if (!cameraSubtypes.empty() && cameraSubtypes[0] != std::string(subtype))
      return nullptr;
    return (ANARICamera) new helium_test::StubObject(ANARI_CAMERA, state());
  }
  const char **getObjectSubtypes(ANARIDataType type) override
  {
    if (type != ANARI_CAMERA || cameraSubtypes.empty())
      return nullptr;
    m_cameraNames.clear();
    for (const auto &name : cameraSubtypes)
      m_cameraNames.push_back(name.c_str());
    m_cameraNames.push_back(nullptr);
    return m_cameraNames.data();
  }

  // The camera subtypes it lists (and the first is the only one it creates);
  // empty lists none and creates any.
  std::vector<std::string> cameraSubtypes;
  ANARIRenderer newRenderer(const char *) override
  {
    return (ANARIRenderer) new helium_test::StubObject(ANARI_RENDERER, state());
  }
  ANARIFrame newFrame() override
  {
    return (ANARIFrame) new StubFrame(state(), m_renders);
  }

  anari::Device handle()
  {
    return this_device();
  }

 private:
  static void recordError(const void *userData,
      ANARIDevice,
      ANARIObject,
      ANARIDataType,
      ANARIStatusSeverity severity,
      ANARIStatusCode,
      const char *message)
  {
    static_cast<DeviceErrorLog *>(const_cast<void *>(userData))
        ->record(severity, message);
  }

  bool m_renders{false};
  std::vector<const char *> m_cameraNames;
};

// A Test whose build would author a world; on a device that can't render, it
// must never run.
Catalog probeCatalog(std::shared_ptr<int> builds)
{
  Catalog cat;
  makeTest("geometry", "triangle")
      .build([builds](BuildContext &) -> anari::World {
        ++*builds;
        return nullptr;
      })
      .registerInto(cat);
  return cat;
}

} // namespace

TEST_CASE("probeDevice names the device's own error", "[cts][runner][probe]")
{
  DeviceErrorLog errors;
  errors.record(ANARI_SEVERITY_WARNING, "only a warning");

  SECTION("a device that renders the probe frame")
  {
    auto *device = new StubRenderDevice(errors, true);
    CHECK(probeDevice(device->handle(), "default", &errors).empty());
    device->state()->commitBuffer.clear();
    delete device;
  }

  SECTION("a device without a perspective camera renders with one it lists")
  {
    auto *device = new StubRenderDevice(errors, true);
    device->cameraSubtypes = {"orthographic"};
    CHECK(probeDevice(device->handle(), "default", &errors).empty());
    device->state()->commitBuffer.clear();
    delete device;
  }

  SECTION("a device whose frame maps null")
  {
    auto *device = new StubRenderDevice(errors, false);
    const auto reason = probeDevice(device->handle(), "default", &errors);
    CHECK(
        reason.find("channel.color mapped data is null") != std::string::npos);
    CHECK(reason.find("libSDL3.so.0: cannot open shared object file")
        != std::string::npos);
    CHECK(reason.find("only a warning") == std::string::npos);
    device->state()->commitBuffer.clear();
    delete device;
  }

  SECTION("a message the device repeats is named once, with a count")
  {
    errors.record(ANARI_SEVERITY_ERROR, "init failed");
    errors.record(ANARI_SEVERITY_ERROR, "init failed");
    auto *device = new StubRenderDevice(errors, false);
    const auto reason = probeDevice(device->handle(), "default", &errors);
    CHECK(reason.find("init failed (2 times)") != std::string::npos);
    CHECK(reason.find("init failed", reason.find("init failed") + 1)
        == std::string::npos);
    device->state()->commitBuffer.clear();
    delete device;
  }

  SECTION("a device that can't create the objects")
  {
    auto *device = new helium_test::TestDevice;
    const auto reason =
        probeDevice(device->this_device(), "pathtracer", nullptr);
    CHECK_FALSE(reason.empty());
    delete device;
  }
}

TEST_CASE("Runner stops before the first Case when the device can't render",
    "[cts][runner][probe]")
{
  const auto root = std::filesystem::temp_directory_path() / "cts_probe_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const Workdir workdir(root);

  DeviceErrorLog errors;
  auto *device = new StubRenderDevice(errors, false);
  auto builds = std::make_shared<int>(0);
  const Catalog cat = probeCatalog(builds);
  Case tri;
  tri.category = "geometry";
  tri.testName = "triangle";

  RunOptions opts;
  opts.device = {"stub", "default", "default"};
  opts.deviceErrors = &errors;
  Runner runner(device->handle(), workdir, opts);

  SECTION("run writes one run-level record and no per-Case result")
  {
    const auto s = runner.run(cat, Filter{""}, {});
    CHECK(s.deviceFailure.find("mapped data is null") != std::string::npos);
    CHECK(s.deviceFailure.find("libSDL3.so.0") != std::string::npos);
    CHECK(s.total == 0);
    CHECK(s.failed == 0);
    CHECK(*builds == 0);
    CHECK_FALSE(std::filesystem::exists(workdir.sidecarPath(tri)));

    const auto record = readDeviceFailure(workdir);
    REQUIRE(record);
    CHECK(record->device.library == "stub");
    CHECK(record->reason == s.deviceFailure);

    // Later slices of the same run (--stdin) stop the same way.
    const auto again = runner.run(cat, Filter{""}, {});
    CHECK(again.deviceFailure == s.deviceFailure);
    CHECK(*builds == 0);
  }

  SECTION("generate writes no Ground truth")
  {
    const auto s = runner.generate(cat, Filter{""}, {});
    CHECK_FALSE(s.deviceFailure.empty());
    CHECK(s.total == 0);
    CHECK(*builds == 0);
    CHECK_FALSE(std::filesystem::exists(
        workdir.groundTruthImagePath(tri, Channel::Color)));
  }

  device->state()->commitBuffer.clear();
  delete device;
  std::filesystem::remove_all(root, ec);
}

TEST_CASE("A run whose device renders the probe clears an earlier record",
    "[cts][runner][probe]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_probe_clear_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const Workdir workdir(root);
  REQUIRE(writeDeviceFailure(workdir, {{"stub"}, "an earlier failure"}));

  DeviceErrorLog errors;
  auto *device = new StubRenderDevice(errors, true);
  auto builds = std::make_shared<int>(0);
  Runner runner(device->handle(), workdir, RunOptions{});

  // The Case runs (it is skipped for want of Ground truth).
  const auto s = runner.run(probeCatalog(builds), Filter{""}, {});
  CHECK(s.deviceFailure.empty());
  CHECK(s.total == 1);
  CHECK(s.skipped == 1);
  CHECK_FALSE(readDeviceFailure(workdir));

  device->state()->commitBuffer.clear();
  delete device;
  std::filesystem::remove_all(root, ec);
}

// Per-Case feature gating (ticket 22) /////////////////////////////////////////

TEST_CASE("Runner skips only the Cases whose value needs a missing extension",
    "[cts][runner][features][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping per-Case gating test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_gating_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog catalog;
  // The build ignores the axis, so every pair fails its effect: what matters
  // is which pairs exist.
  makeTest("demo", "gated")
      .description("Value b of the axis needs an extension.")
      .build(buildTriangleWorld)
      .permute("ignored", {"a", "b", "c"})
      .requireFeatureFor("ignored", Any("b"), "ANARI_KHR_TEST_B")
      .noGroundTruth()
      .registerInto(catalog);

  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};
  const Workdir wd(root);
  const auto at = [](const char *v) {
    return caseOf(
        "demo", "gated", {{"ignored", Any(v), AxisKind::Permutation}});
  };

  SECTION("a device without it skips exactly that Case")
  {
    Runner runner(d, wd, opts);
    const auto summary = runner.run(catalog, Filter{"demo/gated"}, {});
    CHECK(summary.total == 3);
    CHECK(summary.skipped == 1);

    CaseResult ra, rb, rc;
    REQUIRE(readSidecar(wd.sidecarPath(at("a")), ra));
    REQUIRE(readSidecar(wd.sidecarPath(at("b")), rb));
    REQUIRE(readSidecar(wd.sidecarPath(at("c")), rc));
    CHECK(rb.verdict == Verdict::Skipped);
    CHECK(rb.missingFeatures == std::vector<std::string>{"ANARI_KHR_TEST_B"});
    CHECK(rb.skipReason
        == "device is missing a required feature: ANARI_KHR_TEST_B");
    CHECK(rb.requiredFeatures == std::vector<std::string>{"ANARI_KHR_TEST_B"});
    CHECK(ra.missingFeatures.empty());
    CHECK(ra.requiredFeatures.empty());
    CHECK(rc.missingFeatures.empty());
    CHECK_FALSE(
        std::filesystem::exists(wd.resultImagePath(at("b"), Channel::Color)));

    // The skipped Case is not compared with, and not failed for: c's effect
    // with a is evaluated, and the pair with b is recorded as not evaluated.
    CHECK(ra.verdict == Verdict::Passed);
    REQUIRE(rc.assertions.size() == 2);
    CHECK(rc.assertions[0].relatedCases == std::vector<std::string>{"a"});
    CHECK(rc.assertions[0].evaluated);
    CHECK(rc.assertions[0].detail.find("skipped") == std::string::npos);
    CHECK(rc.assertions[1].kind == "effect");
    CHECK(rc.assertions[1].relatedCases == std::vector<std::string>{"b"});
    CHECK_FALSE(rc.assertions[1].evaluated);
    CHECK(rc.assertions[1].detail
        == "not evaluated: partner `" + at("b").id()
            + "` was skipped: device is missing a required feature: "
              "ANARI_KHR_TEST_B");

    CHECK(summary.failed == 1); // c fails its effect (the axis is ignored)
  }

  SECTION("a device with it runs every Case")
  {
    Runner runner(d, wd, opts);
    const auto summary =
        runner.run(catalog, Filter{"demo/gated"}, {"ANARI_KHR_TEST_B"});
    CHECK(summary.total == 3);
    CHECK(summary.skipped == 0);
    CaseResult rc;
    REQUIRE(readSidecar(wd.sidecarPath(at("c")), rc));
    CHECK(rc.assertions.size() == 2);
  }

  SECTION("a filtered run on a device without it neither renders nor fails it")
  {
    Runner runner(d, wd, opts);
    const auto summary = runner.run(catalog, Filter{"demo/gated/c"}, {});
    CHECK(summary.total == 1);
    CaseResult rc;
    REQUIRE(readSidecar(wd.sidecarPath(at("c")), rc));
    REQUIRE(rc.assertions.size() == 2);
    CHECK(rc.assertions[0].relatedCases == std::vector<std::string>{"a"});
    CHECK_FALSE(rc.assertions[1].evaluated);
    // b is a skipped partner: it is not written as a support render.
    CHECK_FALSE(
        std::filesystem::exists(wd.resultImagePath(at("b"), Channel::Color)));
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE(
    "An effect pair's only partner being skipped is not evaluated, not "
    "failed",
    "[cts][runner][features][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping only-partner gating test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_only_partner";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog catalog;
  makeTest("demo", "gated2")
      .description("The first value needs an extension.")
      .build(buildTriangleWorld)
      .permute("ignored", {"a", "b"})
      .requireFeatureFor("ignored", Any("a"), "ANARI_KHR_TEST_A")
      .noGroundTruth()
      .registerInto(catalog);

  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};
  const Workdir wd(root);
  Runner runner(d, wd, opts);
  const auto summary = runner.run(catalog, Filter{"demo/gated2"}, {});
  CHECK(summary.total == 2);
  CHECK(summary.failed == 0);
  CHECK(summary.skipped == 2);

  CaseResult rb;
  REQUIRE(readSidecar(
      wd.sidecarPath(caseOf(
          "demo", "gated2", {{"ignored", Any("b"), AxisKind::Permutation}})),
      rb));
  REQUIRE(rb.assertions.size() == 1);
  CHECK_FALSE(rb.assertions[0].evaluated);
  CHECK(rb.assertions[0].detail.find("not evaluated: partner") == 0);
  // Nothing was evaluated: skipped, not passed and not failed.
  CHECK(rb.verdict == Verdict::Skipped);

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("The material Tests' non-sampler Cases run without image samplers",
    "[cts][runner][features][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping material gating test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_runner_material_gating";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog catalog;
  registerBuiltinTests(catalog);
  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};
  const Workdir wd(root);
  Runner runner(d, wd, opts);
  // A device with the matte scene but no image sampler at all.
  const std::set<std::string> features = {"ANARI_KHR_GEOMETRY_TRIANGLE",
      "ANARI_KHR_MATERIAL_MATTE",
      "ANARI_KHR_CAMERA_PERSPECTIVE"};
  const auto summary =
      runner.run(catalog, Filter{"material/matte_color"}, features);
  CHECK(summary.total == 4);
  CHECK(summary.skipped == 1);

  const auto *test = &catalog.tests().front();
  for (const auto &t : catalog.tests())
    if (t.id() == "material/matte_color")
      test = &t;
  int skipped = 0, ran = 0;
  for (const Case &c : expand(*test)) {
    CaseResult r;
    REQUIRE(readSidecar(wd.sidecarPath(c), r));
    if (r.verdict == Verdict::Skipped && !r.missingFeatures.empty()) {
      ++skipped;
      CHECK(r.missingFeatures
          == std::vector<std::string>{"ANARI_KHR_SAMPLER_IMAGE2D"});
    } else {
      ++ran;
      CHECK(r.missingFeatures.empty());
      CHECK(std::filesystem::exists(wd.resultImagePath(c, Channel::Color)));
    }
  }
  CHECK(skipped == 1);
  CHECK(ran == 3);

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}
