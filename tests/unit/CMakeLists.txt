## Copyright 2021-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

enable_testing()

project(anariUnitTests LANGUAGES CXX)

add_executable(${PROJECT_NAME}
  catch_main.cpp

  test_helium_AnariAny.cpp
  test_helium_fixed_point.cpp
  test_helium_ParameterizedObject.cpp
  test_helium_RefCounted.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE helium)

add_test(NAME unit_test::helium::AnariAny            COMMAND ${PROJECT_NAME} "[helium_AnariAny]"           )
add_test(NAME unit_test::helium::fixed_point         COMMAND ${PROJECT_NAME} "[helium_fixed]"              )
add_test(NAME unit_test::helium::ParameterizedObject COMMAND ${PROJECT_NAME} "[helium_ParameterizedObject]")
add_test(NAME unit_test::helium::RefCounted          COMMAND ${PROJECT_NAME} "[helium_RefCounted]"         )
