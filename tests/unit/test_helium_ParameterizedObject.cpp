// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "helium/utility/ParameterizedObject.h"

namespace {

SCENARIO(
    "helium::ParameterizedObject interface", "[helium_ParameterizedObject]")
{
  GIVEN("A ParameterizedObject with a parameter set")
  {
    helium::ParameterizedObject obj;

    auto name = "test_int";

    THEN("The parameter should not be stored")
    {
      REQUIRE(!obj.hasParam(name));
    }

    int v = 5;
    obj.setParam(name, ANARI_INT32, &v);

    THEN("The named parameter should have the correct type and value")
    {
      REQUIRE(obj.hasParam(name));
      REQUIRE(obj.hasParam(name, ANARI_INT32));
      REQUIRE(obj.getParam<int>(name, 4) == v);
      REQUIRE(obj.getParam<short>(name, 4) == 4);

      int v2 = 0;
      bool success = obj.getParam(name, ANARI_INT32, &v2);
      REQUIRE(success == true);
      REQUIRE(v2 == v);
    }

    WHEN("The parameter is removed")
    {
      obj.removeParam(name);

      THEN("The paramter should no longer exist on the object")
      {
        REQUIRE(!obj.hasParam(name));
        REQUIRE(!obj.hasParam(name, ANARI_INT32));
        REQUIRE(obj.getParam<int>(name, 4) == 4);
        REQUIRE(obj.getParam<short>(name, 4) == 4);

        int v2 = 0;
        bool success = obj.getParam(name, ANARI_INT32, &v2);
        REQUIRE(success == false);
        REQUIRE(v2 == 0);
      }
    }
  }

  GIVEN("A ParameterizedObject with a string parameter")
  {
    helium::ParameterizedObject obj;

    auto name = "test_string";

    THEN("The parameter should not be stored")
    {
      REQUIRE(!obj.hasParam(name));
    }

    const char *testStr = "test";
    obj.setParam(name, ANARI_STRING, testStr);

    THEN("The named parameter should have the correct type and value")
    {
      REQUIRE(obj.hasParam(name));
      REQUIRE(obj.hasParam(name, ANARI_STRING));
      REQUIRE(obj.getParamString(name, "") == testStr);
      REQUIRE(obj.getParam<short>(name, 4) == 4);
    }

    WHEN("The parameter is removed")
    {
      obj.removeParam(name);

      THEN("The paramter should no longer exist on the object")
      {
        REQUIRE(!obj.hasParam(name));
        REQUIRE(!obj.hasParam(name, ANARI_STRING));
        REQUIRE(obj.getParamString(name, "") == "");
        REQUIRE(obj.getParam<short>(name, 4) == 4);
      }
    }
  }
}

} // namespace
