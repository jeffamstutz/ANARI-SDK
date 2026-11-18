// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "generators/ColorPalette.h"
// std
#include <string>
#include <vector>

// The colours the attribute Tests (geometry and instance) put in each
// attribute, so that every attribute holds colours of its own: a material
// reading `attribute2` shows other colours than one reading `attribute3`, and
// a device that reads the wrong attribute shows it.

namespace anari {
namespace cts {

// The attributes a material's `color` can name, in axis order; colour set k
// belongs to the k-th.
inline const std::vector<std::string> &attributeNames()
{
  static const std::vector<std::string> names = {
      "color", "attribute0", "attribute1", "attribute2", "attribute3"};
  return names;
}

// Colour set `set`, item `p` (a primitive, an instance): distinct per item and
// per set.
inline math::float4 setColor(int set, int p)
{
  const auto c = scenes::colors::getColorFromPalette((p + 2 * set) % 12);
  return math::float4(c.x, c.y, c.z, 1.f);
}

// A colour that is none of the set's: what a source that must lose carries.
inline math::float4 decoyColor(int set, int p)
{
  const auto c = setColor(set, p);
  return math::float4(1.f - c.x, 1.f - c.y, 1.f - c.z, 1.f);
}

} // namespace cts
} // namespace anari
