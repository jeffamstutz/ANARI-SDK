// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Report.h"
// anari
#include "anari/frontend/anari_extension_utility.h"
// std
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <tuple>

namespace anari {
namespace cts {

const std::vector<std::string> kReportMetrics = {"ssim", "psnr"};

namespace {

std::string caseKey(const CaseResult &r)
{
  return r.category + "/" + r.test + "/" + r.caseId;
}

// A finite score formatted like the Python reader, or "null" for non-finite.
std::string metricText(double value)
{
  if (!std::isfinite(value))
    return "null";
  std::ostringstream os;
  os << value;
  return os.str();
}

// The per-channel "metric=score" list a summary prints for one Case.
std::string channelScores(const CaseResult &r)
{
  std::ostringstream os;
  bool firstChannel = true;
  for (const auto &ch : r.channels) {
    if (!firstChannel)
      os << "; ";
    firstChannel = false;
    os << channelName(ch.channel) << "(";
    bool firstMetric = true;
    for (const auto &m : kReportMetrics) {
      auto it = ch.metrics.find(m);
      if (it == ch.metrics.end())
        continue;
      if (!firstMetric)
        os << ", ";
      firstMetric = false;
      os << m << "=" << metricText(it->second);
    }
    os << ")";
  }
  return os.str();
}

} // namespace

std::map<std::string, CaseResult> loadResults(
    const std::filesystem::path &workdir, std::ostream &warnings)
{
  std::map<std::string, CaseResult> out;
  const auto results = workdir / "results";
  std::error_code ec;
  if (!std::filesystem::is_directory(results, ec))
    return out;

  // Deterministic order so the "first read wins" duplicate rule is stable.
  std::vector<std::filesystem::path> paths;
  for (auto &entry : std::filesystem::recursive_directory_iterator(results, ec))
    if (entry.path().extension() == ".json")
      paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end());

  for (const auto &path : paths) {
    CaseResult r;
    bool schemaMismatch = false;
    if (!readSidecar(path, r, &schemaMismatch))
      continue; // not a sidecar
    if (schemaMismatch)
      warnings << "warning: " << path
               << " has an unexpected schemaVersion (expected "
               << kSidecarSchemaVersion << "); reading best-effort\n";
    const auto key = caseKey(r);
    if (out.count(key)) {
      warnings << "warning: duplicate logical Case key " << key << " in "
               << path << "; keeping the first\n";
      continue;
    }
    out.emplace(key, std::move(r));
  }
  return out;
}

Summary summarize(const std::map<std::string, CaseResult> &results)
{
  Summary s;
  for (const auto &[key, r] : results) {
    ++s.total;
    auto &cat = s.categories[r.category];
    switch (r.verdict) {
    case Verdict::Passed:
      ++s.passed;
      ++cat.passed;
      break;
    case Verdict::Failed:
      ++s.failed;
      ++cat.failed;
      s.failures.push_back(key);
      break;
    case Verdict::Skipped:
      ++s.skipped;
      ++cat.skipped;
      break;
    }
  }
  return s;
}

std::optional<DeviceSpec> runDevice(
    const std::map<std::string, CaseResult> &results, std::ostream &warnings)
{
  std::map<std::tuple<std::string, std::string, std::string>, DeviceSpec>
      devices;
  for (const auto &[key, r] : results) {
    if (r.device.library.empty())
      continue;
    devices.insert(
        {{r.device.library, r.device.device, r.device.renderer}, r.device});
  }
  if (devices.size() > 1) {
    warnings << "warning: mixed device identities in one run:";
    for (const auto &[id, dev] : devices)
      warnings << " " << dev.library << " (" << dev.device << "/"
               << dev.renderer << ")";
    warnings << "\n";
    return std::nullopt;
  }
  if (devices.empty())
    return std::nullopt;
  return devices.begin()->second;
}

std::string deviceLabel(const std::map<std::string, CaseResult> &results,
    const std::string &fallback,
    std::ostream &warnings)
{
  const auto device = runDevice(results, warnings);
  if (!device)
    return fallback;
  return device->library + " (" + device->device + "/" + device->renderer + ")";
}

std::vector<std::string> canonicalKhrExtensions()
{
  std::vector<std::string> out;
  for (auto name = anariGetExtensionNames(); *name; ++name) {
    const std::string n = *name;
    if (n.rfind("ANARI_KHR_", 0) == 0)
      out.push_back(n);
  }
  std::sort(out.begin(), out.end());
  return out;
}

const char *coverageStateName(CoverageState state)
{
  switch (state) {
  case CoverageState::Passed:
    return "passed";
  case CoverageState::Failed:
    return "failed";
  case CoverageState::NotAdvertised:
    return "not advertised";
  case CoverageState::NoGroundTruth:
    return "advertised, no Ground truth";
  case CoverageState::MissingSupport:
    return "advertised, missing support";
  case CoverageState::NotRun:
    return "advertised, not run";
  case CoverageState::AdvertisedNoTest:
    return "advertised, no Test";
  case CoverageState::NotAdvertisedNoTest:
    return "not advertised, no Test";
  }
  return "unknown";
}

std::string coverageStateLabel(const ExtensionCoverage &row)
{
  const std::string name = coverageStateName(row.state);
  const std::string prefix = "advertised, ";
  if (row.core && name.rfind(prefix, 0) == 0)
    return name.substr(prefix.size());
  return name;
}

std::set<std::string> runExtensions(
    const std::map<std::string, CaseResult> &results, bool *recorded)
{
  std::set<std::string> out;
  bool any = false;
  for (const auto &[key, r] : results) {
    any = any || !r.device.extensions.empty();
    out.insert(r.device.extensions.begin(), r.device.extensions.end());
  }
  if (recorded)
    *recorded = any;
  return out;
}

std::set<std::string> runCoveredExtensions(
    const std::map<std::string, CaseResult> &results, bool *recorded)
{
  std::set<std::string> out;
  for (const auto &[key, r] : results)
    out.insert(
        r.catalogCoveredFeatures.begin(), r.catalogCoveredFeatures.end());
  if (recorded)
    *recorded = !out.empty();
  return out;
}

std::vector<ExtensionCoverage> extensionCoverage(
    const std::map<std::string, CaseResult> &results,
    const std::vector<std::string> &extensions,
    const std::set<std::string> &tested)
{
  std::map<std::string, ExtensionCoverage> rows;
  auto row = [&](const std::string &ext) -> ExtensionCoverage & {
    auto &r = rows[ext];
    r.extension = ext;
    return r;
  };
  for (const auto &ext : extensions)
    row(ext);
  for (const auto &ext : tested)
    row(ext).tested = true;

  ExtensionCoverage core;
  core.extension = kCoreRow;
  core.core = true;
  core.advertised = true; // a device has no choice about core
  core.tested = true;

  // Tally one Case into a row, noting the rows with a Case skipped for want
  // of Ground truth (other skips are for a missing extension).
  std::set<const ExtensionCoverage *> noGroundTruth;
  auto tally = [&](ExtensionCoverage &c, const CaseResult &r) {
    switch (r.verdict) {
    case Verdict::Passed:
      ++c.passed;
      break;
    case Verdict::Failed:
      ++c.failed;
      break;
    case Verdict::Skipped:
      ++c.skipped;
      if (r.skipReason == kNoGroundTruthReason)
        noGroundTruth.insert(&c);
      break;
    }
  };
  for (const auto &[key, r] : results) {
    if (r.coveredFeatures.empty())
      tally(core, r);
    for (const auto &ext : r.coveredFeatures)
      tally(row(ext), r);
  }

  const auto advertised = runExtensions(results);
  auto classify = [&](ExtensionCoverage &c) {
    if (!c.tested)
      c.state = c.advertised ? CoverageState::AdvertisedNoTest
                             : CoverageState::NotAdvertisedNoTest;
    else if (!c.advertised)
      c.state = CoverageState::NotAdvertised;
    else if (c.failed > 0)
      c.state = CoverageState::Failed;
    else if (c.passed > 0)
      c.state = CoverageState::Passed;
    else if (noGroundTruth.count(&c))
      c.state = CoverageState::NoGroundTruth;
    else if (c.skipped > 0)
      c.state = CoverageState::MissingSupport;
    else
      c.state = CoverageState::NotRun;
  };

  std::vector<ExtensionCoverage> out;
  out.reserve(rows.size() + 1);
  classify(core);
  out.push_back(std::move(core));
  for (auto &[ext, c] : rows) {
    const int cases = c.passed + c.failed + c.skipped;
    c.tested = c.tested || cases > 0;
    c.advertised = advertised.count(ext) || c.passed + c.failed > 0;
    classify(c);
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<std::string> reportCaseKeys(
    const std::map<std::string, CaseResult> &results, bool includeAll)
{
  if (!includeAll)
    return summarize(results).failures;
  std::vector<std::string> keys;
  keys.reserve(results.size());
  for (const auto &[key, r] : results)
    keys.push_back(key);
  return keys;
}

void writeTextSummary(std::ostream &out,
    const std::string &workdir,
    const std::map<std::string, CaseResult> &results,
    bool includeAll,
    const std::vector<ExtensionCoverage> &coverage)
{
  const Summary s = summarize(results);
  out << "CTS report: " << workdir << "\n";
  if (const auto device = runDevice(results, out))
    out << "  device: " << device->library << " (" << device->device << "/"
        << device->renderer << ")\n";
  out << "  " << s.total << " cases: " << s.passed << " passed, " << s.failed
      << " failed, " << s.skipped << " skipped\n";
  out << "  by category:\n";
  size_t width = 12;
  for (const auto &category : s.categories)
    width = std::max(width, category.first.size() + 1);
  for (const auto &[cat, c] : s.categories)
    out << "    " << std::left << std::setw(static_cast<int>(width)) << cat
        << std::right << std::setw(4) << c.passed << " passed  " << std::setw(4)
        << c.failed << " failed  " << std::setw(4) << c.skipped << " skipped\n";

  if (!coverage.empty()) {
    bool recorded = false;
    runExtensions(results, &recorded);
    bool catalogRecorded = false;
    runCoveredExtensions(results, &catalogRecorded);
    size_t extWidth = 12;
    for (const auto &c : coverage)
      extWidth = std::max(extWidth, c.extension.size() + 1);
    out << "  by extension (Cases for which it is primary):\n";
    if (!recorded && !results.empty())
      out << "    note: the run did not record the device's extensions "
             "(sidecars older than schema v"
          << kSidecarSchemaVersion
          << "); \"advertised\" is inferred from the Cases that ran\n";
    if (!catalogRecorded && !results.empty())
      out << "    note: " << kUnrecordedCatalogNote << "\n";
    out << "    " << std::left << std::setw(static_cast<int>(extWidth))
        << "extension" << std::right << "advertised  passed  failed  skipped"
        << "  state\n";
    for (const auto &c : coverage) {
      const char *advertised = c.advertised ? "yes" : "no";
      if (c.core)
        advertised = "-";
      out << "    " << std::left << std::setw(static_cast<int>(extWidth))
          << c.extension << std::right << std::setw(10) << advertised
          << std::setw(8) << c.passed << std::setw(8) << c.failed
          << std::setw(9) << c.skipped << "  " << coverageStateLabel(c) << "\n";
    }
  }

  const auto keys = reportCaseKeys(results, includeAll);
  if (keys.empty())
    return;
  out << "  " << (includeAll ? "all cases" : "failed cases") << ":\n";
  for (const auto &key : keys) {
    const auto &r = results.at(key);
    const std::string scores = channelScores(r);
    // Cases with no channel scores (behavioral, or a failed render) carry
    // their reason in detail or skipReason instead.
    const std::string reason = !r.detail.empty() ? r.detail : r.skipReason;
    const std::string verdict =
        includeAll ? std::string(" [") + verdictName(r.verdict) + "]" : "";
    out << "    " << key << verdict << "  "
        << (!scores.empty() ? scores : reason) << "\n";
    if (!r.description.empty())
      out << "      Description: " << r.description << "\n";
  }
}

} // namespace cts
} // namespace anari
