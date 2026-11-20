// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sidecar.h"
#include "Workdir.h"
// std
#include <optional>

// The Reference device (ADR-0005): the device `generate` renders Ground truth
// with by default, and the record of the one it used. The record reads and
// writes files only.
namespace anari {
namespace cts {

// halcyon's `preview` renderer in its default mode is the default Reference;
// helide stands in when halcyon can't be loaded (it is off by default in the
// SDK's build).
inline constexpr const char *kDefaultReferenceLibrary = "halcyon";
inline constexpr const char *kDefaultReferenceRenderer = "preview";
inline constexpr const char *kFallbackReferenceLibrary = "helide";
inline constexpr const char *kFallbackReferenceRenderer = "default";

// Write the device `generate` used, with the extensions it advertised, to
// ground_truth/reference.json (Workdir::referenceRecordPath()), replacing any
// earlier record. False on a filesystem error.
bool writeReference(const Workdir &workdir, const DeviceSpec &reference);

// The device the last `generate` used, if it left a record.
std::optional<DeviceSpec> readReference(const Workdir &workdir);

} // namespace cts
} // namespace anari
