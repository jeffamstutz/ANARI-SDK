// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sidecar.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <set>
#include <string>
#include <vector>

// What a run gates its Cases on (ADR-0005): the extensions the run's renderer
// reports of itself when it reports any, else those the device advertises. A
// renderer may honour less than its device (halcyon's `preview` doesn't
// accumulate or sample a lens, though the device, the union of its
// renderers, advertises both), and a Case needing what the renderer lacks is
// skipped as the renderer's gap, not the device's. The rules are pure; the
// queries read the device's introspection.
namespace anari {
namespace cts {

// The extensions Cases gate on: `device.rendererExtensions` when the renderer
// reported any, else `device.extensions`.
std::set<std::string> gatingExtensions(const DeviceSpec &device);

// Of `missing`, those the device advertises but the renderer didn't report:
// the renderer lacks them, not the device. Empty when the renderer reported
// nothing.
std::vector<std::string> rendererMissingFeatures(
    const DeviceSpec &device, const std::vector<std::string> &missing);

// Why a Case is skipped for `missing`: the device's missing extensions, then
// those of them `rendererMissing` holds as the renderer's, named by its
// subtype `renderer`, e.g. "renderer preview lacks a required feature:
// ANARI_KHR_CAMERA_DEPTH_OF_FIELD".
std::string featureSkipReason(const std::vector<std::string> &missing,
    const std::vector<std::string> &rendererMissing,
    const std::string &renderer);

// The renderer subtype `renderer` creates: itself when the device lists it,
// and the device's first renderer subtype for "default" when the device
// doesn't list "default" (the spec's alias; halcyon lists `preview` and
// `quality`). Unchanged without a device or when the device lists no
// renderer.
std::string resolveRendererSubtype(
    anari::Device device, const std::string &renderer);

// The extensions renderer subtype `renderer` reports of itself, sorted:
// object introspection (anariGetObjectInfo's "extension" for the resolved
// subtype), else the "extension" property of a renderer of that subtype.
// Empty when it reports neither (helide), and so gating falls back to the
// device's list.
std::vector<std::string> queryRendererExtensions(
    anari::Device device, const std::string &renderer);

} // namespace cts
} // namespace anari
