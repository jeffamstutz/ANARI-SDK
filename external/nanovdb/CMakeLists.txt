## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# NanoVDB's data structure headers (OpenVDB 13.0.0, NanoVDB 32.9.0), for the
# CTS to build NanoVDB grids in code. Header-only; see README.md.

if (TARGET anari_nanovdb)
  return()
endif()

add_library(anari_nanovdb INTERFACE)
target_include_directories(anari_nanovdb INTERFACE ${CMAKE_CURRENT_LIST_DIR})
