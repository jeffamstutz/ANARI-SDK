// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ScreenQuad.h"
#include "LightBuilder.h"
#include "WorldBuilder.h"
// std
#include <array>
#include <stdexcept>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using anari::math::uint3;

// The quad's corners in screen coordinates.
const std::array<float2, 4> kCorners = {
    float2(-kScreenQuadOverhang, -kScreenQuadOverhang),
    float2(1.f + kScreenQuadOverhang, -kScreenQuadOverhang),
    float2(-kScreenQuadOverhang, 1.f + kScreenQuadOverhang),
    float2(1.f + kScreenQuadOverhang, 1.f + kScreenQuadOverhang)};

template <typename T>
anari::Array1D cornerArray(anari::Device d, const ScreenAttribute &a)
{
  std::array<T, 4> values;
  for (size_t i = 0; i < kCorners.size(); ++i) {
    const float4 v = a.value(kCorners[i]);
    if constexpr (std::is_same_v<T, float>)
      values[i] = v.x;
    else if constexpr (std::is_same_v<T, float2>)
      values[i] = float2(v.x, v.y);
    else if constexpr (std::is_same_v<T, float3>)
      values[i] = float3(v.x, v.y, v.z);
    else
      values[i] = v;
  }
  return anari::newArray1D(d, values.data(), values.size());
}

} // namespace

anari::Geometry makeScreenQuad(
    anari::Device d, const std::vector<ScreenAttribute> &attributes)
{
  std::array<float3, 4> position;
  for (size_t i = 0; i < kCorners.size(); ++i)
    position[i] = float3(2.f * kCorners[i] - 1.f, 0.f);
  const std::array<uint3, 2> index = {uint3(0, 1, 2), uint3(2, 1, 3)};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, position.data(), position.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "primitive.index",
      anari::newArray1D(d, index.data(), index.size()));
  for (const auto &a : attributes) {
    anari::Array1D array = nullptr;
    switch (a.components) {
    case 1:
      array = cornerArray<float>(d, a);
      break;
    case 2:
      array = cornerArray<float2>(d, a);
      break;
    case 3:
      array = cornerArray<float3>(d, a);
      break;
    case 4:
      array = cornerArray<float4>(d, a);
      break;
    default:
      anari::release(d, geom);
      throw std::invalid_argument("a screen attribute has 1-4 components");
    }
    const std::string param = "vertex." + a.name;
    anari::setAndReleaseParameter(d, geom, param.c_str(), array);
  }
  anari::commitParameters(d, geom);
  return geom;
}

anari::Camera makeScreenQuadCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 1.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  // Half the frame's height at the plane z = 0 (distance 1) is tan(pi/4) = 1.
  anari::setParameter(d, camera, "fovy", 1.5707963f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

anari::World makeScreenQuadWorld(anari::Device d, anari::Surface surface)
{
  auto light = makeDirectionalLight(d, float3(0.f, 0.f, -1.f), 1.5f);
  WorldContents wc;
  if (surface)
    wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, light);
  return world;
}

} // namespace cts
} // namespace anari
