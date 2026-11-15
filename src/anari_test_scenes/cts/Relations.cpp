// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Relations.h"
#include "Metrics.h"
#include "Value.h"
// std
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace anari {
namespace cts {

namespace {

std::string percent(double fraction)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(2) << fraction * 100.0 << " %";
  return os.str();
}

std::string sizeOf(const Image &image)
{
  return std::to_string(image.width) + "x" + std::to_string(image.height);
}

// Why a partner's images can't be used, or empty when they can.
std::string partnerProblem(
    const TestDef &test, const Case &partner, const CaseRender &render)
{
  switch (render.state) {
  case CaseRender::State::Rendered:
    if (render.images.size() != test.channels.size())
      return "partner `" + partner.id() + "` has no image";
    return {};
  case CaseRender::State::Failed:
    return "partner `" + partner.id() + "` did not render: " + render.error;
  case CaseRender::State::Skipped:
    return "partner `" + partner.id() + "` was skipped: " + render.error;
  case CaseRender::State::Absent:
    return "partner `" + partner.id() + "` was not rendered";
  }
  return {};
}

// A failed assertion whose images could not be compared.
void notCompared(RelationOutcome &out, std::string detail)
{
  out.assertion.passed = false;
  out.assertion.score = std::numeric_limits<double>::quiet_NaN();
  out.assertion.detail = std::move(detail);
}

// What one Channel of a Case is compared against: an image, and the mask
// that limits the comparison (null: every pixel).
struct Reference
{
  const Image *image{nullptr};
  const Image *mask{nullptr};
};

enum class Bound
{
  AtLeast, // an effect
  AtMost, // an equivalence
};

// The comparison both kinds share. Scores `actual` against `reference`
// Channel by Channel and judges the most-changed Channel against
// out.assertion.threshold: an effect shows when any Channel changed enough,
// an equivalence holds when the worst one stayed within it. `versus` names
// what was compared against, for the detail.
void judge(RelationOutcome &out,
    const TestDef &test,
    const std::vector<Reference> &reference,
    const std::vector<Image> &actual,
    int tau,
    Bound bound,
    const std::string &versus)
{
  const size_t n = test.channels.size();
  std::vector<double> fractions(n);
  std::vector<int> taus(n);
  for (size_t i = 0; i < n; ++i) {
    const Image &ref = *reference[i].image;
    const Image *mask = reference[i].mask;
    const char *channel = channelName(test.channels[i]);
    taus[i] = channelTau(test.channels[i], tau);
    fractions[i] = mask ? changedFraction(ref, actual[i], taus[i], *mask)
                        : changedFraction(ref, actual[i], taus[i]);
    if (!std::isnan(fractions[i]))
      continue;
    if (!ref.valid() || !actual[i].valid() || ref.width != actual[i].width
        || ref.height != actual[i].height) {
      notCompared(out,
          std::string("images are not comparable: ") + channel + " is "
              + sizeOf(actual[i]) + " vs " + sizeOf(ref));
    } else if (!mask->valid() || mask->width != ref.width
        || mask->height != ref.height) {
      notCompared(out,
          std::string("the mask of ") + channel + " is " + sizeOf(*mask)
              + " vs " + sizeOf(ref));
    } else {
      notCompared(
          out, std::string("the mask of ") + channel + " selects no pixel");
    }
    return;
  }

  size_t best = 0;
  for (size_t i = 1; i < n; ++i)
    if (fractions[i] > fractions[best])
      best = i;
  const Image *mask = reference[best].mask;
  out.channelIndex = static_cast<int>(best);
  out.assertion.channel = channelName(test.channels[best]);
  out.assertion.score = fractions[best];
  out.assertion.tau = taus[best];
  out.assertion.passed = bound == Bound::AtLeast
      ? out.assertion.score >= out.assertion.threshold
      : out.assertion.score <= out.assertion.threshold;
  const std::string pixels = mask
      ? "the " + std::to_string(selectedPixels(*mask)) + " masked "
          + out.assertion.channel + " pixels"
      : out.assertion.channel + " pixels";
  out.assertion.detail = "changed " + percent(out.assertion.score) + " of "
      + pixels + " vs " + versus
      + (bound == Bound::AtLeast ? "; an effect needs >= "
                                 : "; an equivalence allows <= ")
      + percent(out.assertion.threshold);
  if (!out.assertion.passed) {
    const Image &ref = *reference[best].image;
    out.mask = mask ? changedMask(ref, actual[best], taus[best], *mask)
                    : changedMask(ref, actual[best], taus[best]);
  }
}

RelationOutcome attributed(
    const char *kind, const TestDef &test, size_t axis, size_t caseIndex)
{
  RelationOutcome out;
  out.caseIndex = caseIndex;
  out.assertion.kind = kind;
  out.assertion.axis = test.axes[axis].name;
  out.assertion.score = std::numeric_limits<double>::quiet_NaN();
  return out;
}

void setPartners(
    RelationOutcome &out, const std::vector<Case> &cases, std::vector<size_t> p)
{
  out.partners = std::move(p);
  out.assertion.relatedCases.clear();
  for (size_t i : out.partners)
    out.assertion.relatedCases.push_back(cases[i].id());
}

RelationOutcome effect(const TestDef &test,
    size_t axis,
    const std::vector<Case> &cases,
    const std::vector<CaseRender> &renders,
    size_t later,
    size_t partner,
    const SelfCheckDefaults &defaults)
{
  RelationOutcome out = attributed(kEffectAssertion, test, axis, later);
  setPartners(out, cases, {partner});
  const auto margin = test.effectMargins.find(test.axes[axis].name);
  out.assertion.threshold =
      margin != test.effectMargins.end() ? margin->second : defaults.effectMin;
  out.assertion.tau = defaults.tau;

  const std::string problem =
      partnerProblem(test, cases[partner], renders[partner]);
  if (!problem.empty()) {
    notCompared(out, problem);
    return out;
  }
  std::vector<Reference> reference;
  for (const auto &image : renders[partner].images)
    reference.push_back({&image, nullptr});
  judge(out,
      test,
      reference,
      renders[later].images,
      defaults.tau,
      Bound::AtLeast,
      "`" + cases[partner].id() + "`");
  return out;
}

// What a value of an axis must render, if anything: a declared derived
// expectation, or a Variant's identity with its first value.
struct Expectation
{
  const DerivedExpectation *declared{nullptr};
  bool identity{false};

  explicit operator bool() const
  {
    return declared || identity;
  }
};

Expectation expectationFor(const TestDef &test, size_t axis, size_t value)
{
  const Axis &a = test.axes[axis];
  if (const auto *d = test.derivedFor(a.name, anyToString(a.values[value])))
    return {d, false};
  if (a.kind == AxisKind::Variant && value > 0)
    return {nullptr, true};
  return {};
}

// A sibling the expectation asked for and couldn't have.
struct SiblingUnavailable : std::runtime_error
{
  using std::runtime_error::runtime_error;
};

RelationOutcome equivalence(const TestDef &test,
    const RelationGroup &group,
    size_t k,
    const Expectation &expectation,
    const std::vector<Case> &cases,
    const std::vector<CaseRender> &renders,
    const SelfCheckDefaults &defaults)
{
  const size_t later = group.cases[k];
  const Axis &axis = test.axes[group.axis];
  RelationOutcome out =
      attributed(kEquivalenceAssertion, test, group.axis, later);
  const auto override = test.equivalenceTolerances.find(axis.name);
  const bool overridden = override != test.equivalenceTolerances.end();
  const int tau = overridden ? override->second.tau : defaults.tau;
  out.assertion.threshold =
      overridden ? override->second.maxFraction : defaults.equivalenceMax;
  out.assertion.tau = tau;

  // The siblings, by value, and which of them the function read.
  std::vector<size_t> used;
  Channel channel = Channel::Color;
  size_t channelIndex = 0;
  auto lookup = [&](const std::string &value) -> const Image & {
    for (size_t m = 0; m < group.cases.size(); ++m) {
      if (anyToString(axis.values[group.values[m]]) != value)
        continue;
      const size_t sibling = group.cases[m];
      if (sibling == later)
        throw SiblingUnavailable("the expectation of `" + value
            + "` can't be derived from the Case itself");
      if (std::find(used.begin(), used.end(), sibling) == used.end())
        used.push_back(sibling);
      const std::string problem =
          partnerProblem(test, cases[sibling], renders[sibling]);
      if (!problem.empty())
        throw SiblingUnavailable(problem);
      return renders[sibling].images[channelIndex];
    }
    throw SiblingUnavailable(
        "no sibling holds `" + axis.name + "` = `" + value + "`");
  };

  const std::string first = anyToString(axis.values.front());
  const DerivedFn identity = [&](const Siblings &s) -> ExpectedImage {
    return s.at(first);
  };
  const DerivedFn &fn =
      expectation.declared ? expectation.declared->fn : identity;

  std::vector<ExpectedImage> expected(test.channels.size());
  try {
    for (channelIndex = 0; channelIndex < test.channels.size();
        ++channelIndex) {
      channel = test.channels[channelIndex];
      expected[channelIndex] = fn(Siblings(channel, lookup));
      if (!expected[channelIndex].image.valid())
        throw SiblingUnavailable(std::string("the expectation gave no ")
            + channelName(channel) + " image");
    }
  } catch (const SiblingUnavailable &e) {
    notCompared(out, e.what());
  } catch (const std::exception &e) {
    notCompared(out, std::string("the expectation failed: ") + e.what());
  }

  // The siblings read, in the axis's value order (the order the function
  // read them in may be unspecified, e.g. as call arguments).
  std::vector<size_t> read;
  for (size_t c : group.cases)
    if (std::find(used.begin(), used.end(), c) != used.end())
      read.push_back(c);
  setPartners(out, cases, read);
  if (expectation.declared)
    out.assertion.expectation = expectation.declared->description;
  else if (!read.empty())
    out.assertion.expectation =
        "identical to `" + cases[read.front()].id() + "`";
  if (!out.assertion.detail.empty())
    return out;

  std::vector<Reference> reference;
  for (const auto &e : expected)
    reference.push_back({&e.image, e.masked() ? &e.mask : nullptr});
  judge(out,
      test,
      reference,
      renders[later].images,
      tau,
      Bound::AtMost,
      expectation.declared
          ? "the expected image (" + expectation.declared->description + ")"
          : "`" + cases[read.front()].id() + "`");
  if (expectation.declared && out.channelIndex >= 0) {
    auto &scored = expected[size_t(out.channelIndex)];
    out.expected = std::move(scored.image);
    if (scored.masked())
      out.expectedMask = std::move(scored.mask);
  }
  return out;
}

RelationOutcome optedOut(const TestDef &test,
    size_t axis,
    const std::vector<Case> &cases,
    size_t later,
    size_t partner,
    const std::string &reason)
{
  RelationOutcome out = attributed(kEffectAssertion, test, axis, later);
  setPartners(out, cases, {partner});
  out.assertion.evaluated = false;
  out.assertion.passed = false;
  out.assertion.threshold = std::numeric_limits<double>::quiet_NaN();
  out.assertion.detail = "effect check opted out: " + reason;
  return out;
}

// Walk a Test's groups: for each Case of a group (in value order) with
// `visit(group, k, expectation)`. Keeps the grouping and the per-value
// expectation lookup in one place for renderSet() and evaluateRelations().
template <typename Visit>
void forEachGroupMember(
    const TestDef &test, const std::vector<Case> &cases, Visit &&visit)
{
  for (const auto &group : relationGroups(test, cases))
    for (size_t k = 0; k < group.cases.size(); ++k)
      visit(group, k, expectationFor(test, group.axis, group.values[k]));
}

// The members of a group an effect pair may use: those without a declared
// derived expectation.
bool inEffectPairs(const TestDef &test, const RelationGroup &group, size_t k)
{
  return !hasDerivedExpectation(test, group.axis, group.values[k]);
}

} // namespace

int channelTau(Channel channel, int tau)
{
  switch (channel) {
  case Channel::PrimitiveId:
  case Channel::ObjectId:
  case Channel::InstanceId:
    return 0;
  default:
    return tau;
  }
}

std::vector<std::vector<size_t>> caseValueIndices(
    const TestDef &test, const std::vector<Case> &cases)
{
  std::vector<std::vector<size_t>> out;
  out.reserve(cases.size());
  for (const auto &c : cases) {
    std::vector<size_t> idx(test.axes.size(), 0);
    for (size_t a = 0; a < test.axes.size(); ++a) {
      const auto &axis = test.axes[a];
      const std::string where = "test `" + test.id() + "`, Case `" + c.id()
          + "`: axis `" + axis.name + "`";
      const CaseValue *held = nullptr;
      for (const auto &cv : c.values)
        if (cv.axisName == axis.name)
          held = &cv;
      if (!held)
        throw std::invalid_argument(where + " has no value in the Case");
      const std::string value = anyToString(held->value);
      std::vector<size_t> matches;
      for (size_t v = 0; v < axis.values.size(); ++v)
        if (anyToString(axis.values[v]) == value)
          matches.push_back(v);
      if (matches.empty())
        throw std::invalid_argument(
            where + " value `" + value + "` matches none of the axis's values");
      if (matches.size() > 1)
        throw std::invalid_argument(where + " value `" + value + "` matches "
            + std::to_string(matches.size())
            + " of the axis's values (their string forms collide)");
      idx[a] = matches.front();
    }
    out.push_back(std::move(idx));
  }
  return out;
}

std::vector<RelationGroup> relationGroups(
    const TestDef &test, const std::vector<Case> &cases)
{
  const auto indices = caseValueIndices(test, cases);
  std::vector<RelationGroup> groups;
  for (size_t a = 0; a < test.axes.size(); ++a) {
    // Cases with equal values on every other axis form a group.
    std::map<std::vector<size_t>, std::vector<size_t>> byOthers;
    for (size_t i = 0; i < cases.size(); ++i) {
      auto key = indices[i];
      key[a] = 0;
      byOthers[key].push_back(i);
    }
    for (auto &[key, members] : byOthers) {
      if (members.size() < 2)
        continue;
      std::stable_sort(members.begin(), members.end(), [&](size_t x, size_t y) {
        return indices[x][a] < indices[y][a];
      });
      RelationGroup group{a, std::move(members), {}};
      for (size_t m : group.cases)
        group.values.push_back(indices[m][a]);
      groups.push_back(std::move(group));
    }
  }
  return groups;
}

bool hasRelations(const TestDef &test)
{
  if (test.behaviorCheck)
    return false;
  for (const auto &axis : test.axes)
    if (axis.values.size() > 1)
      return true;
  return false;
}

bool hasDerivedExpectation(const TestDef &test, size_t axis, size_t valueIndex)
{
  const Axis &a = test.axes[axis];
  return test.derivedFor(a.name, anyToString(a.values[valueIndex])) != nullptr;
}

std::vector<bool> renderSet(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<bool> &selected)
{
  std::vector<bool> out = selected;
  out.resize(cases.size(), false);
  forEachGroupMember(test,
      cases,
      [&](const RelationGroup &group, size_t k, const Expectation &e) {
        if (!selected[group.cases[k]])
          return;
        const Axis &axis = test.axes[group.axis];
        if (e.identity) {
          // Compared with the first value's Case.
          for (size_t m = 0; m < group.cases.size(); ++m)
            if (group.values[m] == 0)
              out[group.cases[m]] = true;
        } else if (e.declared) {
          // The function may read any sibling.
          for (size_t c : group.cases)
            out[c] = true;
        } else if (axis.kind == AxisKind::Permutation
            && !test.noEffectCheck.count(axis.name)) {
          // Compared with every earlier Case taking part in effect pairs.
          for (size_t p = 0; p < k; ++p)
            if (inEffectPairs(test, group, p))
              out[group.cases[p]] = true;
        }
      });
  return out;
}

std::vector<RelationOutcome> evaluateRelations(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<CaseRender> &renders,
    const SelfCheckDefaults &defaults)
{
  std::vector<RelationOutcome> out;
  if (test.behaviorCheck)
    return out;
  forEachGroupMember(test,
      cases,
      [&](const RelationGroup &group, size_t k, const Expectation &e) {
        const size_t later = group.cases[k];
        // A Case that wasn't rendered here, or failed itself, carries
        // nothing.
        if (renders[later].state != CaseRender::State::Rendered)
          return;
        if (e) {
          out.push_back(
              equivalence(test, group, k, e, cases, renders, defaults));
          return;
        }
        const Axis &axis = test.axes[group.axis];
        if (axis.kind != AxisKind::Permutation)
          return;
        // Effect pairs with every earlier Case, among those taking part.
        std::vector<size_t> partners;
        for (size_t p = 0; p < k; ++p)
          if (inEffectPairs(test, group, p))
            partners.push_back(group.cases[p]);
        if (partners.empty())
          return;
        const auto optOut = test.noEffectCheck.find(axis.name);
        if (optOut != test.noEffectCheck.end()) {
          out.push_back(optedOut(
              test, group.axis, cases, later, partners.back(), optOut->second));
          return;
        }
        for (size_t partner : partners)
          out.push_back(effect(
              test, group.axis, cases, renders, later, partner, defaults));
      });
  return out;
}

void settleVerdict(CaseResult &result, bool participated)
{
  if (result.verdict != Verdict::Passed)
    return;
  bool evaluated = result.groundTruthEvaluated || participated;
  for (const auto &a : result.assertions) {
    if (!a.evaluated)
      continue;
    evaluated = true;
    if (!a.passed)
      result.verdict = Verdict::Failed;
  }
  if (result.verdict == Verdict::Failed || evaluated)
    return;
  result.verdict = Verdict::Skipped;
  result.skipReason =
      result.usesGroundTruth ? kNoGroundTruthReason : kNothingEvaluatedReason;
}

CaseRender loadCaseRender(
    const Workdir &workdir, const TestDef &test, const Case &c)
{
  CaseRender render;
  for (Channel ch : test.channels) {
    Image image = loadPNG(workdir.resultImagePath(c, ch).string());
    if (!image.valid())
      return {};
    render.images.push_back(std::move(image));
  }
  render.state = CaseRender::State::Rendered;
  return render;
}

} // namespace cts
} // namespace anari
