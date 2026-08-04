cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT IS_DIRECTORY "${BUILD_DIR}")
    message(FATAL_ERROR "BUILD_DIR does not exist: ${BUILD_DIR}")
endif()

if(WIN32)
    set(_binary_pattern "${BUILD_DIR}/ZahnerWaveEditor.exe")
elseif(APPLE)
    # GLOB_RECURSE only matches files, so look for the executable inside the
    # .app bundle rather than the bundle directory itself.
    set(_binary_pattern "${BUILD_DIR}/ZahnerWaveEditor")
else()
    set(_binary_pattern "${BUILD_DIR}/ZahnerWaveEditor")
endif()

file(GLOB_RECURSE _app_candidates "${_binary_pattern}")
list(LENGTH _app_candidates _app_candidate_count)
if(_app_candidate_count EQUAL 0)
    message(FATAL_ERROR "Build smoke failed: ZahnerWaveEditor artifact not found in ${BUILD_DIR}")
endif()

set(_found_binary "")
foreach(_candidate IN LISTS _app_candidates)
    if(_candidate MATCHES "/CMakeFiles/" OR _candidate MATCHES "/_CPack_Packages/")
        continue()
    endif()
    if(APPLE AND NOT _candidate MATCHES "\\.app/Contents/MacOS/ZahnerWaveEditor$")
        continue()
    endif()
    set(_found_binary "${_candidate}")
    break()
endforeach()

if(_found_binary STREQUAL "")
    message(FATAL_ERROR "Build smoke failed: only internal CMakeFiles matches found")
endif()

message(STATUS "Build smoke checks passed: ${_found_binary}")
