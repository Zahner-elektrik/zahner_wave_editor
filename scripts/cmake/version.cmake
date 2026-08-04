# SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
# SPDX-License-Identifier: GPL-3.0-only

# The release number is the highest v-prefixed tag in the repository
#
#     git tag --list "v*" --sort=-v:refname


# Sets in the caller's scope:
#
#   ZWE_VERSION          strictly numeric, e.g. "26.32.0" - for project() and for
#                        everything that rejects anything else (Info.plist,
#                        Windows VERSIONINFO)
#   ZWE_VERSION_FULL     build identity, e.g. "v26.32.0", "v26.32.0-13-gabc1234",
#                        "v26.32.0-13-gabc1234-dirty" or the bare commit hash
#                        without any tag - unique per build, used for the
#                        installer artifacts and the window title
#   ZWE_VERSION_MAJOR/_MINOR/_PATCH/_TWEAK
#                        the components of ZWE_VERSION, missing ones as 0
function(zwe_get_git_version)
    set(_tag "")
    set(_version "0.0.0")
    set(_version_full "0.0.0-unknown")

    # --absolute-git-dir also answers "is git usable and is this a repository at
    # all", so a tarball build or a machine without git takes the fallback below
    # instead of erroring out.
    execute_process(
        COMMAND git rev-parse --absolute-git-dir
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_result
        ERROR_QUIET
    )

    if(_git_result EQUAL 0)
        # Re-run the configure step when the checkout moves or a tag arrives, so
        # an incremental build after "git fetch --tags" or a commit does not keep
        # reporting the version of the previous configure run.
        foreach(_watched_ref HEAD packed-refs)
            if(EXISTS "${_git_dir}/${_watched_ref}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                    "${_git_dir}/${_watched_ref}")
            endif()
        endforeach()

        execute_process(
            COMMAND git tag --list v* --sort=-v:refname
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _tags
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(REGEX REPLACE "\r" "" _tags "${_tags}")
        string(REGEX REPLACE "\n" ";" _tags "${_tags}")
        foreach(_candidate IN LISTS _tags)
            string(STRIP "${_candidate}" _candidate)
            if(_candidate)
                set(_tag "${_candidate}")
                break()
            endif()
        endforeach()

        execute_process(
            COMMAND git rev-parse --short=7 HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _revision
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND git rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _branch
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        # "HEAD" against the index and the worktree, so staged changes count as
        # dirty as well.
        execute_process(
            COMMAND git diff --quiet HEAD --
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _dirty_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
    endif()

    if(_tag)
        string(REGEX REPLACE "^[vV]" "" _version "${_tag}")
        string(REGEX MATCH "^[0-9]+\\.[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?" _version "${_version}")
        if(NOT _version)
            message(WARNING
                "Git tag '${_tag}' does not start with a numeric version, "
                "falling back to 0.0.0.")
            set(_version "0.0.0")
        endif()

        # Commits between the tag and the built commit. Also non-zero when the
        # tag lives on a different branch, which is exactly the case the version
        # string should stay distinguishable for.
        execute_process(
            COMMAND git rev-list --count "${_tag}..HEAD"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _commits_ahead
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT _commits_ahead MATCHES "^[0-9]+$")
            set(_commits_ahead 0)
        endif()

        set(_version_full "${_tag}")
        if(_commits_ahead GREATER 0)
            set(_version_full "${_version_full}-${_commits_ahead}-g${_revision}")
        endif()
        if(NOT _dirty_result EQUAL 0)
            set(_version_full "${_version_full}-dirty")
        endif()
    elseif(_git_result EQUAL 0)
        # A repository without release tags - a fresh clone with tags disabled, or
        # a branch pushed before the first tag exists: identify the build by its
        # commit hash and leave the numeric version at the 0.0.0 placeholder. The
        # installer is then called ZahnerWaveEditor-abc1234-<platform>, which
        # nobody can mistake for a release.
        set(_version_full "${_revision}")
        if(NOT _dirty_result EQUAL 0)
            set(_version_full "${_version_full}-dirty")
        endif()
    endif()

    if(NOT _tag)
        message(WARNING "No v-prefixed git tag found in '${CMAKE_CURRENT_SOURCE_DIR}'. The version \
falls back to ${_version} (build ${_version_full}). Run 'git fetch --all --tags --force' if this \
is a CI workspace.")
    endif()

    # VERSIONINFO wants a four-part number and Info.plist a three-part one, so
    # split the components here and default the missing ones to 0.
    set(_major 0)
    set(_minor 0)
    set(_patch 0)
    set(_tweak 0)
    string(REPLACE "." ";" _version_parts "${_version}")
    list(LENGTH _version_parts _version_part_count)
    if(_version_part_count GREATER 0)
        list(GET _version_parts 0 _major)
    endif()
    if(_version_part_count GREATER 1)
        list(GET _version_parts 1 _minor)
    endif()
    if(_version_part_count GREATER 2)
        list(GET _version_parts 2 _patch)
    endif()
    if(_version_part_count GREATER 3)
        list(GET _version_parts 3 _tweak)
    endif()

    set(ZWE_VERSION "${_version}" PARENT_SCOPE)
    set(ZWE_VERSION_FULL "${_version_full}" PARENT_SCOPE)
    set(ZWE_VERSION_MAJOR "${_major}" PARENT_SCOPE)
    set(ZWE_VERSION_MINOR "${_minor}" PARENT_SCOPE)
    set(ZWE_VERSION_PATCH "${_patch}" PARENT_SCOPE)
    set(ZWE_VERSION_TWEAK "${_tweak}" PARENT_SCOPE)
endfunction()
