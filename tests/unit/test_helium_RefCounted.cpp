// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "helium/BaseObject.h"
#include "helium/utility/AnariAny.h"
#include "helium/utility/IntrusivePtr.h"

struct TestObject : public helium::RefCounted
{
  bool on_NoPublicReferences_triggered{false};
  bool on_NoInternalReferences_triggered{false};
  bool &destroyed;
  TestObject(bool &d) : destroyed(d)
  {
    destroyed = false;
  }
  ~TestObject()
  {
    destroyed = true;
  }

 private:
  void on_NoPublicReferences() override
  {
    on_NoPublicReferences_triggered = true;
  }
  void on_NoInternalReferences() override
  {
    on_NoInternalReferences_triggered = true;
  }
};

// NOTE(jda) -- This specialization is *only* for this test so AnariAny can be
//              used.
HELIUM_ANARI_TYPEFOR_SPECIALIZATION(TestObject *, ANARI_OBJECT);
HELIUM_ANARI_TYPEFOR_DEFINITION(TestObject *);

namespace {

using helium::AnariAny;
using helium::BaseObject;
using helium::IntrusivePtr;
using helium::RefCounted;
using helium::RefType;

SCENARIO("helium::RefCounted interface", "[helium_RefCounted]")
{
  GIVEN("A default constructed RefCounted object")
  {
    bool destroyed = false;
    auto *obj = new TestObject(destroyed);

    THEN("The total ref count is initially 1")
    {
      REQUIRE(obj->useCount(RefType::ALL) == 1);
    }

    THEN("The public ref count is initially 1")
    {
      REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
    }

    THEN("The internal ref count is initially 0")
    {
      REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
    }

    WHEN("The public ref count is incremented")
    {
      obj->refInc(RefType::PUBLIC);

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The total ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::PUBLIC) == 2);
      }

      THEN("The internal ref count is still 0")
      {
        REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
      }

      WHEN("The public ref is decremented again")
      {
        obj->refDec(RefType::PUBLIC);

        THEN("It is restored back to its previous state")
        {
          REQUIRE(obj->useCount(RefType::ALL) == 1);
          REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
          REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
        }

        THEN("The ref callbacks still have not triggered")
        {
          REQUIRE(obj->on_NoPublicReferences_triggered == false);
          REQUIRE(obj->on_NoInternalReferences_triggered == false);
        }
      }
    }

    WHEN("The internal ref count is incremented")
    {
      obj->refInc(RefType::INTERNAL);

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The total ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is still 1")
      {
        REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
      }

      THEN("The internal ref count is now 1")
      {
        REQUIRE(obj->useCount(RefType::INTERNAL) == 1);
      }

      WHEN("The internal ref is decremented again")
      {
        obj->refDec(RefType::INTERNAL);
        THEN("Decrementing it again restores it back to its previous state")
        {
          REQUIRE(obj->useCount(RefType::ALL) == 1);
          REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
          REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
        }

        THEN("Then only on_NoInternalReferences() should be triggered")
        {
          REQUIRE(obj->on_NoPublicReferences_triggered == false);
          REQUIRE(obj->on_NoInternalReferences_triggered == true);
        }
      }
    }

    WHEN("The the object is placed inside an AnariAny")
    {
      AnariAny any = obj;

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The total ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is still 1")
      {
        REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
      }

      THEN("The internal ref count is still 0")
      {
        REQUIRE(obj->useCount(RefType::INTERNAL) == 1);
      }

      WHEN("The AnariAny is cleared")
      {
        any.reset();
        THEN("The ref count is restored it back to its previous state")
        {
          REQUIRE(obj->useCount(RefType::ALL) == 1);
          REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
          REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
        }

        THEN("Then only on_NoInternalReferences() should be triggered")
        {
          REQUIRE(obj->on_NoPublicReferences_triggered == false);
          REQUIRE(obj->on_NoInternalReferences_triggered == true);
        }
      }
    }

    WHEN("An IntrusivePtr points to the object")
    {
      IntrusivePtr ptr = obj;

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The object internal ref count is incremented")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
        REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
        REQUIRE(obj->useCount(RefType::INTERNAL) == 1);

        WHEN("The IntrusivePtr<> is removed")
        {
          ptr = nullptr;

          THEN("The object ref count is restored to its previous state")
          {
            REQUIRE(obj->useCount(RefType::ALL) == 1);
            REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
            REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
          }

          THEN("Then only on_NoInternalReferences() should be triggered")
          {
            REQUIRE(obj->on_NoPublicReferences_triggered == false);
            REQUIRE(obj->on_NoInternalReferences_triggered == true);
          }
        }
      }
    }

    WHEN("The object is changed to be an internal-only reference")
    {
      obj->refInc(RefType::INTERNAL);
      obj->refDec(RefType::PUBLIC);

      THEN("Then only on_NoPublicReferences() should be triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == true);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      obj->refInc(RefType::PUBLIC);
      obj->refDec(RefType::INTERNAL);
    }

    WHEN("The refcount is decremented to zero")
    {
      REQUIRE(destroyed == false);

      obj->refDec(RefType::PUBLIC);

      THEN("Then the object is destroyed")
      {
        REQUIRE(destroyed == true);
      }
    }
  }
}

} // namespace
