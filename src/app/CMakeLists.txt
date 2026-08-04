qt_add_executable(ZahnerWaveEditor
    main.cpp
)

if(WIN32)
    target_sources(ZahnerWaveEditor PRIVATE ../../resources/windows/waveeditor.rc)
    # The resource script reads the version numbers from the generated header, so
    # the file properties Explorer shows match the git tag.
    target_include_directories(ZahnerWaveEditor PRIVATE ${ZWE_GENERATED_INCLUDE_DIR})
endif()

set_target_properties(ZahnerWaveEditor PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    # Info.plist takes numeric versions only, so both keys get the release number
    # rather than the full build identity.
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${ZWE_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${ZWE_VERSION}"
)

target_link_libraries(ZahnerWaveEditor PRIVATE waveui)

install(TARGETS ZahnerWaveEditor
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT ${ZWE_INSTALL_COMPONENT}
    BUNDLE DESTINATION .
        COMPONENT ${ZWE_INSTALL_COMPONENT}
)

# Launcher wrapper (Linux/macOS): sets the library path relative to the install
# root, mirroring the zahnerlab wrapper of the main application.
if(UNIX)
    install(PROGRAMS ${CMAKE_CURRENT_LIST_DIR}/zahnerwaveeditor
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        ${ZWE_INSTALL_COMPONENT_ARGS}
    )
endif()

# Application icon for desktop entries / shortcuts. Keep this installed path
# stable because the superproject desktop integration references it.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../resources/icons/waveeditor.png
    DESTINATION resources
    ${ZWE_INSTALL_COMPONENT_ARGS}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../resources/help/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/zahner-wave-editor/help
    ${ZWE_INSTALL_COMPONENT_ARGS}
)

# Standalone installers must deploy the Qt runtime. When the editor is built as
# an optional Zahner Lab component, the superproject owns non-macOS runtime
# deployment; macOS still needs a self-contained app bundle.
if(APPLE OR (ZWE_STANDALONE AND WIN32))
    if(Qt6Core_VERSION VERSION_LESS 6.5)
        qt_generate_deploy_app_script(
            TARGET ZahnerWaveEditor
            FILENAME_VARIABLE zwe_deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    else()
        qt_generate_deploy_app_script(
            TARGET ZahnerWaveEditor
            OUTPUT_SCRIPT zwe_deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    endif()
    install(SCRIPT ${zwe_deploy_script} ${ZWE_INSTALL_COMPONENT_ARGS})
elseif(ZWE_STANDALONE AND UNIX)
    get_target_property(_zwe_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_zwe_qt_bin_dir "${_zwe_qt_qmake_executable}" DIRECTORY)
    get_filename_component(_zwe_qt_prefix "${_zwe_qt_bin_dir}" DIRECTORY)
    get_target_property(_zwe_qt_core_library Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _zwe_qt_core_library)
        get_target_property(_zwe_qt_core_library Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(_zwe_qt_library_dir "${_zwe_qt_core_library}" DIRECTORY)

    set(_zwe_qt_runtime_libraries)
    foreach(_zwe_qt_library_name IN ITEMS
            Core
            DBus
            Gui
            Network
            OpenGL
            Svg
            WaylandClient
            Widgets
            XcbQpa)
        file(GLOB _zwe_qt_library_matches
            "${_zwe_qt_library_dir}/libQt6${_zwe_qt_library_name}.so*"
        )
        list(APPEND _zwe_qt_runtime_libraries ${_zwe_qt_library_matches})
    endforeach()
    if(_zwe_qt_runtime_libraries)
        list(REMOVE_DUPLICATES _zwe_qt_runtime_libraries)
    endif()
    install(FILES ${_zwe_qt_runtime_libraries}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ${ZWE_INSTALL_COMPONENT_ARGS}
    )

    find_program(_zwe_qtpaths_executable
        NAMES qtpaths6 qtpaths
        HINTS "${_zwe_qt_bin_dir}" "${_zwe_qt_prefix}/bin"
    )
    if(_zwe_qtpaths_executable)
        execute_process(
            COMMAND "${_zwe_qtpaths_executable}" --plugin-dir
            OUTPUT_VARIABLE _zwe_qt_plugins_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    if(NOT _zwe_qt_plugins_dir)
        set(_zwe_qt_plugins_dir "${_zwe_qt_prefix}/plugins")
    endif()
    if(NOT EXISTS "${_zwe_qt_plugins_dir}" AND CMAKE_LIBRARY_ARCHITECTURE)
        set(_zwe_qt_plugins_dir "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt6/plugins")
    endif()

    foreach(_zwe_qt_platform_plugin IN ITEMS libqxcb.so libqminimal.so libqoffscreen.so)
        if(EXISTS "${_zwe_qt_plugins_dir}/platforms/${_zwe_qt_platform_plugin}")
            install(FILES "${_zwe_qt_plugins_dir}/platforms/${_zwe_qt_platform_plugin}"
                DESTINATION plugins/platforms
                ${ZWE_INSTALL_COMPONENT_ARGS}
            )
        endif()
    endforeach()
    foreach(_zwe_qt_plugin_subdir IN ITEMS
            iconengines
            imageformats
            networkinformation
            platforminputcontexts
            platformthemes
            styles
            tls
            wayland-decoration-client
            wayland-graphics-integration-client
            wayland-shell-integration
            xcbglintegrations)
        if(EXISTS "${_zwe_qt_plugins_dir}/${_zwe_qt_plugin_subdir}")
            install(DIRECTORY "${_zwe_qt_plugins_dir}/${_zwe_qt_plugin_subdir}/"
                DESTINATION plugins/${_zwe_qt_plugin_subdir}
                ${ZWE_INSTALL_COMPONENT_ARGS}
                PATTERN "*.debug" EXCLUDE
            )
        endif()
    endforeach()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/cmake/deploy_linux_qt_runtime.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/deploy_linux_qt_runtime.cmake"
        @ONLY
    )
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_linux_qt_runtime.cmake"
        ${ZWE_INSTALL_COMPONENT_ARGS}
    )

    install(CODE
        "file(WRITE \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/qt.conf\" \"[Paths]\\nPrefix = ..\\nPlugins = plugins\\n\")"
        ${ZWE_INSTALL_COMPONENT_ARGS}
    )
endif()
