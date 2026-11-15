// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Discrimination.h"
#include "Expansion.h"
#include "Metrics.h"
#include "Relations.h"
#include "Value.h"
// std
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace anari {
namespace cts {

namespace {

bool endsWith(const std::string &s, const std::string &suffix)
{
  return s.size() >= suffix.size()
      && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string axisValue(const Case &c, const std::string &axis)
{
  for (const auto &cv : c.values)
    if (cv.axisName == axis)
      return anyToString(cv.value);
  return {};
}

bool sameValuePair(const Allowance &a,
    const std::string &test,
    const std::string &axis,
    const std::string &first,
    const std::string &second)
{
  return a.test == test && a.axis == axis
      && ((a.first == first && a.second == second)
          || (a.first == second && a.second == first));
}

std::string fixed(double v, int precision)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(precision) << v;
  return os.str();
}

} // namespace

bool isFrameTypeAxis(const std::string &axisName)
{
  return axisName.rfind("frame_", 0) == 0 && endsWith(axisName, "_type")
      && axisName.size() > std::string("frame__type").size();
}

std::vector<DiscriminationPair> discriminationPairs(
    const TestDef &test, const std::vector<Case> &cases)
{
  std::vector<DiscriminationPair> out;
  std::set<std::pair<std::string, std::string>> seen;
  for (const auto &group : relationGroups(test, cases)) {
    const auto &axis = test.axes[group.axis];
    if (axis.kind != AxisKind::Permutation || isFrameTypeAxis(axis.name)
        || test.noEffectCheck.count(axis.name))
      continue;
    for (size_t k = 1; k < group.cases.size(); ++k) {
      for (size_t p = 0; p < k; ++p) {
        const size_t first = group.cases[p];
        const size_t second = group.cases[k];
        // Cases differing also in a Variant value share Ground truth.
        if (!seen
                .insert({cases[first].groundTruthKey(),
                    cases[second].groundTruthKey()})
                .second)
          continue;
        out.push_back(DiscriminationPair{group.axis, first, second});
      }
    }
  }
  return out;
}

PairScore scoreGroundTruthPair(const TestDef &test,
    const std::vector<Image> &render,
    const std::vector<Image> &groundTruth,
    const GroundTruthThresholds &defaults)
{
  PairScore out;
  const size_t n = test.channels.size();
  if (render.size() != n || groundTruth.size() != n)
    return out;
  out.indistinguishable = n > 0;
  for (size_t i = 0; i < n; ++i) {
    ChannelComparison cc;
    cc.channel = test.channels[i];
    cc.ssim = ssim(groundTruth[i], render[i]);
    cc.psnr = psnr(groundTruth[i], render[i]);
    cc.passed = metricPassed(cc.ssim,
                    test.thresholdFor(cc.channel, "ssim", defaults.ssim))
        && metricPassed(
            cc.psnr, test.thresholdFor(cc.channel, "psnr", defaults.psnr));
    out.indistinguishable = out.indistinguishable && cc.passed;
    out.channels.push_back(cc);
  }
  return out;
}

size_t DiscriminationResult::notAllowed() const
{
  size_t n = 0;
  for (const auto &f : findings)
    n += !f.allowedBecause;
  return n;
}

GroundTruthLoader groundTruthFrom(const Workdir &workdir)
{
  return [workdir](const TestDef &test, const Case &c) {
    std::vector<Image> out;
    for (Channel ch : test.channels) {
      auto image = loadPNG(workdir.groundTruthImagePath(c, ch).string());
      if (!image.valid())
        return std::vector<Image>{};
      out.push_back(std::move(image));
    }
    return out;
  };
}

DiscriminationResult checkDiscrimination(
    const std::vector<const TestDef *> &tests,
    const std::vector<Allowance> &allowList,
    const GroundTruthThresholds &defaults,
    const GroundTruthLoader &loadGroundTruth)
{
  DiscriminationResult result;
  // Per allow-list entry: whether a pair it matches was compared, and whether
  // one was indistinguishable.
  std::vector<bool> entryCompared(allowList.size(), false);
  std::vector<bool> entryUsed(allowList.size(), false);

  for (const TestDef *test : tests) {
    if (!test->usesGroundTruth())
      continue;
    const auto cases = expand(*test);
    const auto pairs = discriminationPairs(*test, cases);
    if (pairs.empty())
      continue;
    ++result.tests;

    // Ground truth by key: Variants share a file.
    std::map<std::string, std::vector<Image>> groundTruth;
    auto imagesOf = [&](const Case &c) -> const std::vector<Image> & {
      const auto key = c.groundTruthKey();
      auto it = groundTruth.find(key);
      if (it == groundTruth.end())
        it = groundTruth.emplace(key, loadGroundTruth(*test, c)).first;
      return it->second;
    };

    // Findings of this Test by (axis, first value, second value), in pair
    // order.
    std::map<std::tuple<size_t, std::string, std::string>, size_t> index;
    std::vector<DiscriminationFinding> findings;
    for (const auto &pair : pairs) {
      const auto &axis = test->axes[pair.axis].name;
      const Case &a = cases[pair.first];
      const Case &b = cases[pair.second];
      const auto first = axisValue(a, axis);
      const auto second = axisValue(b, axis);

      const auto &imagesA = imagesOf(a);
      const auto &imagesB = imagesOf(b);
      if (imagesA.empty() || imagesB.empty()) {
        ++result.pairsUnchecked;
        continue;
      }
      ++result.pairsCompared;
      const auto score =
          scoreGroundTruthPair(*test, imagesB, imagesA, defaults);

      for (size_t e = 0; e < allowList.size(); ++e) {
        if (sameValuePair(allowList[e], test->id(), axis, first, second)) {
          entryCompared[e] = true;
          entryUsed[e] = entryUsed[e] || score.indistinguishable;
        }
      }

      const auto key = std::make_tuple(pair.axis, first, second);
      auto it = index.find(key);
      if (it == index.end()) {
        DiscriminationFinding f;
        f.test = test->id();
        f.axis = axis;
        f.first = first;
        f.second = second;
        it = index.emplace(key, findings.size()).first;
        findings.push_back(std::move(f));
      }
      auto &f = findings[it->second];
      ++f.compared;
      if (score.indistinguishable) {
        ++result.pairsIndistinguishable;
        if (f.indistinguishable++ == 0) {
          f.exampleFirst = a.id();
          f.exampleSecond = b.id();
          f.example = score;
        }
      }
    }

    for (auto &f : findings) {
      if (f.indistinguishable == 0)
        continue;
      for (const auto &entry : allowList) {
        if (sameValuePair(entry, f.test, f.axis, f.first, f.second)) {
          f.allowedBecause = entry.reason;
          break;
        }
      }
      result.findings.push_back(std::move(f));
    }
  }

  for (size_t e = 0; e < allowList.size(); ++e)
    if (entryCompared[e] && !entryUsed[e])
      result.stale.push_back(allowList[e]);
  return result;
}

void writeDiscriminationReport(
    std::ostream &out, const DiscriminationResult &result, bool includeAllowed)
{
  std::string currentTest;
  for (const auto &f : result.findings) {
    if (f.allowedBecause && !includeAllowed)
      continue;
    if (f.test != currentTest) {
      currentTest = f.test;
      out << f.test << "\n";
    }
    out << "  " << f.axis << ": `" << f.first << "` vs `" << f.second
        << "` indistinguishable in " << f.indistinguishable << " of "
        << f.compared << " pair(s); e.g. `" << f.exampleFirst << "` vs `"
        << f.exampleSecond << "`:";
    for (const auto &cc : f.example.channels)
      out << " " << channelName(cc.channel) << " ssim " << fixed(cc.ssim, 3)
          << " psnr " << fixed(cc.psnr, 1);
    out << "\n";
    if (f.allowedBecause)
      out << "    allowed: " << *f.allowedBecause << "\n";
  }

  if (!result.stale.empty()) {
    out << "allow-list entries this Ground truth tells apart (remove one "
           "once no Reference device needs it):\n";
    for (const auto &e : result.stale)
      out << "  " << e.test << "  " << e.axis << ": `" << e.first << "` vs `"
          << e.second << "`\n";
  }

  const size_t allowed = result.findings.size() - result.notAllowed();
  out << "check-discrimination: " << result.pairsCompared
      << " Ground-truth pair(s) compared in " << result.tests << " Test(s), "
      << result.pairsIndistinguishable << " indistinguishable; "
      << result.findings.size() << " axis value pair(s) flagged, " << allowed
      << " allowed, " << result.notAllowed() << " not allowed";
  if (result.pairsUnchecked > 0)
    out << "; " << result.pairsUnchecked
        << " pair(s) not checked (Ground truth missing)";
  out << "\n";
}

} // namespace cts
} // namespace anari
