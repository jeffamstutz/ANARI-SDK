// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Reference.h"
#include "scenes/file/nlohmann/json.hpp"
// std
#include <fstream>
#include <sstream>
#include <system_error>

namespace anari {
namespace cts {

namespace {

using json = nlohmann::json;

// The record's own version, independent of the Sidecar schema.
constexpr int kReferenceRecordVersion = 1;

// The strings of array `key`, or none.
std::vector<std::string> readStrings(const json &j, const char *key)
{
  std::vector<std::string> out;
  if (const auto e = j.find(key); e != j.end() && e->is_array())
    for (const auto &s : *e)
      if (s.is_string())
        out.push_back(s.get<std::string>());
  return out;
}

} // namespace

std::string referenceRenderer(
    const std::string &library, const std::optional<std::string> &requested)
{
  if (requested)
    return *requested;
  return library == kDefaultReferenceLibrary ? kDefaultReferenceRenderer
                                             : "default";
}

bool writeReference(const Workdir &workdir, const DeviceSpec &reference)
{
  json j;
  j["referenceRecordVersion"] = kReferenceRecordVersion;
  j["library"] = reference.library;
  j["device"] = reference.device;
  j["renderer"] = reference.renderer;
  j["extensions"] = reference.extensions;
  j["rendererExtensions"] = reference.rendererExtensions;

  const auto path = workdir.referenceRecordPath();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec)
    return false;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  return out && (out << j.dump(2) << "\n");
}

std::optional<DeviceSpec> readReference(const Workdir &workdir)
{
  std::ifstream in(workdir.referenceRecordPath());
  if (!in)
    return std::nullopt;
  std::stringstream text;
  text << in.rdbuf();
  const json j = json::parse(text.str(), nullptr, /*allow_exceptions=*/false);
  if (!j.is_object() || !j.contains("library"))
    return std::nullopt;

  DeviceSpec reference;
  reference.library = j.value("library", "");
  reference.device = j.value("device", "default");
  reference.renderer = j.value("renderer", "default");
  reference.extensions = readStrings(j, "extensions");
  // Absent from a record older than it: the renderer reported none.
  reference.rendererExtensions = readStrings(j, "rendererExtensions");
  if (reference.library.empty())
    return std::nullopt;
  return reference;
}

std::string referenceConflict(
    const Workdir &workdir, const DeviceSpec &reference)
{
  const auto earlier = readReference(workdir);
  if (!earlier || sameDevice(*earlier, reference))
    return {};
  return workdir.groundTruthDir().string() + " holds Ground truth from "
      + deviceSpecLabel(*earlier) + "; generating with "
      + deviceSpecLabel(reference) + " would mix the two devices' files";
}

bool clearGroundTruth(const Workdir &workdir)
{
  std::error_code ec;
  std::filesystem::remove_all(workdir.groundTruthDir(), ec);
  return !ec;
}

} // namespace cts
} // namespace anari
