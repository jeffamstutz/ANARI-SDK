// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/DeviceFailure.h"
#include "cts/HtmlReport.h"
#include "cts/Report.h"
#include "cts/Sidecar.h"
#include "cts/Workdir.h"
// std
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace anari::cts;

namespace {

CaseResult makeCase(const std::string &category,
    const std::string &test,
    const std::string &caseId,
    Verdict verdict)
{
  CaseResult r;
  r.category = category;
  r.test = test;
  r.caseId = caseId;
  r.device = {"helide", "default", "default"};
  r.verdict = verdict;
  // A Case that ran was compared against its Ground truth.
  r.groundTruthEvaluated = verdict != Verdict::Skipped;
  return r;
}

ChannelResult colorChannel(double ssim, bool passed)
{
  ChannelResult ch;
  ch.channel = Channel::Color;
  ch.metrics = {{"ssim", ssim}, {"psnr", 30.0}};
  ch.thresholds = {{"ssim", 0.7}, {"psnr", 20.0}};
  ch.passed = passed;
  return ch;
}

std::map<std::string, CaseResult> keyed(std::vector<CaseResult> cases)
{
  std::map<std::string, CaseResult> out;
  for (auto &c : cases)
    out.emplace(c.category + "/" + c.test + "/" + c.caseId, std::move(c));
  return out;
}

} // namespace

// Sidecar reader /////////////////////////////////////////////////////////////

TEST_CASE("fromJson round-trips a written sidecar", "[cts][sidecar]")
{
  CaseResult r = makeCase("geometry", "sphere", "1_soup", Verdict::Passed);
  r.description = "Checks sphere geometry.";
  r.groundTruthKey = "1";
  r.axisValues = {{"primitiveCount", "1"}, {"primitiveMode", "soup"}};
  r.durationMs = 12.5;
  ChannelResult ch = colorChannel(0.95, true);
  ch.resultImage = "results/geometry/sphere/1_soup.color.png";
  ch.groundTruthImage = "ground_truth/geometry/sphere/1.color.png";
  r.channels.push_back(ch);

  CaseResult back;
  bool mismatch = true;
  REQUIRE(fromJson(toJson(r), back, &mismatch));
  CHECK_FALSE(mismatch);
  CHECK(back.category == "geometry");
  CHECK(back.test == "sphere");
  CHECK(back.caseId == "1_soup");
  CHECK(back.description == "Checks sphere geometry.");
  CHECK(back.device.library == "helide");
  CHECK(back.verdict == Verdict::Passed);
  CHECK(back.durationMs == Approx(12.5));
  REQUIRE(back.axisValues.size() == 2);
  CHECK(back.axisValues[0].first == "primitiveCount");
  REQUIRE(back.channels.size() == 1);
  CHECK(back.channels[0].channel == Channel::Color);
  CHECK(back.channels[0].metrics.at("ssim") == Approx(0.95));
  CHECK(back.channels[0].resultImage
      == "results/geometry/sphere/1_soup.color.png");
}

TEST_CASE("fromJson round-trips the extensions behind a Case (schema v3)",
    "[cts][sidecar]")
{
  CaseResult r = makeCase("light", "point", "default", Verdict::Skipped);
  r.device.extensions = {
      "ANARI_KHR_GEOMETRY_TRIANGLE", "ANARI_KHR_LIGHT_POINT"};
  r.requiredFeatures = {"ANARI_KHR_LIGHT_POINT",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      "ANARI_KHR_MATERIAL_MATTE"};
  r.coveredFeatures = {"ANARI_KHR_LIGHT_POINT"};
  r.missingFeatures = {"ANARI_KHR_MATERIAL_MATTE"};
  r.catalogCoveredFeatures = {
      "ANARI_KHR_GEOMETRY_TRIANGLE", "ANARI_KHR_LIGHT_POINT"};

  CaseResult back;
  bool mismatch = true;
  REQUIRE(fromJson(toJson(r), back, &mismatch));
  CHECK_FALSE(mismatch);
  CHECK(back.device.extensions == r.device.extensions);
  CHECK(back.requiredFeatures == r.requiredFeatures);
  CHECK(back.coveredFeatures == r.coveredFeatures);
  CHECK(back.missingFeatures == r.missingFeatures);
  CHECK(back.catalogCoveredFeatures == r.catalogCoveredFeatures);

  // A v2 sidecar has none of them: they read empty, flagged as a mismatch.
  CaseResult old;
  REQUIRE(
      fromJson("{\"schemaVersion\":2,\"verdict\":\"skipped\","
               "\"device\":{\"library\":\"helide\"}}",
          old,
          &mismatch));
  CHECK(mismatch);
  CHECK(old.device.extensions.empty());
  CHECK(old.requiredFeatures.empty());
  CHECK(old.coveredFeatures.empty());
  CHECK(old.missingFeatures.empty());
  CHECK(old.catalogCoveredFeatures.empty());
}

TEST_CASE("fromJson rejects a non-sidecar document", "[cts][sidecar]")
{
  CaseResult r;
  CHECK_FALSE(fromJson("{\"hello\": 1}", r));
  CHECK_FALSE(fromJson("not json", r));
}

TEST_CASE("fromJson reads a null metric as non-finite", "[cts][sidecar]")
{
  CaseResult r;
  REQUIRE(
      fromJson("{\"verdict\":\"passed\",\"channels\":[{\"channel\":\"color\","
               "\"metrics\":{\"psnr\":null}}]}",
          r));
  REQUIRE(r.channels.size() == 1);
  CHECK_FALSE(std::isfinite(r.channels[0].metrics.at("psnr")));
}

// Aggregation ////////////////////////////////////////////////////////////////

TEST_CASE("summarize counts overall and per category", "[cts][report]")
{
  auto results = keyed({
      makeCase("geometry", "sphere", "a", Verdict::Passed),
      makeCase("geometry", "cone", "b", Verdict::Failed),
      makeCase("light", "directional", "c", Verdict::Skipped),
  });

  const Summary s = summarize(results);
  CHECK(s.total == 3);
  CHECK(s.passed == 1);
  CHECK(s.failed == 1);
  CHECK(s.skipped == 1);
  CHECK(s.categories.at("geometry").passed == 1);
  CHECK(s.categories.at("geometry").failed == 1);
  CHECK(s.categories.at("light").skipped == 1);
  REQUIRE(s.failures.size() == 1);
  CHECK(s.failures[0] == "geometry/cone/b");
}

TEST_CASE(
    "runDevice returns the shared identity, none when mixed", "[cts][report]")
{
  std::ostringstream warnings;
  auto same = keyed({makeCase("geometry", "sphere", "a", Verdict::Passed),
      makeCase("light", "point", "b", Verdict::Passed)});
  const auto device = runDevice(same, warnings);
  REQUIRE(device.has_value());
  CHECK(device->library == "helide");
  CHECK(warnings.str().empty());

  auto mixed = same;
  CaseResult other = makeCase("volume", "field", "c", Verdict::Passed);
  other.device.library = "othurdevice";
  mixed.emplace("volume/field/c", other);
  std::ostringstream warnings2;
  CHECK_FALSE(runDevice(mixed, warnings2).has_value());
  CHECK(warnings2.str().find("mixed device") != std::string::npos);
}

// Per-extension coverage //////////////////////////////////////////////////////

namespace {

// A Case of `test` covering `ext`, run on a device advertising `advertised`.
CaseResult coverageCase(const std::string &test,
    const std::string &ext,
    Verdict verdict,
    const std::vector<std::string> &advertised,
    const std::vector<std::string> &missing = {})
{
  CaseResult r = makeCase("demo", test, "default", verdict);
  r.device.extensions = advertised;
  r.requiredFeatures = {ext};
  r.coveredFeatures = {ext};
  r.missingFeatures = missing;
  if (!missing.empty())
    r.skipReason = "device is missing a required feature: " + missing.front();
  else if (verdict == Verdict::Skipped)
    r.skipReason = kNoGroundTruthReason;
  return r;
}

const ExtensionCoverage &row(
    const std::vector<ExtensionCoverage> &rows, const std::string &ext)
{
  for (const auto &r : rows)
    if (r.extension == ext)
      return r;
  FAIL("no row for " << ext);
  return rows.front();
}

} // namespace

TEST_CASE("canonicalKhrExtensions lists the KHR extensions, sorted",
    "[cts][report][coverage]")
{
  const auto exts = canonicalKhrExtensions();
  CHECK(std::is_sorted(exts.begin(), exts.end()));
  CHECK(std::count(exts.begin(), exts.end(), "ANARI_KHR_LIGHT_POINT") == 1);
  CHECK(std::count(exts.begin(), exts.end(), "ANARI_KHR_CAMERA_STEREO") == 1);
  for (const auto &e : exts)
    CHECK(e.rfind("ANARI_KHR_", 0) == 0);
}

TEST_CASE("extensionCoverage gives every extension one state",
    "[cts][report][coverage]")
{
  const std::vector<std::string> advertised = {"A_PASSES",
      "A_FAILS",
      "A_NO_TRUTH",
      "A_NO_SUPPORT",
      "A_UNTESTED",
      "A_NOT_RUN",
      "A_SUPPORT"};
  const auto results = keyed({
      coverageCase("passes", "A_PASSES", Verdict::Passed, advertised),
      coverageCase("passes2", "A_PASSES", Verdict::Skipped, advertised),
      coverageCase("fails", "A_FAILS", Verdict::Failed, advertised),
      coverageCase("fails2", "A_FAILS", Verdict::Passed, advertised),
      coverageCase("notruth", "A_NO_TRUTH", Verdict::Skipped, advertised),
      coverageCase("nosupport",
          "A_NO_SUPPORT",
          Verdict::Skipped,
          advertised,
          {"A_MISSING_SUPPORT"}),
      coverageCase(
          "hidden", "N_TESTED", Verdict::Skipped, advertised, {"N_TESTED"}),
  });
  const std::set<std::string> tested = {"A_PASSES",
      "A_FAILS",
      "A_NO_TRUTH",
      "A_NO_SUPPORT",
      "A_NOT_RUN",
      "N_TESTED"};
  const auto rows =
      extensionCoverage(results, {"A_UNTESTED", "N_UNTESTED"}, tested);

  // The core row leads; the extensions follow, sorted.
  REQUIRE_FALSE(rows.empty());
  CHECK(rows.front().core);
  CHECK(std::is_sorted(rows.begin() + 1,
      rows.end(),
      [](const auto &a, const auto &b) { return a.extension < b.extension; }));

  // One Case passed, one went unscored: not plain "passed" (ADR-0010).
  const auto &passes = row(rows, "A_PASSES");
  CHECK(passes.state == CoverageState::PartlyScored);
  CHECK(passes.advertised);
  CHECK(passes.passed == 1);
  CHECK(passes.skipped == 1);

  const auto &fails = row(rows, "A_FAILS");
  CHECK(fails.state == CoverageState::Failed);
  CHECK(fails.failed == 1);
  CHECK(fails.passed == 1);

  CHECK(row(rows, "A_NO_TRUTH").state == CoverageState::NoGroundTruth);
  CHECK(row(rows, "A_NO_SUPPORT").state == CoverageState::MissingSupport);
  CHECK(row(rows, "A_NOT_RUN").state == CoverageState::NotRun);

  // Tested, but the device doesn't advertise it.
  const auto &hidden = row(rows, "N_TESTED");
  CHECK(hidden.state == CoverageState::NotAdvertised);
  CHECK_FALSE(hidden.advertised);
  CHECK(hidden.tested);
  CHECK(hidden.skipped == 1);

  // Advertised, but the CTS has no Test for it.
  const auto &untested = row(rows, "A_UNTESTED");
  CHECK(untested.state == CoverageState::AdvertisedNoTest);
  CHECK(untested.advertised);
  CHECK_FALSE(untested.tested);

  CHECK(row(rows, "N_UNTESTED").state == CoverageState::NotAdvertisedNoTest);

  // A support extension is only a row when listed: A_SUPPORT is advertised
  // but neither listed nor covered, so it has none.
  CHECK(std::none_of(rows.begin(), rows.end(), [](const auto &r) {
    return r.extension == "A_SUPPORT";
  }));
}

TEST_CASE("extensionCoverage infers advertised from Cases that ran",
    "[cts][report][coverage]")
{
  // A sidecar older than schema v3 records no device extensions.
  const auto results =
      keyed({coverageCase("passes", "A_PASSES", Verdict::Passed, {})});
  bool recorded = true;
  CHECK(runExtensions(results, &recorded).empty());
  CHECK_FALSE(recorded);
  const auto rows = extensionCoverage(results, {}, {"A_PASSES"});
  CHECK(row(rows, "A_PASSES").advertised);
  CHECK(row(rows, "A_PASSES").state == CoverageState::Passed);
}

namespace {

// A Case of a Test that covers no extension, needing only `support`.
CaseResult coreCase(const std::string &test,
    Verdict verdict,
    const std::vector<std::string> &missing = {})
{
  CaseResult r = makeCase("frame", test, "default", verdict);
  r.device.extensions = {"A_SUPPORT"};
  r.requiredFeatures = {"A_SUPPORT"};
  r.missingFeatures = missing;
  if (!missing.empty())
    r.skipReason = "device is missing a required feature: " + missing.front();
  else if (verdict == Verdict::Skipped)
    r.skipReason = kNoGroundTruthReason;
  return r;
}

} // namespace

TEST_CASE("extensionCoverage collects Tests that cover nothing in a core row",
    "[cts][report][coverage]")
{
  const std::vector<std::string> advertised = {"A_PASSES", "A_SUPPORT"};
  const auto results = keyed({
      coverageCase("passes", "A_PASSES", Verdict::Passed, advertised),
      coreCase("fails", Verdict::Failed),
      coreCase("passes", Verdict::Passed),
      coreCase("nosupport", Verdict::Skipped, {"A_SUPPORT"}),
  });
  const auto rows = extensionCoverage(results, {}, {"A_PASSES"});
  REQUIRE_FALSE(rows.empty());
  const auto &core = rows.front();
  CHECK(core.core);
  CHECK(core.extension == kCoreRow);
  CHECK(core.passed == 1);
  CHECK(core.failed == 1);
  CHECK(core.skipped == 1);
  CHECK(core.state == CoverageState::Failed);
  CHECK(coverageStateLabel(core) == std::string("failed"));
  // A Case that covers an extension counts there, not toward core.
  CHECK(row(rows, "A_PASSES").passed == 1);

  // Core is not an extension a device advertises: its skips name no
  // "advertised" state.
  const auto noTruth =
      extensionCoverage(keyed({coreCase("notruth", Verdict::Skipped)}), {}, {});
  CHECK(noTruth.front().state == CoverageState::NoGroundTruth);
  CHECK(coverageStateLabel(noTruth.front()) == std::string("no Ground truth"));

  // With no core Case in the results the row is still there, not run.
  const auto none = extensionCoverage(
      keyed({coverageCase("passes", "A_PASSES", Verdict::Passed, advertised)}),
      {},
      {"A_PASSES"});
  CHECK(none.front().core);
  CHECK(none.front().state == CoverageState::NotRun);
  CHECK(coverageStateLabel(none.front()) == std::string("not run"));
  CHECK(coverageStateLabel(row(none, "A_PASSES")) == std::string("passed"));
}

TEST_CASE("runCoveredExtensions reads the Catalog's covered set from the run",
    "[cts][report][coverage]")
{
  auto a = coverageCase("a", "A", Verdict::Passed, {"A"});
  a.catalogCoveredFeatures = {"A", "B"};
  auto b = coverageCase("b", "B", Verdict::Passed, {"B"});
  b.catalogCoveredFeatures = {"B", "C"};
  bool recorded = false;
  CHECK(runCoveredExtensions(keyed({a, b}), &recorded)
      == std::set<std::string>{"A", "B", "C"});
  CHECK(recorded);

  // Sidecars from before the run recorded it.
  const auto old = keyed({coverageCase("a", "A", Verdict::Passed, {"A"})});
  CHECK(runCoveredExtensions(old, &recorded).empty());
  CHECK_FALSE(recorded);
}

TEST_CASE("the text summary has a core row and notes an unrecorded Catalog",
    "[cts][report][coverage]")
{
  auto withCatalog = keyed({coreCase("passes", Verdict::Passed),
      coverageCase("passes", "A_PASSES", Verdict::Passed, {"A_PASSES"})});
  for (auto &[key, r] : withCatalog)
    r.catalogCoveredFeatures = {"A_PASSES"};
  std::ostringstream out;
  writeTextSummary(out,
      "run",
      withCatalog,
      false,
      extensionCoverage(withCatalog, {}, runCoveredExtensions(withCatalog)));
  const std::string text = out.str();
  const auto core = text.find("\n    core ");
  REQUIRE(core != std::string::npos);
  CHECK(text.find("passed", core) < text.find('\n', core + 1));
  CHECK(text.find("A_PASSES") > core); // core leads the matrix
  CHECK(text.find("which extensions the Catalog tests") == std::string::npos);

  auto old = withCatalog;
  for (auto &[key, r] : old)
    r.catalogCoveredFeatures.clear();
  std::ostringstream oldOut;
  writeTextSummary(oldOut,
      "run",
      old,
      false,
      extensionCoverage(old, {}, runCoveredExtensions(old)));
  CHECK(oldOut.str().find("which extensions the Catalog tests")
      != std::string::npos);
}

TEST_CASE(
    "the text summary has a per-extension table and names a missing "
    "extension",
    "[cts][report][coverage]")
{
  const std::vector<std::string> advertised = {"A_PASSES"};
  const auto results = keyed({
      coverageCase("passes", "A_PASSES", Verdict::Passed, advertised),
      coverageCase(
          "hidden", "N_TESTED", Verdict::Skipped, advertised, {"N_TESTED"}),
  });
  const auto rows =
      extensionCoverage(results, {"N_UNTESTED"}, {"A_PASSES", "N_TESTED"});
  std::ostringstream out;
  writeTextSummary(out, "run", results, true, rows);
  const std::string text = out.str();

  CHECK(text.find("by extension") != std::string::npos);
  CHECK(text.find("not advertised, no Test") != std::string::npos);
  // The skipped Case's line names the extension the device lacks.
  const auto line = text.find("demo/hidden/default [skipped]");
  REQUIRE(line != std::string::npos);
  CHECK(text.find("missing a required feature: N_TESTED", line)
      != std::string::npos);
}

TEST_CASE("generateHtml leads with the extension matrix", "[cts][html]")
{
  const std::vector<std::string> advertised = {"A_PASSES"};
  const auto results = keyed({
      coverageCase("passes", "A_PASSES", Verdict::Passed, advertised),
  });
  HtmlOptions opts;
  opts.coverage = extensionCoverage(results, {"N_UNTESTED"}, {"A_PASSES"});
  const std::string doc = generateHtml("/tmp/myrun", results, opts);

  const auto matrix = doc.find("class=\"coverage\"");
  REQUIRE(matrix != std::string::npos);
  CHECK(matrix < doc.find("class=\"summary\""));
  // A row with Cases filters the list; one without has nothing to filter.
  CHECK(
      doc.find("data-extension=\"A_PASSES\" data-cases") != std::string::npos);
  CHECK(doc.find("data-extension=\"N_UNTESTED\">") != std::string::npos);
  CHECK(doc.find("not advertised, no Test") != std::string::npos);
  // Case rows carry the extensions they are evidence for.
  CHECK(doc.find("data-ext=\"A_PASSES\"") != std::string::npos);
  // The core row is there, with nothing to filter here.
  CHECK(doc.find("data-extension=\"core\">") != std::string::npos);

  // Without coverage, no matrix.
  CHECK(generateHtml("/tmp/myrun", results, HtmlOptions{})
            .find("class=\"coverage\"")
      == std::string::npos);
}

TEST_CASE("generateHtml's core row filters to Tests that cover nothing",
    "[cts][html]")
{
  auto results = keyed({coreCase("passes", Verdict::Passed),
      coverageCase("passes", "A_PASSES", Verdict::Passed, {"A_PASSES"})});
  HtmlOptions opts;
  opts.coverage = extensionCoverage(results, {}, runCoveredExtensions(results));
  const std::string doc = generateHtml("/tmp/myrun", results, opts);

  CHECK(doc.find("data-extension=\"core\" data-cases") != std::string::npos);
  // The core Case's row is what that filter selects.
  const auto coreRow = doc.find("data-key=\"frame/passes/default\"");
  REQUIRE(coreRow != std::string::npos);
  CHECK(doc.rfind("data-ext=\"core\"", coreRow) != std::string::npos);
  // No sidecar recorded the Catalog's covered set: the matrix says so.
  CHECK(doc.find("which extensions the Catalog tests") != std::string::npos);

  for (auto &[key, r] : results)
    r.catalogCoveredFeatures = {"A_PASSES"};
  opts.coverage = extensionCoverage(results, {}, runCoveredExtensions(results));
  CHECK(generateHtml("/tmp/myrun", results, opts)
            .find("which extensions the Catalog tests")
      == std::string::npos);
}

// Encoding helpers ///////////////////////////////////////////////////////////

TEST_CASE("the text summary lines category counts up past long names",
    "[cts][report]")
{
  const auto results = keyed({
      makeCase("geometry", "sphere", "a", Verdict::Passed),
      makeCase("synchronization", "rule", "default", Verdict::Passed),
  });
  std::ostringstream out;
  writeTextSummary(out, "run", results, false);

  // The counts start in the same column on every category's line.
  std::istringstream lines(out.str());
  std::vector<size_t> columns;
  for (std::string line; std::getline(lines, line);) {
    const auto name = line.find_first_not_of(' ');
    if (name == std::string::npos || line.find(" passed ") == std::string::npos
        || line.find(" cases:") != std::string::npos)
      continue;
    columns.push_back(line.find_first_of("0123456789", name));
  }
  REQUIRE(columns.size() == 2);
  CHECK(columns[0] == columns[1]);
}

TEST_CASE("base64Encode matches RFC 4648 test vectors", "[cts][html]")
{
  CHECK(base64Encode("") == "");
  CHECK(base64Encode("f") == "Zg==");
  CHECK(base64Encode("fo") == "Zm8=");
  CHECK(base64Encode("foo") == "Zm9v");
  CHECK(base64Encode("foob") == "Zm9vYg==");
  CHECK(base64Encode("fooba") == "Zm9vYmE=");
  CHECK(base64Encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("htmlEscape neutralizes markup", "[cts][html]")
{
  CHECK(htmlEscape("<a href=\"x\">&'</a>")
      == "&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;");
}

// HTML document //////////////////////////////////////////////////////////////

TEST_CASE("generateHtml embeds summary, cases, and behavior", "[cts][html]")
{
  auto results = keyed({
      makeCase("geometry", "sphere", "a", Verdict::Passed),
      makeCase("geometry", "cone", "b", Verdict::Failed),
  });

  HtmlOptions opts; // failures-only initial view
  const std::string doc = generateHtml("/tmp/myrun", results, opts);

  CHECK(doc.find("<!DOCTYPE html>") != std::string::npos);
  CHECK(doc.find("helide (default/default)") != std::string::npos);
  CHECK(doc.find("geometry/cone/b") != std::string::npos);
  CHECK(doc.find("data-verdict=\"failed\"") != std::string::npos);
  CHECK(doc.find("data-verdict=\"passed\"") != std::string::npos); // metadata
  CHECK(doc.find("data-key=\"geometry/cone/b\"") != std::string::npos);
  CHECK(doc.find("applyFilter") != std::string::npos); // the script shipped

  // Category groups carry a deep-link anchor.
  CHECK(doc.find("id=\"cat-geometry\"") != std::string::npos);
}

TEST_CASE(
    "generateHtml references images relative to the html file", "[cts][html]")
{
  const auto root = std::filesystem::temp_directory_path() / "cts_htmlsrc_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const auto imgDir = root / "run" / "results" / "geometry" / "cone";
  REQUIRE(std::filesystem::create_directories(imgDir));
  std::ofstream(imgDir / "b.color.png") << "png";

  CaseResult failed = makeCase("geometry", "cone", "b", Verdict::Failed);
  ChannelResult ch = colorChannel(0.1, false);
  ch.resultImage = "results/geometry/cone/b.color.png";
  failed.channels = {ch};
  const auto results = keyed({failed});

  // A document outside the workdir reaches images through the workdir.
  HtmlOptions opts;
  opts.htmlPath = root / "reports" / "index.html";
  const std::string doc = generateHtml(root / "run", results, opts);
  CHECK(doc.find("src=\"../run/results/geometry/cone/b.color.png\"")
      != std::string::npos);

  // A document in the workdir root keeps the workdir-relative reference.
  HtmlOptions inRoot;
  inRoot.htmlPath = root / "run" / "report.html";
  const std::string rootDoc = generateHtml(root / "run", results, inRoot);
  CHECK(rootDoc.find("src=\"results/geometry/cone/b.color.png\"")
      != std::string::npos);

  // No destination: the legacy workdir-relative reference is preserved.
  const std::string bare = generateHtml(root / "run", results, HtmlOptions{});
  CHECK(bare.find("src=\"results/geometry/cone/b.color.png\"")
      != std::string::npos);

  std::filesystem::remove_all(root, ec);
}

// Results tree loading ///////////////////////////////////////////////////////

TEST_CASE("loadResults globs the results tree", "[cts][report]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_loadresults_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const Workdir wd(root);
  Case c;
  c.category = "geometry";
  c.testName = "sphere";
  c.values = {{"primitiveCount", Any(1), AxisKind::Permutation}};
  CaseResult r = makeCase("geometry", "sphere", c.id(), Verdict::Failed);
  REQUIRE(writeSidecar(wd.sidecarPath(c), r));

  std::ostringstream warnings;
  auto results = loadResults(root, warnings);
  REQUIRE(results.size() == 1);
  CHECK(results.begin()->second.verdict == Verdict::Failed);

  std::filesystem::remove_all(root, ec);
}

// A device that could not render ////////////////////////////////////////////

TEST_CASE(
    "A device failure record round-trips and is not a Sidecar", "[cts][report]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_devicefailure_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const Workdir wd(root);

  CHECK_FALSE(readDeviceFailure(wd));

  DeviceFailure failure;
  failure.device = {"helide_gpu", "default", "default"};
  failure.reason =
      "channel.color mapped data is null; the device reported: "
      "libSDL3.so.0: cannot open shared object file";
  REQUIRE(writeDeviceFailure(wd, failure));
  CHECK(wd.runRecordPath().parent_path() == wd.resultsDir());

  const auto back = readDeviceFailure(wd);
  REQUIRE(back);
  CHECK(back->device.library == "helide_gpu");
  CHECK(back->reason == failure.reason);

  // The results tree's sidecar reader passes over it, without a warning.
  std::ostringstream warnings;
  CHECK(loadResults(root, warnings).empty());
  CHECK(warnings.str().empty());

  clearDeviceFailure(wd);
  CHECK_FALSE(readDeviceFailure(wd));
  clearDeviceFailure(wd); // no record: nothing to do

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("The report states why the device could not render", "[cts][report]")
{
  DeviceFailure failure;
  failure.device = {"helide_gpu", "default", "default"};
  failure.reason = "channel.color mapped data is null";

  SECTION("a run that stopped at the probe has no Case table")
  {
    std::ostringstream out;
    writeTextSummary(out, "run", {}, false, {}, &failure);
    const auto text = out.str();
    CHECK(text.find("device could not render: channel.color mapped data is "
                    "null")
        != std::string::npos);
    CHECK(text.find("helide_gpu") != std::string::npos);
    CHECK(text.find("cases:") == std::string::npos);
    CHECK(text.find("by category") == std::string::npos);
  }

  SECTION("Case results left by an earlier run are flagged as such")
  {
    const auto results =
        keyed({makeCase("geometry", "sphere", "default", Verdict::Passed)});
    std::ostringstream out;
    writeTextSummary(out, "run", results, false, {}, &failure);
    const auto text = out.str();
    CHECK(text.find("device could not render") != std::string::npos);
    CHECK(text.find("from an earlier run") != std::string::npos);
    CHECK(text.find("1 cases") != std::string::npos);
  }

  SECTION("the HTML report states it too")
  {
    HtmlOptions opts;
    opts.deviceFailure = failure;
    const std::string doc = generateHtml("/tmp/myrun", {}, opts);
    CHECK(doc.find("Device could not render") != std::string::npos);
    CHECK(doc.find("channel.color mapped data is null") != std::string::npos);
    CHECK(doc.find("helide_gpu") != std::string::npos);
    CHECK(generateHtml("/tmp/myrun", {}, HtmlOptions{})
              .find("Device could not render")
        == std::string::npos);
  }
}

// Self-checks in the results tree (schema v4, ADR-0010) //////////////////////

namespace {

Assertion failedEffect(const std::string &axis, const std::string &partner)
{
  Assertion a;
  a.kind = kEffectAssertion;
  a.axis = axis;
  a.relatedCases = {partner};
  a.channel = "color";
  a.score = 0.0;
  a.threshold = 0.01;
  a.tau = 8;
  a.passed = false;
  a.detail = "changed 0.00 % of color pixels vs `" + partner
      + "`; an effect needs >= 1.00 %";
  a.partnerImage = "results/light/spot/" + partner + ".color.png";
  a.maskImage = "results/light/spot/x.effect-0.mask.png";
  return a;
}

} // namespace

TEST_CASE("fromJson round-trips assertions and the Ground-truth flags",
    "[cts][sidecar]")
{
  CaseResult r = makeCase("light", "spot", "x", Verdict::Failed);
  r.usesGroundTruth = true;
  r.groundTruthEvaluated = false;
  r.groundTruthReason = "falloff shape has no analytic answer";
  r.resultImages = {{Channel::Color, "results/light/spot/x.color.png"}};
  r.assertions.push_back(failedEffect("intensity", "base"));
  Assertion opted;
  opted.kind = kEffectAssertion;
  opted.axis = "power";
  opted.evaluated = false;
  opted.score = std::nan("");
  opted.detail = "effect check opted out: why";
  r.assertions.push_back(opted);
  Assertion future; // a kind a later schema adds is kept as-is
  future.kind = "exact";
  future.passed = true;
  r.assertions.push_back(future);

  CaseResult back;
  bool mismatch = true;
  REQUIRE(fromJson(toJson(r), back, &mismatch));
  CHECK_FALSE(mismatch);
  CHECK(back.usesGroundTruth);
  CHECK_FALSE(back.groundTruthEvaluated);
  CHECK(back.groundTruthReason == "falloff shape has no analytic answer");
  REQUIRE(back.resultImages.size() == 1);
  CHECK(back.resultImages[0].first == Channel::Color);
  REQUIRE(back.assertions.size() == 3);
  const auto &e = back.assertions[0];
  CHECK(e.kind == "effect");
  CHECK(e.axis == "intensity");
  CHECK(e.relatedCases == std::vector<std::string>{"base"});
  CHECK(e.channel == "color");
  CHECK(e.score == 0.0);
  CHECK(e.threshold == Approx(0.01));
  CHECK(e.tau == 8);
  CHECK(e.evaluated);
  CHECK_FALSE(e.passed);
  CHECK(e.partnerImage == "results/light/spot/base.color.png");
  CHECK(e.maskImage == "results/light/spot/x.effect-0.mask.png");
  CHECK_FALSE(back.assertions[1].evaluated);
  CHECK(std::isnan(back.assertions[1].score));
  CHECK(back.assertions[2].kind == "exact");
}

TEST_CASE("fromJson round-trips a Case's frame size and derived expectation",
    "[cts][sidecar]")
{
  CaseResult r =
      makeCase("camera", "camera_general", "region", Verdict::Passed);
  r.frameWidth = 128;
  r.frameHeight = 64;
  Assertion a;
  a.kind = kEquivalenceAssertion;
  a.axis = "imageRegion";
  a.relatedCases = {"none"};
  a.expectation = "the region's crop of the full frame";
  a.passed = true;
  a.expectedImage =
      "results/camera/camera_general/region.equivalence-0."
      "expected.png";
  a.expectedMaskImage =
      "results/camera/camera_general/region.equivalence-0."
      "expected-mask.png";
  r.assertions.push_back(a);

  CaseResult back;
  REQUIRE(fromJson(toJson(r), back));
  CHECK(back.frameWidth == 128);
  CHECK(back.frameHeight == 64);
  REQUIRE(back.assertions.size() == 1);
  CHECK(back.assertions[0].expectation == a.expectation);
  CHECK(back.assertions[0].expectedImage == a.expectedImage);
  CHECK(back.assertions[0].expectedMaskImage == a.expectedMaskImage);

  // Older v4 sidecars have none of it.
  CaseResult old;
  REQUIRE(
      fromJson("{\"schemaVersion\":4,\"verdict\":\"passed\","
               "\"assertions\":[{\"kind\":\"equivalence\"}]}",
          old));
  CHECK(old.frameWidth == 0);
  CHECK(old.frameHeight == 0);
  REQUIRE(old.assertions.size() == 1);
  CHECK(old.assertions[0].expectation.empty());
  CHECK(old.assertions[0].expectedImage.empty());
}

TEST_CASE("a v3 sidecar reads as compared exactly when it has scores",
    "[cts][sidecar]")
{
  CaseResult scored, unscored;
  REQUIRE(
      fromJson("{\"schemaVersion\":3,\"verdict\":\"passed\","
               "\"channels\":[{\"channel\":\"color\",\"passed\":true}]}",
          scored));
  CHECK(scored.usesGroundTruth);
  CHECK(scored.groundTruthEvaluated);
  CHECK(scored.assertions.empty());
  CHECK_FALSE(scored.passedWithoutGroundTruth());
  REQUIRE(fromJson("{\"schemaVersion\":3,\"verdict\":\"skipped\"}", unscored));
  CHECK_FALSE(unscored.groundTruthEvaluated);
}

TEST_CASE(
    "failureKinds tells self-check failures from Ground truth", "[cts][report]")
{
  CaseResult effect = makeCase("light", "spot", "a", Verdict::Failed);
  effect.groundTruthEvaluated = false;
  effect.assertions = {failedEffect("intensity", "base")};
  CHECK(failureKinds(effect) == std::vector<std::string>{"effect"});

  CaseResult both = effect;
  both.groundTruthEvaluated = true;
  both.channels = {colorChannel(0.2, false)};
  CHECK(failureKinds(both)
      == std::vector<std::string>{"effect", kGroundTruthFailure});

  CaseResult crashed = makeCase("light", "spot", "c", Verdict::Failed);
  crashed.groundTruthEvaluated = false;
  CHECK(failureKinds(crashed) == std::vector<std::string>{kOtherFailure});

  CHECK(failureKinds(makeCase("light", "spot", "d", Verdict::Passed)).empty());
}

TEST_CASE("the text summary splits failures by kind and names the partner",
    "[cts][report]")
{
  CaseResult effect = makeCase("light", "spot", "moved", Verdict::Failed);
  effect.groundTruthEvaluated = false;
  effect.assertions = {failedEffect("position", "base")};

  CaseResult equivalence =
      makeCase("geometry", "quad", "vec4", Verdict::Failed);
  equivalence.groundTruthEvaluated = false;
  Assertion eq;
  eq.kind = kEquivalenceAssertion;
  eq.axis = "tangent";
  eq.relatedCases = {"unset"};
  eq.detail = "changed 3.00 % of color pixels vs `unset`";
  equivalence.assertions = {eq};

  CaseResult truth = makeCase("geometry", "cone", "a", Verdict::Failed);
  truth.channels = {colorChannel(0.3, false)};

  CaseResult selfChecked = makeCase("light", "spot", "base", Verdict::Passed);
  selfChecked.groundTruthEvaluated = false;

  const auto results = keyed({effect, equivalence, truth, selfChecked});
  const Summary s = summarize(results);
  CHECK(s.passedWithoutGroundTruth == 1);
  CHECK(s.failuresByKind.at("effect") == 1);
  CHECK(s.failuresByKind.at("equivalence") == 1);
  CHECK(s.failuresByKind.at(kGroundTruthFailure) == 1);

  std::ostringstream os;
  writeTextSummary(os, "run", results, false);
  const std::string text = os.str();
  CHECK(text.find("1 passed, Ground truth not evaluated") != std::string::npos);
  CHECK(text.find("failures by kind: Ground truth 1, effect 1, equivalence 1")
      != std::string::npos);
  CHECK(text.find("effect failed (axis position): changed 0.00 % of color "
                  "pixels vs `base`")
      != std::string::npos);
  CHECK(text.find("equivalence failed (axis tangent)") != std::string::npos);
}

TEST_CASE("extensionCoverage classifies by verdicts and assertions",
    "[cts][report][coverage]")
{
  const std::vector<std::string> advertised = {"A_SELF", "A_MIXED", "A_ODD"};
  auto self = coverageCase("self", "A_SELF", Verdict::Passed, advertised);
  self.groundTruthEvaluated = false; // passed on self-checks only
  // A skip that names no missing extension is a skip for want of something
  // to evaluate, whatever its reason says.
  auto odd = coverageCase("odd", "A_ODD", Verdict::Skipped, advertised);
  odd.skipReason = "some other wording";
  const auto results = keyed({
      self,
      coverageCase("mixed", "A_MIXED", Verdict::Passed, advertised),
      coverageCase("mixed2", "A_MIXED", Verdict::Skipped, advertised),
      odd,
  });
  const auto rows =
      extensionCoverage(results, {}, {"A_SELF", "A_MIXED", "A_ODD"});
  const auto &selfRow = row(rows, "A_SELF");
  CHECK(selfRow.state == CoverageState::PassedWithoutGroundTruth);
  CHECK(selfRow.passedWithoutGroundTruth == 1);
  CHECK(coverageStateLabel(selfRow) == "passed, Ground truth not evaluated");
  CHECK(row(rows, "A_MIXED").state == CoverageState::PartlyScored);
  CHECK(row(rows, "A_ODD").state == CoverageState::NoGroundTruth);
}

TEST_CASE("a Case the Reference device can't render reads as such",
    "[cts][report][coverage][reference]")
{
  const std::vector<std::string> advertised = {"A_GAP", "A_NOT_GENERATED"};
  auto gap = coverageCase("gap", "A_GAP", Verdict::Skipped, advertised);
  gap.reference = {"halcyon", "default", "preview"};
  gap.referenceMissingFeatures = {"A_GAP"};
  gap.skipReason = noGroundTruthReason(gap);
  auto notGenerated = coverageCase(
      "not_generated", "A_NOT_GENERATED", Verdict::Skipped, advertised);
  notGenerated.reference = gap.reference;
  const auto results = keyed({gap, notGenerated});
  const auto rows =
      extensionCoverage(results, {}, {"A_GAP", "A_NOT_GENERATED"});
  CHECK(row(rows, "A_GAP").state == CoverageState::ReferenceLacks);
  CHECK(coverageStateLabel(row(rows, "A_GAP"))
      == "advertised, Reference device lacks it");
  CHECK(row(rows, "A_NOT_GENERATED").state == CoverageState::NoGroundTruth);

  std::ostringstream out;
  writeTextSummary(out, "run", results, true, rows);
  const std::string text = out.str();
  CHECK(text.find("  reference: halcyon (default/preview)\n")
      != std::string::npos);
  CHECK(
      text.find("reference device (halcyon) lacks A_GAP") != std::string::npos);

  const std::string doc = generateHtml("/tmp/myrun", results, HtmlOptions{});
  CHECK(doc.find("Reference device") != std::string::npos);
  CHECK(doc.find("halcyon (default/preview)") != std::string::npos);
}

TEST_CASE("a Case its renderer can't run reads as the renderer's gap",
    "[cts][report][coverage][renderer_extensions]")
{
  // The device advertises A_LENS and A_SUPPORT; its renderer reports
  // neither.
  const std::vector<std::string> advertised = {"A_LENS", "A_SUPPORT"};
  auto lens =
      coverageCase("lens", "A_LENS", Verdict::Skipped, advertised, {"A_LENS"});
  lens.rendererMissingFeatures = {"A_LENS"};
  lens.skipReason = "renderer preview lacks a required feature: A_LENS";
  // Lacking a support extension is a missing support, as for the device.
  auto support = coverageCase(
      "support", "A_SUPPORTED", Verdict::Skipped, advertised, {"A_SUPPORT"});
  support.device.extensions.push_back("A_SUPPORTED");
  support.rendererMissingFeatures = {"A_SUPPORT"};
  // The renderer's gap outranks a sibling Case missing its Ground truth.
  auto noTruth = coverageCase("lens2", "A_LENS", Verdict::Skipped, advertised);
  const auto results = keyed({lens, support, noTruth});
  const auto rows = extensionCoverage(results, {}, {"A_LENS", "A_SUPPORTED"});
  CHECK(row(rows, "A_LENS").state == CoverageState::RendererLacks);
  CHECK(row(rows, "A_LENS").advertised);
  CHECK(coverageStateLabel(row(rows, "A_LENS"))
      == "advertised, renderer lacks it");
  CHECK(row(rows, "A_SUPPORTED").state == CoverageState::MissingSupport);

  // A device-level miss stays "not advertised".
  auto absent = coverageCase(
      "absent", "N_ABSENT", Verdict::Skipped, advertised, {"N_ABSENT"});
  const auto other = extensionCoverage(keyed({absent}), {}, {"N_ABSENT"});
  CHECK(row(other, "N_ABSENT").state == CoverageState::NotAdvertised);

  std::ostringstream out;
  writeTextSummary(out, "run", results, true, rows);
  CHECK(out.str().find("advertised, renderer lacks it") != std::string::npos);
  CHECK(out.str().find("renderer preview lacks a required feature: A_LENS")
      != std::string::npos);
  const std::string doc = generateHtml("/tmp/myrun", results, [&] {
    HtmlOptions html;
    html.coverage = rows;
    return html;
  }());
  CHECK(doc.find("advertised, renderer lacks it") != std::string::npos);
}

TEST_CASE("runReference is the one Reference a run's Sidecars name",
    "[cts][report][reference]")
{
  auto a = makeCase("demo", "a", "default", Verdict::Passed);
  auto b = makeCase("demo", "b", "default", Verdict::Passed);
  std::ostringstream warnings;
  CHECK_FALSE(runReference(keyed({a, b}), warnings));

  a.reference = {"halcyon", "default", "preview"};
  const auto one = runReference(keyed({a, b}), warnings);
  REQUIRE(one);
  CHECK(one->library == "halcyon");
  CHECK(warnings.str().empty());

  b.reference = {"helide", "default", "default"};
  CHECK_FALSE(runReference(keyed({a, b}), warnings));
  CHECK(warnings.str().find("mixed Reference devices") != std::string::npos);
}

TEST_CASE("generateHtml reads Sidecars with assertions", "[cts][html]")
{
  CaseResult effect = makeCase("light", "spot", "moved", Verdict::Failed);
  effect.groundTruthEvaluated = false;
  effect.assertions = {failedEffect("position", "base")};
  const auto results = keyed({effect});
  const std::string doc = generateHtml("/tmp/myrun", results, HtmlOptions{});
  CHECK(doc.find("light/spot/moved") != std::string::npos);
}

TEST_CASE("an opted-out assertion keeps a row from reading plain passed",
    "[cts][report][coverage]")
{
  auto opted = coverageCase("opted", "A_OPTED", Verdict::Passed, {"A_OPTED"});
  Assertion a;
  a.kind = kEffectAssertion;
  a.evaluated = false;
  opted.assertions = {a};
  const auto rows = extensionCoverage(keyed({opted}), {}, {"A_OPTED"});
  CHECK(row(rows, "A_OPTED").state == CoverageState::PartlyScored);
}

// Self-checks in the HTML report (ticket 19) /////////////////////////////////

TEST_CASE("generateHtml shows a failed effect with both images and the mask",
    "[cts][html]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_htmlasrt_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const auto dir = root / "run" / "results" / "light" / "spot";
  REQUIRE(std::filesystem::create_directories(dir));
  for (const char *f :
      {"moved.color.png", "base.color.png", "x.effect-0.mask.png"})
    std::ofstream(dir / f) << "png";

  CaseResult moved = makeCase("light", "spot", "moved", Verdict::Failed);
  moved.groundTruthEvaluated = false;
  moved.resultImages = {{Channel::Color, "results/light/spot/moved.color.png"}};
  moved.assertions = {failedEffect("position", "base")};
  CaseResult base = makeCase("light", "spot", "base", Verdict::Passed);
  base.groundTruthEvaluated = false;

  const std::string doc =
      generateHtml(root / "run", keyed({moved, base}), HtmlOptions{});
  CHECK(doc.find("data-kind=\"effect\"") != std::string::npos);
  CHECK(doc.find("axis <span class=\"kmono\">position</span>")
      != std::string::npos);
  // The partner is a link to its row, which carries the matching id.
  CHECK(doc.find("data-target=\"case-light/spot/base\"") != std::string::npos);
  CHECK(doc.find("id=\"case-light/spot/base\"") != std::string::npos);
  // This Case's image, the partner's and the mask.
  CHECK(doc.find("src=\"results/light/spot/moved.color.png\"")
      != std::string::npos);
  CHECK(doc.find("src=\"results/light/spot/base.color.png\"")
      != std::string::npos);
  CHECK(doc.find("src=\"results/light/spot/x.effect-0.mask.png\"")
      != std::string::npos);
  CHECK(doc.find("data-kinds=\"effect\"") != std::string::npos);
  CHECK(
      doc.find("class=\"kindseg\" data-kind=\"effect\"") != std::string::npos);
  CHECK(doc.find("Ground truth not evaluated") != std::string::npos);

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("generateHtml shows a failed equivalence with its expected image",
    "[cts][html]")
{
  const auto root = std::filesystem::temp_directory_path() / "cts_htmleq_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const auto dir = root / "run" / "results" / "camera" / "g";
  REQUIRE(std::filesystem::create_directories(dir));
  for (const char *f :
      {"region.color.png", "region.exp.png", "region.mask.png"})
    std::ofstream(dir / f) << "png";

  CaseResult region = makeCase("camera", "g", "region", Verdict::Failed);
  region.resultImages = {{Channel::Color, "results/camera/g/region.color.png"}};
  Assertion eq;
  eq.kind = kEquivalenceAssertion;
  eq.axis = "imageRegion";
  eq.relatedCases = {"none"};
  eq.channel = "color";
  eq.score = 0.05;
  eq.threshold = 0.002;
  eq.passed = false;
  eq.expectation = "the region's crop of the full frame";
  eq.detail = "changed 5.00 % of color pixels";
  eq.expectedImage = "results/camera/g/region.exp.png";
  eq.maskImage = "results/camera/g/region.mask.png";
  region.assertions = {eq};

  const std::string doc =
      generateHtml(root / "run", keyed({region}), HtmlOptions{});
  CHECK(doc.find("data-kind=\"equivalence\"") != std::string::npos);
  CHECK(
      doc.find("the region&#39;s crop of the full frame") != std::string::npos);
  CHECK(doc.find("<figcaption>expected</figcaption>") != std::string::npos);
  CHECK(
      doc.find("src=\"results/camera/g/region.exp.png\"") != std::string::npos);
  CHECK(doc.find("src=\"results/camera/g/region.mask.png\"")
      != std::string::npos);
  // `none` is not a Case of the run (a support render): no link.
  CHECK(doc.find("data-target=\"case-camera/g/none\"") == std::string::npos);

  std::filesystem::remove_all(root, ec);
}

TEST_CASE(
    "generateHtml shows an opted-out axis and an unknown kind", "[cts][html]")
{
  CaseResult r = makeCase("light", "spot", "a", Verdict::Passed);
  Assertion opted;
  opted.kind = kEffectAssertion;
  opted.axis = "power";
  opted.evaluated = false;
  opted.score = std::nan("");
  opted.detail = "effect check opted out: power only scales an invisible term";
  Assertion odd;
  odd.kind = "futurekind";
  odd.passed = false;
  odd.detail = "something a later schema measures";
  r.verdict = Verdict::Failed;
  r.assertions = {opted, odd};

  const std::string doc = generateHtml("/tmp/myrun", keyed({r}), HtmlOptions{});
  CHECK(doc.find("opted out: power only scales an invisible term")
      != std::string::npos);
  CHECK(doc.find("<span class=\"res skip-fg\">opted out</span>")
      != std::string::npos);
  CHECK(doc.find("data-kind=\"futurekind\"") != std::string::npos);
  CHECK(doc.find("something a later schema measures") != std::string::npos);
  CHECK(doc.find("data-kinds=\"futurekind\"") != std::string::npos);
}

TEST_CASE("generateHtml shows exact and convergence findings", "[cts][html]")
{
  CaseResult r = makeCase("geometry", "quad", "a", Verdict::Failed);
  Assertion ex;
  ex.kind = kExactAssertion;
  ex.label = "ids";
  ex.passed = false;
  ex.pixels = PixelCounts{3, 100, FirstOffender{4, 5, "7", "0"}};
  Assertion conv;
  conv.kind = kConvergenceAssertion;
  conv.passed = false;
  conv.score = 0.1;
  conv.threshold = 0.002;
  conv.framesRendered = 12;
  conv.refinementProgress = 0.5;
  r.assertions = {ex, conv};
  const std::string doc = generateHtml("/tmp/myrun", keyed({r}), HtmlOptions{});
  CHECK(doc.find("3 of 100 pixels offend; first at (4, 5): got 7, expected 0")
      != std::string::npos);
  CHECK(doc.find("frames rendered 12, refinementProgress 0.500")
      != std::string::npos);
}

TEST_CASE("generateHtml counts and shows Cases that weren't guarded",
    "[cts][html][convergence]")
{
  CaseResult r = makeCase("material", "pbr", "a", Verdict::Passed);
  r.notGuarded = "the renderer lacks ANARI_KHR_FRAME_ACCUMULATION";
  Assertion effect; // what ran unguarded
  effect.kind = kEffectAssertion;
  effect.axis = "specular";
  effect.passed = true;
  r.assertions = {effect};
  CaseResult guarded = makeCase("material", "pbr", "b", Verdict::Passed);
  const std::string doc =
      generateHtml("/tmp/myrun", keyed({r, guarded}), HtmlOptions{});
  // The summary's count, with the reason, and the Case's own row.
  CHECK(doc.find("Not guarded") != std::string::npos);
  CHECK(doc.find("not guarded: the renderer lacks "
                 "ANARI_KHR_FRAME_ACCUMULATION")
      != std::string::npos);

  const std::string plain =
      generateHtml("/tmp/myrun", keyed({guarded}), HtmlOptions{});
  CHECK(plain.find("Not guarded") == std::string::npos);
}
