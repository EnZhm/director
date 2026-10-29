# Cross-plugin tests for the Director suite: the shared follower edge detector, and (AUDIT card)
# one scripted clock driving every follower's scheduler exactly once per edge.
#   cmake -S tests/xplugin -B build-xplugin && cmake --build build-xplugin && ctest --test-dir build-xplugin
cmake_minimum_required(VERSION 3.16)
project(director_xplugin_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
enable_testing()

set(DIRECTOR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(xplugin_tests main.cpp test_follow.cpp)
target_include_directories(
  xplugin_tests
  PRIVATE ${DIRECTOR_ROOT}/common ${DIRECTOR_ROOT}/plugins/palette-director/third_party/doctest
)
if(MSVC)
  target_compile_options(xplugin_tests PRIVATE /W4 /WX)
else()
  target_compile_options(xplugin_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME xplugin COMMAND xplugin_tests)
