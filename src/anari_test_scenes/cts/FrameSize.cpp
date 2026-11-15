// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "FrameSize.h"
#include "BuildContext.h"
#include "Case.h"
#include "Image.h"
#include "TestDef.h"
// std
#include <sstream>

namespace anari {
namespace cts {

std::string sizeText(FrameSize size)
{
  return std::to_string(size.width) + "x" + std::to_string(size.height);
}

CaseFrameSize regionFrameSize(
    FrameSize run, float loX, float loY, float hiX, float hiY)
{
  const auto rect = regionPixels(run.width, run.height, loX, loY, hiX, hiY);
  if (!rect) {
    std::ostringstream os;
    os << "the region (" << loX << ", " << loY << ")-(" << hiX << ", " << hiY
       << ") does not cover whole pixels at " << sizeText(run);
    return CaseFrameSize::skip(os.str());
  }
  return FrameSize{rect->width, rect->height};
}

CaseFrameSize resolveFrameSize(
    const TestDef &test, const Case &c, FrameSize run, anari::Device device)
{
  if (!test.frameSize)
    return run;
  BuildContext ctx(device);
  for (const auto &cv : c.values)
    ctx.set(cv.axisName, cv.value);
  CaseFrameSize out = test.frameSize(ctx, run);
  if (out.resolved() && (out.size.width == 0 || out.size.height == 0))
    return CaseFrameSize::skip("the frame-size hook gave an empty frame ("
        + sizeText(out.size) + ") at " + sizeText(run));
  return out;
}

} // namespace cts
} // namespace anari
