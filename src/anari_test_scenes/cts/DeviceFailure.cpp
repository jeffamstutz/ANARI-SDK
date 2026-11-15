// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeviceFailure.h"
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
constexpr int kRunRecordVersion = 1;

} // namespace

bool writeDeviceFailure(const Workdir &workdir, const DeviceFailure &failure)
{
  json j;
  j["runRecordVersion"] = kRunRecordVersion;
  j["deviceCouldNotRender"] = {{"device",
                                   {{"library", failure.device.library},
                                       {"device", failure.device.device},
                                       {"renderer", failure.device.renderer}}},
      {"reason", failure.reason}};

  const auto path = workdir.runRecordPath();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec)
    return false;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  return out && (out << j.dump(2) << "\n");
}

std::optional<DeviceFailure> readDeviceFailure(const Workdir &workdir)
{
  std::ifstream in(workdir.runRecordPath());
  if (!in)
    return std::nullopt;
  std::stringstream text;
  text << in.rdbuf();
  const json j = json::parse(text.str(), nullptr, /*allow_exceptions=*/false);
  if (!j.is_object())
    return std::nullopt;
  const auto f = j.find("deviceCouldNotRender");
  if (f == j.end() || !f->is_object())
    return std::nullopt;

  DeviceFailure failure;
  failure.reason = f->value("reason", "");
  if (const auto d = f->find("device"); d != f->end() && d->is_object()) {
    failure.device.library = d->value("library", "");
    failure.device.device = d->value("device", "default");
    failure.device.renderer = d->value("renderer", "default");
  }
  return failure;
}

void clearDeviceFailure(const Workdir &workdir)
{
  std::error_code ec;
  std::filesystem::remove(workdir.runRecordPath(), ec);
}

} // namespace cts
} // namespace anari
