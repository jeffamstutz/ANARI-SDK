// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sidecar.h"
#include "Workdir.h"
// std
#include <optional>
#include <string>

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

// Why `generate` with `reference` must not add to the workdir's Ground truth:
// the record names a different device, whose files would stay beside the new
// ones (ADR-0005). Names both devices; empty when there is no record or it
// names the same device.
std::string referenceConflict(
    const Workdir &workdir, const DeviceSpec &reference);

// Remove ground_truth/ and everything in it, the record included. False on a
// filesystem error.
bool clearGroundTruth(const Workdir &workdir);

} // namespace cts
} // namespace anari
