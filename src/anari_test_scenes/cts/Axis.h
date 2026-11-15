// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Value.h"
// std
#include <string>
#include <vector>

namespace anari {
namespace cts {

// Every Axis is either a Permutation or a Variant (ADR-0010).
//  - Permutation: values produce *different* output, so each value gets its own
//    ground-truth image, and Cases differing only in it must render
//    differently (an effect check, unless opted out).
//  - Variant: values must produce *identical* output (e.g. soup vs indexed).
//    All values share one ground-truth image (written from the first value's
//    Case), and each Case is compared with the first value's (equivalence).
enum class AxisKind
{
  Permutation,
  Variant,
};

// A named parameter dimension a Test is expanded over.
struct Axis
{
  std::string name;
  std::vector<Any> values;
  AxisKind kind{AxisKind::Permutation};
};

} // namespace cts
} // namespace anari
