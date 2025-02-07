// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeferredCommitBuffer.h"
#include "BaseObject.h"
// std
#include <algorithm>

namespace helium {

// Helper functions ///////////////////////////////////////////////////////////

template <typename T, typename FCN_T>
static void dyanmic_foreach(std::vector<T> &buffer, FCN_T &&fcn)
{
  size_t i = 0;
  size_t end = buffer.size();
  while (i != end) {
    for (; i < end; i++)
      fcn(i);
    end = buffer.size();
  }
}

// DeferredCommitBuffer definitions ///////////////////////////////////////////

DeferredCommitBuffer::DeferredCommitBuffer()
{
  m_commitBuffer.reserve(100);
}

DeferredCommitBuffer::~DeferredCommitBuffer()
{
  clear();
}

void DeferredCommitBuffer::addObject(BaseObject *obj)
{
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  obj->refInc(RefType::INTERNAL);
  if (commitPriority(obj->type()) != commitPriority(ANARI_OBJECT))
    m_needToSortCommits = true;
  m_commitBuffer.push_back(obj);
}

bool DeferredCommitBuffer::flush()
{
  if (m_commitBuffer.empty())
    return false;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (m_needToSortCommits) {
    std::sort(m_commitBuffer.begin(),
        m_commitBuffer.end(),
        [](BaseObject *o1, BaseObject *o2) {
          return commitPriority(o1->type()) < commitPriority(o2->type());
        });
  }

  m_needToSortCommits = false;

  dyanmic_foreach(m_commitBuffer, [&](size_t i) {
    auto obj = m_commitBuffer[i];
    if (obj->useCount() > 1
        && obj->lastParameterChanged() > obj->lastCommitted()) {
      obj->commitParameters();
      obj->markCommitted();
      obj->markUpdated();
    }
  });

  bool didFinalize = false;
  dyanmic_foreach(m_commitBuffer, [&](size_t i) {
    auto obj = m_commitBuffer[i];
    if (obj->useCount() > 1 && obj->lastUpdated() > obj->lastFinalized()) {
      didFinalize = true;
      obj->finalize();
      obj->markFinalized();
    }
  });

  clear();
  m_lastFlush = newTimeStamp();
  return didFinalize;
}

TimeStamp DeferredCommitBuffer::lastFlush() const
{
  return m_lastFlush;
}

void DeferredCommitBuffer::clear()
{
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  for (auto &obj : m_commitBuffer)
    obj->refDec(RefType::INTERNAL);
  m_commitBuffer.clear();
  m_lastFlush = 0;
}

bool DeferredCommitBuffer::empty() const
{
  return m_commitBuffer.empty();
}

} // namespace helium