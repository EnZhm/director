# Self-contained offline Music Director clock test project. Builds the pure-C++ clock library
# (no libobs/Qt) plus a doctest suite driven by synthetic WAV fixtures.
#
# Standalone:  cmake -S tests -B build-tests && cmake --build build-tests \
#              && ctest --test-dir build-tests --output-on-failure
#
# The AUDIT card links md_core (built here) together with Palette Director's scheduler/follower core
# into tests/xplugin; every core source is listed below so another CMake project can compile it by
# path (e.g. ${CMAKE_CURRENT_SOURCE_DIR}/../src/director/clock.cpp).
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(md_clock_tests C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

enable_testing()

set(MD_DIRECTOR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../src/director)

# The Music Director clock core: libobs-free, Qt-free, static. See the header comment.
add_library(
  md_core
  STATIC
  ${MD_DIRECTOR_ROOT}/clock.cpp
  ${MD_DIRECTOR_ROOT}/analysis.cpp
  ${MD_DIRECTOR_ROOT}/beat_tracker.cpp
  ${MD_DIRECTOR_ROOT}/section_state.cpp
  ${MD_DIRECTOR_ROOT}/migration.cpp
)
target_include_directories(md_core PUBLIC ${MD_DIRECTOR_ROOT})

add_executable(
  md_clock_tests
  doctest_main.cpp
  test_rng.cpp
  test_bpm.cpp
  test_sections.cpp
  test_silence.cpp
  test_determinism.cpp
  test_breakdown_bpm.cpp
  test_audio_level.cpp
  test_clock_api.cpp
  test_clock_phrase_beat.cpp
  test_music_commands.cpp
  test_clock_epoch.cpp
  test_migration.cpp
  fixtures/gen_fixtures.cpp
)
target_link_libraries(md_clock_tests PRIVATE md_core)
target_compile_definitions(md_clock_tests PRIVATE MD_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(
  md_clock_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    ${MD_DIRECTOR_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest
)

add_test(NAME clock_tests COMMAND md_clock_tests)
