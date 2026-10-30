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
set(DIRECTOR_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)
set(DIRECTOR_OSC ${DIRECTOR_COMMON}/osc)
set(DIRECTOR_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

find_package(Threads REQUIRED)

add_library(
  sd_core
  STATIC
  ${SD_CORE}/settings.cpp
  ${SD_CORE}/switch_sched.cpp
  ${SD_CORE}/confirm.cpp
  ${SD_CORE}/event_map.cpp
  ${SD_CORE}/event_engine.cpp
)
target_include_directories(sd_core PUBLIC ${SD_CORE} ${DIRECTOR_COMMON} ${DIRECTOR_INCLUDE})
target_link_libraries(sd_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(sd_core PRIVATE /W4 /WX)
else()
  target_compile_options(sd_core PRIVATE -Wall -Wextra -Werror)
endif()

# The shared OSC library (codec + UDP transport), namespace director_osc. The source-layer-free
# integration test drives a real transport over 127.0.0.1.
add_library(
  director_osc
  STATIC
  ${DIRECTOR_OSC}/osc.cpp
  ${DIRECTOR_OSC}/osc-transport.cpp
)
target_include_directories(director_osc PUBLIC ${DIRECTOR_OSC})
target_link_libraries(director_osc PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(director_osc PUBLIC ws2_32)
endif()

add_executable(
  sd_core_tests
  doctest_main.cpp
  test_settings.cpp
  test_switch_sched.cpp
  test_confirm.cpp
  test_follow_script.cpp
  test_seq_guard.cpp
  test_event_map.cpp
  test_event_engine.cpp
  test_event_guard.cpp
)
target_include_directories(sd_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest)
target_link_libraries(sd_core_tests PRIVATE sd_core)
add_test(NAME sd_core_tests COMMAND sd_core_tests)

# Source-layer-free integration: engine + confirm + a real director_osc transport on 127.0.0.1
# against a fake Synesthesia (spec 4.5/4.6).
add_executable(
  sd_osc_integration
  doctest_main.cpp
  test_osc_integration.cpp
)
target_include_directories(
  sd_osc_integration
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest ${SD_CORE} ${DIRECTOR_COMMON} ${DIRECTOR_OSC} ${DIRECTOR_INCLUDE}
)
target_link_libraries(sd_osc_integration PRIVATE sd_core director_osc)
add_test(NAME sd_osc_integration COMMAND sd_osc_integration)

# Static contract: a source with OBS_SOURCE_VIDEO must set video_render (see the script).
add_test(
  NAME source_contract
  COMMAND
    ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../src -P ${CMAKE_CURRENT_SOURCE_DIR}/source_contract.cmake
)
