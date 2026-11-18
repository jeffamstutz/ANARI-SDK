// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The parameter-level companion to the per-extension coverage report: for
// every extension the Catalog covers, every parameter its spec JSON
// (code_gen/api/khr_*.json) defines is set by some Test's build, camera or
// renderer hook, or is on the allow-list below with a reason. The hooks run
// against a recording device, so a name a Test composes at run time counts,
// and a literal in a comment or an unused branch does not.

#include "catch.hpp"
// anari
#include "anari/anari_cpp.hpp"
#include "anari/backend/DeviceImpl.h"
// anari_test_scenes
#include "cts/BuildContext.h"
#include "cts/BuiltinTests.h"
#include "cts/Case.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/TestDef.h"
#include "cts/WorldBuilder.h"
#include "scenes/file/nlohmann/json.hpp"
// std
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef CTS_API_JSON_DIR
#error "CTS_API_JSON_DIR must name code_gen/api"
#endif

using namespace anari::cts;

namespace {

const char *typeName(ANARIDataType t)
{
  switch (t) {
  case ANARI_ARRAY1D:
    return "ANARI_ARRAY1D";
  case ANARI_ARRAY2D:
    return "ANARI_ARRAY2D";
  case ANARI_ARRAY3D:
    return "ANARI_ARRAY3D";
  case ANARI_CAMERA:
    return "ANARI_CAMERA";
  case ANARI_FRAME:
    return "ANARI_FRAME";
  case ANARI_GEOMETRY:
    return "ANARI_GEOMETRY";
  case ANARI_GROUP:
    return "ANARI_GROUP";
  case ANARI_INSTANCE:
    return "ANARI_INSTANCE";
  case ANARI_LIGHT:
    return "ANARI_LIGHT";
  case ANARI_MATERIAL:
    return "ANARI_MATERIAL";
  case ANARI_RENDERER:
    return "ANARI_RENDERER";
  case ANARI_SAMPLER:
    return "ANARI_SAMPLER";
  case ANARI_SPATIAL_FIELD:
    return "ANARI_SPATIAL_FIELD";
  case ANARI_SURFACE:
    return "ANARI_SURFACE";
  case ANARI_VOLUME:
    return "ANARI_VOLUME";
  case ANARI_WORLD:
    return "ANARI_WORLD";
  default:
    return "ANARI_UNKNOWN";
  }
}

// A device that keeps nothing but which parameters were set on which kind of
// object (its type and subtype). Arrays keep their memory so a Test that maps
// one gets storage; every query answers "nothing".
struct RecordingDevice : public anari::DeviceImpl
{
  struct Object
  {
    ANARIDataType type;
    std::string subtype;
    std::vector<char> memory;
  };

  // "ANARI_GEOMETRY/triangle/vertex.color"
  std::set<std::string> set;

  ANARIArray1D newArray1D(const void *mem,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType t,
      uint64_t n) override
  {
    return array<ANARIArray1D>(ANARI_ARRAY1D, mem, t, n);
  }
  ANARIArray2D newArray2D(const void *mem,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType t,
      uint64_t n,
      uint64_t m) override
  {
    return array<ANARIArray2D>(ANARI_ARRAY2D, mem, t, n * m);
  }
  ANARIArray3D newArray3D(const void *mem,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType t,
      uint64_t n,
      uint64_t m,
      uint64_t k) override
  {
    return array<ANARIArray3D>(ANARI_ARRAY3D, mem, t, n * m * k);
  }
  ANARIGeometry newGeometry(const char *t) override
  {
    return make<ANARIGeometry>(ANARI_GEOMETRY, t);
  }
  ANARIMaterial newMaterial(const char *t) override
  {
    return make<ANARIMaterial>(ANARI_MATERIAL, t);
  }
  ANARISampler newSampler(const char *t) override
  {
    return make<ANARISampler>(ANARI_SAMPLER, t);
  }
  ANARISurface newSurface() override
  {
    return make<ANARISurface>(ANARI_SURFACE, "");
  }
  ANARISpatialField newSpatialField(const char *t) override
  {
    return make<ANARISpatialField>(ANARI_SPATIAL_FIELD, t);
  }
  ANARIVolume newVolume(const char *t) override
  {
    return make<ANARIVolume>(ANARI_VOLUME, t);
  }
  ANARILight newLight(const char *t) override
  {
    return make<ANARILight>(ANARI_LIGHT, t);
  }
  ANARIGroup newGroup() override
  {
    return make<ANARIGroup>(ANARI_GROUP, "");
  }
  ANARIInstance newInstance(const char *t) override
  {
    return make<ANARIInstance>(ANARI_INSTANCE, t);
  }
  ANARIWorld newWorld() override
  {
    return make<ANARIWorld>(ANARI_WORLD, "");
  }
  ANARICamera newCamera(const char *t) override
  {
    return make<ANARICamera>(ANARI_CAMERA, t);
  }
  ANARIRenderer newRenderer(const char *t) override
  {
    return make<ANARIRenderer>(ANARI_RENDERER, t);
  }
  ANARIFrame newFrame() override
  {
    return make<ANARIFrame>(ANARI_FRAME, "");
  }

  void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType,
      const void *) override
  {
    record(object, name);
  }
  void unsetParameter(ANARIObject, const char *) override {}
  void unsetAllParameters(ANARIObject) override {}
  void *mapParameterArray1D(ANARIObject object,
      const char *name,
      ANARIDataType t,
      uint64_t n,
      uint64_t *stride) override
  {
    record(object, name);
    return scratch(t, n, stride);
  }
  void *mapParameterArray2D(ANARIObject object,
      const char *name,
      ANARIDataType t,
      uint64_t n,
      uint64_t m,
      uint64_t *stride) override
  {
    record(object, name);
    return scratch(t, n * m, stride);
  }
  void *mapParameterArray3D(ANARIObject object,
      const char *name,
      ANARIDataType t,
      uint64_t n,
      uint64_t m,
      uint64_t k,
      uint64_t *stride) override
  {
    record(object, name);
    return scratch(t, n * m * k, stride);
  }
  void unmapParameterArray(ANARIObject, const char *) override {}
  void commitParameters(ANARIObject) override {}
  void release(ANARIObject) override {}
  void retain(ANARIObject) override {}
  void *mapArray(ANARIArray a) override
  {
    const auto i = reinterpret_cast<uintptr_t>(a);
    return i && i <= objects.size() ? objects[i - 1].memory.data() : nullptr;
  }
  void unmapArray(ANARIArray) override {}
  int getProperty(ANARIObject,
      const char *,
      ANARIDataType,
      void *,
      uint64_t,
      ANARIWaitMask) override
  {
    return 0;
  }
  const char **getObjectSubtypes(ANARIDataType) override
  {
    return nullptr;
  }
  const void *getObjectInfo(
      ANARIDataType, const char *, const char *, ANARIDataType) override
  {
    return nullptr;
  }
  const void *getParameterInfo(ANARIDataType,
      const char *,
      const char *,
      ANARIDataType,
      const char *,
      ANARIDataType) override
  {
    return nullptr;
  }
  const void *frameBufferMap(ANARIFrame,
      const char *,
      uint32_t *,
      uint32_t *,
      ANARIDataType *) override
  {
    return nullptr;
  }
  void frameBufferUnmap(ANARIFrame, const char *) override {}
  void renderFrame(ANARIFrame) override {}
  int frameReady(ANARIFrame, ANARIWaitMask) override
  {
    return 1;
  }
  void discardFrame(ANARIFrame) override {}

 private:
  void record(ANARIObject object, const char *name)
  {
    const auto i = reinterpret_cast<uintptr_t>(object);
    if (i == 0 || i > objects.size())
      return;
    const Object &o = objects[i - 1];
    set.insert(std::string(typeName(o.type)) + "/" + o.subtype + "/" + name);
  }

  template <typename H>
  H array(
      ANARIDataType type, const void *mem, ANARIDataType element, uint64_t n)
  {
    auto h = make<H>(type, "");
    if (!mem)
      objects.back().memory.assign(anari::sizeOf(element) * n, 0);
    return h;
  }

  template <typename H>
  H make(ANARIDataType type, const char *subtype)
  {
    objects.push_back({type, subtype ? subtype : "", {}});
    return reinterpret_cast<H>(uintptr_t(objects.size()));
  }

  void *scratch(ANARIDataType t, uint64_t count, uint64_t *stride)
  {
    const size_t size = anari::sizeOf(t);
    if (stride)
      *stride = size;
    mapped.assign(size * count, 0);
    return mapped.data();
  }

  std::vector<Object> objects;
  std::vector<char> mapped;
};

// Parameters of the covered extensions' spec JSON that no Test sets, and why.
// Each entry is "<extension>: <object type>/<subtype>/<parameter>", with a
// reason that says what stops a Test from setting it (a device nobody has, a
// parameter the spec leaves without an observable effect), never "later".
struct Skipped
{
  const char *parameter;
  const char *reason;
};

const std::vector<Skipped> &allowList();

std::set<std::string> parametersTheSpecDefines(const std::string &extension)
{
  std::string file = extension.substr(std::strlen("ANARI_"));
  for (auto &c : file)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  std::ifstream in(std::string(CTS_API_JSON_DIR) + "/" + file + ".json");
  std::set<std::string> out;
  if (!in)
    return out;
  const auto json = nlohmann::json::parse(in);
  for (const auto &object : json.value("objects", nlohmann::json::array()))
    for (const auto &p : object.value("parameters", nlohmann::json::array()))
      out.insert(object.value("type", "") + "/" + object.value("name", "") + "/"
          + p.value("name", ""));
  return out;
}

// Whether `parameter` ("<type>/<subtype>/<name>") was set. A parameter of an
// object type without subtypes (a frame, a surface; a spec JSON names it
// "<type>//<name>") counts when set on any object of the type, whatever its
// subtype.
bool isSet(const std::set<std::string> &set, const std::string &parameter)
{
  if (set.count(parameter))
    return true;
  const auto first = parameter.find('/');
  const auto second = parameter.find('/', first + 1);
  if (second != first + 1)
    return false;
  const std::string type = parameter.substr(0, first + 1);
  const std::string name = parameter.substr(second);
  for (const auto &s : set)
    if (s.compare(0, type.size(), type) == 0 && s.size() >= name.size()
        && s.compare(s.size() - name.size(), name.size(), name) == 0)
      return true;
  return false;
}

// What the Catalog's Cases set: every Case's build, camera and renderer hooks.
std::set<std::string> parametersTheCatalogSets(const Catalog &catalog)
{
  RecordingDevice impl;
  anari::Device d = impl.this_device();
  for (const TestDef &test : catalog.tests()) {
    if (!test.build)
      continue;
    for (const Case &c : expand(test)) {
      try {
        BuildContext ctx(d);
        for (const auto &cv : c.values)
          ctx.set(cv.axisName, cv.value);
        anari::World world = test.build(ctx);
        if (test.cameraBuild) {
          anari::Camera camera = test.cameraBuild(ctx, anari::scenes::Bounds{});
          (void)camera;
        }
        if (test.rendererConfig) {
          anari::Renderer renderer =
              anari::newObject<anari::Renderer>(d, "default");
          test.rendererConfig(ctx, renderer);
        }
        (void)world;
      } catch (...) {
        // A hook that can't build here is the sink smoke test's business.
      }
    }
  }
  return impl.set;
}

struct Gaps
{
  std::set<std::string> uncovered; // set by no Test and not allowed
  std::set<std::string> stale; // allow-list entries that no longer apply
};

Gaps findGaps(const Catalog &catalog)
{
  std::set<std::string> covered;
  for (const TestDef &test : catalog.tests())
    covered.insert(test.coveredFeatures.begin(), test.coveredFeatures.end());

  const auto setByTests = parametersTheCatalogSets(catalog);

  std::set<std::string> allowed;
  for (const auto &s : allowList()) {
    CHECK(std::strlen(s.reason) > 20);
    allowed.insert(s.parameter);
  }

  Gaps gaps;
  std::set<std::string> defined;
  for (const auto &extension : covered)
    for (const auto &p : parametersTheSpecDefines(extension)) {
      defined.insert(extension + ": " + p);
      if (p.size() >= 5 && p.compare(p.size() - 5, 5, "/name") == 0)
        continue; // `name` is the object name, which carries no behaviour
      if (!isSet(setByTests, p) && !allowed.count(extension + ": " + p))
        gaps.uncovered.insert(extension + ": " + p);
    }
  for (const auto &a : allowed) {
    if (!defined.count(a))
      gaps.stale.insert(a + " (not a parameter of a covered extension)");
    else {
      const auto at = a.find(": ");
      if (isSet(setByTests, a.substr(at + 2)))
        gaps.stale.insert(a + " (a Test sets it now; drop the entry)");
    }
  }
  return gaps;
}

} // namespace

TEST_CASE(
    "every parameter of a covered extension is set by a Test or is "
    "allowed",
    "[cts][parameter_coverage]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  REQUIRE(catalog.size() > 0);

  const auto gaps = findGaps(catalog);
  for (const auto &p : gaps.uncovered)
    UNSCOPED_INFO("not set by any Test: " << p);
  for (const auto &p : gaps.stale)
    UNSCOPED_INFO("stale allow-list entry: " << p);
  CHECK(gaps.uncovered.empty());
  CHECK(gaps.stale.empty());
}

TEST_CASE("removing the Test that sets a parameter makes it a gap",
    "[cts][parameter_coverage]")
{
  Catalog all;
  registerBuiltinTests(all);
  REQUIRE(findGaps(all).uncovered.empty());

  // instance/attributes is the only Test setting an instance's attributes.
  Catalog without;
  for (const TestDef &test : all.tests())
    if (!(test.category == "instance" && test.name == "attributes"))
      without.add(test);
  REQUIRE(without.size() + 1 == all.size());

  const auto gaps = findGaps(without);
  CHECK(gaps.uncovered.count(
            "ANARI_KHR_INSTANCE_TRANSFORM: ANARI_INSTANCE/transform/attribute2")
      == 1);
  CHECK(gaps.uncovered.count(
            "ANARI_KHR_INSTANCE_TRANSFORM: ANARI_INSTANCE/transform/color")
      == 1);
  // Parameters other Tests set stay covered.
  CHECK(gaps.uncovered.count(
            "ANARI_KHR_GEOMETRY_TRIANGLE: ANARI_GEOMETRY/triangle/attribute2")
      == 0);
}

TEST_CASE("the recording device sees a parameter a Test sets and no other",
    "[cts][parameter_coverage]")
{
  Catalog catalog;
  TestDef t;
  t.category = "probe";
  t.name = "one";
  t.build = [](BuildContext &ctx) {
    auto d = ctx.device();
    auto g = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, g, "primitive.attribute1", 1.f);
    return anari::newObject<anari::World>(d);
  };
  catalog.add(std::move(t));
  const auto set = parametersTheCatalogSets(catalog);
  CHECK(set.count("ANARI_GEOMETRY/triangle/primitive.attribute1") == 1);
  CHECK(set.count("ANARI_GEOMETRY/triangle/primitive.attribute2") == 0);
  CHECK(set.count("ANARI_GEOMETRY/quad/primitive.attribute1") == 0);
}

namespace {

const std::vector<Skipped> &allowList()
{
  static const std::vector<Skipped> list = {
      {"ANARI_KHR_DATA_PARALLEL_MPI: ANARI_FRAME//mpiCommunicator",
          "anariCts runs no MPI ranks and no SDK device has a noisy setup to "
          "calibrate one on: the extension is query-only in the CTS and "
          "rendering is left to the ptc_* tests (map, ticket 12 follow-up)"},
      {"ANARI_KHR_FRAME_ACCUMULATION: ANARI_FRAME//accumulation",
          "the Runner sets it on every Case's frame, and "
          "frame/progressive_rendering sets it in a behaviour check, neither "
          "of which a Test's build, camera or renderer hook can show here"},
      {"ANARI_KHR_FRAME_CHANNEL_ALBEDO: ANARI_FRAME//channel.albedo",
          "the Runner sets a frame's channel.* parameters from the Channels a "
          "Test lists (the albedo Tests list Channel::Albedo), not a Test's "
          "build hook"},
      {"ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID: ANARI_FRAME//channel.instanceId",
          "the Runner sets a frame's channel.* parameters from the Channels a "
          "Test lists (frame_instanceID_channel lists Channel::InstanceId), "
          "not a Test's build hook"},
      {"ANARI_KHR_FRAME_CHANNEL_NORMAL: ANARI_FRAME//channel.normal",
          "the Runner sets a frame's channel.* parameters from the Channels a "
          "Test lists (the normal Tests list Channel::Normal), not a Test's "
          "build hook"},
      {"ANARI_KHR_FRAME_CHANNEL_OBJECT_ID: ANARI_FRAME//channel.objectId",
          "the Runner sets a frame's channel.* parameters from the Channels a "
          "Test lists (frame_objectID_channel_* list Channel::ObjectId), not "
          "a Test's build hook"},
      {"ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID: ANARI_FRAME//channel.primitiveId",
          "the Runner sets a frame's channel.* parameters from the Channels a "
          "Test lists (frame_primitiveID_channel lists "
          "Channel::PrimitiveId), not a Test's build hook"},
      {"ANARI_KHR_FRAME_COMPLETION_CALLBACK: ANARI_FRAME//frameCompletionCallback",
          "frame/frame_completion_callback sets it on a frame of its own in "
          "its behaviour check, which a Test's build hook doesn't reach"},
      {"ANARI_KHR_FRAME_COMPLETION_CALLBACK: "
       "ANARI_FRAME//frameCompletionCallbackUserData",
          "frame/frame_completion_callback sets it on a frame of its own in "
          "its behaviour check, which a Test's build hook doesn't reach"},
      {"ANARI_KHR_LIGHT_PRIMARY_VISIBILITY: ANARI_LIGHT/directional/visible",
          "the spec makes visible meaningful only for area lights (the quad, "
          "ring and hdri Tests set it); a directional light has no footprint "
          "whose visibility an image could show"},
      {"ANARI_KHR_LIGHT_PRIMARY_VISIBILITY: ANARI_LIGHT/point/visible",
          "the spec makes visible meaningful only for area lights (the quad, "
          "ring and hdri Tests set it); a point light has no footprint whose "
          "visibility an image could show"},
      {"ANARI_KHR_LIGHT_PRIMARY_VISIBILITY: ANARI_LIGHT/spot/visible",
          "the spec makes visible meaningful only for area lights (the quad, "
          "ring and hdri Tests set it); a spot light has no footprint whose "
          "visibility an image could show"},
  };
  return list;
}

} // namespace
