// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
// std
#include <cstring>

// Query-level checks: what a device's introspection says an object accepts.
namespace anari {
namespace cts {

// Whether the device lists parameter `name` of data type `type` on objects of
// `objectType` and `subtype` (null for an object without subtypes, such as a
// frame).
inline bool listsParameter(anari::Device d,
    ANARIDataType objectType,
    const char *subtype,
    const char *name,
    ANARIDataType type)
{
  const auto *parameters =
      static_cast<const ANARIParameter *>(anariGetObjectInfo(
          d, objectType, subtype, "parameter", ANARI_PARAMETER_LIST));
  for (int i = 0; parameters && parameters[i].name; ++i)
    if (std::strcmp(parameters[i].name, name) == 0
        && parameters[i].type == type)
      return true;
  return false;
}

} // namespace cts
} // namespace anari
