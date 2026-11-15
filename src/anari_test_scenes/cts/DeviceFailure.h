// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sidecar.h"
#include "Workdir.h"
// std
#include <optional>
#include <string>

// The run record: what a run leaves in the results tree when it stopped
// because the device could not render the probe frame (DeviceProbe.h). It
// reads and writes files only, so the report can use it (ADR-0008).
namespace anari {
namespace cts {

// A run that stopped because the device could not render the probe frame.
struct DeviceFailure
{
  DeviceSpec device;
  std::string reason; // what probeDevice() said
};

// The record lives in the results tree (results/run.json) but is not a
// Sidecar: it has no `verdict`, so sidecar readers pass over it.
bool writeDeviceFailure(const Workdir &workdir, const DeviceFailure &failure);

// The record a run left, if the last run stopped at the probe.
std::optional<DeviceFailure> readDeviceFailure(const Workdir &workdir);

// Remove the record, as a run whose device rendered the probe does.
void clearDeviceFailure(const Workdir &workdir);

} // namespace cts
} // namespace anari
