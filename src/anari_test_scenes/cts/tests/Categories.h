// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Catalog.h"
// std
#include <string>
#include <vector>

// Per-category Test registration. Each category lives in its own translation
// unit (geometry.cpp, material.cpp, ...) to keep the migrated corpus navigable;
// registerBuiltinTests() calls them all.

namespace anari {
namespace cts {

// Support extensions (TestBuilder::requireFeatures) for the scenes many Tests
// share: what a Test needs to build and render its scene but is not evidence
// for. A Test requires the geometry and material its surface is made of and
// the camera it renders with: the runner's default camera is perspective, so
// every set below includes it, and a Test that brings its own camera
// (TestBuilder::camera) requires that camera instead. The directional light
// most scenes add is not required: a device without one ignores it and still
// renders the scene the Test is about. Extensions only some of a Test's Cases
// need (such as the image samplers some material Cases bind) are not required
// yet: gating per Case is pending (cts-1.2-coverage ticket 22).
inline constexpr const char *kSupportPerspective =
    "ANARI_KHR_CAMERA_PERSPECTIVE";
inline const std::vector<std::string> kSupportTriangle = {
    "ANARI_KHR_GEOMETRY_TRIANGLE", kSupportPerspective};
inline const std::vector<std::string> kSupportMatte = {
    "ANARI_KHR_MATERIAL_MATTE", kSupportPerspective};
inline const std::vector<std::string> kSupportMatteTriangle = {
    "ANARI_KHR_GEOMETRY_TRIANGLE",
    "ANARI_KHR_MATERIAL_MATTE",
    kSupportPerspective};

void registerGeometryTests(Catalog &catalog);
void registerMaterialTests(Catalog &catalog);
void registerSamplerTests(Catalog &catalog);
void registerLightTests(Catalog &catalog);
void registerCameraTests(Catalog &catalog);
void registerFrameTests(Catalog &catalog);
void registerRendererTests(Catalog &catalog);
void registerInstanceTests(Catalog &catalog);
void registerVolumeTests(Catalog &catalog);
void registerSynchronizationTests(Catalog &catalog);

// glTF asset-scan factory: registers one Test per asset found under the
// gltf-Sample-Assets tree. A no-op unless the build enabled glTF (ENABLE_GLTF).
void registerGltfTests(Catalog &catalog);

} // namespace cts
} // namespace anari
