# Source registration contract (static check, no libobs needed).
#
# libobs calls obs_source_info.video_render without a NULL check when a filter on a source
# skips to its parent (obs_source_skip_video_filter -> obs_source_default_render). So every
# source registered with OBS_SOURCE_VIDEO must set a non-NULL video_render, even one that
# draws nothing. Scene Director 2.0.0 shipped without one and crashed OBS at startup for a
# collection carrying a Spout filter on the Clip Director source.
#
#   cmake -DSRC_DIR=<repo>/src -P source_contract.cmake

if(NOT SRC_DIR)
  message(FATAL_ERROR "SRC_DIR not set")
endif()

file(GLOB_RECURSE files "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.c")
set(checked 0)
set(failed 0)
foreach(f IN LISTS files)
  file(READ "${f}" text)
  if(NOT text MATCHES "obs_register_source")
    continue()
  endif()
  # every output_flags assignment that declares OBS_SOURCE_VIDEO ...
  string(REGEX MATCHALL "output_flags[ \t]*=[^;]*;" flag_lines "${text}")
  set(video FALSE)
  foreach(l IN LISTS flag_lines)
    if(l MATCHES "OBS_SOURCE_VIDEO")
      set(video TRUE)
    endif()
  endforeach()
  if(NOT video)
    continue()
  endif()
  math(EXPR checked "${checked} + 1")
  # ... needs a video_render assignment in the same file that is not NULL / nullptr / 0
  string(REGEX MATCHALL "\\.video_render[ \t]*=[ \t]*[A-Za-z_][A-Za-z0-9_:]*" renders "${text}")
  set(ok FALSE)
  foreach(r IN LISTS renders)
    if(NOT r MATCHES "=[ \t]*(NULL|nullptr)$")
      set(ok TRUE)
    endif()
  endforeach()
  if(ok)
    message(STATUS "ok: ${f} declares OBS_SOURCE_VIDEO and sets video_render")
  else()
    message(SEND_ERROR "${f}: registers a source with OBS_SOURCE_VIDEO but sets no video_render")
    math(EXPR failed "${failed} + 1")
  endif()
endforeach()

if(checked EQUAL 0)
  message(FATAL_ERROR "no OBS_SOURCE_VIDEO registration found under ${SRC_DIR}; the check is stale")
endif()
if(failed GREATER 0)
  message(FATAL_ERROR "${failed} source registration(s) violate the video_render contract")
endif()
message(STATUS "source contract: ${checked} file(s) checked, all pass")
