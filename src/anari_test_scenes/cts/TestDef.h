// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Axis.h"
#include "BuildContext.h"
#include "Channel.h"
#include "FrameSize.h"
#include "Image.h"
#include "RendererParams.h"
#include "anari_test_scenes.h" // anari::scenes::Bounds / Camera
// anari
#include "anari/anari_cpp.hpp"
// std
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// A test's world-build function: reads the current Case's axis values from the
// context, authors an ANARI world with normal ANARI C++ calls plus the shared
// world-build helpers, and returns the World. The harness owns the returned
// World and releases it.
using BuildFn = std::function<anari::World(BuildContext &)>;

// Optional per-Case camera builder. By default the runner frames a perspective
// camera from the world's bounds; a Test that exercises camera parameters
// supplies one of these instead, reading axis values from the context and the
// world bounds the runner computed. Returns a committed camera the runner owns.
using CameraFn =
    std::function<anari::Camera(BuildContext &, const anari::scenes::Bounds &)>;

// Optional per-Case renderer configuration. The runner creates the configured
// renderer subtype and applies its baseline parameters first; a Test that
// exercises renderer parameters (ambient, background) can then override them.
// The runner commits and owns the renderer.
using RendererFn = std::function<void(BuildContext &, anari::Renderer)>;

// The outcome of a behavioral check: a self-contained pass/fail with a
// human-readable detail. Distinct from render-and-compare scoring.
struct BehaviorResult
{
  bool passed{false};
  std::string detail;
};

// Optional per-Case behavioral check, for conformance guarantees the
// render-and-compare path can't express (e.g. a frame-completion callback
// firing, progressive accumulation converging). When a Test sets one, the
// runner builds the world/camera/renderer, calls this instead of rendering and
// scoring against ground truth, and records the returned verdict + detail in
// the sidecar. No ground truth is generated or needed for such a Test.
using BehaviorFn = std::function<BehaviorResult(anari::Device,
    anari::World,
    anari::Camera,
    anari::Renderer,
    uint32_t width,
    uint32_t height)>;

// The renders a derived expectation is computed from (ADR-0010): the Case's
// siblings on the axis (the Cases holding the same values on every other
// axis), one Channel at a time, and the Case's own axis values.
class Siblings
{
 public:
  using Lookup = std::function<const Image &(const std::string &value)>;

  // It keeps a reference to `values`: no temporaries.
  Siblings(Channel channel, Lookup lookup, const BuildContext &values)
      : m_channel(channel), m_lookup(std::move(lookup)), m_values(values)
  {}
  Siblings(Channel, Lookup, BuildContext &&) = delete;

  // The image of the sibling holding `value` on the axis (its string form,
  // as in Case ids: "none" for unset). Throws when there is no such sibling
  // or it did not render; the expectation then fails with that reason.
  const Image &at(const std::string &value) const
  {
    return m_lookup(value);
  }

  // The Channel being compared.
  Channel channel() const
  {
    return m_channel;
  }

  // The Case's axis values (shared by its siblings on every other axis), for
  // an expectation that depends on them: values().get<float>("u", 2.f).
  const BuildContext &values() const
  {
    return m_values;
  }

 private:
  Channel m_channel;
  Lookup m_lookup;
  const BuildContext &m_values;
};

// A pure image function over a Case's siblings: the image (and optional mask)
// the Case must match within the equivalence tolerance.
using DerivedFn = std::function<ExpectedImage(const Siblings &)>;

// What one value of an axis must look like, derived from its siblings
// (ADR-0010): `.expectDerived(axis, value, description, fn)`. A Variant
// axis's non-first values carry the identity expectation implicitly.
struct DerivedExpectation
{
  std::string axis;
  std::string value; // anyToString() form of the axis value
  std::string description; // one line: what the expected image is
  DerivedFn fn;
};

class ExactCheck; // ExactCheck.h

// An exact check (ADR-0010): a free-form per-Case function over the Case's
// raw channel values, reporting through the ExactCheck's helpers.
using ExactFn = std::function<void(ExactCheck &)>;

// `.expectExact(label, fn)`: run on every rendered Case of the Test.
struct ExactCheckDef
{
  std::string label;
  ExactFn fn;
};

// Whether a Test compares its Cases against Ground truth (ADR-0010).
enum class GroundTruthUse
{
  // The default: fully self-checked, which is also `.noGroundTruth()`;
  // `generate` skips the Test.
  None,
  // `.groundTruth(reason)`: needs a Reference device comparison, and why.
  Required,
};

// `.noEffectCheckOn`: an axis opted out of its effect check on the devices
// whose library is `library` (and renderer subtype one of `renderers`, any
// when empty), because they are known not to render the parameter.
struct DeviceEffectOptOut
{
  std::string axis;
  std::string library;
  std::vector<std::string> renderers;
  std::string reason;
};

// Per-axis overrides of the self-check tolerances (ADR-0010). They change the
// numbers, never the metric (changed fraction).
struct EquivalenceTolerance
{
  double maxFraction{0.0};
  int tau{0}; // per-pixel tolerance, in 8-bit levels
};

// An extension only some Cases of a Test need: those holding `value` (its
// anyToString() form) on `axis` (`.requireFeatureFor`).
struct ValueRequirement
{
  std::string axis;
  std::string value;
  std::string feature;
};

// A single registered definition in the Catalog: a human-readable description,
// world-build function, axes, required features, thresholds, and bounds
// tolerance. A Test expands into one or more Cases.
struct TestDef
{
  std::string category;
  std::string name;
  std::string description;
  BuildFn build;
  CameraFn cameraBuild; // empty -> runner frames camera from world bounds
  RendererFn rendererConfig; // empty -> runner keeps its renderer baseline
  // Optional renderer parameters (`.rendererParam`): set on the Case's
  // renderer only when the renderer subtype lists the parameter.
  std::vector<RendererParam> rendererParams;
  FrameSizeFn frameSize; // empty -> every Case renders at the run's size
  BehaviorFn behaviorCheck; // set -> verify behavior instead of render+compare
  // For a behavior check that may hang the device: when non-zero, `anariCts
  // run` runs each Case in a child process and records it as failed if it
  // doesn't finish within this time (ADR-0009).
  std::chrono::milliseconds timeout{0};
  // Fail a behavior check's Case if the device reports an ERROR while the
  // check runs: the check only makes calls the spec allows (ADR-0009).
  bool failOnDeviceErrors{false};
  std::vector<Axis> axes;
  // Every extension the Test needs; the skip gate (isSupported).
  std::vector<std::string> requiredFeatures;
  // Extensions only some Cases need, in declaration order. A device lacking
  // one skips exactly the Cases holding the value; see requiredFeaturesOf().
  std::vector<ValueRequirement> valueRequirements;
  // The primary extensions: those the Test is evidence for, a subset of
  // requiredFeatures. The rest of requiredFeatures are support extensions it
  // merely needs to build a scene (a triangle to light, a matte material to
  // sample into). The report attributes a Case's verdict to its primary
  // extensions only. Empty for a Test of core behaviour.
  std::vector<std::string> coveredFeatures;
  std::map<std::string, double>
      thresholds; // metric name -> threshold (all channels)
  // Per-channel metric thresholds, overriding `thresholds` for that channel
  // only. Most tests compare every channel against one threshold; a test that
  // needs a stricter (or looser) bar on a specific channel sets it here.
  std::map<Channel, std::map<std::string, double>> channelThresholds;
  double boundsTolerance{0.0};
  std::vector<Channel> channels{Channel::Color};
  // When set, expand one-factor-at-a-time instead of the full cartesian
  // product: a baseline of every axis's first value, plus one Case per
  // additional value of each axis.
  bool simplified{false};

  // Self-checks (ADR-0010). A Permutation axis implies an effect check unless
  // it is opted out here (axis -> reason); a Variant axis implies an
  // equivalence check.
  std::map<std::string, std::string> noEffectCheck;
  // Opt-outs that hold only on some devices.
  std::vector<DeviceEffectOptOut> noEffectCheckOn;
  // Per-axis minimum changed fraction for an effect (overrides the default).
  std::map<std::string, double> effectMargins;
  // Per-axis equivalence tolerance (overrides the default); it also applies
  // to the axis's derived expectations.
  std::map<std::string, EquivalenceTolerance> equivalenceTolerances;
  // Axis values whose image is derived from their siblings'; such a value
  // takes no part in its axis's effect pairs.
  std::vector<DerivedExpectation> derivedExpectations;
  // Per-Case assertions over raw channel values, in declaration order.
  std::vector<ExactCheckDef> exactChecks;
  // Whether the Test's Cases are compared against Ground truth, and why.
  GroundTruthUse groundTruthUse{GroundTruthUse::None};
  std::string groundTruthReason;

  // Whether the runner compares this Test's Cases against Ground truth (a
  // behaviour Test never does).
  bool usesGroundTruth() const
  {
    return !behaviorCheck && groundTruthUse == GroundTruthUse::Required;
  }

  std::string id() const
  {
    return category + "/" + name;
  }

  // Whether `axis` is a Variant axis of this Test.
  bool isVariant(const std::string &axis) const
  {
    for (const auto &a : axes)
      if (a.name == axis)
        return a.kind == AxisKind::Variant;
    return false;
  }

  // The derived expectation declared for `value` of `axis`, or null.
  const DerivedExpectation *derivedFor(
      const std::string &axis, const std::string &value) const
  {
    for (const auto &d : derivedExpectations)
      if (d.axis == axis && d.value == value)
        return &d;
    return nullptr;
  }

  double thresholdOr(const std::string &metric, double valIfNotFound) const
  {
    auto it = thresholds.find(metric);
    return it != thresholds.end() ? it->second : valIfNotFound;
  }

  // The threshold to apply for a metric on a specific channel: a per-channel
  // override if one is set, else the test-wide metric threshold, else the
  // supplied default (the runner's RunOptions default).
  double thresholdFor(
      Channel channel, const std::string &metric, double valIfNotFound) const
  {
    auto cit = channelThresholds.find(channel);
    if (cit != channelThresholds.end()) {
      auto mit = cit->second.find(metric);
      if (mit != cit->second.end())
        return mit->second;
    }
    return thresholdOr(metric, valIfNotFound);
  }
};

} // namespace cts
} // namespace anari
