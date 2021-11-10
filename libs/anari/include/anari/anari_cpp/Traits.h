// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"

namespace anari {

// Infer (compile time) ANARIDataType from input type /////////////////////////

template <typename T>
struct ANARITypeFor
{
  static constexpr int value = ANARI_UNKNOWN;
};

#define ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                         \
  template <>                                                                  \
  struct ANARITypeFor<type>                                                    \
  {                                                                            \
    static constexpr int value = anari_type;                                   \
  };                                                                           \
                                                                               \
  template <>                                                                  \
  struct ANARITypeFor<const type>                                              \
  {                                                                            \
    static constexpr int value = anari_type;                                   \
  };

#define ANARI_TYPEFOR_SPECIALIZATION_STRING(type, anari_type)                  \
  template <>                                                                  \
  struct ANARITypeFor<type>                                                    \
  {                                                                            \
    static constexpr int value = anari_type;                                   \
  };

ANARI_TYPEFOR_SPECIALIZATION(void *, ANARI_VOID_POINTER);
ANARI_TYPEFOR_SPECIALIZATION_STRING(char *, ANARI_STRING);
ANARI_TYPEFOR_SPECIALIZATION_STRING(char[], ANARI_STRING);
ANARI_TYPEFOR_SPECIALIZATION_STRING(const char *, ANARI_STRING);
ANARI_TYPEFOR_SPECIALIZATION_STRING(const char[], ANARI_STRING);
ANARI_TYPEFOR_SPECIALIZATION(bool, ANARI_BOOL);
ANARI_TYPEFOR_SPECIALIZATION(signed char, ANARI_INT8);
ANARI_TYPEFOR_SPECIALIZATION(unsigned char, ANARI_UINT8);
ANARI_TYPEFOR_SPECIALIZATION(short, ANARI_INT16);
ANARI_TYPEFOR_SPECIALIZATION(unsigned short, ANARI_UINT16);
ANARI_TYPEFOR_SPECIALIZATION(int, ANARI_INT32);
ANARI_TYPEFOR_SPECIALIZATION(unsigned int, ANARI_UINT32);
ANARI_TYPEFOR_SPECIALIZATION(long, ANARI_INT64);
ANARI_TYPEFOR_SPECIALIZATION(unsigned long, ANARI_UINT64);
ANARI_TYPEFOR_SPECIALIZATION(long long, ANARI_INT64);
ANARI_TYPEFOR_SPECIALIZATION(unsigned long long, ANARI_UINT64);
ANARI_TYPEFOR_SPECIALIZATION(float, ANARI_FLOAT32);
ANARI_TYPEFOR_SPECIALIZATION(double, ANARI_FLOAT64);

ANARI_TYPEFOR_SPECIALIZATION(unsigned char[2], ANARI_UINT8_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(unsigned char[3], ANARI_UINT8_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(unsigned char[4], ANARI_UINT8_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(int[2], ANARI_INT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(int[3], ANARI_INT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(int[4], ANARI_INT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(unsigned int[2], ANARI_UINT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(unsigned int[3], ANARI_UINT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(unsigned int[4], ANARI_UINT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(long long[2], ANARI_INT64_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(long long[3], ANARI_INT64_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(long long[4], ANARI_INT64_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(unsigned long long[2], ANARI_UINT64_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(unsigned long long[3], ANARI_UINT64_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(unsigned long long[4], ANARI_UINT64_VEC4);

ANARI_TYPEFOR_SPECIALIZATION(float[2], ANARI_FLOAT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(float[3], ANARI_FLOAT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(float[4], ANARI_FLOAT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(float[12], ANARI_FLOAT32_MAT3x4);

ANARI_TYPEFOR_SPECIALIZATION(ANARILibrary, ANARI_LIBRARY);
ANARI_TYPEFOR_SPECIALIZATION(ANARIObject, ANARI_OBJECT);
ANARI_TYPEFOR_SPECIALIZATION(ANARICamera, ANARI_CAMERA);
ANARI_TYPEFOR_SPECIALIZATION(ANARIArray, ANARI_ARRAY);
ANARI_TYPEFOR_SPECIALIZATION(ANARIArray1D, ANARI_ARRAY1D);
ANARI_TYPEFOR_SPECIALIZATION(ANARIArray2D, ANARI_ARRAY2D);
ANARI_TYPEFOR_SPECIALIZATION(ANARIArray3D, ANARI_ARRAY3D);
ANARI_TYPEFOR_SPECIALIZATION(ANARIFrame, ANARI_FRAME);
ANARI_TYPEFOR_SPECIALIZATION(ANARIGeometry, ANARI_GEOMETRY);
ANARI_TYPEFOR_SPECIALIZATION(ANARIGroup, ANARI_GROUP);
ANARI_TYPEFOR_SPECIALIZATION(ANARIInstance, ANARI_INSTANCE);
ANARI_TYPEFOR_SPECIALIZATION(ANARILight, ANARI_LIGHT);
ANARI_TYPEFOR_SPECIALIZATION(ANARIMaterial, ANARI_MATERIAL);
ANARI_TYPEFOR_SPECIALIZATION(ANARIRenderer, ANARI_RENDERER);
ANARI_TYPEFOR_SPECIALIZATION(ANARISampler, ANARI_SAMPLER);
ANARI_TYPEFOR_SPECIALIZATION(ANARISurface, ANARI_SURFACE);
ANARI_TYPEFOR_SPECIALIZATION(ANARISpatialField, ANARI_SPATIAL_FIELD);
ANARI_TYPEFOR_SPECIALIZATION(ANARIVolume, ANARI_VOLUME);
ANARI_TYPEFOR_SPECIALIZATION(ANARIWorld, ANARI_WORLD);

ANARI_TYPEFOR_SPECIALIZATION(ANARIStatusCallback, ANARI_STATUS_CALLBACK);
ANARI_TYPEFOR_SPECIALIZATION(ANARIMemoryDeleter, ANARI_MEMORY_DELETER);
ANARI_TYPEFOR_SPECIALIZATION(
    ANARIFrameCompletionCallback, ANARI_FRAME_COMPLETION_CALLBACK);

ANARI_TYPEFOR_SPECIALIZATION(ANARIDataType, ANARI_DATA_TYPE);

#define ANARI_TYPEFOR_DEFINITION(type)                                         \
  constexpr int ANARITypeFor<type>::value;                                     \
  constexpr int ANARITypeFor<const type>::value

#define ANARI_TYPEFOR_DEFINITION_STRING(type)                                  \
  constexpr int ANARITypeFor<type>::value;

// Infer whether ANARI_DATA_TYPE is an object or array ////////////////////////

constexpr bool isObject(ANARIDataType type)
{
  return type >= ANARI_OBJECT && type <= ANARI_WORLD;
}

constexpr bool isArray(ANARIDataType type)
{
  return type >= ANARI_ARRAY && type <= ANARI_ARRAY3D;
}

} // namespace anari
