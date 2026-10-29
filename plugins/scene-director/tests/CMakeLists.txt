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
set(DIRECTOR_MIDI ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/midi)
set(DIRECTOR_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)
set(DIRECTOR_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

find_package(Threads REQUIRED)

add_library(
  sd_core
  STATIC
  ${DIRECTOR_MIDI}/midi_map.cpp
  ${DIRECTOR_MIDI}/midi_msg.cpp
  ${SD_CORE}/settings.cpp
  ${SD_CORE}/switch_sched.cpp
)
target_include_directories(sd_core PUBLIC ${SD_CORE} ${DIRECTOR_MIDI} ${DIRECTOR_COMMON} ${DIRECTOR_INCLUDE})
target_link_libraries(sd_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(sd_core PRIVATE /W4 /WX)
else()
  target_compile_options(sd_core PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(
  sd_core_tests
  doctest_main.cpp
  test_settings.cpp
  test_switch_sched.cpp
  test_follow_script.cpp
  test_seq_guard.cpp
)
target_include_directories(sd_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest)
target_link_libraries(sd_core_tests PRIVATE sd_core)
add_test(NAME sd_core_tests COMMAND sd_core_tests)

# Static contract: a source with OBS_SOURCE_VIDEO must set video_render (see the script).
add_test(
  NAME source_contract
  COMMAND
    ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../src -P ${CMAKE_CURRENT_SOURCE_DIR}/source_contract.cmake
)
