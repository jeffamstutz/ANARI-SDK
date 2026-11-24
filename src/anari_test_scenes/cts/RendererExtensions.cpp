// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "RendererExtensions.h"
// std
#include <algorithm>

namespace anari {
namespace cts {

namespace {

// "<prefix>: a, b", or empty for no names.
std::string prefixedList(
    const std::string &prefix, const std::vector<std::string> &names)
{
  if (names.empty())
    return {};
  std::string out = prefix + ":";
  for (size_t i = 0; i < names.size(); ++i)
    out += (i ? ", " : " ") + names[i];
  return out;
}

// A null-terminated string list, sorted and without repeats.
std::vector<std::string> toSortedList(const char *const *list)
{
  std::set<std::string> unique;
  for (size_t i = 0; list && list[i]; ++i)
    unique.insert(list[i]);
  return {unique.begin(), unique.end()};
}

} // namespace

std::set<std::string> gatingExtensions(const DeviceSpec &device)
{
  const auto &list = device.rendererExtensions.empty()
      ? device.extensions
      : device.rendererExtensions;
  return {list.begin(), list.end()};
}

std::vector<std::string> rendererMissingFeatures(
    const DeviceSpec &device, const std::vector<std::string> &missing)
{
  std::vector<std::string> out;
  if (device.rendererExtensions.empty())
    return out;
  const auto has = [](const std::vector<std::string> &list,
                       const std::string &f) {
    return std::find(list.begin(), list.end(), f) != list.end();
  };
  for (const auto &f : missing)
    if (has(device.extensions, f) && !has(device.rendererExtensions, f))
      out.push_back(f);
  return out;
}

std::vector<std::string> deviceMissingFeatures(
    const std::vector<std::string> &missing,
    const std::vector<std::string> &rendererMissing)
{
  std::vector<std::string> out;
  for (const auto &f : missing)
    if (std::find(rendererMissing.begin(), rendererMissing.end(), f)
        == rendererMissing.end())
      out.push_back(f);
  return out;
}

std::string featureSkipReason(const std::vector<std::string> &missing,
    const std::vector<std::string> &rendererMissing,
    const std::string &renderer)
{
  const std::string device =
      prefixedList("device is missing a required feature",
          deviceMissingFeatures(missing, rendererMissing));
  const std::string ofRenderer = prefixedList(
      "renderer " + renderer + " lacks a required feature", rendererMissing);
  if (device.empty())
    return ofRenderer;
  return ofRenderer.empty() ? device : device + "; " + ofRenderer;
}

std::string resolveRendererSubtype(
    anari::Device device, const std::string &renderer)
{
  if (!device)
    return renderer;
  const char **subtypes = anariGetObjectSubtypes(device, ANARI_RENDERER);
  if (!subtypes || !subtypes[0])
    return renderer;
  for (size_t i = 0; subtypes[i]; ++i)
    if (renderer == subtypes[i])
      return renderer;
  return renderer == "default" ? std::string(subtypes[0]) : renderer;
}

std::vector<std::string> queryRendererExtensions(
    anari::Device device, const std::string &renderer)
{
  if (!device)
    return {};
  const std::string subtype = resolveRendererSubtype(device, renderer);
  const auto *info = static_cast<const char *const *>(anariGetObjectInfo(
      device, ANARI_RENDERER, subtype.c_str(), "extension", ANARI_STRING_LIST));
  if (info && info[0])
    return toSortedList(info);

  // Copied before the renderer is released: the list may be its own.
  std::vector<std::string> out;
  auto r = anari::newObject<anari::Renderer>(device, subtype.c_str());
  if (!r)
    return out;
  anari::commitParameters(device, r);
  const char *const *property = nullptr;
  if (anariGetProperty(device,
          r,
          "extension",
          ANARI_STRING_LIST,
          &property,
          sizeof(property),
          ANARI_WAIT))
    out = toSortedList(property);
  anari::release(device, r);
  return out;
}

} // namespace cts
} // namespace anari
