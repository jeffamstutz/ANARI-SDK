// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/RendererExtensions.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace anari::cts;

namespace {

std::vector<std::string> deviceList(anari::Library lib)
{
  std::vector<std::string> out;
  for (auto **e = anariGetDeviceExtensions(lib, "default"); e && *e; ++e)
    out.push_back(*e);
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

TEST_CASE("Cases gate on the renderer's extensions, else the device's",
    "[cts][renderer_extensions]")
{
  DeviceSpec device{"halcyon", "default", "default", {"A", "B", "C"}};

  SECTION("a renderer that reports nothing falls back to the device")
  {
    CHECK(gatingExtensions(device) == std::set<std::string>{"A", "B", "C"});
    CHECK(rendererMissingFeatures(device, {"B", "D"}).empty());
  }

  SECTION("a renderer's own list is what Cases gate on")
  {
    device.rendererExtensions = {"A", "C"};
    CHECK(gatingExtensions(device) == std::set<std::string>{"A", "C"});
    // B is the device's but not the renderer's; D is neither's.
    CHECK(rendererMissingFeatures(device, {"B", "D"})
        == std::vector<std::string>{"B"});
  }
}

TEST_CASE("a feature skip names the renderer for what only it lacks",
    "[cts][renderer_extensions]")
{
  CHECK(featureSkipReason({"D"}, {}, "preview")
      == "device is missing a required feature: D");
  CHECK(featureSkipReason({"D", "E"}, {}, "preview")
      == "device is missing a required feature: D, E");
  CHECK(featureSkipReason({"ANARI_KHR_CAMERA_DEPTH_OF_FIELD"},
            {"ANARI_KHR_CAMERA_DEPTH_OF_FIELD"},
            "preview")
      == "renderer preview lacks a required feature: "
         "ANARI_KHR_CAMERA_DEPTH_OF_FIELD");
  CHECK(featureSkipReason({"B", "D"}, {"B"}, "preview")
      == "device is missing a required feature: D; renderer preview lacks a "
         "required feature: B");
}

TEST_CASE("without a device the renderer is named as given and reports nothing",
    "[cts][renderer_extensions]")
{
  CHECK(resolveRendererSubtype(nullptr, "default") == "default");
  CHECK(queryRendererExtensions(nullptr, "default").empty());
}

TEST_CASE("helide's renderer reports the device's extensions",
    "[cts][renderer_extensions][helide]")
{
  anari::Library lib = anari::loadLibrary("helide");
  if (!lib) {
    WARN("helide library not available; skipping renderer extensions test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);

  // helide lists "default", so the run's gating is what it was before
  // renderers' lists were read.
  CHECK(resolveRendererSubtype(d, "default") == "default");
  DeviceSpec device{"helide", "default", "default", deviceList(lib)};
  device.rendererExtensions = queryRendererExtensions(d, "default");
  const auto &advertised = device.extensions;
  CHECK(gatingExtensions(device)
      == std::set<std::string>(advertised.begin(), advertised.end()));
  CHECK(rendererMissingFeatures(device, advertised).empty());

  anari::release(d, d);
  anari::unloadLibrary(lib);
}

// Hidden (no ctest runs it; halcyon is off in the SDK's build): run it with
// `anariCatalogTests "[halcyon]"` and halcyon on the loader path.
TEST_CASE("halcyon's preview renderer reports less than the device",
    "[cts][renderer_extensions][.halcyon]")
{
  anari::Library lib = anari::loadLibrary("halcyon");
  if (!lib) {
    WARN("halcyon library not available; skipping renderer extensions test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);

  // "default" is the spec's alias of the first subtype, which halcyon
  // doesn't list.
  CHECK(resolveRendererSubtype(d, "default") == "preview");
  const auto preview = queryRendererExtensions(d, "default");
  CHECK(preview == queryRendererExtensions(d, "preview"));
  const auto quality = queryRendererExtensions(d, "quality");
  const std::string dof = "ANARI_KHR_CAMERA_DEPTH_OF_FIELD";
  CHECK(std::find(preview.begin(), preview.end(), dof) == preview.end());
  CHECK(std::find(quality.begin(), quality.end(), dof) != quality.end());

  DeviceSpec device{"halcyon", "default", "default", deviceList(lib)};
  device.rendererExtensions = preview;
  CHECK(
      rendererMissingFeatures(device, {dof}) == std::vector<std::string>{dof});

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
