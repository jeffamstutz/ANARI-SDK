// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LockableObject.h"
#include "utility/TimeStamp.h"
// anari_cpp
#include <anari/anari_cpp.hpp>
// std
#include <string_view>

#include "BaseGlobalDeviceState.h"
#include "utility/IntrusivePtr.h"
#include "utility/ParameterizedObject.h"

namespace helium {

struct BaseObject : public RefCounted, ParameterizedObject, LockableObject
{
  // Construct
  BaseObject(ANARIDataType type, BaseGlobalDeviceState *state);
  virtual ~BaseObject();

  // Base hook for devices to check if the object is valid to use, whatever that
  // means. Devices must be able to handle object subtypes that it does not
  // implement, or handle cases when objects are ill-formed. This gives a
  // generic place to ask the object if it's 'OK' to use.
  virtual bool isValid() const = 0;

  // Implement anariGetProperty()
  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) = 0;

  // Implement anariCommitParameters(), but this will only occur when the
  // commit buffer is flushed. This will get skipped if the object has not
  // received any parameter changes since the last commit. Simply call
  // markUpdated() before adding to the commit buffer if you want the commit to
  // occur apart from the public anariCommitParameters() API call.
  virtual void commitParameters() = 0;

  // This hook is used to indicate that the internals of an object need to be
  // updated, either because new parameters have been committed or that another
  // object has changed causing the need for this object to update. Similar to
  // commitParameters(), this is intended to be done in a buffered manner in
  // between frames and is not expected to be called directly.
  virtual void finalize() = 0;

  // Object
  ANARIDataType type() const;

  // Event tracking of when parameters have changed (via set or unset)
  TimeStamp lastParameterChanged() const;
  void markParameterChanged();

  // Event tracking of when an object's internal state changed
  TimeStamp lastUpdated() const;
  void markUpdated();

  // Event tracking of when the objects parameters have been committed
  TimeStamp lastCommitted() const;
  virtual void markCommitted();

  // Event tracking of when the objects parameters have been finalized
  TimeStamp lastFinalized() const;
  virtual void markFinalized();

  // Report a message through the status callback set on the device
  template <typename... Args>
  void reportMessage(
      ANARIStatusSeverity, const char *fmt, Args &&...args) const;

  // Allow other objects to "listen" for when this object changes. By default
  // listening objects are put into the commit buffer so they are committed
  // again next frame.
  void addChangeObserver(BaseObject *obj);
  void removeChangeObserver(BaseObject *obj);
  void notifyChangeObservers() const;

  BaseGlobalDeviceState *deviceState() const;

 protected:
  // Handle what happens when the observing object 'obj' is being notified of
  // that this object has changed. Default behavior is to mark 'obj' as being
  // updated and putting it into the commit queue (so it gets committed next
  // frame).
  virtual void notifyChangeObserver(BaseObject *obj) const;

  BaseGlobalDeviceState *m_state{nullptr};

 private:
  void incrementObjectCount();
  void decrementObjectCount();

  std::vector<BaseObject *> m_changeObservers;
  TimeStamp m_lastParameterChanged{0};
  TimeStamp m_lastUpdated{0};
  TimeStamp m_lastCommitted{0};
  TimeStamp m_lastFinalized{0};
  ANARIDataType m_type{ANARI_OBJECT};
};

// Return a value to correctly order object by type in the commit buffer
int commitPriority(ANARIDataType type);

std::string string_printf(const char *fmt, ...);

// Inlined defintions /////////////////////////////////////////////////////////

template <typename... Args>
inline void BaseObject::reportMessage(
    ANARIStatusSeverity severity, const char *fmt, Args &&...args) const
{
  auto msg = string_printf(fmt, std::forward<Args>(args)...);
  m_state->messageFunction(severity, msg, type(), this);
}

// Helper functions ///////////////////////////////////////////////////////////

template <typename T>
inline void writeToVoidP(void *_p, T v)
{
  auto *p = (T *)_p;
  *p = v;
}

template <>
inline void writeToVoidP(void *_p, bool _v)
{
  uint8_t v = _v;
  auto *p = (uint8_t *)_p;
  *p = v;
}

} // namespace helium

#define HELIUM_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define HELIUM_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::BaseObject *, ANARI_OBJECT);
