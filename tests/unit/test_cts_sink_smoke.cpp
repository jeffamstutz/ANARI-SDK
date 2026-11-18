// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts harness
#include "cts/BuildContext.h"
#include "cts/BuiltinTests.h"
#include "cts/Case.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/TestDef.h"
#include "cts/WorldBuilder.h"
// std
#include <exception>
#include <string>
#include <vector>

using namespace anari::cts;

namespace {

// Runs the build, cameraBuild and rendererConfig hooks of every Case of every
// Test in `catalog` against `device`, bypassing feature gating, and returns one
// message per defect. The bounds handed to cameraBuild come from the same query
// the Runner makes (sink answers none, so worldBounds keeps its fixed default
// box). A behaviorCheck is not run, only built for.
std::vector<std::string> smokeBuild(const Catalog &catalog, anari::Device d)
{
  std::vector<std::string> failures;
  for (const TestDef &test : catalog.tests()) {
    for (const Case &c : expand(test)) {
      const std::string where = c.qualifiedId() + ": ";
      try {
        BuildContext ctx(d);
        for (const auto &cv : c.values)
          ctx.set(cv.axisName, cv.value);

        if (!test.build) {
          failures.push_back(where + "no build function");
          continue;
        }
        anari::World world = test.build(ctx);
        if (!world) {
          failures.push_back(where + "build returned a null world");
          continue;
        }
        const auto bounds = worldBounds(d, world);

        if (test.cameraBuild) {
          anari::Camera camera = test.cameraBuild(ctx, bounds);
          if (!camera)
            failures.push_back(where + "cameraBuild returned a null camera");
          else
            anari::release(d, camera);
        }
        if (test.rendererConfig) {
          anari::Renderer renderer =
              anari::newObject<anari::Renderer>(d, "default");
          test.rendererConfig(ctx, renderer);
          anari::release(d, renderer);
        }
        anari::release(d, world);
      } catch (const std::exception &e) {
        failures.push_back(where + "threw: " + e.what());
      } catch (...) {
        failures.push_back(where + "threw a non-standard exception");
      }
    }
  }
  return failures;
}

TestDef testWith(const std::string &name, BuildFn build)
{
  TestDef t;
  t.category = "smoke";
  t.name = name;
  t.build = std::move(build);
  return t;
}

anari::Device loadSink(anari::Library &lib)
{
  lib = anari::loadLibrary("sink", nullptr);
  if (!lib) {
    WARN("sink library not available (BUILD_SINK_DEVICE off?); skipping");
    return nullptr;
  }
  anari::Device d = anari::newDevice(lib, "default");
  if (d)
    anari::commitParameters(d, d);
  return d;
}

} // namespace

TEST_CASE("every Case of the Catalog builds against the sink device",
    "[cts][sink_smoke]")
{
  anari::Library lib = nullptr;
  anari::Device d = loadSink(lib);
  if (!d)
    return;

  Catalog catalog;
  registerBuiltinTests(catalog);
  REQUIRE(catalog.size() > 0);

  const auto failures = smokeBuild(catalog, d);
  for (const auto &f : failures)
    UNSCOPED_INFO(f);
  CHECK(failures.empty());

  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("the sink smoke test catches a null world and a throwing build",
    "[cts][sink_smoke]")
{
  anari::Library lib = nullptr;
  anari::Device d = loadSink(lib);
  if (!d)
    return;

  Catalog good;
  good.add(testWith(
      "ok", [d](BuildContext &) { return anari::newObject<anari::World>(d); }));
  CHECK(smokeBuild(good, d).empty());

  Catalog nullWorld;
  nullWorld.add(
      testWith("null", [](BuildContext &) -> anari::World { return nullptr; }));
  const auto nullFailures = smokeBuild(nullWorld, d);
  REQUIRE(nullFailures.size() == 1);
  CHECK(nullFailures[0].find("null world") != std::string::npos);

  Catalog throws;
  throws.add(testWith("throws", [](BuildContext &) -> anari::World {
    throw std::runtime_error("boom");
  }));
  const auto throwFailures = smokeBuild(throws, d);
  REQUIRE(throwFailures.size() == 1);
  CHECK(throwFailures[0].find("boom") != std::string::npos);

  Catalog nullCamera;
  TestDef t = testWith("nullcam",
      [d](BuildContext &) { return anari::newObject<anari::World>(d); });
  t.cameraBuild = [](BuildContext &, const anari::scenes::Bounds &) {
    return anari::Camera(nullptr);
  };
  nullCamera.add(std::move(t));
  const auto camFailures = smokeBuild(nullCamera, d);
  REQUIRE(camFailures.size() == 1);
  CHECK(camFailures[0].find("null camera") != std::string::npos);

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
