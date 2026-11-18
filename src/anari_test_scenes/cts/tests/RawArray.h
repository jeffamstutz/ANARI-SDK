// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <cstring>

// Arrays of an element type the C++ wrappers have no C++ type for (the
// fixed-point and sRGB colour types, the 64-bit index vectors), filled from
// raw bytes: the array is created, mapped, written and unmapped.

namespace anari {
namespace cts {

inline anari::Array1D makeRawArray1D(
    anari::Device d, ANARIDataType type, uint64_t count, const void *bytes)
{
  auto array = anari::newArray1D(d, type, count);
  void *dst = anariMapArray(d, array);
  if (dst && bytes)
    std::memcpy(dst, bytes, anari::sizeOf(type) * count);
  anariUnmapArray(d, array);
  return array;
}

inline anari::Array2D makeRawArray2D(anari::Device d,
    ANARIDataType type,
    uint64_t width,
    uint64_t height,
    const void *bytes)
{
  auto array = anari::newArray2D(d, type, width, height);
  void *dst = anariMapArray(d, array);
  if (dst && bytes)
    std::memcpy(dst, bytes, anari::sizeOf(type) * width * height);
  anariUnmapArray(d, array);
  return array;
}

} // namespace cts
} // namespace anari
