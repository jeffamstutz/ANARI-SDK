// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DeviceErrorLog.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <string>

// Whether a device can render at all. `run` and `generate` render a probe
// frame before the first Case: a device that can't (say, its backend failed
// to initialise) is one error, not a failure per Case (DeviceFailure.h
// records it).
namespace anari {
namespace cts {

// The probe frame's width and height.
inline constexpr uint32_t kProbeFrameSize = 16;

// Render an empty world through a camera (perspective, unless the device lists
// camera subtypes without it: then the first it lists) and a
// 'rendererSubtype' renderer into a small frame, and map its color channel.
// Empty when that worked; otherwise why not, followed by each distinct ERROR
// message 'errors' (when non-null) has recorded so far, which is where a
// device says what went wrong.
std::string probeDevice(anari::Device device,
    const std::string &rendererSubtype,
    const DeviceErrorLog *errors = nullptr);

} // namespace cts
} // namespace anari
