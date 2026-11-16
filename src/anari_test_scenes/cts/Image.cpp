// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image.h"
// stb (implementation lives in the stb_image static library)
#include "stb_image.h"
#include "stb_image_write.h"
// std
#include <cmath>
#include <cstring>
#include <filesystem>

namespace anari {
namespace cts {

Image loadPNG(const std::string &path)
{
  int w = 0, h = 0, channels = 0;
  // Files are stored top-left; flip on load so row 0 lands at the bottom again,
  // keeping the in-memory Image in ANARI's bottom-left convention. Restore the
  // global flag afterward so we don't leak it into the scene texture loader,
  // which manages this flag itself.
  stbi_set_flip_vertically_on_load(1);
  // Force 4 channels (RGBA) regardless of the file's native channel count.
  unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
  stbi_set_flip_vertically_on_load(0);
  if (pixels == nullptr || w <= 0 || h <= 0) {
    if (pixels)
      stbi_image_free(pixels);
    return {};
  }

  Image image;
  image.width = static_cast<uint32_t>(w);
  image.height = static_cast<uint32_t>(h);
  const size_t count = static_cast<size_t>(w) * h * 4;
  image.rgba.assign(pixels, pixels + count);
  stbi_image_free(pixels);
  return image;
}

namespace {

// The pixel boundary a normalized coordinate falls on, if it falls on one.
std::optional<uint32_t> pixelEdge(float t, uint32_t size)
{
  if (!(t >= 0.f && t <= 1.f))
    return std::nullopt;
  const double edge = static_cast<double>(t) * size;
  const double rounded = std::round(edge);
  if (std::abs(edge - rounded) > 1e-3)
    return std::nullopt;
  return static_cast<uint32_t>(rounded);
}

Image blank(uint32_t w, uint32_t h)
{
  Image out;
  out.width = w;
  out.height = h;
  out.rgba.resize(static_cast<size_t>(w) * h * 4);
  return out;
}

// Copy `src` into `dst` with its bottom-left pixel at (x, y).
void blit(Image &dst, const Image &src, uint32_t x, uint32_t y)
{
  const size_t row = static_cast<size_t>(src.width) * 4;
  for (uint32_t r = 0; r < src.height; ++r)
    std::memcpy(&dst.rgba[((static_cast<size_t>(y) + r) * dst.width + x) * 4],
        &src.rgba[static_cast<size_t>(r) * row],
        row);
}

} // namespace

std::optional<PixelRect> regionPixels(
    uint32_t w, uint32_t h, float loX, float loY, float hiX, float hiY)
{
  const auto x0 = pixelEdge(loX, w);
  const auto y0 = pixelEdge(loY, h);
  const auto x1 = pixelEdge(hiX, w);
  const auto y1 = pixelEdge(hiY, h);
  if (!x0 || !y0 || !x1 || !y1 || *x1 <= *x0 || *y1 <= *y0)
    return std::nullopt;
  return PixelRect{*x0, *y0, *x1 - *x0, *y1 - *y0};
}

Image crop(const Image &image, const PixelRect &rect)
{
  if (!image.valid() || rect.width == 0 || rect.height == 0
      || static_cast<uint64_t>(rect.x) + rect.width > image.width
      || static_cast<uint64_t>(rect.y) + rect.height > image.height)
    return {};
  Image out = blank(rect.width, rect.height);
  const size_t row = static_cast<size_t>(rect.width) * 4;
  for (uint32_t r = 0; r < rect.height; ++r)
    std::memcpy(&out.rgba[r * row],
        &image.rgba[((static_cast<size_t>(rect.y) + r) * image.width + rect.x)
            * 4],
        row);
  return out;
}

Image hconcat(const Image &left, const Image &right)
{
  if (!left.valid() || !right.valid() || left.height != right.height)
    return {};
  Image out = blank(left.width + right.width, left.height);
  blit(out, left, 0, 0);
  blit(out, right, left.width, 0);
  return out;
}

Image vconcat(const Image &top, const Image &bottom)
{
  if (!top.valid() || !bottom.valid() || top.width != bottom.width)
    return {};
  // Row 0 is the bottom: `bottom` goes first.
  Image out = blank(top.width, top.height + bottom.height);
  blit(out, bottom, 0, 0);
  blit(out, top, 0, bottom.height);
  return out;
}

bool maskSelects(const Image &mask, size_t i)
{
  return mask.rgba[i * 4 + 0] != 0 || mask.rgba[i * 4 + 1] != 0
      || mask.rgba[i * 4 + 2] != 0;
}

ExpectedImage maskedBy(Image expected, Image mask)
{
  ExpectedImage out(std::move(expected));
  out.mask = std::move(mask);
  return out;
}

ExpectedImage regionComposite(const Image &inside,
    const Image &outside,
    const std::function<bool(float x, float y)> &region,
    float marginPixels)
{
  if (!inside.valid() || !outside.valid() || inside.width != outside.width
      || inside.height != outside.height)
    return {};
  const uint32_t w = inside.width;
  const uint32_t h = inside.height;
  Image expected = blank(w, h);
  Image mask = blank(w, h);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      // Corners inset by a hair, so an edge on the pixel's own border
      // doesn't count as crossing it.
      const float g = marginPixels - 1e-3f;
      const float lo[2] = {(x - g) / w, (y - g) / h};
      const float hi[2] = {(x + 1 + g) / w, (y + 1 + g) / h};
      const bool in = region(lo[0], lo[1]);
      const bool decided = region(hi[0], lo[1]) == in
          && region(lo[0], hi[1]) == in && region(hi[0], hi[1]) == in;
      const size_t i = (static_cast<size_t>(y) * w + x) * 4;
      std::memcpy(&expected.rgba[i], &(in ? inside : outside).rgba[i], 4);
      const uint8_t m = decided ? 255 : 0;
      mask.rgba[i + 0] = m;
      mask.rgba[i + 1] = m;
      mask.rgba[i + 2] = m;
      mask.rgba[i + 3] = 255;
    }
  }
  return maskedBy(std::move(expected), std::move(mask));
}

bool savePNG(const std::string &path, const Image &image)
{
  if (!image.valid())
    return false;

  const std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec)
      return false;
  }

  // Image rows run bottom-to-top (ANARI convention); flip on write so the PNG
  // file is stored top-left and views right-side-up. Restore the global flag
  // afterward to keep this self-contained.
  const int stride = static_cast<int>(image.width) * 4;
  stbi_flip_vertically_on_write(1);
  const int ok = stbi_write_png(path.c_str(),
      static_cast<int>(image.width),
      static_cast<int>(image.height),
      4,
      image.rgba.data(),
      stride);
  stbi_flip_vertically_on_write(0);
  return ok != 0;
}

} // namespace cts
} // namespace anari
