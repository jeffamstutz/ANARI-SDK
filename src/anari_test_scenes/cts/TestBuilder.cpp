// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TestBuilder.h"
#include "Value.h"
// std
#include <algorithm>
#include <stdexcept>

namespace anari {
namespace cts {

TestBuilder::TestBuilder(std::string category, std::string name)
{
  m_def.category = std::move(category);
  m_def.name = std::move(name);
}

TestBuilder &TestBuilder::description(std::string text)
{
  m_def.description = std::move(text);
  return *this;
}

TestBuilder &TestBuilder::build(BuildFn fn)
{
  m_def.build = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::camera(CameraFn fn)
{
  m_def.cameraBuild = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::renderer(RendererFn fn)
{
  m_def.rendererConfig = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::rendererParam(std::string name, std::string value)
{
  m_def.rendererParams.push_back({std::move(name), std::move(value)});
  return *this;
}

TestBuilder &TestBuilder::frameSize(FrameSizeFn fn)
{
  m_def.frameSize = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::behavior(BehaviorFn fn)
{
  if (!m_def.exactChecks.empty())
    throw std::invalid_argument("test `" + m_def.id()
        + "`: a behaviour Test has no exact checks (ADR-0010)");
  m_def.behaviorCheck = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::timeout(std::chrono::milliseconds limit)
{
  m_def.timeout = limit;
  return *this;
}

TestBuilder &TestBuilder::failOnDeviceErrors()
{
  m_def.failOnDeviceErrors = true;
  return *this;
}

TestBuilder &TestBuilder::permute(std::string axis, std::vector<Any> values)
{
  m_def.axes.push_back(
      Axis{std::move(axis), std::move(values), AxisKind::Permutation});
  return *this;
}

TestBuilder &TestBuilder::variant(std::string axis, std::vector<Any> values)
{
  m_def.axes.push_back(
      Axis{std::move(axis), std::move(values), AxisKind::Variant});
  return *this;
}

TestBuilder &TestBuilder::simplified(bool on)
{
  m_def.simplified = on;
  return *this;
}

TestBuilder &TestBuilder::noEffectCheckOn(std::string axis,
    std::string library,
    std::vector<std::string> renderers,
    std::string reason)
{
  m_def.noEffectCheckOn.push_back({std::move(axis),
      std::move(library),
      std::move(renderers),
      std::move(reason)});
  return *this;
}

TestBuilder &TestBuilder::noEffectCheck(std::string axis, std::string reason)
{
  m_def.noEffectCheck[std::move(axis)] = std::move(reason);
  return *this;
}

TestBuilder &TestBuilder::effectMargin(std::string axis, double minFraction)
{
  m_def.effectMargins[std::move(axis)] = minFraction;
  return *this;
}

TestBuilder &TestBuilder::equivalenceTolerance(
    std::string axis, double maxFraction, int tau)
{
  m_def.equivalenceTolerances[std::move(axis)] =
      EquivalenceTolerance{maxFraction, tau};
  return *this;
}

TestBuilder &TestBuilder::expectDerived(
    std::string axis, std::string value, std::string description, DerivedFn fn)
{
  const std::string where = "test `" + m_def.id() + "`: expectDerived(`" + axis
      + "`, `" + value + "`): ";
  const auto a = std::find_if(m_def.axes.begin(),
      m_def.axes.end(),
      [&](const Axis &x) { return x.name == axis; });
  if (a == m_def.axes.end())
    throw std::invalid_argument(where + "no such axis (declare it first)");
  if (std::none_of(a->values.begin(), a->values.end(), [&](const Any &v) {
        return anyToString(v) == value;
      }))
    throw std::invalid_argument(where + "the axis has no such value");
  if (m_def.derivedFor(axis, value))
    throw std::invalid_argument(where + "already declared");
  if (description.empty() || !fn)
    throw std::invalid_argument(where + "needs a description and a function");
  m_def.derivedExpectations.push_back(DerivedExpectation{std::move(axis),
      std::move(value),
      std::move(description),
      std::move(fn)});
  return *this;
}

TestBuilder &TestBuilder::expectExact(std::string label, ExactFn fn)
{
  const std::string where = "test `" + m_def.id() + "`: ";
  if (m_def.behaviorCheck)
    throw std::invalid_argument(
        where + "a behaviour Test has no exact checks (ADR-0010)");
  if (label.empty())
    throw std::invalid_argument(where + "an exact check needs a label");
  for (const auto &check : m_def.exactChecks)
    if (check.label == label)
      throw std::invalid_argument(
          where + "exact check `" + label + "` is declared twice");
  if (!fn)
    throw std::invalid_argument(
        where + "exact check `" + label + "` has no function");
  m_def.exactChecks.push_back({std::move(label), std::move(fn)});
  return *this;
}

TestBuilder &TestBuilder::groundTruth(std::string reason)
{
  m_def.groundTruthUse = GroundTruthUse::Required;
  m_def.groundTruthReason = std::move(reason);
  return *this;
}

TestBuilder &TestBuilder::noGroundTruth()
{
  m_def.groundTruthUse = GroundTruthUse::None;
  m_def.groundTruthReason.clear();
  return *this;
}

TestBuilder &TestBuilder::requireFeatures(std::vector<std::string> features)
{
  for (auto &f : features)
    requireFeature(std::move(f));
  return *this;
}

TestBuilder &TestBuilder::requireFeature(std::string feature)
{
  auto &required = m_def.requiredFeatures;
  if (std::find(required.begin(), required.end(), feature) == required.end())
    required.push_back(std::move(feature));
  return *this;
}

TestBuilder &TestBuilder::requireFeatureFor(
    std::string axis, const Any &value, std::string feature)
{
  const std::string text = anyToString(value);
  const std::string where = "test `" + m_def.id() + "`: requireFeatureFor(`"
      + axis + "`, `" + text + "`): ";
  const auto a = std::find_if(m_def.axes.begin(),
      m_def.axes.end(),
      [&](const Axis &x) { return x.name == axis; });
  if (a == m_def.axes.end())
    throw std::invalid_argument(where + "no such axis (declare it first)");
  if (std::none_of(a->values.begin(), a->values.end(), [&](const Any &v) {
        return anyToString(v) == text;
      }))
    throw std::invalid_argument(where + "the axis has no such value");
  if (feature.empty())
    throw std::invalid_argument(where + "needs a feature");
  for (const auto &r : m_def.valueRequirements)
    if (r.axis == axis && r.value == text && r.feature == feature)
      return *this;
  m_def.valueRequirements.push_back(
      {std::move(axis), text, std::move(feature)});
  return *this;
}

TestBuilder &TestBuilder::covers(std::string feature)
{
  auto &covered = m_def.coveredFeatures;
  if (std::find(covered.begin(), covered.end(), feature) == covered.end())
    covered.push_back(feature);
  return requireFeature(std::move(feature));
}

TestBuilder &TestBuilder::threshold(std::string metric, double value)
{
  m_def.thresholds[std::move(metric)] = value;
  return *this;
}

TestBuilder &TestBuilder::threshold(
    Channel channel, std::string metric, double value)
{
  m_def.channelThresholds[channel][std::move(metric)] = value;
  return *this;
}

TestBuilder &TestBuilder::boundsTolerance(double tolerance)
{
  m_def.boundsTolerance = tolerance;
  return *this;
}

TestBuilder &TestBuilder::channels(std::vector<Channel> ch)
{
  m_def.channels = std::move(ch);
  return *this;
}

const TestDef &TestBuilder::registerInto(Catalog &catalog)
{
  return catalog.add(m_def);
}

const TestDef &TestBuilder::registerInto()
{
  return globalCatalog().add(m_def);
}

} // namespace cts
} // namespace anari
