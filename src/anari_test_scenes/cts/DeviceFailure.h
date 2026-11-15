// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DeviceErrorLog.h"
#include "Sidecar.h"
#include "Workdir.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <optional>
#include <string>

// Whether a device can render at all, and the run-level record of one that
// can't. `run` and `generate` render a probe frame before the first Case: a
// device that can't (say, its backend failed to initialise) is one error, not
// a failure per Case.
namespace anari {
namespace cts {

// The probe frame's width and height.
inline constexpr uint32_t kProbeFrameSize = 16;

// Render an empty world through a perspective camera and a 'rendererSubtype'
// renderer into a small frame, and map its color channel. Empty when that
// worked; otherwise why not, followed by every ERROR message 'errors' (when
// non-null) has recorded so far, which is where a device says what went wrong.
std::string probeDevice(anari::Device device,
    const std::string &rendererSubtype,
    const DeviceErrorLog *errors = nullptr);

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
