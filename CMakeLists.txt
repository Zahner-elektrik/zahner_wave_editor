cmake_minimum_required(VERSION 3.20)

# The version is the newest v-prefixed git tag of the repository, as in Zahner
# Lab. This has to run before project(), which takes the number.
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/version.cmake")
zwe_get_git_version()
message(STATUS "Zahner Wave Editor version: ${ZWE_VERSION} (build ${ZWE_VERSION_FULL})")

project(ZahnerWaveEditor LANGUAGES CXX VERSION ${ZWE_VERSION})

# Read by src/core/version.cpp and by the Windows resource script, so both report
# the same numbers without either of them containing a hard coded version.
set(ZWE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/src/core/version.h.in"
    "${ZWE_GENERATED_INCLUDE_DIR}/zwe_version.h"
    @ONLY
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Widgets LinguistTools)

# Newer macOS SDKs (Xcode 26) no longer ship the legacy AGL framework, but
# Qt 6.8's FindWrapOpenGL still adds "-framework AGL" to its link interface,
# which fails with "ld: framework 'AGL' not found". AGL was only needed for
# long-gone 32-bit OpenGL contexts; strip it from the imported targets.
if(APPLE)
    foreach(_zwe_agl_target WrapOpenGL::WrapOpenGL Qt6::Gui Qt6::OpenGL)
        if(TARGET ${_zwe_agl_target})
            get_target_property(_zwe_agl_libs ${_zwe_agl_target} INTERFACE_LINK_LIBRARIES)
            if(_zwe_agl_libs)
                list(FILTER _zwe_agl_libs EXCLUDE REGEX "AGL")
                set_target_properties(${_zwe_agl_target} PROPERTIES
                    INTERFACE_LINK_LIBRARIES "${_zwe_agl_libs}")
            endif()
        endif()
    endforeach()
endif()

# PROJECT_IS_TOP_LEVEL needs CMake >= 3.21; compute it for 3.20 compatibility.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ZWE_STANDALONE ON)
else()
    set(ZWE_STANDALONE OFF)
endif()

# Installer artifacts and the IFW package carry the full build identity, not just
# the release number: two installers built from the same tag stay distinguishable
# and the IFW update check sees a new version for every build.
set(ZWE_PACKAGE_VERSION "${ZWE_VERSION_FULL}")

# When built as part of the zahner_lab superproject the editor is packaged as
# the optional installer component "ZahnerLab.WaveEditor". Standalone builds
# install without a component.
if(ZWE_STANDALONE)
    set(ZWE_INSTALL_COMPONENT "ZahnerWaveEditor.Application")
else()
    set(ZWE_INSTALL_COMPONENT "ZahnerLab.WaveEditor")
endif()
set(ZWE_INSTALL_COMPONENT_ARGS COMPONENT ${ZWE_INSTALL_COMPONENT})

option(ZWE_BUILD_TESTS "Build Zahner Wave Editor unit tests" ON)
option(ZWE_BUILD_INSTALLER "Configure Qt Installer Framework packaging for standalone builds" ON)

add_subdirectory(third_party/muparser)
add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/app)

file(GLOB ZWE_TRANSLATION_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp
    src/ui/*.cpp
    src/ui/*.h
)

# The languages the editor ships, English (the source language of every tr()
# string) aside. Keep this in sync with ShippedLanguages in src/ui/appsettings.cpp.
set(ZWE_TRANSLATION_LANGUAGES de)

list(TRANSFORM ZWE_TRANSLATION_LANGUAGES
    REPLACE "^(.+)$" "translations/zwe_\\1.ts"
    OUTPUT_VARIABLE ZWE_TS_FILES
)
qt_add_translations(ZahnerWaveEditor
    TS_FILES ${ZWE_TS_FILES}
    SOURCES ${ZWE_TRANSLATION_SOURCES}
    RESOURCE_PREFIX /i18n
)

# Qt translates its own strings from its own catalogs: the standard button
# labels of QMessageBox and QDialogButtonBox, the file dialog, the context menu
# of a text field. None of them pass through tr() in this code base, so without
# these catalogs a translated dialog ends up with English buttons. They are
# embedded next to our own instead of being read from the installation at run
# time, which keeps them independent of what each platform's deploy step copies.
set(ZWE_QT_TRANSLATIONS_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
set(ZWE_QT_TRANSLATION_CATALOGS "")
foreach(zwe_language IN LISTS ZWE_TRANSLATION_LANGUAGES)
    set(zwe_qt_catalog "${ZWE_QT_TRANSLATIONS_DIR}/qtbase_${zwe_language}.qm")
    if(EXISTS "${zwe_qt_catalog}")
        list(APPEND ZWE_QT_TRANSLATION_CATALOGS "${zwe_qt_catalog}")
    else()
        message(WARNING
            "Qt catalog qtbase_${zwe_language}.qm was not found in "
            "'${ZWE_QT_TRANSLATIONS_DIR}'. Qt's own strings stay English in that language.")
    endif()
endforeach()
if(ZWE_QT_TRANSLATION_CATALOGS)
    qt_add_resources(ZahnerWaveEditor "zwe_qt_translations"
        PREFIX /i18n
        BASE "${ZWE_QT_TRANSLATIONS_DIR}"
        FILES ${ZWE_QT_TRANSLATION_CATALOGS}
    )
endif()

if(ZWE_BUILD_TESTS)
    if(ZWE_STANDALONE)
        include(CTest)
    endif()
    add_subdirectory(tests)
endif()

if(ZWE_STANDALONE AND ZWE_BUILD_INSTALLER)
    if(DEFINED ENV{QTIFW_ROOT} AND NOT CPACK_IFW_ROOT)
        set(CPACK_IFW_ROOT "$ENV{QTIFW_ROOT}" CACHE PATH "Qt Installer Framework root")
    elseif(DEFINED ENV{QTIFWDIR} AND NOT CPACK_IFW_ROOT)
        get_filename_component(_zwe_qtifw_root "$ENV{QTIFWDIR}/.." ABSOLUTE)
        set(CPACK_IFW_ROOT "${_zwe_qtifw_root}" CACHE PATH "Qt Installer Framework root")
    elseif(DEFINED ENV{HOME} AND NOT CPACK_IFW_ROOT)
        file(GLOB _zwe_qtifw_home_roots LIST_DIRECTORIES true
            "$ENV{HOME}/Qt/Tools/QtInstallerFramework/*"
            "$ENV{HOME}/Qt/QtIFW-*"
        )
        if(_zwe_qtifw_home_roots)
            list(SORT _zwe_qtifw_home_roots COMPARE NATURAL ORDER DESCENDING)
            list(GET _zwe_qtifw_home_roots 0 _zwe_qtifw_home_root)
            set(CPACK_IFW_ROOT "${_zwe_qtifw_home_root}" CACHE PATH "Qt Installer Framework root")
        endif()
    endif()

    if(CPACK_IFW_ROOT)
        find_program(ZWE_BINARYCREATOR_EXECUTABLE
            NAMES binarycreator binarycreator.exe
            HINTS "${CPACK_IFW_ROOT}/bin" "${CPACK_IFW_ROOT}"
            NO_DEFAULT_PATH
        )
    endif()
    find_program(ZWE_BINARYCREATOR_EXECUTABLE
        NAMES binarycreator binarycreator.exe
        HINTS ENV PATH
    )

    if(ZWE_BINARYCREATOR_EXECUTABLE)
        message(STATUS "Configuring standalone Qt IFW installer using ${ZWE_BINARYCREATOR_EXECUTABLE}")

        set(CPACK_GENERATOR "IFW")
        set(CPACK_COMPONENTS_GROUPING "IGNORE")
        set(CPACK_PACKAGE_NAME "ZahnerWaveEditor")
        set(CPACK_PACKAGE_VENDOR "Zahner-Elektrik")
        set(CPACK_PACKAGE_CONTACT "Zahner-Elektrik")
        set(CPACK_PACKAGE_VERSION "${ZWE_PACKAGE_VERSION}")
        set(CPACK_PACKAGE_FILE_NAME "ZahnerWaveEditor-${ZWE_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "Zahner/ZahnerWaveEditor")

        set(CPACK_IFW_PACKAGE_WIZARD_STYLE Modern)
        set(CPACK_IFW_PACKAGE_NAME "ZahnerWaveEditor")
        set(CPACK_IFW_PACKAGE_VERSION "${ZWE_PACKAGE_VERSION}")
        set(CPACK_IFW_PACKAGE_TITLE "Zahner Wave Editor ${ZWE_PACKAGE_VERSION}")
        set(CPACK_IFW_PACKAGE_PUBLISHER "Zahner-Elektrik")
        set(CPACK_IFW_ARCHIVE_COMPRESSION 1)
        set(CPACK_IFW_PACKAGE_CONTROL_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ifw/scripts/controlscript.qs")
        if(APPLE)
            set(CPACK_IFW_TARGET_DIRECTORY "@RootDir@/Library/Application Support/ZahnerWaveEditor")
            set(CPACK_IFW_ADMIN_TARGET_DIRECTORY "@RootDir@/Library/Application Support/ZahnerWaveEditor")
            list(APPEND CPACK_PRE_BUILD_SCRIPTS
                "${CMAKE_CURRENT_LIST_DIR}/scripts/ci/finalize_standalone_macos_bundle.cmake"
            )
        endif()

        string(TIMESTAMP TODAY "%Y-%m-%d")
        set(GIT_TAG "${ZWE_PACKAGE_VERSION}")
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/ifw/packages/ZahnerWaveEditor/meta/package.xml.in"
            "${CMAKE_CURRENT_BINARY_DIR}/ifw/packages/ZahnerWaveEditor/meta/package.xml"
            @ONLY
        )

        set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT "${ZWE_INSTALL_COMPONENT}")
        include(InstallRequiredSystemLibraries)
        include(CPackIFW)
        include(CPack)

        cpack_add_component("${ZWE_INSTALL_COMPONENT}"
            DISPLAY_NAME "Zahner Wave Editor"
            DESCRIPTION "Standalone waveform editor for creating custom wave signal files."
            REQUIRED
        )
        cpack_ifw_configure_component("${ZWE_INSTALL_COMPONENT}"
            FORCED_INSTALLATION
            NAME "de.zahner.waveeditor.standalone"
            VERSION "${ZWE_PACKAGE_VERSION}"
            SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ifw/scripts/waveeditor.qs"
            DEFAULT TRUE
            LICENSES
                "End-User License Agreement (EULA)" "${CMAKE_CURRENT_LIST_DIR}/ifw/licenses/eula"
                "Qt LGPL 3.0" "${CMAKE_CURRENT_LIST_DIR}/resources/licenses/qt/LGPL-3.0.txt"
                "muParser License" "${CMAKE_CURRENT_LIST_DIR}/third_party/muparser/LICENSE"
        )
    else()
        message(WARNING
            "Qt Installer Framework binarycreator was not found. "
            "Standalone installer target is disabled. Set CPACK_IFW_ROOT, QTIFW_ROOT, or PATH.")
    endif()
endif()
