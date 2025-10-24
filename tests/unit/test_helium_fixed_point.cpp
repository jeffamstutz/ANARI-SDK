// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// helium
#include "helium/helium_math.h"

TEST_CASE("helium::ufixed8 'type behavior", "[helium_fixed]")
{
  using helium::ufixed8;

  SECTION("Can construct from integer")
  {
    ufixed8 a(5);
  }

  SECTION("Can construct from float")
  {
    ufixed8 a(0.5f);
  }

  SECTION("Supports addition")
  {
    ufixed8 a(2.5f);
    ufixed8 b(3.5f);
    ufixed8 c = a + b;
    REQUIRE(c.toFloat() == Approx(6.0f));
  }

  SECTION("Supports subtraction")
  {
    ufixed8 a(5.5f);
    ufixed8 b(2.0f);
    ufixed8 c = a - b;
    REQUIRE(c.toFloat() == Approx(3.5f));
  }

  SECTION("Supports multiplication")
  {
    ufixed8 a(2.0f);
    ufixed8 b(3.0f);
    ufixed8 c = a * b;
    REQUIRE(c.toFloat() == Approx(6.0f));
  }

  SECTION("Supports division")
  {
    ufixed8 a(7.0f);
    ufixed8 b(2.0f);
    ufixed8 c = a / b;
    REQUIRE(c.toFloat() == Approx(3.5f));
  }
}
