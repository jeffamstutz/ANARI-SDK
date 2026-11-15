// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Synthetic images for the CTS unit tests, generated in code: no image file is
// checked in or fetched (ADR-0005, ADR-0010).

#pragma once

#include "cts/Image.h"
// std
#include <cstddef>
#include <cstdint>

namespace cts_test {

using anari::cts::Image;

struct Rgba
{
  uint8_t r{0}, g{0}, b{0}, a{255};
};

// Every pixel one colour.
inline Image solid(
    uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  Image im;
  im.width = w;
  im.height = h;
  im.rgba.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < im.pixelCount(); ++i) {
    im.rgba[i * 4 + 0] = r;
    im.rgba[i * 4 + 1] = g;
    im.rgba[i * 4 + 2] = b;
    im.rgba[i * 4 + 3] = a;
  }
  return im;
}

// A deterministic opaque gradient, optionally offset to perturb every pixel.
inline Image gradient(uint32_t w, uint32_t h, int offset = 0)
{
  Image im;
  im.width = w;
  im.height = h;
  im.rgba.resize(static_cast<size_t>(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const size_t i = (static_cast<size_t>(y) * w + x) * 4;
      int v = static_cast<int>((x * 7 + y * 3)) + offset;
      v = v < 0 ? 0 : (v > 255 ? 255 : v);
      im.rgba[i + 0] = static_cast<uint8_t>(v);
      im.rgba[i + 1] = static_cast<uint8_t>((v * 2) % 256);
      im.rgba[i + 2] = static_cast<uint8_t>((v + 64) % 256);
      im.rgba[i + 3] = 255;
    }
  }
  return im;
}

inline void setPixel(Image &im, uint32_t x, uint32_t y, Rgba c)
{
  const size_t i = (static_cast<size_t>(y) * im.width + x) * 4;
  im.rgba[i + 0] = c.r;
  im.rgba[i + 1] = c.g;
  im.rgba[i + 2] = c.b;
  im.rgba[i + 3] = c.a;
}

// A filled axis-aligned rectangle [x0, x1) x [y0, y1) of `fg` on `bg`: a
// subject on a background, as a rendered Case looks. Covers (x1-x0)*(y1-y0)
// pixels exactly.
inline Image rectangle(uint32_t w,
    uint32_t h,
    uint32_t x0,
    uint32_t y0,
    uint32_t x1,
    uint32_t y1,
    Rgba fg,
    Rgba bg = {})
{
  Image im = solid(w, h, bg.r, bg.g, bg.b, bg.a);
  for (uint32_t y = y0; y < y1 && y < h; ++y)
    for (uint32_t x = x0; x < x1 && x < w; ++x)
      setPixel(im, x, y, fg);
  return im;
}

// A filled disc of `fg` on `bg`, centred at (cx, cy) with radius r (pixel
// centres inside it).
inline Image disc(
    uint32_t w, uint32_t h, float cx, float cy, float r, Rgba fg, Rgba bg = {})
{
  Image im = solid(w, h, bg.r, bg.g, bg.b, bg.a);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const float dx = x + 0.5f - cx;
      const float dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r)
        setPixel(im, x, y, fg);
    }
  }
  return im;
}

} // namespace cts_test
