// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

// An 8-bit RGBA raster, row-major in ANARI's convention: row 0 is the bottom
// of the image. The common currency for rendered channels, ground truth, and
// metric comparison. PNG I/O (loadPNG/savePNG) flips vertically so files on
// disk are stored top-left and view right-side-up; the in-memory data always
// stays bottom-left, matching what ANARI hands back from a mapped frame.
struct Image
{
  uint32_t width{0};
  uint32_t height{0};
  std::vector<uint8_t> rgba; // width * height * 4 bytes

  bool valid() const
  {
    return width > 0 && height > 0
        && rgba.size() == static_cast<size_t>(width) * height * 4;
  }

  size_t pixelCount() const
  {
    return static_cast<size_t>(width) * height;
  }
};

// Image helpers for derived expectations (ADR-0010) ////////////////////////

// A rectangle of whole pixels in Image coordinates (row 0 at the bottom).
struct PixelRect
{
  uint32_t x{0};
  uint32_t y{0};
  uint32_t width{0};
  uint32_t height{0};
};

// The pixels of a w x h image covered by the normalized region
// [lo, hi] (ANARI screen space: (0, 0) is the bottom left, as row 0 of an
// Image is). Empty when the region is empty, leaves [0, 1], or an edge doesn't
// fall on a pixel boundary: a region must cover whole pixels to be compared
// pixel for pixel.
std::optional<PixelRect> regionPixels(
    uint32_t w, uint32_t h, float loX, float loY, float hiX, float hiY);

// The pixels of `image` inside `rect`. Invalid when `rect` is empty or leaves
// the image.
Image crop(const Image &image, const PixelRect &rect);

// `left` and `right` side by side. Invalid unless both are valid and equally
// tall.
Image hconcat(const Image &left, const Image &right);

// `top` above `bottom`. Invalid unless both are valid and equally wide.
Image vconcat(const Image &top, const Image &bottom);

// Whether pixel `i` of a mask selects its pixel: any of its RGB channels is
// non-zero (alpha is ignored).
bool maskSelects(const Image &mask, size_t i);

// What a derived expectation returns: the image a Case must match, and
// optionally a mask of the pixels that must match (maskSelects(); without
// one, every pixel must). Implicit from an Image, so an unmasked expectation
// returns one.
struct ExpectedImage
{
  Image image;
  Image mask;

  ExpectedImage() = default;
  ExpectedImage(Image expected) : image(std::move(expected)) {}

  // Whether a mask was given (a given mask must be valid and as large as the
  // image).
  bool masked() const
  {
    return mask.width != 0 || mask.height != 0 || !mask.rgba.empty();
  }
};

// `expected`, compared only where `mask` selects (maskSelects()): a partial
// equivalence. The mask must be as large as the image.
ExpectedImage maskedBy(Image expected, Image mask);

// `inside` where `region(x, y)` holds and `outside` elsewhere, for a Case
// that must look like one sibling in part of the frame and like another in
// the rest. `region` takes normalized screen coordinates ([0, 1], (0, 0) the
// bottom left). A pixel whose square, grown by `marginPixels` on each side,
// has corners on both sides of the region's edge is not compared (the mask
// leaves it out), so an edge that falls inside a pixel, or that a device
// filters or antialiases, isn't judged. The edges must be straight lines for
// the corners to find them. Invalid when the images differ in size.
ExpectedImage regionComposite(const Image &inside,
    const Image &outside,
    const std::function<bool(float x, float y)> &region,
    float marginPixels = 2.f);

// Load a PNG as RGBA8. Returns an invalid Image (valid() == false) on failure.
Image loadPNG(const std::string &path);

// Write an RGBA8 image as a PNG, creating parent directories as needed.
// Returns false on failure (including an invalid image).
bool savePNG(const std::string &path, const Image &image);

} // namespace cts
} // namespace anari
