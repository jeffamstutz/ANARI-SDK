// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// helium
#include "helium/utility/AnariAny.h"
// std
#include <cstring>
#include <string>
#include <type_traits>

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(helium::RefCounted *, ANARI_OBJECT);
ANARI_TYPEFOR_DEFINITION(helium::RefCounted *);
} // namespace anari

namespace {

template <typename T>
static constexpr const bool IsStringListLike =
    std::is_pointer_v<T> && std::is_pointer_v<std::remove_pointer_t<T>>
    && std::is_same_v<
        std::decay_t<std::remove_pointer_t<std::remove_pointer_t<T>>>,
        char>;

using helium::AnariAny;

template <typename T, typename Enabler = void>
struct VerifyValue
{
  static inline void verify_value(const AnariAny &v, T correctValue)
  {
    REQUIRE(v.valid());
    REQUIRE(v.is<T>());
    REQUIRE(v.get<T>() == correctValue);
  }
};

template <>
struct VerifyValue<const char *>
{
  static inline void verify_value(const AnariAny &v, const char *correctValue)
  {
    REQUIRE(v.valid());
    REQUIRE(v.type() == ANARI_STRING);
    REQUIRE(v.getString() == correctValue);
  }
};

template <typename T>
struct VerifyValue<T, std::enable_if_t<IsStringListLike<T>>>
{
  static inline void verify_value(const AnariAny &v, T correctValue)
  {
    REQUIRE(v.valid());
    REQUIRE(v.type() == ANARI_STRING_LIST);
    auto stringList = v.getStringList();
    REQUIRE(stringList.empty()
        == (correctValue == nullptr || *correctValue == nullptr));

    auto it = stringList.cbegin();
    while (it != stringList.cend() && *correctValue) {
      REQUIRE(*it == *correctValue);
      ++it;
      ++correctValue;
    }
    REQUIRE((it == stringList.cend() && *correctValue == nullptr));
  }
};

template <typename T, typename Enabler = void>
struct TestInterface
{
  using VerifyValue = ::VerifyValue<T>;
  static inline void test_interface(T testValue, T testValue2)
  {
    AnariAny v;
    REQUIRE(!v.valid());

    SECTION("Can make valid by C++ construction")
    {
      AnariAny v2(testValue);
      VerifyValue::verify_value(v2, testValue);
    }

    if (!std::is_same_v<bool, T>) {
      SECTION("Can make valid by C construction")
      {
        AnariAny v2(anari::ANARITypeFor<T>::value, &testValue);
        VerifyValue::verify_value(v2, testValue);
      }
    }

    SECTION("Can make valid by calling operator=()")
    {
      v = testValue;
      VerifyValue::verify_value(v, testValue);
    }

    SECTION("Can make valid by copy construction")
    {
      v = testValue;
      AnariAny v2(v);
      VerifyValue::verify_value(v2, testValue);
    }

    SECTION("Two objects with same value are equal if constructed the same")
    {
      v = testValue;
      AnariAny v2 = testValue;
      REQUIRE(v.type() == v2.type());
      REQUIRE(v == v2);
    }

    SECTION("Two objects with same value are equal if assigned from another")
    {
      v = testValue;
      AnariAny v2 = testValue2;
      v = v2;
      REQUIRE(v == v2);
    }

    SECTION("Two objects with different values are not equal")
    {
      v = testValue;
      AnariAny v2 = testValue2;
      REQUIRE(v != v2);
    }
  }
};

template <>
struct TestInterface<const char *>
{
  using VerifyValue = ::VerifyValue<const char *>;
  static inline void test_interface(
      const char *testValue, const char *testValue2)
  {
    AnariAny v;
    REQUIRE(!v.valid());

    SECTION("Can make valid by C++ construction")
    {
      AnariAny v2(testValue);
      VerifyValue::verify_value(v2, testValue);
    }

    SECTION("Can make valid by C construction")
    {
      AnariAny v2(ANARI_STRING, testValue);
      VerifyValue::verify_value(v2, testValue);
    }

    SECTION("Can make valid by calling operator=()")
    {
      v = testValue;
      VerifyValue::verify_value(v, testValue);
    }

    SECTION("Can make valid by copy construction")
    {
      v = testValue;
      AnariAny v2(v);
      VerifyValue::verify_value(v2, testValue);
    }

    SECTION("Two objects with same value are equal if constructed the same")
    {
      v = testValue;
      AnariAny v2 = testValue;
      REQUIRE(v.type() == v2.type());
      REQUIRE(v == v2);
    }

    SECTION("Two objects with same value are equal if assigned from another")
    {
      v = testValue;
      AnariAny v2 = testValue2;
      v = v2;
      REQUIRE(v == v2);
    }

    SECTION("Two objects with different values are not equal")
    {
      v = testValue;
      AnariAny v2 = testValue2;
      REQUIRE(v != v2);
    }
  }
};

template <typename T>
struct TestInterface<T, std::enable_if_t<IsStringListLike<T>>>
{
  using VerifyValue = ::VerifyValue<T>;
  static inline void test_interface(T testValue, T testValue2)
  {
    AnariAny v;
    REQUIRE(!v.valid());

    SECTION("Can make valid by C++ construction")
    {
      AnariAny v2(testValue);
      VerifyValue::verify_value(v2, testValue);
    }

    SECTION("Can make valid by C construction")
    {
      AnariAny v2(ANARI_STRING_LIST, testValue);
      VerifyValue::verify_value(v2, testValue);
    }

    SECTION("Can make valid by calling operator=()")
    {
      v = testValue;
      VerifyValue::verify_value(v, testValue);
    }

    SECTION("Can make valid by copy construction")
    {
      v = testValue;
      AnariAny v2(v);
      VerifyValue::verify_value(v2, testValue);
    }

    SECTION("Two objects with same value are equal if constructed the same")
    {
      v = testValue;
      AnariAny v2 = testValue;
      REQUIRE(v.type() == v2.type());
      REQUIRE(v == v2);
    }

    SECTION("Two objects with same value are equal if assigned from another")
    {
      v = testValue;
      AnariAny v2 = testValue2;
      v = v2;
      REQUIRE(v == v2);
    }

    SECTION("Two objects with different values are not equal")
    {
      v = testValue;
      AnariAny v2 = testValue2;
      REQUIRE(v != v2);
    }

    SECTION("Get raw data from matches the input data")
    {
      v = testValue;
      auto anyValue = reinterpret_cast<decltype(testValue)>(v.data());
      REQUIRE(anyValue != nullptr);
      while (*testValue && *anyValue) {
        REQUIRE(strcmp(*testValue++, *anyValue++) == 0);
      }
      REQUIRE(*testValue == nullptr);
      REQUIRE(*anyValue == nullptr);
    }
  }
};

// Value Tests ////////////////////////////////////////////////////////////////

TEST_CASE("helium::AnariAny 'int' type behavior", "[helium_AnariAny]")
{
  TestInterface<int>::test_interface(5, 7);
}

TEST_CASE("helium::AnariAny 'float' type behavior", "[helium_AnariAny]")
{
  TestInterface<float>::test_interface(1.f, 2.f);
}

TEST_CASE("helium::AnariAny 'bool' type behavior", "[helium_AnariAny]")
{
  TestInterface<bool>::test_interface(true, false);
}

TEST_CASE("helium::AnariAny 'string' type behavior", "[helium_AnariAny]")
{
  TestInterface<const char *>::test_interface("test1", "test2");
}

TEST_CASE("helium::AnariAny 'stringList' type behavior", "[helium_AnariAny]")
{
  char *test1[] = {
      strdup("a"),
      strdup("b"),
      strdup("c"),
      nullptr,
  };
  char *test2[] = {
      strdup("d"),
      strdup("e"),
      nullptr,
  };
  TestInterface<char **>::test_interface(test1, test2);
  TestInterface<const char **>::test_interface(
      (const char **)test1, (const char **)test2);
  TestInterface<char *const *>::test_interface(
      (char *const *)test1, (char *const *)test2);
  TestInterface<const char *const *>::test_interface(
      (const char *const *)test1, (const char *const *)test2);

  for (auto ptr : test1)
    free(ptr);
  for (auto ptr : test2)
    free(ptr);
}
// Object Tests ///////////////////////////////////////////////////////////////

SCENARIO("helium::AnariAny object behavior", "[helium_AnariAny]")
{
  GIVEN("A ref counted object")
  {
    auto *obj = new helium::RefCounted();

    THEN("Object use count starts at 1")
    {
      REQUIRE(obj->useCount() == 1);
    }

    WHEN("Placing the object in AnariAny")
    {
      AnariAny v = obj;

      THEN("The ref count increments")
      {
        REQUIRE(obj->useCount() == 2);
      }

      THEN("Copying AnariAny also increments the object ref count")
      {
        AnariAny v2 = v;
        REQUIRE(obj->useCount() == 3);
      }

      THEN("Moving AnariAny keeps the object ref count the same")
      {
        AnariAny v2 = std::move(v);
        REQUIRE(obj->useCount() == 2);
      }
    }

    REQUIRE(obj->useCount() == 1);

    obj->refDec(helium::RefType::PUBLIC);
  }
}

} // namespace
