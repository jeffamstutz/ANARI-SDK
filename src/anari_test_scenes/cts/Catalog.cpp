// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Catalog.h"
#include "Expansion.h"

namespace anari {
namespace cts {

bool isSupported(
    const TestDef &test, const std::set<std::string> &deviceFeatures)
{
  for (const auto &f : test.requiredFeatures) {
    if (deviceFeatures.find(f) == deviceFeatures.end())
      return false;
  }
  return true;
}

std::vector<std::string> missingFeatures(
    const TestDef &test, const std::set<std::string> &deviceFeatures)
{
  std::vector<std::string> out;
  for (const auto &f : test.requiredFeatures) {
    if (deviceFeatures.find(f) == deviceFeatures.end())
      out.push_back(f);
  }
  return out;
}

const TestDef &Catalog::add(TestDef test)
{
  m_tests.push_back(std::move(test));
  return m_tests.back();
}

const std::deque<TestDef> &Catalog::tests() const
{
  return m_tests;
}

std::vector<std::string> Catalog::categories() const
{
  std::set<std::string> unique;
  for (const auto &t : m_tests)
    unique.insert(t.category);
  return {unique.begin(), unique.end()};
}

std::vector<const TestDef *> Catalog::list() const
{
  std::vector<const TestDef *> out;
  out.reserve(m_tests.size());
  for (const auto &t : m_tests)
    out.push_back(&t);
  return out;
}

std::vector<const TestDef *> Catalog::filter(const Filter &f) const
{
  std::vector<const TestDef *> out;
  for (const auto &t : m_tests) {
    bool any = matches(f, t);
    if (!any)
      for (const Case &c : expand(t))
        if ((any = matches(f, t, c)))
          break;
    if (any)
      out.push_back(&t);
  }
  return out;
}

std::set<std::string> coveredExtensions(const Catalog &catalog)
{
  std::set<std::string> out;
  for (const auto &t : catalog.tests())
    out.insert(t.coveredFeatures.begin(), t.coveredFeatures.end());
  return out;
}

Catalog &globalCatalog()
{
  static Catalog catalog;
  return catalog;
}

} // namespace cts
} // namespace anari
