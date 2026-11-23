// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "NanoVdbGrid.h"
// nanovdb
#include <nanovdb/NanoVDB.h>
// std
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace anari {
namespace cts {

namespace {

using Leaf = nanovdb::NanoLeaf<float>;
using Lower = nanovdb::NanoLower<float>;
using Upper = nanovdb::NanoUpper<float>;
using Root = nanovdb::NanoRoot<float>;

// Running statistics of a node's active values, as NanoVDB keeps them.
struct Stats
{
  float min{std::numeric_limits<float>::max()};
  float max{std::numeric_limits<float>::lowest()};
  double sum{0.0};
  double sumSq{0.0};
  uint64_t count{0};

  void add(float v)
  {
    min = std::min(min, v);
    max = std::max(max, v);
    sum += v;
    sumSq += double(v) * v;
    ++count;
  }
  void add(const Stats &o)
  {
    min = std::min(min, o.min);
    max = std::max(max, o.max);
    sum += o.sum;
    sumSq += o.sumSq;
    count += o.count;
  }
  float average() const
  {
    return count ? float(sum / count) : 0.f;
  }
  float stdDev() const
  {
    if (!count)
      return 0.f;
    const double mean = sum / count;
    return float(std::sqrt(std::max(0.0, sumSq / count - mean * mean)));
  }
};

template <typename Data>
void setStats(Data &node, const Stats &s)
{
  node.mMinimum = s.min;
  node.mMaximum = s.max;
  node.mAverage = s.average();
  node.mStdDevi = s.stdDev();
}

} // namespace

std::vector<uint8_t> makeNanoVdbFloatGrid(const NanoVdbGridSpec &spec)
{
  constexpr uint32_t kLeafDim = Leaf::DIM; // 8
  constexpr uint32_t kLowerDim = Lower::DIM; // 128: one lower node holds all
  for (uint32_t n : spec.voxels)
    if (n == 0 || n > kLowerDim)
      throw std::invalid_argument(
          "makeNanoVdbFloatGrid: each dimension must be in [1, "
          + std::to_string(kLowerDim) + "]");
  if (!spec.value)
    throw std::invalid_argument("makeNanoVdbFloatGrid: no value function");

  const std::array<uint32_t, 3> leaves = {
      (spec.voxels[0] + kLeafDim - 1) / kLeafDim,
      (spec.voxels[1] + kLeafDim - 1) / kLeafDim,
      (spec.voxels[2] + kLeafDim - 1) / kLeafDim};
  const uint32_t leafCount = leaves[0] * leaves[1] * leaves[2];

  // Breadth-first, as NanoVDB's own builder lays a grid out: grid, tree,
  // root (one tile), the upper node, the lower node, the leaves.
  const uint64_t gridOffset = 0;
  const uint64_t treeOffset = gridOffset + nanovdb::GridData::memUsage();
  const uint64_t rootOffset = treeOffset + sizeof(nanovdb::TreeData);
  const uint64_t upperOffset = rootOffset + Root::memUsage(1);
  const uint64_t lowerOffset = upperOffset + Upper::memUsage();
  const uint64_t leafOffset = lowerOffset + Lower::memUsage();
  const uint64_t total =
      leafOffset + uint64_t(leafCount) * Leaf::DataType::memUsage();

  // NanoVDB's structures want 32-byte alignment; build in such a buffer.
  struct Aligned
  {
    void operator()(uint8_t *p) const
    {
      ::operator delete[](p, std::align_val_t(NANOVDB_DATA_ALIGNMENT));
    }
  };
  std::unique_ptr<uint8_t[], Aligned> buffer(static_cast<uint8_t *>(
      ::operator new[](total, std::align_val_t(NANOVDB_DATA_ALIGNMENT))));
  std::memset(buffer.get(), 0, total);
  uint8_t *base = buffer.get();

  auto *grid = reinterpret_cast<nanovdb::GridData *>(base + gridOffset);
  auto *tree = reinterpret_cast<nanovdb::TreeData *>(base + treeOffset);
  auto *root = reinterpret_cast<Root *>(base + rootOffset);
  auto *upper = reinterpret_cast<Upper *>(base + upperOffset);
  auto *lower = reinterpret_cast<Lower *>(base + lowerOffset);
  auto leafAt = [&](uint32_t n) {
    return reinterpret_cast<Leaf *>(
        base + leafOffset + n * Leaf::DataType::memUsage());
  };

  const nanovdb::Coord maxIndex(int(spec.voxels[0]) - 1,
      int(spec.voxels[1]) - 1,
      int(spec.voxels[2]) - 1);
  const nanovdb::CoordBBox indexBox(nanovdb::Coord(0), maxIndex);

  // Leaves, in the order of their offsets in the lower node (x, then y,
  // then z slowest-to-fastest), each holding its part of the box.
  Stats all;
  uint32_t n = 0;
  for (uint32_t lx = 0; lx < leaves[0]; ++lx)
    for (uint32_t ly = 0; ly < leaves[1]; ++ly)
      for (uint32_t lz = 0; lz < leaves[2]; ++lz, ++n) {
        Leaf *leaf = leafAt(n);
        auto *ld = leaf->data();
        const nanovdb::Coord origin(
            int(lx * kLeafDim), int(ly * kLeafDim), int(lz * kLeafDim));
        ld->mBBoxMin = origin;
        Stats s;
        for (uint32_t i = 0; i < kLeafDim; ++i)
          for (uint32_t j = 0; j < kLeafDim; ++j)
            for (uint32_t k = 0; k < kLeafDim; ++k) {
              const nanovdb::Coord ijk = origin + nanovdb::Coord(i, j, k);
              if (!indexBox.isInside(ijk))
                continue;
              const float v = spec.value(ijk[0], ijk[1], ijk[2]);
              ld->setValue(Leaf::CoordToOffset(ijk), v);
              s.add(v);
            }
        setStats(*ld, s);
        leaf->updateBBox();
        all.add(s);

        const uint32_t slot = Lower::CoordToOffset(origin);
        lower->data()->mChildMask.setOn(slot);
        lower->data()->setChild(slot, leaf);
      }

  lower->data()->mBBox = indexBox;
  setStats(*lower->data(), all);
  upper->data()->mBBox = indexBox;
  upper->data()->mChildMask.setOn(0);
  upper->data()->setChild(0, lower);
  setStats(*upper->data(), all);

  auto *rd = root->data();
  rd->mBBox = indexBox;
  rd->mTableSize = 1;
  rd->mBackground = 0.f;
  setStats(*rd, all);
  rd->tile(0)->setChild(nanovdb::Coord(0), upper, rd);

  tree->mNodeOffset[0] = int64_t(leafOffset - treeOffset);
  tree->mNodeOffset[1] = int64_t(lowerOffset - treeOffset);
  tree->mNodeOffset[2] = int64_t(upperOffset - treeOffset);
  tree->mNodeOffset[3] = int64_t(rootOffset - treeOffset);
  tree->mNodeCount[0] = leafCount;
  tree->mNodeCount[1] = 1;
  tree->mNodeCount[2] = 1;
  tree->mVoxelCount =
      uint64_t(spec.voxels[0]) * spec.voxels[1] * spec.voxels[2];

  const nanovdb::Vec3d origin(spec.origin.x, spec.origin.y, spec.origin.z);
  grid->init({nanovdb::GridFlags::IsBreadthFirst,
                 nanovdb::GridFlags::HasBBox,
                 nanovdb::GridFlags::HasMinMax,
                 nanovdb::GridFlags::HasAverage,
                 nanovdb::GridFlags::HasStdDeviation},
      total,
      nanovdb::Map(double(spec.voxelSize), origin),
      nanovdb::GridType::Float,
      nanovdb::GridClass::FogVolume);
  grid->setGridName("cts");
  grid->mWorldBBox = nanovdb::Vec3dBBox(origin,
      origin
          + nanovdb::Vec3d(spec.voxels[0], spec.voxels[1], spec.voxels[2])
              * double(spec.voxelSize));

  return std::vector<uint8_t>(base, base + total);
}

} // namespace cts
} // namespace anari
