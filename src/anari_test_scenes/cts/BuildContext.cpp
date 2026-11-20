// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BuildContext.h"
// std
#include <stdexcept>

namespace anari {
namespace cts {

BuildContext::BuildContext(anari::Device device) : m_device(device) {}

anari::Device BuildContext::device() const
{
  return m_device;
}

std::string BuildContext::getString(
    const std::string &name, const std::string &valIfNotFound) const
{
  return m_params.getParamString(name, valIfNotFound);
}

bool BuildContext::has(const std::string &name) const
{
  return m_params.hasParam(name) || m_bindings.find(name) != m_bindings.end();
}

Any BuildContext::value(const std::string &name) const
{
  const auto binding = m_bindings.find(name);
  if (binding != m_bindings.end())
    return Any(binding->second);
  return m_params.getParamDirect(name);
}

const ParameterBinding &BuildContext::binding(const std::string &name) const
{
  const auto it = m_bindings.find(name);
  if (it == m_bindings.end()) {
    throw std::invalid_argument(
        "axis '" + name + "' does not contain a typed parameter binding");
  }
  return it->second;
}

std::set<size_t> BuildContext::boundSamplerSlots() const
{
  std::set<size_t> slots;
  for (const auto &entry : m_bindings)
    if (const auto *s = std::get_if<SamplerParameter>(&entry.second.value()))
      slots.insert(s->index);
  return slots;
}

void BuildContext::set(const std::string &name, const Any &value)
{
  if (value.isBinding()) {
    m_bindings.insert_or_assign(name, value.binding());
    return;
  }

  m_bindings.erase(name);
  // A `none()` value means "leave unset"; dropping it lets get<>() fall back to
  // the default the build() function supplies.
  if (value.valid())
    m_params.setParamDirect(name, value.raw());
}

void BuildContext::setCameraProjection(const CameraProjection &projection)
{
  m_projection = projection;
}

const std::optional<CameraProjection> &BuildContext::cameraProjection() const
{
  return m_projection;
}

anari::Camera BuildContext::newCamera(CameraProjection projection)
{
  if (m_frameSize && m_frameSize->width != 0 && m_frameSize->height != 0)
    projection.aspect = float(m_frameSize->width) / float(m_frameSize->height);
  const bool perspective =
      projection.type == CameraProjection::Type::Perspective;
  auto d = m_device;
  auto camera = anari::newObject<anari::Camera>(
      d, perspective ? "perspective" : "orthographic");
  anari::setParameter(d, camera, "position", projection.position);
  anari::setParameter(d, camera, "direction", projection.direction);
  anari::setParameter(d, camera, "up", projection.up);
  if (perspective)
    anari::setParameter(d, camera, "fovy", projection.fovy);
  else
    anari::setParameter(d, camera, "height", projection.height);
  anari::setParameter(d, camera, "aspect", projection.aspect);
  const bool wholeSensor = projection.regionLo == math::float2(0.f, 0.f)
      && projection.regionHi == math::float2(1.f, 1.f);
  if (!wholeSensor) {
    const float region[4] = {projection.regionLo.x,
        projection.regionLo.y,
        projection.regionHi.x,
        projection.regionHi.y};
    anariSetParameter(d, camera, "imageRegion", ANARI_FLOAT32_BOX2, region);
  }
  m_projection = projection;
  return camera;
}

void BuildContext::setFrameSize(FrameSize size)
{
  m_frameSize = size;
}

const std::optional<FrameSize> &BuildContext::frameSize() const
{
  return m_frameSize;
}

} // namespace cts
} // namespace anari
