// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Synchronization rules (ADR-0009): which calls must return, checked as timed
// behaviour tests. A device that breaks one usually hangs, so each Case runs
// in its own process under a timeout, and fails on any device ERROR, since
// the checks only make calls the spec allows.

#include "../AnariObject.h"
#include "../BuildContext.h"
#include "../FrameReadback.h"
#include "../GeometryBuilder.h"
#include "../GeometryLayout.h"
#include "../LightBuilder.h"
#include "../Metrics.h"
#include "../Relations.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../TestDef.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using namespace std::chrono_literals;

// Long enough that a device that's slow but not blocked always makes it, even
// on a loaded machine: the calls these checks time either return promptly or
// wait for a render held off until the check lets it go, i.e. forever.
constexpr auto kReturnBudget = 1000ms;
// How long checkCallbackReentry() leaves the callback to run on its own
// before waiting on the frame.
constexpr auto kCallbackDeadline = 10s;
// Every Case finishes in well under a second on a conforming device (the
// concurrent ones in a few seconds).
constexpr auto kCaseTimeout = 30s;

// The concurrent checks (KHR_DEVICE_SYNCHRONIZATION): how many frames the
// render thread renders while other threads work, and how many threads work.
constexpr int kConcurrentFrames = 40;
constexpr int kWorkerThreads = 4;
// The torn-frame check's scene: enough triangles that a render reading a
// half-written position array shows it.
constexpr int kTearTriangles = 16;

// A world with one triangle whose vertex positions are a shared array over
// app memory, which a check maps to hold renders of the world off.
struct SharedArrayWorld
{
  std::vector<float3> positions = layoutTriangleSoup(1);
  UniqueAnariObject<anari::Array1D> positionArray;
  UniqueAnariObject<anari::World> world;

  explicit SharedArrayWorld(anari::Device d)
  {
    positionArray = UniqueAnariObject<anari::Array1D>(
        d, anari::newArray1D(d, positions.data(), positions.size()));
    auto geom = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geom, "vertex.position", positionArray.get());
    anari::commitParameters(d, geom);
    auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
    auto surface = makeSurface(d, geom, mat);
    auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
    WorldContents wc;
    wc.surfaces = {surface};
    wc.lights = {light};
    world = UniqueAnariObject<anari::World>(d, assembleWorld(d, wc));
    anari::release(d, geom);
    anari::release(d, mat);
    anari::release(d, surface);
    anari::release(d, light);
  }
};

// The Tests' world, used only for the runner's camera framing: the triangles
// the checks render (the checks build their own, to map their arrays).
anari::World triangleSoupWorld(BuildContext &ctx, int primitiveCount)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = primitiveCount;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  WorldContents wc;
  wc.surfaces = {surface};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

anari::World triangleWorld(BuildContext &ctx)
{
  return triangleSoupWorld(ctx, 1);
}

UniqueAnariObject<anari::Frame> newFrame(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  UniqueAnariObject<anari::Frame> frame(d, anari::newObject<anari::Frame>(d));
  const auto f = frame.get();
  anari::setParameter(d, f, "size", anari::math::vec<uint32_t, 2>(w, h));
  anari::setParameter(d, f, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, f, "renderer", renderer);
  anari::setParameter(d, f, "camera", camera);
  anari::setParameter(d, f, "world", world);
  return frame;
}

// Calls fn() and returns how long it took.
template <typename F>
std::chrono::duration<double, std::milli> timeOf(F &&fn)
{
  const auto start = std::chrono::steady_clock::now();
  fn();
  return std::chrono::steady_clock::now() - start;
}

// Collects a check's findings: the failures, and notes for the detail.
struct Findings
{
  std::vector<std::string> failures;
  std::vector<std::string> notes;

  void fail(std::string what)
  {
    failures.push_back(std::move(what));
  }

  // A call that has to return without waiting took 'took'.
  void returned(
      const std::string &call, std::chrono::duration<double, std::milli> took)
  {
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(3);
    text << call << " took " << took.count() << " ms";
    if (took > kReturnBudget) {
      text << " (more than " << kReturnBudget.count() << " ms: it waited)";
      fail(text.str());
    } else
      notes.push_back(text.str());
  }

  BehaviorResult result(const std::string &passed) const
  {
    std::string detail;
    for (const auto &list : {failures, notes}) {
      for (const auto &item : list)
        detail += (detail.empty() ? "" : "; ") + item;
    }
    if (failures.empty())
      detail = passed + (detail.empty() ? "" : " (" + detail + ")");
    return {failures.empty(), detail};
  }
};

// With a render held off by a mapped shared array (anariMapArray: the device
// "should not execute any rendering operations" of its parents), rendering
// the frame (its first render), polling it with ANARI_NO_WAIT and querying the
// frame's and the world's properties with ANARI_NO_WAIT all return without
// waiting. After the unmap, anariFrameReady(ANARI_WAIT) returns true.
BehaviorResult checkNoWaitNeverBlocks(anari::Device d,
    anari::World,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  SharedArrayWorld scene(d);
  auto frame = newFrame(d, scene.world.get(), camera, renderer, w, h);
  const auto f = frame.get();
  anari::commitParameters(d, f);

  Findings findings;
  void *mapped = anariMapArray(d, scene.positionArray.get());
  if (mapped != scene.positions.data())
    findings.fail("mapping a shared array didn't return the app's memory");

  findings.returned(
      "anariRenderFrame", timeOf([&]() { anariRenderFrame(d, f); }));

  int ready = -1;
  findings.returned("anariFrameReady(ANARI_NO_WAIT)",
      timeOf([&]() { ready = anariFrameReady(d, f, ANARI_NO_WAIT); }));

  float duration = 0.f;
  findings.returned("frame 'duration' query with ANARI_NO_WAIT", timeOf([&]() {
    anariGetProperty(d,
        f,
        "duration",
        ANARI_FLOAT32,
        &duration,
        sizeof(duration),
        ANARI_NO_WAIT);
  }));

  anari::scenes::Bounds bounds{};
  findings.returned("world 'bounds' query with ANARI_NO_WAIT", timeOf([&]() {
    anariGetProperty(d,
        scene.world.get(),
        "bounds",
        ANARI_FLOAT32_BOX3,
        &bounds,
        sizeof(bounds),
        ANARI_NO_WAIT);
  }));

  int readyAgain = -1;
  findings.returned("anariFrameReady(ANARI_NO_WAIT), again",
      timeOf([&]() { readyAgain = anariFrameReady(d, f, ANARI_NO_WAIT); }));

  anariUnmapArray(d, scene.positionArray.get());

  if (!anariFrameReady(d, f, ANARI_WAIT))
    findings.fail(
        "anariFrameReady(ANARI_WAIT) after the unmap didn't return true");

  // A recommendation ("should not"), so a note rather than a failure: it tells
  // whether the calls above really ran with the render in flight.
  findings.notes.push_back(ready == 0 && readyAgain == 0
          ? "the render was held off while the array was mapped"
          : "the frame was ready while the array was mapped (the render wasn't "
            "held off, so nothing was in flight to wait for)");

  return findings.result("no call waited");
}

// What the completion callback in checkCallbackReentry() saw.
struct CallbackProbe
{
  anari::Array1D positionArray{nullptr};
  const void *positions{nullptr};

  std::atomic<int> calls{0};
  std::atomic<bool> finished{false};
  void *mappedArray{nullptr};
  int readyNoWait{-1};
  int durationNoWait{-1};
  int durationWait{-1};
  float duration{0.f};
  const void *mappedFrame{nullptr};
  uint32_t mappedWidth{0};
  uint32_t mappedHeight{0};
  ANARIDataType mappedType{ANARI_UNKNOWN};
};

void reentrantCallback(const void *userPtr, ANARIDevice d, ANARIFrame f)
{
  auto &p = *static_cast<CallbackProbe *>(const_cast<void *>(userPtr));
  if (p.calls++ != 0)
    return;

  // Map and unmap an array the frame's world uses.
  p.mappedArray = anariMapArray(d, p.positionArray);
  anariUnmapArray(d, p.positionArray);

  p.readyNoWait = anariFrameReady(d, f, ANARI_NO_WAIT);
  p.durationNoWait = anariGetProperty(d,
      f,
      "duration",
      ANARI_FLOAT32,
      &p.duration,
      sizeof(p.duration),
      ANARI_NO_WAIT);
  p.durationWait = anariGetProperty(d,
      f,
      "duration",
      ANARI_FLOAT32,
      &p.duration,
      sizeof(p.duration),
      ANARI_WAIT);

  p.mappedFrame = anariMapFrame(
      d, f, "channel.color", &p.mappedWidth, &p.mappedHeight, &p.mappedType);
  if (p.mappedFrame)
    anariUnmapFrame(d, f, "channel.color");

  p.finished = true;
}

// A frame's completion callback may call the API (all but
// anariFrameReady(ANARI_WAIT), per the spec's note on the callback): map and
// unmap an array the frame's world uses, poll and query its own frame, with and
// without waiting, and map the frame. It runs once, and has finished when
// anariFrameReady(ANARI_WAIT) returns.
BehaviorResult checkCallbackReentry(anari::Device d,
    anari::World,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  SharedArrayWorld scene(d);
  auto frame = newFrame(d, scene.world.get(), camera, renderer, w, h);
  const auto f = frame.get();

  CallbackProbe probe;
  probe.positionArray = scene.positionArray.get();
  probe.positions = scene.positions.data();
  anari::setParameter(d,
      f,
      "frameCompletionCallback",
      (anari::FrameCompletionCallback)reentrantCallback);
  anari::setParameter(
      d, f, "frameCompletionCallbackUserData", static_cast<void *>(&probe));
  anari::commitParameters(d, f);

  anariRenderFrame(d, f);
  // Make no call while the callback may run on another thread: calls on the
  // frame (or device) must not overlap without KHR_DEVICE_SYNCHRONIZATION. A
  // device that runs the callback inside anariFrameReady(ANARI_WAIT) instead
  // gets there after the deadline.
  const auto deadline = std::chrono::steady_clock::now() + kCallbackDeadline;
  while (!probe.finished && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  const int ready = anariFrameReady(d, f, ANARI_WAIT);
  const bool finishedByWait = probe.finished;

  Findings findings;
  if (!ready)
    findings.fail("anariFrameReady(ANARI_WAIT) didn't return true");
  if (probe.calls != 1) {
    findings.fail(
        "the callback ran " + std::to_string(probe.calls.load()) + " times");
  }
  if (!finishedByWait) {
    findings.fail(
        "the callback hadn't finished when anariFrameReady(ANARI_WAIT) "
        "returned");
  } else {
    if (probe.mappedArray != probe.positions) {
      findings.fail(
          "mapping a shared array in the callback didn't return the app's "
          "memory");
    }
    if (!probe.mappedFrame)
      findings.fail("mapping the frame's color channel in the callback failed");
    else if (probe.mappedWidth != w || probe.mappedHeight != h
        || probe.mappedType != ANARI_UFIXED8_RGBA_SRGB) {
      findings.fail(
          "the frame mapped in the callback has the wrong size or type");
    }
    // Neither is required: the spec doesn't say whether the frame counts as
    // ready while its callback runs, and 'duration' is "if available".
    findings.notes.push_back(std::string("in the callback, the frame was ")
        + (probe.readyNoWait == 1 ? "ready" : "not ready") + ", its duration "
        + (probe.durationWait == 1 || probe.durationNoWait == 1
                ? "available"
                : "unavailable"));
  }

  return findings.result("the callback's calls all returned");
}

// Keeps anariRenderFrame from overlapping any other call, which
// KHR_DEVICE_SYNCHRONIZATION still requires ("anariRenderFrame must be
// synchronized with all other ANARI calls as well"), while letting the other
// threads' calls overlap each other and the asynchronous renders. A pending
// render goes ahead of new calls, so the working threads can't starve it.
class RenderFrameGate
{
 public:
  // Runs fn, a group of calls on objects this thread owns, overlapping other
  // threads' calls but no anariRenderFrame.
  void calls(const std::function<void()> &fn)
  {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_changed.wait(lock, [&]() { return !m_renderPending; });
      ++m_activeCalls;
    }
    fn();
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      --m_activeCalls;
    }
    m_changed.notify_all();
  }

  // Calls anariRenderFrame with no other call in progress.
  void renderFrame(anari::Device d, anari::Frame f)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_renderPending = true;
    m_changed.wait(lock, [&]() { return m_activeCalls == 0; });
    anariRenderFrame(d, f);
    m_renderPending = false;
    lock.unlock();
    m_changed.notify_all();
  }

 private:
  std::mutex m_mutex;
  std::condition_variable m_changed;
  int m_activeCalls{0};
  bool m_renderPending{false};
};

// Joins a thread on scope exit, so an early return can't leave one running.
struct JoinOnExit
{
  std::vector<std::thread> &threads;
  ~JoinOnExit()
  {
    for (auto &t : threads) {
      if (t.joinable())
        t.join();
    }
  }
};

// Renders the frame and reads its color channel, with no other thread active.
FrameReadbackResult renderAlone(
    anari::Device d, anari::Frame f, uint32_t w, uint32_t h)
{
  anariRenderFrame(d, f);
  anariFrameReady(d, f, ANARI_WAIT);
  return readFrameChannel(d, f, Channel::Color, w, h);
}

// The concurrent checks compare frames with the self-checks' metric and
// tolerances (ADR-0010), so a renderer need not be deterministic to pass them:
// two frames are equivalent when the changed fraction is within the
// equivalence default, and differ when it reaches the effect default.
const SelfCheckDefaults kSelfCheck;

double framesChanged(const Image &a, const Image &b)
{
  return changedFraction(a, b, kSelfCheck.tau);
}

bool framesEquivalent(const Image &a, const Image &b)
{
  return framesChanged(a, b) <= kSelfCheck.equivalenceMax;
}

bool framesDiffer(const Image &a, const Image &b)
{
  return framesChanged(a, b) >= kSelfCheck.effectMin;
}

std::string percentOf(double fraction)
{
  std::ostringstream text;
  text.setf(std::ios::fixed);
  text.precision(2);
  text << fraction * 100.0 << "%";
  return text.str();
}

// What one working thread in checkDifferentObjectsConcurrently() did.
struct WorkerReport
{
  int iterations{0};
  std::string failure; // the first thing it found wrong
};

// One working thread's loop: build, map, commit, query and release a small
// world of its own, offset along x so its bounds tell it apart from the
// others', until told to stop.
void workOnOwnObjects(anari::Device d,
    RenderFrameGate &gate,
    int index,
    const std::atomic<bool> &stop,
    WorkerReport &report)
{
  const float3 offset(10.f * float(index + 1), 0.f, 0.f);
  const std::vector<float3> layout = layoutTriangleSoup(1);

  while (!stop) {
    gate.calls([&]() {
      // The vertex positions are a managed array the thread writes through
      // a mapping, a little further along on each iteration. (Managed, so the
      // app's memory isn't reused while a released shared array may still be
      // copying it.)
      const float3 shift =
          offset + float3(0.f, 0.f, 0.01f * float(report.iterations % 8));
      auto array = anari::newArray1D(d, ANARI_FLOAT32_VEC3, layout.size());
      auto *mapped = static_cast<float3 *>(anariMapArray(d, array));
      if (!mapped && report.failure.empty())
        report.failure = "mapping its array returned null";
      if (mapped) {
        for (size_t i = 0; i < layout.size(); ++i)
          mapped[i] = layout[i] + shift;
      }
      anariUnmapArray(d, array);

      auto geom = anari::newObject<anari::Geometry>(d, "triangle");
      anari::setAndReleaseParameter(d, geom, "vertex.position", array);
      anari::commitParameters(d, geom);
      auto mat = makeMatteMaterial(d, float3(0.2f, 0.4f, 0.8f));
      auto surface = makeSurface(d, geom, mat);
      WorldContents wc;
      wc.surfaces = {surface};
      auto world = assembleWorld(d, wc);

      anari::scenes::Bounds bounds{};
      const int found = anariGetProperty(d,
          world,
          "bounds",
          ANARI_FLOAT32_BOX3,
          &bounds,
          sizeof(bounds),
          ANARI_WAIT);
      // The world holds only this thread's triangle: its bounds are the
      // triangle's (to a small tolerance for padding), not another thread's.
      float3 lower = layout[0] + shift, upper = lower;
      for (const auto &p : layout) {
        lower = anari::math::min(lower, p + shift);
        upper = anari::math::max(upper, p + shift);
      }
      constexpr float kTolerance = 0.05f;
      bool near = true;
      for (int a = 0; a < 3; ++a) {
        near = near && std::fabs(bounds[0][a] - lower[a]) <= kTolerance
            && std::fabs(bounds[1][a] - upper[a]) <= kTolerance;
      }
      if (report.failure.empty()) {
        if (!found)
          report.failure = "its world's 'bounds' query with ANARI_WAIT failed";
        else if (!near) {
          report.failure =
              "its world's bounds aren't its own triangle's (another "
              "thread's objects leaked into it, or its writes were lost)";
        }
      }

      anari::release(d, world);
      anari::release(d, surface);
      anari::release(d, mat);
      anari::release(d, geom);
    });
    ++report.iterations;
  }
}

// KHR_DEVICE_SYNCHRONIZATION: "different threads [may] operate concurrently
// on different objects within the same device". While one thread renders a
// frame again and again, several others each create a small world, write its
// vertices through a mapped array, commit it, query its bounds and
// release it, over and over. Every frame is equivalent (ADR-0010's changed
// fraction) to the one rendered with nothing else going on, each thread's world
// has its own bounds, and (with failOnDeviceErrors) the device reports no
// ERROR.
BehaviorResult checkDifferentObjectsConcurrently(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  auto frame = newFrame(d, world, camera, renderer, w, h);
  const auto f = frame.get();
  anari::commitParameters(d, f);

  Findings findings;
  const auto reference = renderAlone(d, f, w, h);
  if (!reference) {
    findings.fail(
        "reading the frame rendered alone failed: " + reference.detail);
    return findings.result("");
  }

  RenderFrameGate gate;
  std::atomic<bool> stop{false};
  std::vector<WorkerReport> reports(kWorkerThreads);
  int differing = 0;
  double worstFraction = 0.0;
  std::string readFailure;
  {
    std::vector<std::thread> threads;
    JoinOnExit joiner{threads};
    for (int i = 0; i < kWorkerThreads; ++i) {
      threads.emplace_back(workOnOwnObjects,
          d,
          std::ref(gate),
          i,
          std::cref(stop),
          std::ref(reports[i]));
    }

    for (int i = 0; i < kConcurrentFrames; ++i) {
      gate.renderFrame(d, f);
      // Calls on the frame alone: these may overlap the other threads' calls.
      anariFrameReady(d, f, ANARI_WAIT);
      const auto image = readFrameChannel(d, f, Channel::Color, w, h);
      if (!image) {
        readFailure = image.detail;
        break;
      }
      const double fraction = framesChanged(reference.image, image.image);
      if (!framesEquivalent(reference.image, image.image))
        ++differing;
      worstFraction = std::max(worstFraction, fraction);
    }
    stop = true;
  }

  if (!readFailure.empty())
    findings.fail(
        "reading a frame rendered concurrently failed: " + readFailure);
  if (differing > 0) {
    findings.fail(std::to_string(differing) + " of "
        + std::to_string(kConcurrentFrames)
        + " frames rendered while other threads worked aren't equivalent to "
          "the frame rendered alone (up to "
        + percentOf(worstFraction) + " of pixels changed, more than the "
        + percentOf(kSelfCheck.equivalenceMax) + " allowed)");
  }
  std::string iterations;
  for (int i = 0; i < kWorkerThreads; ++i) {
    if (!reports[i].failure.empty())
      findings.fail("thread " + std::to_string(i) + ": " + reports[i].failure);
    iterations += (i ? ", " : "") + std::to_string(reports[i].iterations);
  }
  findings.notes.push_back(std::to_string(kConcurrentFrames)
      + " frames rendered while " + std::to_string(kWorkerThreads)
      + " threads built and released worlds (" + iterations + " each); at most "
      + percentOf(worstFraction)
      + " of pixels changed from the frame rendered alone");
  return findings.result(
      "every frame was equivalent to the one rendered alone");
}

// A world of kTearTriangles triangles whose vertex positions are a shared
// array over app memory, which checkMapNeverTears() rewrites through a mapping
// between two layouts: the soup as laid out, and the soup moved a little.
struct PositionArrayWorld
{
  std::vector<float3> laidOut = layoutTriangleSoup(kTearTriangles);
  std::vector<float3> moved;
  std::vector<float3> positions = laidOut;
  UniqueAnariObject<anari::Array1D> positionArray;
  UniqueAnariObject<anari::World> world;

  explicit PositionArrayWorld(anari::Device d)
  {
    // Move each triangle by a tenth of the soup's width, enough to change
    // every triangle's pixels and little enough to stay in the framed view.
    float lowX = laidOut[0].x, highX = lowX;
    for (const auto &p : laidOut) {
      lowX = std::min(lowX, p.x);
      highX = std::max(highX, p.x);
    }
    const float3 shift(0.1f * (highX - lowX), 0.f, 0.f);
    for (const auto &p : laidOut)
      moved.push_back(p + shift);

    positionArray = UniqueAnariObject<anari::Array1D>(
        d, anari::newArray1D(d, positions.data(), positions.size()));
    auto geom = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geom, "vertex.position", positionArray.get());
    anari::commitParameters(d, geom);
    auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
    auto surface = makeSurface(d, geom, mat);
    auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
    WorldContents wc;
    wc.surfaces = {surface};
    wc.lights = {light};
    world = UniqueAnariObject<anari::World>(d, assembleWorld(d, wc));
    anari::release(d, geom);
    anari::release(d, mat);
    anari::release(d, surface);
    anari::release(d, light);
  }

  // The vertices of the first half of the triangles, where write() pauses.
  size_t halfTheTriangles() const
  {
    return (laidOut.size() / 6) * 3; // whole triangles
  }

  // Writes one layout through a mapping of the position array, half the
  // triangles at a time. The pause gives a render that ignores the mapping
  // time to read the array half written.
  void write(float3 *mapped, bool movedLayout, bool pause) const
  {
    const auto &from = movedLayout ? moved : laidOut;
    const size_t half = halfTheTriangles();
    std::copy(from.begin(), from.begin() + half, mapped);
    if (pause)
      std::this_thread::sleep_for(500us);
    std::copy(from.begin() + half, from.end(), mapped + half);
  }

  // Writes what a render reading the array halfway through write() would
  // see: the first half of the triangles moved, the rest as laid out.
  void writeTorn(float3 *mapped) const
  {
    const size_t half = halfTheTriangles();
    std::copy(moved.begin(), moved.begin() + half, mapped);
    std::copy(laidOut.begin() + half, laidOut.end(), mapped + half);
  }
};

// anariMapArray: "Applications are only permitted to write to the memory
// visible to ANARIArray objects if all array objects involved are mapped.
// Mapping a shared array object indicates that the device should not execute
// any rendering operations (or internal state updates, such as building
// acceleration structures) of any parent objects". With
// KHR_DEVICE_SYNCHRONIZATION one thread keeps mapping the world's vertex
// position array, rewriting it (the triangles as laid out, then moved, half of
// them at a time) and unmapping it, while another renders frames of that
// world. Every frame is equivalent (ADR-0010's changed fraction) to the world
// rendered alone in one of the two layouts, never a mix; once the writing
// stops, the next frame shows the last layout written.
BehaviorResult checkMapNeverTears(anari::Device d,
    anari::World,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  PositionArrayWorld scene(d);
  const auto array = scene.positionArray.get();
  auto frame = newFrame(d, scene.world.get(), camera, renderer, w, h);
  const auto f = frame.get();
  anari::commitParameters(d, f);

  Findings findings;
  // Renders the world alone after writing the array through a mapping.
  auto renderWritten = [&](const std::function<void(float3 *)> &write) {
    auto *mapped = static_cast<float3 *>(anariMapArray(d, array));
    if (mapped != scene.positions.data())
      findings.fail("mapping a shared array didn't return the app's memory");
    if (mapped)
      write(mapped);
    anariUnmapArray(d, array);
    return renderAlone(d, f, w, h);
  };
  const auto laidOutFrame = renderAlone(d, f, w, h);
  // A torn frame rendered on purpose, to show that the comparison below
  // would see one: it must differ from both layouts.
  const auto tornFrame = renderWritten([&](float3 *p) { scene.writeTorn(p); });
  const auto movedFrame =
      renderWritten([&](float3 *p) { scene.write(p, true, false); });
  for (const auto *alone : {&laidOutFrame, &tornFrame, &movedFrame}) {
    if (!*alone) {
      findings.fail("reading a frame rendered alone failed: " + alone->detail);
      return findings.result("");
    }
  }
  if (!framesDiffer(laidOutFrame.image, movedFrame.image)) {
    findings.fail(
        "the frames rendered alone before and after moving the triangles "
        "through the mapping differ by less than the effect tolerance (the "
        "render didn't use the positions written)");
    return findings.result("");
  }
  const double tornFromLaidOut =
      framesChanged(laidOutFrame.image, tornFrame.image);
  const double tornFromMoved = framesChanged(movedFrame.image, tornFrame.image);
  findings.notes.push_back("a torn frame rendered alone changes "
      + percentOf(tornFromLaidOut) + " of pixels from the laid-out layout and "
      + percentOf(tornFromMoved) + " from the moved one");
  if (!framesDiffer(laidOutFrame.image, tornFrame.image)
      || !framesDiffer(movedFrame.image, tornFrame.image)) {
    findings.fail(
        "a torn frame rendered alone differs from one of the layouts by less "
        "than the effect tolerance, so the check couldn't tell it apart");
    return findings.result("");
  }

  RenderFrameGate gate;
  std::atomic<bool> stop{false};
  std::atomic<int> rewrites{0};
  std::atomic<bool> writerMappedOwnMemory{true};
  std::mutex issuedMutex;
  std::condition_variable issuedChanged;
  int framesIssued = 0;
  bool lastMoved = true;
  int laidOutFrames = 0, movedFrames = 0, torn = 0;
  // Of the torn frames, how close the closest one came to either layout.
  double closestTorn = 1.0;
  std::string readFailure;
  {
    std::vector<std::thread> threads;
    JoinOnExit joiner{threads};
    // One rewrite per render, started once the render is issued, so it
    // overlaps the render in flight. Rewriting back to back instead would keep
    // the array mapped nearly all the time and could starve the renders,
    // which (per anariMapArray) wait for the unmap.
    threads.emplace_back([&]() {
      bool moveNext = false;
      int rewritten = 0;
      while (true) {
        {
          std::unique_lock<std::mutex> lock(issuedMutex);
          issuedChanged.wait(
              lock, [&]() { return stop || framesIssued > rewritten; });
          if (stop)
            break;
          rewritten = framesIssued;
        }
        // Map, write and unmap as one group, so the array is never left
        // mapped while an anariRenderFrame call waits to go through the gate.
        gate.calls([&]() {
          auto *p = static_cast<float3 *>(anariMapArray(d, array));
          if (p != scene.positions.data())
            writerMappedOwnMemory = false;
          if (p)
            scene.write(p, moveNext, true);
          anariUnmapArray(d, array);
        });
        lastMoved = moveNext;
        moveNext = !moveNext;
        ++rewrites;
      }
    });

    for (int i = 0; i < kConcurrentFrames; ++i) {
      gate.renderFrame(d, f);
      {
        std::lock_guard<std::mutex> lock(issuedMutex);
        ++framesIssued;
      }
      issuedChanged.notify_all();
      anariFrameReady(d, f, ANARI_WAIT);
      const auto image = readFrameChannel(d, f, Channel::Color, w, h);
      if (!image) {
        readFailure = image.detail;
        break;
      }
      if (framesEquivalent(laidOutFrame.image, image.image))
        ++laidOutFrames;
      else if (framesEquivalent(movedFrame.image, image.image))
        ++movedFrames;
      else {
        ++torn;
        closestTorn = std::min(closestTorn,
            std::min(framesChanged(laidOutFrame.image, image.image),
                framesChanged(movedFrame.image, image.image)));
      }
    }
    {
      std::lock_guard<std::mutex> lock(issuedMutex);
      stop = true;
    }
    issuedChanged.notify_all();
  }

  if (!readFailure.empty())
    findings.fail(
        "reading a frame rendered concurrently failed: " + readFailure);
  if (!writerMappedOwnMemory)
    findings.fail("mapping a shared array didn't return the app's memory");
  if (torn > 0) {
    findings.fail(std::to_string(torn) + " of "
        + std::to_string(kConcurrentFrames)
        + " frames are equivalent to neither layout rendered alone (a render "
          "read the array while it was mapped and half written; the closest "
          "changes "
        + percentOf(closestTorn) + " of pixels from a layout)");
  }

  const auto last = renderAlone(d, f, w, h);
  const auto &expected = lastMoved ? movedFrame : laidOutFrame;
  if (!last)
    findings.fail(
        "reading the frame after the writing stopped failed: " + last.detail);
  else if (!framesEquivalent(expected.image, last.image)) {
    findings.fail(std::string("after the writing stopped, the frame isn't the ")
        + (lastMoved ? "moved" : "laid-out")
        + " layout last written through the mapping");
  }

  findings.notes.push_back(std::to_string(laidOutFrames) + " laid-out and "
      + std::to_string(movedFrames) + " moved frames while the array was "
      + "rewritten " + std::to_string(rewrites.load()) + " times");
  return findings.result("no frame was torn");
}

} // namespace

void registerSynchronizationTests(Catalog &catalog)
{
  makeTest("synchronization", "no_wait_never_blocks")
      .description(
          "With a render held off by a mapped shared array, "
          "anariRenderFrame, anariFrameReady(ANARI_NO_WAIT) and ANARI_NO_WAIT "
          "property queries on the frame and world return without waiting; "
          "after the unmap, anariFrameReady(ANARI_WAIT) returns true.")
      .build(triangleWorld)
      .behavior(checkNoWaitNeverBlocks)
      .timeout(kCaseTimeout)
      .failOnDeviceErrors()
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("synchronization", "completion_callback_reentry")
      .description(
          "A frame's completion callback maps and unmaps an array the "
          "frame's world uses, polls and queries its own frame (with and "
          "without waiting) and maps it; the callback runs once and has "
          "finished when anariFrameReady(ANARI_WAIT) returns.")
      .build(triangleWorld)
      .behavior(checkCallbackReentry)
      .timeout(kCaseTimeout)
      .failOnDeviceErrors()
      .covers("ANARI_KHR_FRAME_COMPLETION_CALLBACK")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("synchronization", "different_objects_concurrently")
      .description(
          "KHR_DEVICE_SYNCHRONIZATION, spec 3.1: \"This lowers the scope of "
          "required synchronization to individual ANARI objects allowing "
          "different threads to operate concurrently on different objects "
          "within the same device.\" While one thread renders a frame "
          "repeatedly (anariRenderFrame kept apart from every other call, as "
          "3.1 still requires), four threads each create a world, write its "
          "vertices through a mapped array, commit it, query its "
          "bounds with ANARI_WAIT and release it, over and over. Every frame "
          "is equivalent to the frame rendered alone (changed fraction within "
          "the self-checks' equivalence tolerance), each world's bounds are "
          "its own, and the device reports no ERROR.")
      .build(triangleWorld)
      .behavior(checkDifferentObjectsConcurrently)
      .timeout(kCaseTimeout)
      .failOnDeviceErrors()
      .covers("ANARI_KHR_DEVICE_SYNCHRONIZATION")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("synchronization", "map_during_render_never_tears")
      .description(
          "anariMapArray: \"Applications are only permitted to write to the "
          "memory visible to ANARIArray objects if all array objects involved "
          "are mapped. Mapping a shared array object indicates that the "
          "device should not execute any rendering operations ... of any "
          "parent objects\". With KHR_DEVICE_SYNCHRONIZATION, one thread "
          "maps the world's vertex position array, rewrites it (16 "
          "triangles as laid out, then moved, half of them at a time) and "
          "unmaps it, over and over, while another renders frames of the "
          "world. Every frame is equivalent to one of the two layouts rendered "
          "alone (changed fraction within the self-checks' equivalence "
          "tolerance), never a mix, which a torn frame rendered alone must "
          "differ from by the effect tolerance; after the writing stops the "
          "frame shows the last layout written.")
      .build([](BuildContext &ctx) {
        return triangleSoupWorld(ctx, kTearTriangles);
      })
      .behavior(checkMapNeverTears)
      .timeout(kCaseTimeout)
      .failOnDeviceErrors()
      .covers("ANARI_KHR_DEVICE_SYNCHRONIZATION")
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
