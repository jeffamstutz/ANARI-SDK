// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "cts_test_images.h"
// cts
#include "cts/Image.h"
#include "cts/Metrics.h"
// std
#include <cmath>
#include <filesystem>

using namespace anari::cts;

using cts_test::gradient;
using cts_test::solid;

// PSNR ///////////////////////////////////////////////////////////////////////

TEST_CASE("psnr is infinite for identical images", "[cts][metrics]")
{
  auto a = gradient(32, 32);
  CHECK(std::isinf(psnr(a, a)));
  CHECK(psnr(a, a) > 0.0);
}

TEST_CASE("psnr matches the closed form for a constant offset", "[cts][metrics]")
{
  // Every channel differs by 10 -> MSE = 100 -> 10*log10(255^2/100).
  auto a = solid(8, 8, 100, 100, 100, 255);
  auto b = solid(8, 8, 110, 110, 110, 255);
  const double expected = 10.0 * std::log10(255.0 * 255.0 / 100.0);
  CHECK(psnr(a, b) == Approx(expected).epsilon(0.001));
}

TEST_CASE("psnr decreases as images diverge", "[cts][metrics]")
{
  auto ref = gradient(32, 32);
  const double near = psnr(ref, gradient(32, 32, 5));
  const double far = psnr(ref, gradient(32, 32, 40));
  CHECK(near > far);
}

TEST_CASE("psnr is NaN for mismatched sizes", "[cts][metrics]")
{
  CHECK(std::isnan(psnr(solid(8, 8, 0, 0, 0, 255), solid(16, 16, 0, 0, 0, 255))));
}

// SSIM ///////////////////////////////////////////////////////////////////////

TEST_CASE("ssim is 1.0 for identical images", "[cts][metrics]")
{
  auto a = gradient(32, 32);
  CHECK(ssim(a, a) == Approx(1.0));
}

TEST_CASE("ssim decreases as images diverge", "[cts][metrics]")
{
  auto ref = gradient(64, 64);
  const double same = ssim(ref, ref);
  const double near = ssim(ref, gradient(64, 64, 8));
  const double far = ssim(ref, gradient(64, 64, 80));
  CHECK(same == Approx(1.0));
  CHECK(same > near);
  CHECK(near > far);
  CHECK(far < 1.0);
}

TEST_CASE("ssim stays within [-1, 1]", "[cts][metrics]")
{
  auto black = solid(32, 32, 0, 0, 0, 255);
  auto white = solid(32, 32, 255, 255, 255, 255);
  const double s = ssim(black, white);
  CHECK(s <= 1.0);
  CHECK(s >= -1.0);
}

TEST_CASE("ssim is NaN when smaller than the window or mismatched",
    "[cts][metrics]")
{
  CHECK(std::isnan(ssim(solid(4, 4, 0, 0, 0, 255), solid(4, 4, 0, 0, 0, 255))));
  CHECK(std::isnan(
      ssim(solid(32, 32, 0, 0, 0, 255), solid(16, 16, 0, 0, 0, 255))));
}

// Alpha compositing //////////////////////////////////////////////////////////

TEST_CASE("metrics composite alpha over white before comparing", "[cts][metrics]")
{
  // Fully transparent pixels composite to white regardless of their RGB, so a
  // transparent-black image and an opaque-white image are identical.
  auto transparentBlack = solid(16, 16, 0, 0, 0, 0);
  auto opaqueWhite = solid(16, 16, 255, 255, 255, 255);
  CHECK(std::isinf(psnr(transparentBlack, opaqueWhite)));
  CHECK(ssim(transparentBlack, opaqueWhite) == Approx(1.0));
}

// PNG round-trip /////////////////////////////////////////////////////////////

TEST_CASE("savePNG/loadPNG round-trips RGBA8 pixels", "[cts][metrics][image]")
{
  auto image = gradient(24, 16);

  const auto dir = std::filesystem::temp_directory_path()
      / "cts_image_test" / "nested";
  const auto path = (dir / "roundtrip.png").string();
  std::error_code ec;
  std::filesystem::remove_all(dir.parent_path(), ec);

  REQUIRE(savePNG(path, image)); // also creates the nested directories
  CHECK(std::filesystem::exists(path));

  auto loaded = loadPNG(path);
  REQUIRE(loaded.valid());
  CHECK(loaded.width == image.width);
  CHECK(loaded.height == image.height);
  CHECK(loaded.rgba == image.rgba);

  CHECK_FALSE(loadPNG((dir / "does_not_exist.png").string()).valid());

  std::filesystem::remove_all(dir.parent_path(), ec);
}

// Changed fraction (ADR-0010) ////////////////////////////////////////////////

TEST_CASE("changedFraction is zero for identical images", "[cts][metrics]")
{
  const auto a = gradient(32, 32);
  CHECK(changedFraction(a, a, 0) == 0.0);
}

TEST_CASE("changedFraction counts the share of pixels beyond tau",
    "[cts][metrics]")
{
  using cts_test::rectangle;
  // A 10x10 subject on a 100x100 background: 1 % of the pixels.
  const auto bg = solid(100, 100, 0, 0, 0, 255);
  const auto subject =
      rectangle(100, 100, 20, 30, 30, 40, {200, 100, 50, 255}, {0, 0, 0, 255});
  CHECK(changedFraction(bg, subject, 8) == Approx(0.01));
  // Symmetric.
  CHECK(changedFraction(subject, bg, 8) == Approx(0.01));
}

TEST_CASE("changedFraction ignores differences within tau", "[cts][metrics]")
{
  const auto a = solid(16, 16, 100, 100, 100, 255);
  const auto b = solid(16, 16, 108, 100, 100, 255); // 8 levels on one channel
  CHECK(changedFraction(a, b, 8) == 0.0);
  CHECK(changedFraction(a, b, 7) == 1.0);
  // The largest per-channel difference decides, not the sum.
  const auto c = solid(16, 16, 105, 105, 105, 255);
  CHECK(changedFraction(a, c, 8) == 0.0);
}

TEST_CASE("changedFraction with tau 0 catches a one-level change",
    "[cts][metrics]")
{
  auto a = solid(4, 4, 10, 10, 10, 255);
  auto b = a;
  b.rgba[0] = 11; // one pixel, one level
  CHECK(changedFraction(a, b, 0) == Approx(1.0 / 16.0));
}

TEST_CASE("changedFraction is NaN for mismatched sizes", "[cts][metrics]")
{
  CHECK(std::isnan(changedFraction(
      solid(8, 8, 0, 0, 0, 255), solid(16, 8, 0, 0, 0, 255), 8)));
  CHECK(std::isnan(changedFraction(Image{}, Image{}, 8)));
}

TEST_CASE("changedMask marks exactly the changed pixels", "[cts][metrics]")
{
  using cts_test::rectangle;
  const auto bg = solid(20, 10, 0, 0, 0, 255);
  const auto subject =
      rectangle(20, 10, 0, 0, 5, 2, {255, 255, 255, 255}, {0, 0, 0, 255});
  const auto mask = changedMask(bg, subject, 8);
  REQUIRE(mask.valid());
  size_t white = 0;
  for (size_t i = 0; i < mask.pixelCount(); ++i) {
    CHECK(mask.rgba[i * 4 + 3] == 255);
    white += mask.rgba[i * 4] == 255 ? 1 : 0;
  }
  CHECK(white == 10);
  CHECK_FALSE(changedMask(bg, solid(4, 4, 0, 0, 0, 255), 8).valid());
}

TEST_CASE("meanAbsoluteDifference averages RGB levels", "[cts][metrics]")
{
  const Image a = solid(4, 4, 10, 20, 30, 255);
  const Image b = solid(4, 4, 13, 20, 24, 0); // alpha ignored
  CHECK(meanAbsoluteDifference(a, a) == 0.0);
  CHECK(meanAbsoluteDifference(a, b) == Approx(3.0));
  CHECK(std::isnan(meanAbsoluteDifference(a, solid(2, 2, 0, 0, 0, 255))));
}
