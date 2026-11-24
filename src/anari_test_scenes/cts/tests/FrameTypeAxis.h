// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../ExactCheck.h"
#include "../Image.h"
#include "../TestBuilder.h"
// std
#include <algorithm>
#include <cmath>
#include <string>

// The axis of a Test that renders a Channel at each of the buffer types the
// spec lists for it: a linear 8-bit type, a float type and an sRGB-encoded
// 8-bit type. The three show the same colours, in different storage:
//  - the float type's 8-bit comparison image equals the linear one's;
//  - the sRGB type holds the linear colours encoded with the sRGB transfer
//    function (the 8-bit comparison image is the buffer's bytes, undecoded).
// So the axis is a Permutation whose float and sRGB values are derived
// expectations (ADR-0010): their images are computed from the linear value's.

namespace anari {
namespace cts {

inline float srgbEncodedValue(float v)
{
  return v <= 0.0031308f ? 12.92f * v
                         : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

// The linear value of sRGB-encoded `e` (the inverse of srgbEncodedValue()).
inline float srgbDecodedValue(float e)
{
  return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}

// `linear` with its RGB encoded by the sRGB transfer function (alpha kept).
inline Image srgbEncoded(const Image &linear)
{
  Image out = linear;
  for (size_t i = 0; i + 3 < out.rgba.size(); i += 4)
    for (size_t c = 0; c < 3; ++c) {
      const float e = srgbEncodedValue(out.rgba[i + c] / 255.f);
      out.rgba[i + c] = uint8_t(std::clamp(std::lround(e * 255.f), 0l, 255l));
    }
  return out;
}

// The sRGB type can only be told from the linear one on pixels that are
// neither black nor saturated, so the frame must hold some: at least 200
// pixels of the Channel whose brightest component lies in [20, 235].
inline void hasMidtones(ExactCheck &x, Channel channel)
{
  const auto view = x.rgba(channel);
  size_t count = 0;
  for (uint32_t y = 0; y < view.height; ++y)
    for (uint32_t px = 0; px < view.width; ++px) {
      const auto c = view.at(px, y);
      const int top = std::max({int(c[0]), int(c[1]), int(c[2])});
      if (top >= 20 && top <= 235)
        ++count;
    }
  x.measure(
      "pixels that are neither dark nor saturated, so the sRGB encoding "
      "shows",
      double(count),
      Limit::AtLeast,
      200.0);
}

// Declare `axis` on `builder` with the linear, float and sRGB type names (the
// linear one is the baseline).
inline TestBuilder &frameTypeAxis(TestBuilder &builder,
    const std::string &axis,
    const std::string &linear,
    const std::string &floating,
    const std::string &srgb)
{
  return builder
      .permute(
          axis, {Any(linear.c_str()), Any(floating.c_str()), Any(srgb.c_str())})
      .expectDerived(axis,
          floating,
          "the " + linear + " image (the same colours in float storage)",
          [linear](const Siblings &s) -> ExpectedImage { return s.at(linear); })
      .expectDerived(axis,
          srgb,
          "the " + linear + " image with its colours sRGB-encoded",
          [linear](const Siblings &s) -> ExpectedImage {
            return srgbEncoded(s.at(linear));
          });
}

} // namespace cts
} // namespace anari
