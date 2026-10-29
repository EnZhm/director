# Scene Director core (libobs/Qt-free) + doctest suite.
#   cmake -S tests -B build-tests && cmake --build build-tests --config Release
#   ctest --test-dir build-tests -C Release --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(sd_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

set(SD_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)

find_package(Threads REQUIRED)

add_library(
  sd_core
  STATIC
  ${SD_CORE}/midi_map.cpp
  ${SD_CORE}/midi_msg.cpp
  ${SD_CORE}/settings.cpp
  ${SD_CORE}/switch_sched.cpp
)
target_include_directories(sd_core PUBLIC ${SD_CORE})
target_link_libraries(sd_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(sd_core PRIVATE /W4 /WX)
else()
  target_compile_options(sd_core PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(
  sd_core_tests
  doctest_main.cpp
  test_midi_map.cpp
  test_midi_msg.cpp
  test_settings.cpp
  test_switch_sched.cpp
)
target_include_directories(sd_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest)
target_link_libraries(sd_core_tests PRIVATE sd_core)
add_test(NAME sd_core_tests COMMAND sd_core_tests)
