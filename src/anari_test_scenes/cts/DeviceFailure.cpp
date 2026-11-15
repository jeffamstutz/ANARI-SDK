// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeviceFailure.h"
#include "AnariObject.h"
#include "FrameReadback.h"
// anari
#include "anari/anari_cpp/ext/linalg.h"
#include "scenes/file/nlohmann/json.hpp"
// std
#include <algorithm>
#include <exception>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

namespace {

using json = nlohmann::json;

// The record's own version, independent of the Sidecar schema.
constexpr int kRunRecordVersion = 1;

// At most this many device error messages go into a probe failure's reason.
constexpr size_t kMaxReportedErrors = 8;

// Render the probe frame: empty when it mapped, else what went wrong.
std::string renderProbeFrame(
    anari::Device d, const std::string &rendererSubtype)
{
  UniqueAnariObject<anari::World> world(d, anari::newObject<anari::World>(d));
  if (!world)
    return "the device could not create a world";
  anari::commitParameters(d, world.get());

  UniqueAnariObject<anari::Camera> camera(
      d, anari::newObject<anari::Camera>(d, "perspective"));
  if (!camera)
    return "the device could not create a perspective camera";
  anari::commitParameters(d, camera.get());

  UniqueAnariObject<anari::Renderer> renderer(
      d, anari::newObject<anari::Renderer>(d, rendererSubtype.c_str()));
  if (!renderer)
    return "the device could not create a '" + rendererSubtype + "' renderer";
  anari::commitParameters(d, renderer.get());

  UniqueAnariObject<anari::Frame> frame(d, anari::newObject<anari::Frame>(d));
  if (!frame)
    return "the device could not create a frame";
  const auto f = frame.get();
  anari::setParameter(d,
      f,
      "size",
      anari::math::vec<uint32_t, 2>(kProbeFrameSize, kProbeFrameSize));
  anari::setParameter(d, f, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, f, "world", world.get());
  anari::setParameter(d, f, "camera", camera.get());
  anari::setParameter(d, f, "renderer", renderer.get());
  anari::commitParameters(d, f);
  anari::render(d, f);
  anari::wait(d, f);

  const auto readback =
      readFrameChannel(d, f, Channel::Color, kProbeFrameSize, kProbeFrameSize);
  return readback ? std::string() : readback.detail;
}

} // namespace

std::string probeDevice(anari::Device device,
    const std::string &rendererSubtype,
    const DeviceErrorLog *errors)
{
  std::string reason;
  try {
    reason = renderProbeFrame(device, rendererSubtype);
  } catch (const std::exception &e) {
    reason = std::string("exception while rendering: ") + e.what();
  } catch (...) {
    reason = "unknown exception while rendering";
  }
  if (reason.empty() || !errors)
    return reason;

  // Each distinct message once, in the order first reported: a device that
  // fails to initialise tends to say so on every call.
  std::vector<std::pair<std::string, size_t>> distinct;
  for (const auto &message : errors->since(0)) {
    auto it = std::find_if(distinct.begin(), distinct.end(), [&](auto &d) {
      return d.first == message;
    });
    if (it == distinct.end())
      distinct.emplace_back(message, 1);
    else
      ++it->second;
  }
  if (distinct.empty())
    return reason;
  reason += "; the device reported: ";
  for (size_t i = 0; i < distinct.size() && i < kMaxReportedErrors; ++i) {
    reason += (i ? " | " : "") + distinct[i].first;
    if (distinct[i].second > 1)
      reason += " (" + std::to_string(distinct[i].second) + " times)";
  }
  if (distinct.size() > kMaxReportedErrors)
    reason += " | (and " + std::to_string(distinct.size() - kMaxReportedErrors)
        + " other messages)";
  return reason;
}

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
