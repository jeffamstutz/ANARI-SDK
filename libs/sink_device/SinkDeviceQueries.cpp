// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// This file was generated by generate_queries.py
// Don't make changes to this directly

#include <anari/anari.h>
namespace anari {
namespace sink_device {
static int subtype_hash(const char *str) {
   static const uint32_t table[] = {0x7a6f0012u,0x6a65002bu,0x0u,0x0u,0x0u,0x0u,0x6e6d0040u,0x0u,0x0u,0x0u,0x6261004du,0x0u,0x736d0052u,0x73650071u,0x76750095u,0x0u,0x75630099u,0x737200ceu,0x6f6e001du,0x0u,0x0u,0x0u,0x0u,0x0u,0x73720020u,0x0u,0x0u,0x0u,0x6d6c0024u,0x6665001eu,0x100001fu,0x8000000au,0x77760021u,0x66650022u,0x1000023u,0x80000006u,0x6a690025u,0x6f6e0026u,0x65640027u,0x66650028u,0x73720029u,0x100002au,0x80000003u,0x67660030u,0x0u,0x0u,0x0u,0x73720036u,0x62610031u,0x76750032u,0x6d6c0033u,0x75740034u,0x1000035u,0x80000000u,0x66650037u,0x64630038u,0x75740039u,0x6a69003au,0x706f003bu,0x6f6e003cu,0x6261003du,0x6d6c003eu,0x100003fu,0x80000013u,0x62610041u,0x68670042u,0x66650043u,0x34310044u,0x45440047u,0x45440049u,0x4544004bu,0x1000048u,0x80000005u,0x100004au,0x8000000bu,0x100004cu,0x80000001u,0x7574004eu,0x7574004fu,0x66650050u,0x1000051u,0x8000000cu,0x6f6e0058u,0x0u,0x0u,0x0u,0x0u,0x75740066u,0x6a690059u,0x6564005au,0x6a69005bu,0x7372005cu,0x6665005du,0x6463005eu,0x7574005fu,0x6a690060u,0x706f0061u,0x6f6e0062u,0x62610063u,0x6d6c0064u,0x1000065u,0x80000014u,0x69680067u,0x706f0068u,0x68670069u,0x7372006au,0x6261006bu,0x7170006cu,0x6968006du,0x6a69006eu,0x6463006fu,0x1000070u,0x80000011u,0x7372007fu,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x6a690089u,0x0u,0x0u,0x6a69008du,0x74730080u,0x71700081u,0x66650082u,0x64630083u,0x75740084u,0x6a690085u,0x77760086u,0x66650087u,0x1000088u,0x80000007u,0x6f6e008au,0x7574008bu,0x100008cu,0x8000000du,0x6e6d008eu,0x6a69008fu,0x75740090u,0x6a690091u,0x77760092u,0x66650093u,0x1000094u,0x8000000eu,0x62610096u,0x65640097u,0x1000098u,0x80000012u,0x6a6900abu,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x706800b0u,0x0u,0x0u,0x0u,0x737200beu,0x777600acu,0x6a6900adu,0x747300aeu,0x10000afu,0x8000000fu,0x666500b8u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x757400bcu,0x737200b9u,0x666500bau,0x10000bbu,0x80000010u,0x10000bdu,0x80000009u,0x767500bfu,0x646300c0u,0x757400c1u,0x767500c2u,0x737200c3u,0x666500c4u,0x656400c5u,0x535200c6u,0x666500c7u,0x686700c8u,0x767500c9u,0x6d6c00cau,0x626100cbu,0x737200ccu,0x10000cdu,0x80000008u,0x6a6100cfu,0x6f6e00d8u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x626100f4u,0x747300d9u,0x716600dau,0x706f00e5u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x626100e9u,0x737200e6u,0x6e6d00e7u,0x10000e8u,0x80000004u,0x737200eau,0x666500ebu,0x6f6e00ecu,0x757400edu,0x4e4d00eeu,0x626100efu,0x757400f0u,0x757400f1u,0x666500f2u,0x10000f3u,0x80000002u,0x6f6e00f5u,0x686700f6u,0x6d6c00f7u,0x666500f8u,0x10000f9u,0x80000015u};
   uint32_t cur = 0x75630000u;
   for(int i = 0;cur!=0;++i) {
      uint32_t idx = cur&0xFFFFu;
      uint32_t low = (cur>>16u)&0xFFu;
      uint32_t high = (cur>>24u)&0xFFu;
      uint32_t c = str[i];
      if(c>=low && c<high) {
         cur = table[idx+c-low];
      } else {
         break;
      }
      if(cur&0x80000000u) {
         return cur&0xFFFFu;
      }
      if(str[i]==0) {
         break;
      }
   }
   return -1;
}
const char ** query_object_types(ANARIDataType type) {
   switch(type) {
      case ANARI_SAMPLER:
      {
         static const char *ANARI_SAMPLER_subtypes[] = {"image1D", "image2D", "image3D", "primitive", "transform", 0};
         return ANARI_SAMPLER_subtypes;
      }
      case ANARI_VOLUME:
      {
         static const char *ANARI_VOLUME_subtypes[] = {"scivis", 0};
         return ANARI_VOLUME_subtypes;
      }
      case ANARI_RENDERER:
      {
         static const char *ANARI_RENDERER_subtypes[] = {"default", 0};
         return ANARI_RENDERER_subtypes;
      }
      case ANARI_CAMERA:
      {
         static const char *ANARI_CAMERA_subtypes[] = {"perspective", "orthographic", "omnidirectional", 0};
         return ANARI_CAMERA_subtypes;
      }
      case ANARI_LIGHT:
      {
         static const char *ANARI_LIGHT_subtypes[] = {"directional", "point", "spot", 0};
         return ANARI_LIGHT_subtypes;
      }
      case ANARI_SPATIAL_FIELD:
      {
         static const char *ANARI_SPATIAL_FIELD_subtypes[] = {"structuredRegular", 0};
         return ANARI_SPATIAL_FIELD_subtypes;
      }
      case ANARI_GEOMETRY:
      {
         static const char *ANARI_GEOMETRY_subtypes[] = {"triangle", "quad", "sphere", "curve", "cone", "cylinder", 0};
         return ANARI_GEOMETRY_subtypes;
      }
      case ANARI_MATERIAL:
      {
         static const char *ANARI_MATERIAL_subtypes[] = {"matte", "transparentMatte", 0};
         return ANARI_MATERIAL_subtypes;
      }
      default:
      {
         static const char *none_subtypes[] = {0};
         return none_subtypes;
      }
   }
}
static const ANARIParameter * ANARI_GEOMETRY_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 21:
      {
         static const ANARIParameter triangle_params[] = {{"name", ANARI_STRING}, {"primitive.color", ANARI_ARRAY1D}, {"primitive.attribute0", ANARI_ARRAY1D}, {"primitive.attribute1", ANARI_ARRAY1D}, {"primitive.attribute2", ANARI_ARRAY1D}, {"primitive.attribute3", ANARI_ARRAY1D}, {"primitive.id", ANARI_ARRAY1D}, {"vertex.position", ANARI_ARRAY1D}, {"vertex.normal", ANARI_ARRAY1D}, {"vertex.color", ANARI_ARRAY1D}, {"vertex.attribute0", ANARI_ARRAY1D}, {"vertex.attribute1", ANARI_ARRAY1D}, {"vertex.attribute2", ANARI_ARRAY1D}, {"vertex.attribute3", ANARI_ARRAY1D}, {"primitive.index", ANARI_ARRAY1D}, {0, ANARI_UNKNOWN}};
         return triangle_params;
      }
      case 18:
      {
         static const ANARIParameter quad_params[] = {{"name", ANARI_STRING}, {"primitive.color", ANARI_ARRAY1D}, {"primitive.attribute0", ANARI_ARRAY1D}, {"primitive.attribute1", ANARI_ARRAY1D}, {"primitive.attribute2", ANARI_ARRAY1D}, {"primitive.attribute3", ANARI_ARRAY1D}, {"primitive.id", ANARI_ARRAY1D}, {"vertex.position", ANARI_ARRAY1D}, {"vertex.normal", ANARI_ARRAY1D}, {"vertex.color", ANARI_ARRAY1D}, {"vertex.attribute0", ANARI_ARRAY1D}, {"vertex.attribute1", ANARI_ARRAY1D}, {"vertex.attribute2", ANARI_ARRAY1D}, {"vertex.attribute3", ANARI_ARRAY1D}, {"primitive.index", ANARI_ARRAY1D}, {0, ANARI_UNKNOWN}};
         return quad_params;
      }
      case 16:
      {
         static const ANARIParameter sphere_params[] = {{"name", ANARI_STRING}, {"primitive.color", ANARI_ARRAY1D}, {"primitive.attribute0", ANARI_ARRAY1D}, {"primitive.attribute1", ANARI_ARRAY1D}, {"primitive.attribute2", ANARI_ARRAY1D}, {"primitive.attribute3", ANARI_ARRAY1D}, {"primitive.id", ANARI_ARRAY1D}, {"vertex.position", ANARI_ARRAY1D}, {"vertex.radius", ANARI_ARRAY1D}, {"vertex.color", ANARI_ARRAY1D}, {"vertex.attribute0", ANARI_ARRAY1D}, {"vertex.attribute1", ANARI_ARRAY1D}, {"vertex.attribute2", ANARI_ARRAY1D}, {"vertex.attribute3", ANARI_ARRAY1D}, {"primitive.index", ANARI_ARRAY1D}, {"radius", ANARI_FLOAT32}, {0, ANARI_UNKNOWN}};
         return sphere_params;
      }
      case 6:
      {
         static const ANARIParameter curve_params[] = {{"name", ANARI_STRING}, {"primitive.color", ANARI_ARRAY1D}, {"primitive.attribute0", ANARI_ARRAY1D}, {"primitive.attribute1", ANARI_ARRAY1D}, {"primitive.attribute2", ANARI_ARRAY1D}, {"primitive.attribute3", ANARI_ARRAY1D}, {"primitive.id", ANARI_ARRAY1D}, {"vertex.position", ANARI_ARRAY1D}, {"vertex.radius", ANARI_ARRAY1D}, {"vertex.color", ANARI_ARRAY1D}, {"vertex.attribute0", ANARI_ARRAY1D}, {"vertex.attribute1", ANARI_ARRAY1D}, {"vertex.attribute2", ANARI_ARRAY1D}, {"vertex.attribute3", ANARI_ARRAY1D}, {"primitive.index", ANARI_ARRAY1D}, {"radius", ANARI_FLOAT32}, {0, ANARI_UNKNOWN}};
         return curve_params;
      }
      case 10:
      {
         static const ANARIParameter cone_params[] = {{"name", ANARI_STRING}, {"primitive.color", ANARI_ARRAY1D}, {"primitive.attribute0", ANARI_ARRAY1D}, {"primitive.attribute1", ANARI_ARRAY1D}, {"primitive.attribute2", ANARI_ARRAY1D}, {"primitive.attribute3", ANARI_ARRAY1D}, {"primitive.id", ANARI_ARRAY1D}, {"vertex.position", ANARI_ARRAY1D}, {"vertex.radius", ANARI_ARRAY1D}, {"vertex.cap", ANARI_ARRAY1D}, {"vertex.color", ANARI_ARRAY1D}, {"vertex.attribute0", ANARI_ARRAY1D}, {"vertex.attribute1", ANARI_ARRAY1D}, {"vertex.attribute2", ANARI_ARRAY1D}, {"vertex.attribute3", ANARI_ARRAY1D}, {"primitive.index", ANARI_ARRAY1D}, {"caps", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return cone_params;
      }
      case 3:
      {
         static const ANARIParameter cylinder_params[] = {{"name", ANARI_STRING}, {"primitive.color", ANARI_ARRAY1D}, {"primitive.attribute0", ANARI_ARRAY1D}, {"primitive.attribute1", ANARI_ARRAY1D}, {"primitive.attribute2", ANARI_ARRAY1D}, {"primitive.attribute3", ANARI_ARRAY1D}, {"primitive.id", ANARI_ARRAY1D}, {"vertex.position", ANARI_ARRAY1D}, {"vertex.cap", ANARI_ARRAY1D}, {"vertex.color", ANARI_ARRAY1D}, {"vertex.attribute0", ANARI_ARRAY1D}, {"vertex.attribute1", ANARI_ARRAY1D}, {"vertex.attribute2", ANARI_ARRAY1D}, {"vertex.attribute3", ANARI_ARRAY1D}, {"primitive.index", ANARI_ARRAY1D}, {"primitive.radius", ANARI_ARRAY1D}, {"radius", ANARI_FLOAT32}, {"caps", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return cylinder_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
static const ANARIParameter * ANARI_RENDERER_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 0:
      {
         static const ANARIParameter default_params[] = {{"name", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return default_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
static const ANARIParameter * ANARI_CAMERA_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 7:
      {
         static const ANARIParameter perspective_params[] = {{"name", ANARI_STRING}, {"position", ANARI_FLOAT32_VEC3}, {"direction", ANARI_FLOAT32_VEC3}, {"up", ANARI_FLOAT32_VEC3}, {"transform", ANARI_FLOAT32_MAT3x4}, {"imageRegion", ANARI_FLOAT32_BOX2}, {"apertureRadius", ANARI_FLOAT32}, {"focusDistance", ANARI_FLOAT32}, {"stereoMode", ANARI_STRING}, {"interpupillaryDistance", ANARI_FLOAT32}, {"fovy", ANARI_FLOAT32}, {"aspect", ANARI_FLOAT32}, {0, ANARI_UNKNOWN}};
         return perspective_params;
      }
      case 17:
      {
         static const ANARIParameter orthographic_params[] = {{"name", ANARI_STRING}, {"position", ANARI_FLOAT32_VEC3}, {"direction", ANARI_FLOAT32_VEC3}, {"up", ANARI_FLOAT32_VEC3}, {"transform", ANARI_FLOAT32_MAT3x4}, {"imageRegion", ANARI_FLOAT32_BOX2}, {"apertureRadius", ANARI_FLOAT32}, {"focusDistance", ANARI_FLOAT32}, {"stereoMode", ANARI_STRING}, {"interpupillaryDistance", ANARI_FLOAT32}, {"aspect", ANARI_FLOAT32}, {0, ANARI_UNKNOWN}};
         return orthographic_params;
      }
      case 20:
      {
         static const ANARIParameter omnidirectional_params[] = {{"name", ANARI_STRING}, {"position", ANARI_FLOAT32_VEC3}, {"direction", ANARI_FLOAT32_VEC3}, {"up", ANARI_FLOAT32_VEC3}, {"transform", ANARI_FLOAT32_MAT3x4}, {"imageRegion", ANARI_FLOAT32_BOX2}, {"apertureRadius", ANARI_FLOAT32}, {"focusDistance", ANARI_FLOAT32}, {"stereoMode", ANARI_STRING}, {"interpupillaryDistance", ANARI_FLOAT32}, {"layout", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return omnidirectional_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
static const ANARIParameter * ANARI_VOLUME_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 15:
      {
         static const ANARIParameter scivis_params[] = {{"name", ANARI_STRING}, {"field", ANARI_SPATIAL_FIELD}, {"valueRange", ANARI_FLOAT32_BOX1}, {"color", ANARI_ARRAY1D}, {"color.position", ANARI_ARRAY1D}, {"opacity", ANARI_ARRAY1D}, {"opacity.position", ANARI_ARRAY1D}, {"densityScale", ANARI_FLOAT32}, {0, ANARI_UNKNOWN}};
         return scivis_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
static const ANARIParameter * ANARI_LIGHT_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 19:
      {
         static const ANARIParameter directional_params[] = {{"name", ANARI_STRING}, {"color", ANARI_FLOAT32_VEC3}, {"irridance", ANARI_FLOAT32}, {"direction", ANARI_FLOAT32_VEC3}, {0, ANARI_UNKNOWN}};
         return directional_params;
      }
      case 13:
      {
         static const ANARIParameter point_params[] = {{"name", ANARI_STRING}, {"color", ANARI_FLOAT32_VEC3}, {"position", ANARI_FLOAT32_VEC3}, {"intensity", ANARI_FLOAT32}, {"power", ANARI_FLOAT32}, {0, ANARI_UNKNOWN}};
         return point_params;
      }
      case 9:
      {
         static const ANARIParameter spot_params[] = {{"name", ANARI_STRING}, {"color", ANARI_FLOAT32_VEC3}, {"position", ANARI_FLOAT32_VEC3}, {"direction", ANARI_FLOAT32_VEC3}, {"openingAngle", ANARI_FLOAT32}, {"falloffAngle", ANARI_FLOAT32}, {"intensity", ANARI_FLOAT32}, {"power", ANARI_FLOAT32}, {0, ANARI_UNKNOWN}};
         return spot_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
static const ANARIParameter * ANARI_SPATIAL_FIELD_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 8:
      {
         static const ANARIParameter structuredRegular_params[] = {{"name", ANARI_STRING}, {"data", ANARI_ARRAY3D}, {"origin", ANARI_FLOAT32_VEC3}, {"spacing", ANARI_FLOAT32_VEC3}, {"filter", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return structuredRegular_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
static const ANARIParameter * ANARI_SAMPLER_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 5:
      {
         static const ANARIParameter image1D_params[] = {{"name", ANARI_STRING}, {"image", ANARI_ARRAY1D}, {"inAttribute", ANARI_STRING}, {"filter", ANARI_STRING}, {"wrapMode1", ANARI_STRING}, {"inTransform", ANARI_FLOAT32_MAT4}, {"outTransform", ANARI_FLOAT32_MAT4}, {0, ANARI_UNKNOWN}};
         return image1D_params;
      }
      case 11:
      {
         static const ANARIParameter image2D_params[] = {{"name", ANARI_STRING}, {"image", ANARI_ARRAY2D}, {"inAttribute", ANARI_STRING}, {"filter", ANARI_STRING}, {"wrapMode1", ANARI_STRING}, {"wrapMode2", ANARI_STRING}, {"inTransform", ANARI_FLOAT32_MAT4}, {"outTransform", ANARI_FLOAT32_MAT4}, {0, ANARI_UNKNOWN}};
         return image2D_params;
      }
      case 1:
      {
         static const ANARIParameter image3D_params[] = {{"name", ANARI_STRING}, {"image", ANARI_ARRAY3D}, {"inAttribute", ANARI_STRING}, {"filter", ANARI_STRING}, {"wrapMode1", ANARI_STRING}, {"wrapMode2", ANARI_STRING}, {"wrapMode3", ANARI_STRING}, {"inTransform", ANARI_FLOAT32_MAT4}, {"outTransform", ANARI_FLOAT32_MAT4}, {0, ANARI_UNKNOWN}};
         return image3D_params;
      }
      case 14:
      {
         static const ANARIParameter primitive_params[] = {{"name", ANARI_STRING}, {"array", ANARI_ARRAY1D}, {0, ANARI_UNKNOWN}};
         return primitive_params;
      }
      case 4:
      {
         static const ANARIParameter transform_params[] = {{"name", ANARI_STRING}, {"inAttribute", ANARI_STRING}, {"transform", ANARI_FLOAT32_MAT4}, {0, ANARI_UNKNOWN}};
         return transform_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
static const ANARIParameter * ANARI_MATERIAL_params(const char *subtype) {
   switch(subtype_hash(subtype)) {
      case 12:
      {
         static const ANARIParameter matte_params[] = {{"name", ANARI_STRING}, {"color", ANARI_FLOAT32_VEC3}, {"color", ANARI_SAMPLER}, {"color", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return matte_params;
      }
      case 2:
      {
         static const ANARIParameter transparentMatte_params[] = {{"name", ANARI_STRING}, {"color", ANARI_FLOAT32_VEC3}, {"color", ANARI_SAMPLER}, {"color", ANARI_STRING}, {"opacity", ANARI_FLOAT32}, {"opacity", ANARI_SAMPLER}, {"opacity", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return transparentMatte_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
const ANARIParameter * query_params(ANARIDataType type, const char *subtype) {
   switch(type) {
      case ANARI_GEOMETRY:
      {
         return ANARI_GEOMETRY_params(subtype);
      }
      case ANARI_RENDERER:
      {
         return ANARI_RENDERER_params(subtype);
      }
      case ANARI_CAMERA:
      {
         return ANARI_CAMERA_params(subtype);
      }
      case ANARI_VOLUME:
      {
         return ANARI_VOLUME_params(subtype);
      }
      case ANARI_LIGHT:
      {
         return ANARI_LIGHT_params(subtype);
      }
      case ANARI_SPATIAL_FIELD:
      {
         return ANARI_SPATIAL_FIELD_params(subtype);
      }
      case ANARI_SAMPLER:
      {
         return ANARI_SAMPLER_params(subtype);
      }
      case ANARI_MATERIAL:
      {
         return ANARI_MATERIAL_params(subtype);
      }
      case ANARI_DEVICE:
      {
         static const ANARIParameter ANARI_DEVICE_params[] = {{"name", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return ANARI_DEVICE_params;
      }
      case ANARI_ARRAY1D:
      {
         static const ANARIParameter ANARI_ARRAY1D_params[] = {{"name", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return ANARI_ARRAY1D_params;
      }
      case ANARI_ARRAY2D:
      {
         static const ANARIParameter ANARI_ARRAY2D_params[] = {{"name", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return ANARI_ARRAY2D_params;
      }
      case ANARI_ARRAY3D:
      {
         static const ANARIParameter ANARI_ARRAY3D_params[] = {{"name", ANARI_STRING}, {0, ANARI_UNKNOWN}};
         return ANARI_ARRAY3D_params;
      }
      case ANARI_FRAME:
      {
         static const ANARIParameter ANARI_FRAME_params[] = {{"name", ANARI_STRING}, {"world", ANARI_WORLD}, {"renderer", ANARI_RENDERER}, {"camera", ANARI_CAMERA}, {"size", ANARI_UINT32_VEC2}, {"color", ANARI_DATA_TYPE}, {"depth", ANARI_DATA_TYPE}, {0, ANARI_UNKNOWN}};
         return ANARI_FRAME_params;
      }
      case ANARI_GROUP:
      {
         static const ANARIParameter ANARI_GROUP_params[] = {{"name", ANARI_STRING}, {"surface", ANARI_ARRAY1D}, {"volume", ANARI_ARRAY1D}, {"light", ANARI_ARRAY1D}, {0, ANARI_UNKNOWN}};
         return ANARI_GROUP_params;
      }
      case ANARI_INSTANCE:
      {
         static const ANARIParameter ANARI_INSTANCE_params[] = {{"name", ANARI_STRING}, {"transform", ANARI_FLOAT32_MAT3x4}, {"group", ANARI_GROUP}, {0, ANARI_UNKNOWN}};
         return ANARI_INSTANCE_params;
      }
      case ANARI_WORLD:
      {
         static const ANARIParameter ANARI_WORLD_params[] = {{"name", ANARI_STRING}, {"instance", ANARI_ARRAY1D}, {"surface", ANARI_ARRAY1D}, {"volume", ANARI_ARRAY1D}, {"light", ANARI_ARRAY1D}, {0, ANARI_UNKNOWN}};
         return ANARI_WORLD_params;
      }
      case ANARI_SURFACE:
      {
         static const ANARIParameter ANARI_SURFACE_params[] = {{"name", ANARI_STRING}, {"geometry", ANARI_GEOMETRY}, {"material", ANARI_MATERIAL}, {0, ANARI_UNKNOWN}};
         return ANARI_SURFACE_params;
      }
      default:
      {
         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};
         return none;
      }
   }
}
}
}
