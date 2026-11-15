// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DeviceFailure.h"
#include "Sidecar.h"
// std
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

// The reporting layer: it reads a run's results tree (the per-Case sidecars,
// ADR-0003) and aggregates it into a human summary. It never renders and never
// opens a device; the sidecar tree is its only input. HTML rendering lives in
// HtmlReport; PDF stays in the Python layer (the only output whose dependency
// is not worth porting to C++).
namespace anari {
namespace cts {

// The metrics surfaced in summaries, in display order. The single source is the
// C++ runner; this list must track what it records.
extern const std::vector<std::string> kReportMetrics;

// Every sidecar under workdir/results, keyed by "category/test/case". Duplicate
// logical keys and schema-version mismatches are diagnosed to `warnings` (the
// first-read sidecar wins), mirroring the Python reader.
std::map<std::string, CaseResult> loadResults(
    const std::filesystem::path &workdir, std::ostream &warnings);

// Per-category pass/fail/skip counts.
struct CategoryCounts
{
  int passed{0};
  int failed{0};
  int skipped{0};
};

// The failure kind of a failed Ground-truth comparison, and of a failure that
// is neither that nor a failed assertion (a build or render failure, a
// behaviour check).
inline constexpr const char *kGroundTruthFailure = "Ground truth";
inline constexpr const char *kOtherFailure = "other";

// Why a Case failed, once per kind: the kind of each failed evaluated
// assertion (effect, equivalence, or a kind a later schema adds),
// kGroundTruthFailure when its Ground-truth comparison failed, and
// kOtherFailure when it failed for neither. Empty unless it failed.
std::vector<std::string> failureKinds(const CaseResult &result);

// A run's aggregate: overall counts, per-category counts, and failed case keys.
struct Summary
{
  int total{0};
  int passed{0};
  int failed{0};
  int skipped{0};
  // Of `passed`: Cases that passed their self-checks while the Ground truth
  // their Test asks for was missing (ADR-0010).
  int passedWithoutGroundTruth{0};
  // Failed Cases per failure kind (failureKinds()); a Case failing in two
  // ways counts under both.
  std::map<std::string, int> failuresByKind;
  std::map<std::string, CategoryCounts> categories;
  std::vector<std::string> failures;
};

Summary summarize(const std::map<std::string, CaseResult> &results);

// The single device identity shared by a run's sidecars. Absent when the run is
// empty or names more than one device (which is diagnosed to `warnings`), so a
// mixed run is flagged rather than mislabeled by whichever sidecar read first.
std::optional<DeviceSpec> runDevice(
    const std::map<std::string, CaseResult> &results, std::ostream &warnings);

// A human label for a run: its device identity, or `fallback` (the workdir
// name) when the sidecars predate the device field.
std::string deviceLabel(const std::map<std::string, CaseResult> &results,
    const std::string &fallback,
    std::ostream &warnings);

// Per-extension coverage /////////////////////////////////////////////////////

// The canonical KHR extensions: those the API JSON (code_gen/api/) defines,
// sorted.
std::vector<std::string> canonicalKhrExtensions();

// What a run says about one extension, from the Cases for which it is primary.
enum class CoverageState
{
  Passed, // advertised; its Cases ran, were fully scored and none failed
  // Advertised; none failed or skipped, but some passed their self-checks
  // with the Ground truth they ask for not evaluated.
  PassedWithoutGroundTruth,
  // Advertised; none failed, some passed, and some were skipped or carry an
  // unevaluated (opted-out) assertion.
  PartlyScored,
  Failed, // advertised; at least one of its Cases failed
  NotAdvertised, // the device doesn't advertise it (its Cases skip)
  NoGroundTruth, // advertised; its Cases skipped for want of Ground truth
  MissingSupport, // advertised; its Cases skipped for a support extension
  NotRun, // advertised and tested, but no Case of it is in the results
  AdvertisedNoTest, // advertised; the CTS has no Test for it
  NotAdvertisedNoTest, // neither advertised nor tested
};

const char *coverageStateName(CoverageState state);

// The name of the matrix row that collects the Cases of Tests covering no
// extension (core behaviour such as the frame channels every device has).
inline constexpr const char *kCoreRow = "core";

struct ExtensionCoverage
{
  std::string extension; // kCoreRow for the core row
  bool core{false}; // the core row: not an extension; always "advertised"
  bool advertised{false};
  bool tested{false}; // some Test in the catalog covers it
  // Cases for which this is a primary extension.
  int passed{0};
  int failed{0};
  int skipped{0};
  // Of `passed`: those with the Ground truth they ask for not evaluated.
  int passedWithoutGroundTruth{0};
  CoverageState state{CoverageState::NotAdvertisedNoTest};
};

// A row's state as the matrix shows it: coverageStateName(), less the
// "advertised, " that doesn't apply to the core row.
std::string coverageStateLabel(const ExtensionCoverage &row);

// The extensions the run's device advertised: the union of the sidecars'
// `device.extensions`. `recorded` (when non-null) is set false when no sidecar
// recorded any (they predate schema v3).
std::set<std::string> runExtensions(
    const std::map<std::string, CaseResult> &results, bool *recorded = nullptr);

// The extensions the run's Catalog has a Test for: the union of the sidecars'
// `catalogCoveredFeatures`. `recorded` (when non-null) is set false when no
// sidecar recorded any (they predate it).
std::set<std::string> runCoveredExtensions(
    const std::map<std::string, CaseResult> &results, bool *recorded = nullptr);

// What the report says when no sidecar recorded runCoveredExtensions().
inline constexpr const char *kUnrecordedCatalogNote =
    "the run did not record which extensions the Catalog tests (sidecars "
    "from before it did); an extension counts as tested only if a Case in "
    "the results covers it, so \"no Test\" may mean \"not run\"";

// The core row first, then one row per extension in `extensions` plus any
// extension that `tested` or a Case's coveredFeatures names, sorted by name.
// `tested` is the set of extensions some Test covers (runCoveredExtensions()).
// The core row counts the Cases that cover no extension. An extension a Case
// ran for counts as advertised even if the run did not record the device's
// extensions.
std::vector<ExtensionCoverage> extensionCoverage(
    const std::map<std::string, CaseResult> &results,
    const std::vector<std::string> &extensions,
    const std::set<std::string> &tested);

// The case keys to itemize in a report: every case, or only failures.
std::vector<std::string> reportCaseKeys(
    const std::map<std::string, CaseResult> &results, bool includeAll);

// Write the text summary (counts, per-category table, the per-extension
// table when `coverage` is non-empty, itemized cases). With `deviceFailure`
// (the run stopped because the device could not render, readDeviceFailure()),
// it leads with why; any Case results then come from an earlier run, and with
// none, the summary ends there.
void writeTextSummary(std::ostream &out,
    const std::string &workdir,
    const std::map<std::string, CaseResult> &results,
    bool includeAll,
    const std::vector<ExtensionCoverage> &coverage = {},
    const DeviceFailure *deviceFailure = nullptr);

} // namespace cts
} // namespace anari
