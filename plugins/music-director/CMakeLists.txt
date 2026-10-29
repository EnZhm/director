cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(MD_TEST_HOOKS "Compile the MIDI dock sandbox test hooks; never in releases" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api QUIET)
  if(TARGET OBS::obs-frontend-api)
    set(HAVE_FRONTEND_API TRUE)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  else()
    message(WARNING "obs-frontend-api not found; building without the Music Director docks")
    set(HAVE_FRONTEND_API FALSE)
  endif()
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# The one clock contract and the shared MIDI helper live at the repo root.
set(MD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(MD_MIDI "${MD_ROOT}/common/midi")

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/host/music-clock-host.cpp
    src/obs/md-hotkeys.cpp
    src/director/clock.cpp
    src/director/analysis.cpp
    src/director/beat_tracker.cpp
    src/director/section_state.cpp
    src/director/migration.cpp
    src/dock/dock-widgets.cpp
    src/dock/music-dock.cpp
    src/dock/midi-director-dock.cpp
    ${MD_MIDI}/director-midi.cpp
    ${MD_MIDI}/midi_map.cpp
    ${MD_MIDI}/midi_msg.cpp
)
target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src
    ${MD_ROOT}/include
    ${MD_MIDI}
)

if(HAVE_FRONTEND_API)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
endif()

if(ENABLE_QT)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_QT)
endif()

if(MD_TEST_HOOKS)
  message(WARNING "MD_TEST_HOOKS is ON: sandbox test hooks are compiled in (do not ship this build)")
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MD_TEST_HOOKS)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# --- Offline clock unit tests (pure C++, no libobs/Qt) ---
# Build with: cmake -S . -B build-tests -DMD_BUILD_TESTS=ON
# (Alternatively build the self-contained project directly: cmake -S tests -B build-tests)
option(MD_BUILD_TESTS "Build the offline clock unit tests" OFF)
if(MD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
