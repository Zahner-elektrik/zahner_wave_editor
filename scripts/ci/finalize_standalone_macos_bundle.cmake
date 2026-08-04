cmake_minimum_required(VERSION 3.20)

set(_zwe_bundle_candidates)
foreach(_root IN ITEMS
        "${CMAKE_INSTALL_PREFIX}"
        "${CPACK_TEMPORARY_DIRECTORY}"
        "${CPACK_TOPLEVEL_DIRECTORY}")
    if(NOT _root)
        continue()
    endif()
    if(EXISTS "${_root}/ZahnerWaveEditor.app")
        list(APPEND _zwe_bundle_candidates "${_root}/ZahnerWaveEditor.app")
    endif()
    file(GLOB _root_apps LIST_DIRECTORIES true
        "${_root}/packages/*/data/ZahnerWaveEditor.app"
        "${_root}/*/packages/*/data/ZahnerWaveEditor.app"
        "${_root}/*/*/packages/*/data/ZahnerWaveEditor.app"
    )
    list(APPEND _zwe_bundle_candidates ${_root_apps})
endforeach()

if(NOT _zwe_bundle_candidates)
    message(FATAL_ERROR "Could not find staged ZahnerWaveEditor.app to finalize")
endif()

list(REMOVE_DUPLICATES _zwe_bundle_candidates)
foreach(_zwe_app IN LISTS _zwe_bundle_candidates)
    if(NOT EXISTS "${_zwe_app}/Contents/MacOS/ZahnerWaveEditor")
        continue()
    endif()

    execute_process(COMMAND /usr/bin/xattr -cr "${_zwe_app}")

    file(GLOB_RECURSE _zwe_dynamic_libraries
        "${_zwe_app}/Contents/Frameworks/*.dylib"
        "${_zwe_app}/Contents/PlugIns/*.dylib"
        "${_zwe_app}/Contents/Frameworks/*.framework/Versions/*/*"
    )
    foreach(_zwe_library IN LISTS _zwe_dynamic_libraries)
        if(IS_DIRECTORY "${_zwe_library}" OR IS_SYMLINK "${_zwe_library}")
            continue()
        endif()
        execute_process(
            COMMAND /usr/bin/file "${_zwe_library}"
            OUTPUT_VARIABLE _zwe_file_type
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _zwe_file_type MATCHES "Mach-O")
            continue()
        endif()
        execute_process(
            COMMAND /usr/bin/codesign --force --sign - "${_zwe_library}"
            RESULT_VARIABLE _zwe_codesign_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT _zwe_codesign_result EQUAL 0)
            message(FATAL_ERROR "Failed to codesign ${_zwe_library}")
        endif()
    endforeach()

    execute_process(
        COMMAND /usr/bin/codesign --force --sign - "${_zwe_app}"
        RESULT_VARIABLE _zwe_codesign_result
    )
    if(NOT _zwe_codesign_result EQUAL 0)
        message(FATAL_ERROR "Failed to codesign ${_zwe_app}")
    endif()
endforeach()
