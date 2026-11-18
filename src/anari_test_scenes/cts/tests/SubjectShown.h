// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../ExactCheck.h"

namespace anari {
namespace cts {

// The Test's subject shows: at least 5 % of the frame differs, by more than 8
// levels, from its bottom-left pixel (the background), so equivalent Cases
// can't pass by all being empty.
inline void subjectIsShown(ExactCheck &x)
{
  const auto color = x.rgba(Channel::Color);
  const auto corner = color.at(0, 0);
  size_t shown = 0;
  for (uint32_t y = 0; y < color.height; ++y)
    for (uint32_t px = 0; px < color.width; ++px)
      if (valueDistance(color.at(px, y), corner) > 8.0)
        ++shown;
  x.measure("share of the frame that isn't background",
      double(shown) / (double(color.width) * color.height),
      Limit::AtLeast,
      0.05);
}

} // namespace cts
} // namespace anari
