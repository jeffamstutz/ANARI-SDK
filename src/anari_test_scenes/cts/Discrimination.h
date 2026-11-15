// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "Channel.h"
#include "FrameFormats.h"
#include "Image.h"
#include "TestDef.h"
#include "Workdir.h"
// std
#include <cstddef>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// The guard against indistinguishable Ground truth (`anariCts
// check-discrimination`). A Permutation promises that each of its values
// renders differently, and the Ground-truth comparison is only evidence for a
// value if a render of a sibling value would fail it. This scores the Ground
// truth of every pair of Cases that differ in one Permutation axis value
// against each other, with the Test's own thresholds: a pair that passes is
// indistinguishable, and the Test can't tell those two values apart. No
// device: it reads the Ground truth `generate` wrote and the Catalog.
namespace anari {
namespace cts {

// Two Cases (indices into the Test's Cases) whose Ground truth must differ:
// they differ only in one value of the Permutation axis `axis`, `first`
// holding the earlier value in the axis's value order.
struct DiscriminationPair
{
  size_t axis{0};
  size_t first{0};
  size_t second{0};
};

// Every pair of Cases of a Test that differ in exactly one Permutation axis
// value: all pairs within each group of relationGroups() (cartesian: one group
// per combination of the other axes; simplified: the baseline and the axis's
// other Cases). Variant axes and `frame_<channel>_type` axes are not pairing
// axes. Pairs that compare the same two
// Ground-truth files (Cases differing also in a Variant value) are listed once.
std::vector<DiscriminationPair> discriminationPairs(
    const TestDef &test, const std::vector<Case> &cases);

// The run's default Ground-truth thresholds, which a Test may override per
// metric and Channel (TestDef::thresholdFor). main passes RunOptions'.
struct GroundTruthThresholds
{
  double ssim{0.0};
  double psnr{0.0};
};

// One Channel of a pair, scored as a run would score a render against Ground
// truth.
struct ChannelComparison
{
  Channel channel{Channel::Color};
  double ssim{0.0};
  double psnr{0.0};
  bool passed{false};
};

// A pair of Ground-truth image sets scored against each other:
// indistinguishable when every Channel passes the Test's thresholds, i.e. a
// render of one would pass as the other.
struct PairScore
{
  bool indistinguishable{false};
  std::vector<ChannelComparison> channels;
};

// Score `render` against `groundTruth` (both in TestDef::channels order) with
// the Test's thresholds. Image sets that don't hold one valid image per
// Channel are distinguishable (a NaN score never passes).
PairScore scoreGroundTruthPair(const TestDef &test,
    const std::vector<Image> &render,
    const std::vector<Image> &groundTruth,
    const GroundTruthThresholds &defaults);

// An entry of the allow-list: a pair of values of one Permutation axis whose
// Ground truth may be indistinguishable, and why. Matches either value order.
struct Allowance
{
  std::string test; // "<category>/<name>"
  std::string axis;
  std::string first;
  std::string second;
  std::string reason;
};

// The pairs of one Test's axis value pair (all groups) that were
// indistinguishable.
struct DiscriminationFinding
{
  std::string test;
  std::string axis;
  std::string first;
  std::string second;
  size_t indistinguishable{0}; // pairs that passed against each other
  size_t compared{0}; // pairs whose Ground truth both existed
  // One indistinguishable pair: its Case ids and scores.
  std::string exampleFirst;
  std::string exampleSecond;
  PairScore example;
  // The allow-list's reason, when an entry allows the pair.
  std::optional<std::string> allowedBecause;
};

struct DiscriminationResult
{
  size_t tests{0}; // Tests with at least one pair
  size_t pairsCompared{0};
  size_t pairsIndistinguishable{0};
  size_t pairsUnchecked{0}; // a Case's Ground truth was missing
  // Axis value pairs with at least one indistinguishable pair, in Catalog
  // order.
  std::vector<DiscriminationFinding> findings;
  // Allow-list entries whose pairs were compared and all distinguished on
  // this Ground truth. Informational: another Reference device may need them.
  std::vector<Allowance> stale;

  // Findings no allow-list entry allows; the check fails when non-zero.
  size_t notAllowed() const;
};

// A Case's Ground-truth images in TestDef::channels order, or empty when any
// is missing.
using GroundTruthLoader =
    std::function<std::vector<Image>(const TestDef &, const Case &)>;

// Ground truth read from a Workdir's ground_truth/.
GroundTruthLoader groundTruthFrom(const Workdir &workdir);

// Score every discrimination pair of every Test that compares against Ground
// truth (TestDef::usesGroundTruth), and match the indistinguishable ones
// against the allow-list.
DiscriminationResult checkDiscrimination(
    const std::vector<const TestDef *> &tests,
    const std::vector<Allowance> &allowList,
    const GroundTruthThresholds &defaults,
    const GroundTruthLoader &loadGroundTruth);

// The text output: each finding not allowed (and with `includeAllowed` the
// allowed ones too), the stale allow-list entries, and a summary line.
void writeDiscriminationReport(
    std::ostream &out, const DiscriminationResult &result, bool includeAllowed);

} // namespace cts
} // namespace anari
