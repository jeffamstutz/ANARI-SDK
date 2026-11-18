// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sidecar.h"
// nlohmann json (vendored under scenes/file/)
#include "scenes/file/nlohmann/json.hpp"
// std
#include <atomic>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace anari {
namespace cts {

using nlohmann::json;

namespace {

std::filesystem::path temporarySidecarPath(
    const std::filesystem::path &path, const char *purpose)
{
  static std::atomic_uint64_t nextId{0};
  return path.parent_path()
      / ("." + path.filename().string() + "." + purpose + "."
          + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)));
}

} // namespace

const char *verdictName(Verdict v)
{
  switch (v) {
  case Verdict::Passed:
    return "passed";
  case Verdict::Failed:
    return "failed";
  case Verdict::Skipped:
    return "skipped";
  }
  return "skipped";
}

Verdict verdictFromName(const std::string &name)
{
  if (name == "passed")
    return Verdict::Passed;
  if (name == "failed")
    return Verdict::Failed;
  return Verdict::Skipped;
}

std::string toJson(const CaseResult &result)
{
  json j;
  j["schemaVersion"] = kSidecarSchemaVersion;
  j["category"] = result.category;
  j["test"] = result.test;
  j["description"] = result.description;
  j["case"] = result.caseId;
  j["groundTruthKey"] = result.groundTruthKey;
  // Which device produced this result (schema v2): the diff tool labels runs
  // by this identity instead of by workdir name.
  j["device"] = {{"library", result.device.library},
      {"device", result.device.device},
      {"renderer", result.device.renderer},
      {"extensions", result.device.extensions}};
  j["requiredFeatures"] = result.requiredFeatures;
  j["coveredFeatures"] = result.coveredFeatures;
  j["missingFeatures"] = result.missingFeatures;
  j["catalogCoveredFeatures"] = result.catalogCoveredFeatures;
  j["verdict"] = verdictName(result.verdict);
  j["skipReason"] = result.skipReason;
  j["detail"] = result.detail;
  j["durationMs"] = result.durationMs;
  j["frameSize"] = {
      {"width", result.frameWidth}, {"height", result.frameHeight}};
  j["usesGroundTruth"] = result.usesGroundTruth;
  j["groundTruthEvaluated"] = result.groundTruthEvaluated;
  j["groundTruthReason"] = result.groundTruthReason;
  j["rendererParams"] = json::array();
  for (const auto &[name, value] : result.rendererParams)
    j["rendererParams"].push_back({{"name", name}, {"value", value}});

  // Ordered array so axis declaration order survives the round-trip.
  j["axes"] = json::array();
  for (const auto &[name, value] : result.axisValues)
    j["axes"].push_back({{"name", name}, {"value", value}});

  j["channels"] = json::array();
  for (const auto &ch : result.channels) {
    json c;
    c["channel"] = channelName(ch.channel);
    c["passed"] = ch.passed;
    c["resultImage"] = ch.resultImage;
    c["groundTruthImage"] = ch.groundTruthImage;
    // Optional debug images; absent (empty string) when not emitted.
    c["diffImage"] = ch.diffImage;
    c["thresholdImage"] = ch.thresholdImage;
    // Non-finite scores (e.g. PSNR of identical images) serialize as null.
    c["metrics"] = json::object();
    for (const auto &[name, score] : ch.metrics)
      c["metrics"][name] = score;
    c["thresholds"] = json::object();
    for (const auto &[name, threshold] : ch.thresholds)
      c["thresholds"][name] = threshold;
    j["channels"].push_back(std::move(c));
  }

  j["resultImages"] = json::object();
  for (const auto &[channel, path] : result.resultImages)
    j["resultImages"][channelName(channel)] = path;

  j["assertions"] = json::array();
  for (const auto &a : result.assertions) {
    json e;
    e["kind"] = a.kind;
    e["axis"] = a.axis;
    e["cases"] = a.relatedCases;
    e["expectation"] = a.expectation;
    e["channel"] = a.channel;
    // A non-finite score (not computed) serializes as null.
    e["score"] = a.score;
    e["threshold"] = a.threshold;
    e["tau"] = a.tau;
    e["evaluated"] = a.evaluated;
    e["passed"] = a.passed;
    e["detail"] = a.detail;
    e["partnerImage"] = a.partnerImage;
    e["maskImage"] = a.maskImage;
    e["expectedImage"] = a.expectedImage;
    e["expectedMaskImage"] = a.expectedMaskImage;
    if (!a.label.empty())
      e["label"] = a.label;
    if (a.pixels) {
      json p;
      p["offending"] = a.pixels->offending;
      p["checked"] = a.pixels->checked;
      if (a.pixels->first) {
        const auto &f = *a.pixels->first;
        p["firstOffender"] = {
            {"x", f.x}, {"y", f.y}, {"got", f.got}, {"expected", f.expected}};
      }
      e["pixels"] = std::move(p);
    }
    if (a.measurement) {
      e["measurement"] = {{"value", a.measurement->value},
          {"bound", a.measurement->bound},
          {"limit", a.measurement->limit}};
    }
    if (a.framesRendered > 0)
      e["framesRendered"] = a.framesRendered;
    if (a.refinementProgress)
      e["refinementProgress"] = *a.refinementProgress;
    j["assertions"].push_back(std::move(e));
  }

  return j.dump(2);
}

namespace {

// A finite number, or NaN for JSON null / non-numbers. The writer emits
// non-finite scores (e.g. PSNR of identical images) as null; reading them back
// as NaN keeps them out of numeric comparisons without a separate presence
// flag.
double numberOrNan(const json &j)
{
  return j.is_number() ? j.get<double>()
                       : std::numeric_limits<double>::quiet_NaN();
}

// An array of strings, or empty when absent or malformed; non-string
// elements are dropped.
std::vector<std::string> readStrings(const json &obj, const char *key)
{
  std::vector<std::string> out;
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_array())
    return out;
  for (const auto &e : *it)
    if (e.is_string())
      out.push_back(e.get<std::string>());
  return out;
}

void readMetricMap(const json &obj, std::map<std::string, double> &out)
{
  if (!obj.is_object())
    return;
  for (const auto &[name, value] : obj.items())
    out[name] = numberOrNan(value);
}

} // namespace

bool fromJson(const std::string &text, CaseResult &out, bool *schemaMismatch)
{
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (!j.is_object() || !j.contains("verdict"))
    return false; // not a sidecar

  if (schemaMismatch)
    *schemaMismatch = j.value("schemaVersion", 0) != kSidecarSchemaVersion;

  out = CaseResult{};
  out.category = j.value("category", "");
  out.test = j.value("test", "");
  out.description = j.value("description", "");
  out.caseId = j.value("case", "");
  out.groundTruthKey = j.value("groundTruthKey", "");
  if (const auto d = j.find("device"); d != j.end() && d->is_object()) {
    out.device.library = d->value("library", "");
    out.device.device = d->value("device", "default");
    out.device.renderer = d->value("renderer", "default");
    out.device.extensions = readStrings(*d, "extensions");
  }
  out.requiredFeatures = readStrings(j, "requiredFeatures");
  out.coveredFeatures = readStrings(j, "coveredFeatures");
  out.missingFeatures = readStrings(j, "missingFeatures");
  out.catalogCoveredFeatures = readStrings(j, "catalogCoveredFeatures");
  out.verdict = verdictFromName(j.value("verdict", "skipped"));
  out.skipReason = j.value("skipReason", "");
  out.detail = j.value("detail", "");
  out.durationMs = j.value("durationMs", 0.0);
  if (const auto fs = j.find("frameSize"); fs != j.end() && fs->is_object()) {
    out.frameWidth = fs->value("width", 0u);
    out.frameHeight = fs->value("height", 0u);
  }

  if (const auto axes = j.find("axes"); axes != j.end() && axes->is_array())
    for (const auto &ax : *axes)
      out.axisValues.emplace_back(ax.value("name", ""), ax.value("value", ""));

  if (const auto chs = j.find("channels"); chs != j.end() && chs->is_array()) {
    for (const auto &c : *chs) {
      ChannelResult ch;
      ch.channel = channelFromName(c.value("channel", "color"));
      ch.passed = c.value("passed", false);
      ch.resultImage = c.value("resultImage", "");
      ch.groundTruthImage = c.value("groundTruthImage", "");
      ch.diffImage = c.value("diffImage", "");
      ch.thresholdImage = c.value("thresholdImage", "");
      readMetricMap(c.value("metrics", json::object()), ch.metrics);
      readMetricMap(c.value("thresholds", json::object()), ch.thresholds);
      out.channels.push_back(std::move(ch));
    }
  }

  // Schema v4; older sidecars always compared against Ground truth, and did
  // exactly when they have channel scores.
  out.usesGroundTruth = j.value("usesGroundTruth", true);
  out.groundTruthEvaluated =
      j.value("groundTruthEvaluated", !out.channels.empty());
  out.groundTruthReason = j.value("groundTruthReason", "");
  if (const auto rp = j.find("rendererParams"); rp != j.end() && rp->is_array())
    for (const auto &p : *rp)
      out.rendererParams.emplace_back(
          p.value("name", ""), p.value("value", ""));
  if (const auto ri = j.find("resultImages");
      ri != j.end() && ri->is_object()) {
    for (const auto &[name, path] : ri->items())
      if (path.is_string())
        out.resultImages.emplace_back(
            channelFromName(name), path.get<std::string>());
  }
  if (const auto as = j.find("assertions"); as != j.end() && as->is_array()) {
    for (const auto &e : *as) {
      if (!e.is_object())
        continue;
      Assertion a;
      a.kind = e.value("kind", "");
      a.axis = e.value("axis", "");
      a.relatedCases = readStrings(e, "cases");
      a.expectation = e.value("expectation", "");
      a.channel = e.value("channel", "");
      a.score = numberOrNan(e.value("score", json()));
      a.threshold = numberOrNan(e.value("threshold", json()));
      a.tau = e.value("tau", 0);
      a.evaluated = e.value("evaluated", true);
      a.passed = e.value("passed", false);
      a.detail = e.value("detail", "");
      a.partnerImage = e.value("partnerImage", "");
      a.maskImage = e.value("maskImage", "");
      a.expectedImage = e.value("expectedImage", "");
      a.expectedMaskImage = e.value("expectedMaskImage", "");
      a.label = e.value("label", "");
      if (const auto p = e.find("pixels"); p != e.end() && p->is_object()) {
        PixelCounts counts;
        counts.offending = p->value("offending", uint64_t{0});
        counts.checked = p->value("checked", uint64_t{0});
        if (const auto f = p->find("firstOffender");
            f != p->end() && f->is_object()) {
          FirstOffender first;
          first.x = f->value("x", 0u);
          first.y = f->value("y", 0u);
          first.got = f->value("got", "");
          first.expected = f->value("expected", "");
          counts.first = std::move(first);
        }
        a.pixels = std::move(counts);
      }
      if (const auto m = e.find("measurement");
          m != e.end() && m->is_object()) {
        Measurement measurement;
        measurement.value = numberOrNan(m->value("value", json()));
        measurement.bound = numberOrNan(m->value("bound", json()));
        measurement.limit = m->value("limit", "");
        a.measurement = std::move(measurement);
      }
      a.framesRendered = e.value("framesRendered", 0u);
      if (const auto rp = e.find("refinementProgress");
          rp != e.end() && rp->is_number())
        a.refinementProgress = rp->get<double>();
      out.assertions.push_back(std::move(a));
    }
  }

  return true;
}

bool readSidecar(
    const std::filesystem::path &path, CaseResult &out, bool *schemaMismatch)
{
  std::ifstream in(path);
  if (!in)
    return false;
  std::stringstream buffer;
  buffer << in.rdbuf();
  return fromJson(buffer.str(), out, schemaMismatch);
}

bool writeSidecar(const std::filesystem::path &path, const CaseResult &result)
{
  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
      return false;
  }

  const auto stagedPath = temporarySidecarPath(path, "stage");
  {
    std::ofstream out(stagedPath);
    if (!out)
      return false;
    out << toJson(result) << '\n';
    out.close();
    if (!out) {
      std::error_code ec;
      std::filesystem::remove(stagedPath, ec);
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::path backupPath;
  if (std::filesystem::exists(path, ec)) {
    if (ec || !std::filesystem::is_regular_file(path, ec) || ec) {
      std::filesystem::remove(stagedPath, ec);
      return false;
    }
    backupPath = temporarySidecarPath(path, "backup");
    std::filesystem::rename(path, backupPath, ec);
    if (ec) {
      std::filesystem::remove(stagedPath, ec);
      return false;
    }
  } else if (ec) {
    std::filesystem::remove(stagedPath, ec);
    return false;
  }

  std::filesystem::rename(stagedPath, path, ec);
  if (ec) {
    std::filesystem::remove(stagedPath, ec);
    if (!backupPath.empty())
      std::filesystem::rename(backupPath, path, ec);
    return false;
  }

  if (!backupPath.empty())
    std::filesystem::remove(backupPath, ec);
  return true;
}

} // namespace cts
} // namespace anari
