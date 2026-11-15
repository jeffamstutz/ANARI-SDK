// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

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

// A run's aggregate: overall counts, per-category counts, and failed case keys.
struct Summary
{
  int total{0};
  int passed{0};
  int failed{0};
  int skipped{0};
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
  Passed, // advertised; its Cases ran and none failed
  Failed, // advertised; at least one of its Cases failed
  NotAdvertised, // the device doesn't advertise it (its Cases skip)
  NoGroundTruth, // advertised; its Cases skipped for want of Ground truth
  MissingSupport, // advertised; its Cases skipped for a support extension
  NotRun, // advertised and tested, but no Case of it is in the results
  AdvertisedNoTest, // advertised; the CTS has no Test for it
  NotAdvertisedNoTest, // neither advertised nor tested
};

const char *coverageStateName(CoverageState state);

struct ExtensionCoverage
{
  std::string extension;
  bool advertised{false};
  bool tested{false}; // some Test in the catalog covers it
  // Cases for which this is a primary extension.
  int passed{0};
  int failed{0};
  int skipped{0};
  CoverageState state{CoverageState::NotAdvertisedNoTest};
};

// The extensions the run's device advertised: the union of the sidecars'
// `device.extensions`. `recorded` (when non-null) is set false when no sidecar
// recorded any (they predate schema v3).
std::set<std::string> runExtensions(
    const std::map<std::string, CaseResult> &results, bool *recorded = nullptr);

// One row per extension in `extensions` plus any extension that `tested` or a
// Case's coveredFeatures names, sorted by name. `tested` is the set of
// extensions some Test covers (coveredExtensions() of the catalog). An
// extension a Case ran for counts as advertised even if the run did not record
// the device's extensions.
std::vector<ExtensionCoverage> extensionCoverage(
    const std::map<std::string, CaseResult> &results,
    const std::vector<std::string> &extensions,
    const std::set<std::string> &tested);

// The case keys to itemize in a report: every case, or only failures.
std::vector<std::string> reportCaseKeys(
    const std::map<std::string, CaseResult> &results, bool includeAll);

// Write the text summary (counts, per-category table, the per-extension
// table when `coverage` is non-empty, itemized cases).
void writeTextSummary(std::ostream &out,
    const std::string &workdir,
    const std::map<std::string, CaseResult> &results,
    bool includeAll,
    const std::vector<ExtensionCoverage> &coverage = {});

} // namespace cts
} // namespace anari
