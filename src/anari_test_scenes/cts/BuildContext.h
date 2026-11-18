// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CameraProjection.h"
#include "Value.h"
// anari
#include "anari/anari_cpp.hpp"
// helium
#include "helium/utility/ParameterizedObject.h"
// std
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace anari {
namespace cts {

// The object the harness hands to a Test's build() carrying the current Case's
// resolved axis values, read typed-by-name (get<int>("count", 1)). Backed by
// helium::ParameterizedObject. The contained device is a non-owning handle; the
// runner owns the device's lifetime.
struct BuildContext
{
  explicit BuildContext(anari::Device device = nullptr);

  anari::Device device() const;

  // Read an axis value, falling back to a default when the value was left
  // unset (a `none()` axis value, or simply not present).
  template <typename T>
  T get(const std::string &name, T valIfNotFound) const
  {
    return m_params.getParam<T>(name, valIfNotFound);
  }

  std::string getString(
      const std::string &name, const std::string &valIfNotFound) const;

  bool has(const std::string &name) const;

  // The raw axis value carried under 'name', or an invalid Any (none()) when
  // unset.
  Any value(const std::string &name) const;

  // The typed material binding carried under 'name'. Throws an actionable
  // diagnostic when the axis is absent or contains a regular value.
  const ParameterBinding &binding(const std::string &name) const;

  // The sampler slots the Case's typed bindings reference. A Test that builds
  // only the samplers a Case binds can run its other Cases on a device lacking
  // that sampler's extension (TestBuilder::requireFeatureFor).
  std::set<size_t> boundSamplerSlots() const;

  // Load a resolved axis value. A `none()` (invalid) value is intentionally
  // dropped so that get<>() falls back to the build()-supplied default.
  void set(const std::string &name, const Any &value);

  template <typename T>
  void setValue(const std::string &name, const T &v)
  {
    m_params.setParam(name, v);
  }

  // What the Case's camera does to a world-space point, so its exact checks
  // can project() (ADR-0010). The runner records it for the camera it frames;
  // a Test's own camera hook records it here, or its exact checks can't
  // project.
  void setCameraProjection(const CameraProjection &projection);
  const std::optional<CameraProjection> &cameraProjection() const;

 private:
  anari::Device m_device{nullptr};
  std::optional<CameraProjection> m_projection;
  helium::ParameterizedObject m_params;
  std::unordered_map<std::string, ParameterBinding> m_bindings;
};

} // namespace cts
} // namespace anari
