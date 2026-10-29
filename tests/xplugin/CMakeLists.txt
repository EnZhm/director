# Cross-plugin tests for the Director suite:
#   xplugin_tests  director-follow edge cases, the shared MIDI code, Palette Director exactly-once,
#                  and the Music Director + Palette Director equivalence with PD 1.2.0 (golden)
#   xplugin_cd     Clip Director exactly-once (own executable: Clip and Scene Director both use
#   xplugin_sd     Scene Director exactly-once   namespace cd for their cores)
# All three are driven by the same scripted clock (clock_script.h).
#   cmake -S tests/xplugin -B build-xplugin && cmake --build build-xplugin && ctest --test-dir build-xplugin
cmake_minimum_required(VERSION 3.16)
project(director_xplugin_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
enable_testing()

set(DIRECTOR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(PLUGINS ${DIRECTOR_ROOT}/plugins)
set(DOCTEST ${PLUGINS}/palette-director/third_party/doctest)
find_package(Threads REQUIRED)

if(MSVC)
  set(XP_WARN /W4 /WX)
else()
  set(XP_WARN -Wall -Wextra -Werror)
endif()

add_library(
  director_midi_core
  STATIC
  ${DIRECTOR_ROOT}/common/midi/midi_map.cpp
  ${DIRECTOR_ROOT}/common/midi/midi_msg.cpp
)
target_include_directories(director_midi_core PUBLIC ${DIRECTOR_ROOT}/common/midi)
target_link_libraries(director_midi_core PUBLIC Threads::Threads)

# Music Director clock core (each library sees only its own plugin's headers)
add_library(
  xp_md_core
  STATIC
  ${PLUGINS}/music-director/src/director/clock.cpp
  ${PLUGINS}/music-director/src/director/analysis.cpp
  ${PLUGINS}/music-director/src/director/beat_tracker.cpp
  ${PLUGINS}/music-director/src/director/section_state.cpp
  ${PLUGINS}/music-director/tests/fixtures/gen_fixtures.cpp
)
target_include_directories(
  xp_md_core
  PRIVATE ${PLUGINS}/music-director/src/director ${PLUGINS}/music-director/tests ${DIRECTOR_ROOT}/common
)

# Palette Director follower core
add_library(
  xp_pd_core
  STATIC
  ${PLUGINS}/palette-director/src/director/palette-director.cpp
  ${PLUGINS}/palette-director/src/director/scheduler.cpp
)
target_include_directories(
  xp_pd_core
  PRIVATE ${PLUGINS}/palette-director/src/director ${DIRECTOR_ROOT}/common ${DIRECTOR_ROOT}/include
)

add_executable(
  xplugin_tests
  main.cpp
  test_follow.cpp
  test_midi_map.cpp
  test_midi_msg.cpp
  test_xonce_pd.cpp
  test_equivalence.cpp
)
target_include_directories(xplugin_tests PRIVATE ${DIRECTOR_ROOT}/common ${PLUGINS} ${DOCTEST})
target_compile_definitions(xplugin_tests PRIVATE XPLUGIN_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_link_libraries(xplugin_tests PRIVATE director_midi_core xp_md_core xp_pd_core)
target_compile_options(xplugin_tests PRIVATE ${XP_WARN})
add_test(NAME xplugin COMMAND xplugin_tests)

add_executable(xplugin_cd test_xonce_cd.cpp ${PLUGINS}/clip-director/src/core/scheduler.cpp)
target_include_directories(xplugin_cd PRIVATE ${PLUGINS} ${PLUGINS}/clip-director/src/core ${DOCTEST})
target_compile_options(xplugin_cd PRIVATE ${XP_WARN})
add_test(NAME xplugin_cd COMMAND xplugin_cd)

add_executable(xplugin_sd test_xonce_sd.cpp ${PLUGINS}/scene-director/src/core/switch_sched.cpp)
target_include_directories(xplugin_sd PRIVATE ${PLUGINS} ${PLUGINS}/scene-director/src/core ${DOCTEST})
target_compile_options(xplugin_sd PRIVATE ${XP_WARN})
add_test(NAME xplugin_sd COMMAND xplugin_sd)
