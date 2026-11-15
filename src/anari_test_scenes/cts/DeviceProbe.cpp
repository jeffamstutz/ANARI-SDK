// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeviceProbe.h"
#include "AnariObject.h"
#include "FrameReadback.h"
// anari
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

namespace {

// At most this many device error messages go into a probe failure's reason.
constexpr size_t kMaxReportedErrors = 8;

// Perspective, unless the device lists camera subtypes and it isn't one of
// them: then the first it lists.
std::string probeCameraSubtype(anari::Device d)
{
  const char **subtypes = anariGetObjectSubtypes(d, ANARI_CAMERA);
  if (!subtypes || !subtypes[0])
    return "perspective";
  for (int i = 0; subtypes[i]; ++i)
    if (std::string(subtypes[i]) == "perspective")
      return "perspective";
  return subtypes[0];
}

// Render the probe frame: empty when it mapped, else what went wrong.
std::string renderProbeFrame(
    anari::Device d, const std::string &rendererSubtype)
{
  UniqueAnariObject<anari::World> world(d, anari::newObject<anari::World>(d));
  if (!world)
    return "the device could not create a world";
  anari::commitParameters(d, world.get());

  const std::string cameraSubtype = probeCameraSubtype(d);
  UniqueAnariObject<anari::Camera> camera(
      d, anari::newObject<anari::Camera>(d, cameraSubtype.c_str()));
  if (!camera)
    return "the device could not create a '" + cameraSubtype + "' camera";
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

} // namespace cts
} // namespace anari
