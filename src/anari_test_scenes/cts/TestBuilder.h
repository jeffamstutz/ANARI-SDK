// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Catalog.h"
#include "TestDef.h"
#include "Value.h"
// std
#include <chrono>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

// Fluent builder for a TestDef. Note: the conformance verb is requireFeatures()
// rather than `requires`, which is a reserved keyword in C++20.
struct TestBuilder
{
  TestBuilder(std::string category, std::string name);

  // Summarize the test's primary conformance check for human readers.
  TestBuilder &description(std::string text);
  TestBuilder &build(BuildFn fn);

  // Supply a custom render camera, or configure the renderer after the runner
  // applies its selected subtype and baseline parameters.
  TestBuilder &camera(CameraFn fn);
  TestBuilder &renderer(RendererFn fn);
  // Render each Case at a size derived from the run's (ADR-0010); without
  // it every Case renders at the run's size.
  TestBuilder &frameSize(FrameSizeFn fn);

  // Verify device behavior with a bespoke check instead of rendering and
  // comparing against ground truth (e.g. a completion callback firing). A Test
  // with a behavior check generates no ground truth.
  TestBuilder &behavior(BehaviorFn fn);
  // Run each Case of a behavior check in its own process, failing it if it
  // doesn't finish in time (a hang), and fail it on a device ERROR
  // (ADR-0009).
  TestBuilder &timeout(std::chrono::milliseconds limit);
  TestBuilder &failOnDeviceErrors();

  // Permutation axis: values must produce different output. Cases differing
  // only in this axis must render differently on the Candidate itself: an
  // effect check, on by default (ADR-0010). A value the spec says renders like
  // another (an unset `filter`, which is the default `linear`) is a derived
  // expectation (expectDerived), not a Permutation value of its own. A Test
  // that declares `.groundTruth()` gives each value its own Ground truth.
  TestBuilder &permute(std::string axis, std::vector<Any> values);
  template <typename T>
  TestBuilder &permute(std::string axis, std::initializer_list<T> values)
  {
    return permute(std::move(axis), toAnyVector(values));
  }

  // Variant axis: values must produce identical output. They share one Ground
  // truth (written from the first value's Case), and every Case is compared
  // with the first value's Case on the Candidate itself: an equivalence check
  // (ADR-0010).
  TestBuilder &variant(std::string axis, std::vector<Any> values);
  template <typename T>
  TestBuilder &variant(std::string axis, std::initializer_list<T> values)
  {
    return variant(std::move(axis), toAnyVector(values));
  }

  TestBuilder &simplified(bool on = true);

  // Self-checks (ADR-0010) ///////////////////////////////////////////////////

  // Opt a Permutation axis out of its effect check, for an axis whose effect
  // can't be judged by image difference. The reason is recorded in the
  // Sidecar. An axis whose values must *not* change the image is a Variant.
  TestBuilder &noEffectCheck(std::string axis, std::string reason);
  // The minimum changed fraction an effect of this axis must reach.
  TestBuilder &effectMargin(std::string axis, double minFraction);
  // The maximum changed fraction (and per-pixel tolerance, in 8-bit levels)
  // an equivalence of this Variant axis allows.
  TestBuilder &equivalenceTolerance(
      std::string axis, double maxFraction, int tau);
  // What `value` (its string form, as in Case ids) of `axis` must render:
  // an image function over its siblings (the Cases holding the same values
  // on every other axis), compared with the equivalence tolerance. The
  // description says in one line what the expected image is. The value takes
  // no part in the axis's effect pairs. Throws std::invalid_argument for an
  // undeclared axis, a value the axis doesn't hold, or a repeated value.
  //   .expectDerived("stereoMode", "sideBySide", "left and right",
  //       [](const Siblings &s) {
  //         return hconcat(s.at("left"), s.at("right"));
  //       })
  TestBuilder &expectDerived(std::string axis,
      std::string value,
      std::string description,
      DerivedFn fn);
  // An exact check (ADR-0010): `fn` runs on every rendered Case with the
  // Case's axis values, frame size, raw channel values (colour, float depth,
  // uint32_t IDs) and camera projection, and reports through the
  // ExactCheck's helpers (ExactCheck.h). Any number per Test; the label names
  // it in the Sidecar and the report. Throws std::invalid_argument for an
  // empty or repeated label, or on a behaviour Test.
  //   .expectExact("ID at each centroid", [](ExactCheck &x) {
  //     x.matches("triangle 0", x.ids(Channel::PrimitiveId), ...);
  //   })
  TestBuilder &expectExact(std::string label, ExactFn fn);
  // The Test needs a Reference device comparison; say why (appearance with no
  // analytic answer).
  TestBuilder &groundTruth(std::string reason);
  // The Test is fully self-checked: no Ground truth is generated or compared.
  TestBuilder &noGroundTruth();

  TestBuilder &requireFeatures(std::vector<std::string> features);
  TestBuilder &requireFeature(std::string feature);
  // Only the Cases holding `value` on `axis` need `feature`: a device lacking
  // it skips exactly those Cases, naming the extension, and the others run.
  // The skipped Cases take no part in the Test's effect or equivalence checks
  // (their partners are not failed for their absence). Use it for an
  // extension a few values need (an image sampler a material Case binds); one
  // every Case needs goes in requireFeature(). Throws std::invalid_argument
  // for an undeclared axis (declare it first) or a value it doesn't hold.
  //   .permute("color", V{unsetBinding(), sampler(0)})
  //   .requireFeatureFor("color", sampler(0), "ANARI_KHR_SAMPLER_IMAGE2D")
  TestBuilder &requireFeatureFor(
      std::string axis, const Any &value, std::string feature);
  // Name a primary extension: one the Test is evidence for (call once per
  // extension). It is also required. Support extensions the Test merely needs
  // to build its scene go in requireFeatures().
  TestBuilder &covers(std::string feature);
  // Test-wide metric threshold (applies to every channel).
  TestBuilder &threshold(std::string metric, double value);
  // Per-channel metric threshold, overriding the test-wide one for that
  // channel.
  TestBuilder &threshold(Channel channel, std::string metric, double value);
  TestBuilder &boundsTolerance(double tolerance);
  TestBuilder &channels(std::vector<Channel> ch);

  const TestDef &def() const
  {
    return m_def;
  }

  TestDef take()
  {
    return std::move(m_def);
  }

  // Register the finished Test into a catalog (the global one by default).
  const TestDef &registerInto(Catalog &catalog);
  const TestDef &registerInto();

 private:
  template <typename T>
  static std::vector<Any> toAnyVector(std::initializer_list<T> values)
  {
    std::vector<Any> out;
    out.reserve(values.size());
    for (const auto &v : values)
      out.emplace_back(Any(v));
    return out;
  }

  TestDef m_def;
};

inline TestBuilder makeTest(std::string category, std::string name)
{
  return TestBuilder(std::move(category), std::move(name));
}

} // namespace cts
} // namespace anari
