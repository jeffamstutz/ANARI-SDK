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

namespace anari {
namespace cts {

namespace {

std::string percent(double fraction)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(2) << fraction * 100.0 << " %";
  return os.str();
}

std::string sizeText(const Image &image)
{
  return std::to_string(image.width) + "x" + std::to_string(image.height);
}

// The changed fraction of one Channel of a pair of Cases.
struct ChannelScore
{
  double fraction{std::numeric_limits<double>::quiet_NaN()};
  int tau{0};
};

// Fill the common fields of a pair's assertion.
RelationOutcome pairOutcome(const char *kind,
    const TestDef &test,
    size_t axis,
    const std::vector<Case> &cases,
    size_t later,
    size_t partner)
{
  RelationOutcome out;
  out.caseIndex = later;
  out.partnerIndex = partner;
  out.assertion.kind = kind;
  out.assertion.axis = test.axes[axis].name;
  out.assertion.relatedCases = {cases[partner].id()};
  return out;
}

// Why a pair can't be compared because of its partner, or empty when it can.
std::string partnerProblem(const Case &partner, const CaseRender &render)
{
  switch (render.state) {
  case CaseRender::State::Rendered:
    return {};
  case CaseRender::State::Failed:
    return "partner `" + partner.id() + "` did not render: " + render.error;
  case CaseRender::State::Absent:
    return "partner `" + partner.id() + "` was not rendered";
  }
  return {};
}

// Score every Channel of a pair; a size mismatch on any Channel is reported
// in `mismatch` (the pair is then not comparable).
std::vector<ChannelScore> scorePair(const TestDef &test,
    const CaseRender &a,
    const CaseRender &b,
    int tau,
    std::string &mismatch)
{
  std::vector<ChannelScore> scores(test.channels.size());
  for (size_t i = 0; i < test.channels.size(); ++i) {
    const Image &ia = a.images[i];
    const Image &ib = b.images[i];
    scores[i].tau = channelTau(test.channels[i], tau);
    scores[i].fraction = changedFraction(ia, ib, scores[i].tau);
    if (std::isnan(scores[i].fraction) && mismatch.empty()) {
      mismatch = std::string("images are not comparable: ")
          + channelName(test.channels[i]) + " is " + sizeText(ib) + " vs "
          + sizeText(ia);
    }
  }
  return scores;
}

// The index of the Channel with the largest changed fraction.
size_t mostChanged(const std::vector<ChannelScore> &scores)
{
  size_t best = 0;
  for (size_t i = 1; i < scores.size(); ++i)
    if (scores[i].fraction > scores[best].fraction)
      best = i;
  return best;
}

void recordChannel(RelationOutcome &out,
    const TestDef &test,
    const std::vector<ChannelScore> &scores,
    size_t channel)
{
  out.channelIndex = static_cast<int>(channel);
  out.assertion.channel = channelName(test.channels[channel]);
  out.assertion.score = scores[channel].fraction;
  out.assertion.tau = scores[channel].tau;
}

bool usable(const CaseRender &render, const TestDef &test)
{
  return render.state == CaseRender::State::Rendered
      && render.images.size() == test.channels.size();
}

RelationOutcome effect(const TestDef &test,
    size_t axis,
    const std::vector<Case> &cases,
    const std::vector<CaseRender> &renders,
    size_t later,
    size_t partner,
    const SelfCheckDefaults &defaults)
{
  RelationOutcome out =
      pairOutcome(kEffectAssertion, test, axis, cases, later, partner);
  const auto &axisName = test.axes[axis].name;
  const auto margin = test.effectMargins.find(axisName);
  out.assertion.threshold =
      margin != test.effectMargins.end() ? margin->second : defaults.effectMin;
  out.assertion.score = std::numeric_limits<double>::quiet_NaN();
  out.assertion.tau = defaults.tau;

  const std::string problem = partnerProblem(cases[partner], renders[partner]);
  if (!problem.empty() || !usable(renders[partner], test)) {
    out.assertion.passed = false;
    out.assertion.detail = problem.empty()
        ? "partner `" + cases[partner].id() + "` has no image"
        : problem;
    return out;
  }

  std::string mismatch;
  const auto scores =
      scorePair(test, renders[partner], renders[later], defaults.tau, mismatch);
  if (!mismatch.empty()) {
    out.assertion.passed = false;
    out.assertion.detail = mismatch;
    return out;
  }
  const size_t best = mostChanged(scores);
  recordChannel(out, test, scores, best);
  // Any Channel showing the effect is enough.
  out.assertion.passed = out.assertion.score >= out.assertion.threshold;
  out.assertion.detail = "changed " + percent(out.assertion.score) + " of "
      + out.assertion.channel + " pixels vs `" + cases[partner].id()
      + "`; an effect needs >= " + percent(out.assertion.threshold);
  if (!out.assertion.passed) {
    const auto &l = renders[later].images[best];
    const auto &p = renders[partner].images[best];
    out.mask = changedMask(p, l, scores[best].tau);
  }
  return out;
}

RelationOutcome equivalence(const TestDef &test,
    size_t axis,
    const std::vector<Case> &cases,
    const std::vector<CaseRender> &renders,
    size_t later,
    size_t first,
    const SelfCheckDefaults &defaults)
{
  RelationOutcome out =
      pairOutcome(kEquivalenceAssertion, test, axis, cases, later, first);
  const auto &axisName = test.axes[axis].name;
  const auto override = test.equivalenceTolerances.find(axisName);
  const bool overridden = override != test.equivalenceTolerances.end();
  const int tau = overridden ? override->second.tau : defaults.tau;
  out.assertion.threshold =
      overridden ? override->second.maxFraction : defaults.equivalenceMax;
  out.assertion.score = std::numeric_limits<double>::quiet_NaN();
  out.assertion.tau = tau;

  const std::string problem = partnerProblem(cases[first], renders[first]);
  if (!problem.empty() || !usable(renders[first], test)) {
    out.assertion.passed = false;
    out.assertion.detail = problem.empty()
        ? "partner `" + cases[first].id() + "` has no image"
        : problem;
    return out;
  }

  std::string mismatch;
  const auto scores =
      scorePair(test, renders[first], renders[later], tau, mismatch);
  if (!mismatch.empty()) {
    out.assertion.passed = false;
    out.assertion.detail = mismatch;
    return out;
  }
  // Every Channel must match: score the worst.
  const size_t worst = mostChanged(scores);
  recordChannel(out, test, scores, worst);
  out.assertion.passed = out.assertion.score <= out.assertion.threshold;
  out.assertion.detail = "changed " + percent(out.assertion.score) + " of "
      + out.assertion.channel + " pixels vs `" + cases[first].id()
      + "`; an equivalence allows <= " + percent(out.assertion.threshold);
  if (!out.assertion.passed) {
    const auto &l = renders[later].images[worst];
    const auto &f = renders[first].images[worst];
    out.mask = changedMask(f, l, scores[worst].tau);
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
  RelationOutcome out =
      pairOutcome(kEffectAssertion, test, axis, cases, later, partner);
  out.assertion.evaluated = false;
  out.assertion.passed = false;
  out.assertion.score = std::numeric_limits<double>::quiet_NaN();
  out.assertion.threshold = std::numeric_limits<double>::quiet_NaN();
  out.assertion.detail = "effect check opted out: " + reason;
  return out;
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
      std::string value;
      for (const auto &cv : c.values)
        if (cv.axisName == axis.name)
          value = anyToString(cv.value);
      for (size_t v = 0; v < axis.values.size(); ++v) {
        if (anyToString(axis.values[v]) == value) {
          idx[a] = v;
          break;
        }
      }
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
      groups.push_back(RelationGroup{a, std::move(members)});
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

std::vector<bool> renderSet(const TestDef &test,
    const std::vector<Case> &cases,
    const std::vector<bool> &selected)
{
  std::vector<bool> out = selected;
  out.resize(cases.size(), false);
  for (const auto &group : relationGroups(test, cases)) {
    const auto &axis = test.axes[group.axis];
    if (axis.kind == AxisKind::Variant) {
      // Each Case is compared with the group's first.
      for (size_t k = 1; k < group.cases.size(); ++k)
        if (selected[group.cases[k]])
          out[group.cases.front()] = true;
    } else if (!test.noEffectCheck.count(axis.name)) {
      // Each later Case is compared with every earlier one.
      for (size_t k = 1; k < group.cases.size(); ++k)
        if (selected[group.cases[k]])
          for (size_t p = 0; p < k; ++p)
            out[group.cases[p]] = true;
    }
  }
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
  for (const auto &group : relationGroups(test, cases)) {
    const auto &axis = test.axes[group.axis];
    const auto optOut = test.noEffectCheck.find(axis.name);
    for (size_t k = 1; k < group.cases.size(); ++k) {
      const size_t later = group.cases[k];
      // A Case that wasn't rendered here, or failed itself, carries nothing.
      if (renders[later].state != CaseRender::State::Rendered)
        continue;
      if (axis.kind == AxisKind::Variant) {
        out.push_back(equivalence(test,
            group.axis,
            cases,
            renders,
            later,
            group.cases.front(),
            defaults));
      } else if (optOut != test.noEffectCheck.end()) {
        out.push_back(optedOut(test,
            group.axis,
            cases,
            later,
            group.cases[k - 1],
            optOut->second));
      } else {
        for (size_t p = 0; p < k; ++p)
          out.push_back(effect(test,
              group.axis,
              cases,
              renders,
              later,
              group.cases[p],
              defaults));
      }
    }
  }
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
