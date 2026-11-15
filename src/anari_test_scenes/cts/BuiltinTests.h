// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Catalog.h"
#include "Discrimination.h"
// std
#include <vector>

namespace anari {
namespace cts {

// Register the built-in conformance Tests into a catalog.
void registerBuiltinTests(Catalog &catalog);

// The pairs of Permutation values whose Ground truth may be indistinguishable
// (`anariCts check-discrimination`), with the reason. Kept next to the Tests
// in tests/IndistinguishableGroundTruth.cpp; it should only shrink.
const std::vector<Allowance> &builtinIndistinguishableAllowList();

} // namespace cts
} // namespace anari
