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

} // namespace

bool writeReference(const Workdir &workdir, const DeviceSpec &reference)
{
  json j;
  j["referenceRecordVersion"] = kReferenceRecordVersion;
  j["library"] = reference.library;
  j["device"] = reference.device;
  j["renderer"] = reference.renderer;
  j["extensions"] = reference.extensions;

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
  if (const auto e = j.find("extensions"); e != j.end() && e->is_array())
    for (const auto &ext : *e)
      if (ext.is_string())
        reference.extensions.push_back(ext.get<std::string>());
  if (reference.library.empty())
    return std::nullopt;
  return reference;
}

} // namespace cts
} // namespace anari
