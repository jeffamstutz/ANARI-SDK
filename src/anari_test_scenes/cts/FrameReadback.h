// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Channel.h"
#include "Image.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// The descriptor returned by anariMapFrame, separated from the map operation
// so malformed device results can be tested without a deliberately broken
// device.
struct MappedFrameDescriptor
{
  const void *data{nullptr};
  uint32_t width{0};
  uint32_t height{0};
  ANARIDataType pixelType{ANARI_UNKNOWN};
};

enum class FrameReadbackError
{
  None,
  NullData,
  DimensionMismatch,
  UnsupportedType,
  SizeOverflow,
};

// A decoded comparison image or a structured validation/conversion failure.
// The depth and ID Channels also keep their raw values, for exact checks
// (ADR-0010): they are kept in memory for the Case only, never written.
struct FrameReadbackResult
{
  Image image;
  std::vector<float> depth; // Channel::Depth: the mapped FLOAT32 values
  std::vector<uint32_t> ids; // the ID Channels: the mapped UINT32 values
  FrameReadbackError error{FrameReadbackError::None};
  std::string detail;

  explicit operator bool() const
  {
    return error == FrameReadbackError::None;
  }
};

// One rendered Channel of a Case, as an exact check sees it (ADR-0010): the
// 8-bit comparison image, plus the raw values of the depth (FLOAT32) and ID
// (UINT32) Channels. Kept in memory for the per-Case phase only.
struct ChannelValues
{
  Channel channel{Channel::Color};
  Image image;
  std::vector<float> depth; // Channel::Depth
  std::vector<uint32_t> ids; // Channel::PrimitiveId / ObjectId / InstanceId
};

// A successful readback of `channel` as ChannelValues (consumes it).
ChannelValues toChannelValues(Channel channel, FrameReadbackResult &&readback);

// Validate and decode one mapped Channel into the CTS comparison-image format.
// depthScale is used only for Channel::Depth.
FrameReadbackResult decodeFrameChannel(Channel channel,
    const MappedFrameDescriptor &mapped,
    uint32_t expectedWidth,
    uint32_t expectedHeight,
    float depthScale = 1.f);

// Map, validate, and decode one frame Channel. The scoped mapping pairs every
// successful map with exactly one unmap, including validation failures and
// exceptions raised while allocating or converting the comparison image.
FrameReadbackResult readFrameChannel(anari::Device device,
    anari::Frame frame,
    Channel channel,
    uint32_t expectedWidth,
    uint32_t expectedHeight,
    float depthScale = 1.f);

} // namespace cts
} // namespace anari
