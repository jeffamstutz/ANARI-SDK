## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## Installation of scripts (independent of code-gen targets) ##

option(INSTALL_CODE_GEN_SCRIPTS "Install scripts in code_gen/" ON)
if (INSTALL_CODE_GEN_SCRIPTS)
  install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari
    COMPONENT code_gen
    FILES_MATCHING
      PATTERN *.py
      PATTERN *.h
      PATTERN *.cpp
      PATTERN *.json
      PATTERN templates/CMakeLists.txt
      PATTERN ./CMakeLists.txt EXCLUDE
  )
endif()

## Local targets for code-gen + API bindings ##

if(CMAKE_VERSION VERSION_LESS "3.12")
  return()
endif()

find_package(Python3 OPTIONAL_COMPONENTS Interpreter)

if (NOT TARGET Python3::Interpreter)
  message(WARNING "Unable to find python interpreter, skipping code-gen + API bindings")
  return()
endif()

option(INSTALL_PYTHON_BINDINGS "Install python bindings" OFF) # If turned on, draws in all the custom targets below (generate_all)

set(API_JSON api/anari_core_1_0.json api/khr_frame_completion_callback.json)

add_custom_target(generate_type_utility
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/src/anari/include/anari/frontend/type_utility.h ${API_JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${API_JSON} generate_headers.py
)

add_custom_target(generate_extension_utility
  COMMAND ${Python3_EXECUTABLE} generate_extension_utility.py
  -j ${CMAKE_CURRENT_SOURCE_DIR}/api/
  -d devices/experimental_device.json
  -o ${CMAKE_SOURCE_DIR}/src/anari/include/anari/frontend
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS devices/experimental_device.json generate_extension_utility.py
)

add_custom_target(generate_anari_h
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/src/anari/include/anari/anari.h ${API_JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${API_JSON} generate_headers.py
)

add_custom_target(generate_anari_enums_h
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/src/anari/include/anari/frontend/anari_enums.h ${API_JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${API_JSON} generate_headers.py
)
add_custom_target(generate_headers DEPENDS
    generate_type_utility
    generate_extension_utility
    generate_anari_h
    generate_anari_enums_h
)

add_dependencies(generate_all generate_headers)

if (INSTALL_PYTHON_BINDINGS)
  set(BUILD_PYTHON_BINDINGS_BY_DEFAULT ALL)
endif()

add_custom_target(python_bindings ${BUILD_PYTHON_BINDINGS_BY_DEFAULT}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pyanari_build.py
  -d ${CMAKE_CURRENT_SOURCE_DIR}/devices/generic_device.json
  -j ${CMAKE_CURRENT_SOURCE_DIR}/api/
  -I ${CMAKE_SOURCE_DIR}/src/anari/include/
  -I ${CMAKE_BINARY_DIR}/src/anari/include/anari/
  -L ${CMAKE_BINARY_DIR}
  -L ${CMAKE_BINARY_DIR}/$<CONFIG>
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS pyanari_build.py anari ${API_JSON} devices/generic_device.json
)

if (INSTALL_PYTHON_BINDINGS)
  if (WIN32)
    set(PYANARI_LIB_FILE pyanari.pyd)
  else()
    set(PYANARI_LIB_FILE pyanari.so)
  endif()
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/anari.py ${CMAKE_CURRENT_BINARY_DIR}/${PYANARI_LIB_FILE}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/python_bindings)
endif()
