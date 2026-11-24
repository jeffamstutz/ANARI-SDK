// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../TestDef.h"
#include "anari/anari_cpp.hpp"
// std
#include <cstring>
#include <functional>
#include <utility>

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

// A behaviour check that asks only the device's introspection, as the
// BehaviorFn a query-only Test declares (with emptyWorld() as its build): the
// world, camera, renderer and frame size the runner passes go unused.
inline BehaviorFn queryCheck(std::function<BehaviorResult(anari::Device)> fn)
{
  return [fn = std::move(fn)](anari::Device d,
             anari::World,
             anari::Camera,
             anari::Renderer,
             uint32_t,
             uint32_t) { return fn(d); };
}

} // namespace cts
} // namespace anari
