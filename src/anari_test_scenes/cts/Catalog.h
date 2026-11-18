// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "Filter.h"
#include "TestDef.h"
// std
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// True when every required feature of the test is present in deviceFeatures.
// A test with no required features is always supported.
bool isSupported(
    const TestDef &test, const std::set<std::string> &deviceFeatures);

// The Test's required features absent from deviceFeatures, in declaration
// order. Empty exactly when isSupported().
std::vector<std::string> missingFeatures(
    const TestDef &test, const std::set<std::string> &deviceFeatures);

// The features one Case needs: the Test's requiredFeatures, then those the
// Test requires of the values the Case holds (`requireFeatureFor`), without
// repeats.
std::vector<std::string> requiredFeaturesOf(const TestDef &test, const Case &c);

// isSupported() and missingFeatures() for one Case: they also count the
// features its axis values require, so a device lacking such an extension
// skips that Case and not its siblings.
bool isSupported(const TestDef &test,
    const Case &c,
    const std::set<std::string> &deviceFeatures);
std::vector<std::string> missingFeatures(const TestDef &test,
    const Case &c,
    const std::set<std::string> &deviceFeatures);

// The in-C++ registry of all Tests. `cts list` enumerates it; `cts run`
// expands and executes a filtered slice of it. Distinct from the pre-existing
// scene registry (registerScene) used by example viewers.
struct Catalog
{
  // Register a Test. Returns a reference to the stored definition, which stays
  // valid for the catalog's lifetime even as further tests are registered.
  const TestDef &add(TestDef test);

  const std::deque<TestDef> &tests() const;

  // Sorted, de-duplicated category names.
  std::vector<std::string> categories() const;

  // All tests, in registration order.
  std::vector<const TestDef *> list() const;

  // Tests selected by the filter (the Test's id or one of its Cases' matches),
  // in registration order.
  std::vector<const TestDef *> filter(const Filter &f) const;

  size_t size() const
  {
    return m_tests.size();
  }

 private:
  // A deque (not a vector) so references and pointers handed out by add(),
  // list(), and filter() remain valid as more tests are registered.
  std::deque<TestDef> m_tests;
};

// Every extension some Test in the catalog names as primary (covers()).
std::set<std::string> coveredExtensions(const Catalog &catalog);

// The process-wide catalog that authored tests register into.
Catalog &globalCatalog();

} // namespace cts
} // namespace anari
