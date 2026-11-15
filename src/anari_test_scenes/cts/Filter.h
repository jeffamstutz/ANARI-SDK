// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "TestDef.h"
// std
#include <string>

namespace anari {
namespace cts {

// Selects a slice of the Catalog. An empty pattern matches everything. A
// pattern containing '*' or '?' is matched as an anchored, case-insensitive
// glob against the test id ("<category>/<name>"); otherwise it is matched as a
// case-insensitive substring.
struct Filter
{
  std::string pattern;
};

// Anchored case-insensitive glob: '*' matches any run of characters, '?' any
// single character. Every other character matches itself.
bool globMatch(
    const std::string &pattern, const std::string &text);

bool matches(const Filter &f, const TestDef &test);

// Whether the pattern matches one Case's "<category>/<test>/<case>" id, so a
// run can select some Cases of a Test (the others may still be rendered as
// support renders, ADR-0010). A Case of a matching Test is always selected.
bool matches(const Filter &f, const TestDef &test, const Case &c);

} // namespace cts
} // namespace anari
