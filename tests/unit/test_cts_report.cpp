// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
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

  const auto &passes = row(rows, "A_PASSES");
  CHECK(passes.state == CoverageState::Passed);
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
