# ──────────────────────────────────────────────────────────────────────────
# Version.cmake — the version comes from the git tag, nowhere else.
#
# Included before project(). Sets:
#   MODALDISH_VERSION_NUMBER   X.Y.Z, for project() and so for the hosts (VST3,
#                        AU, CLAP and the standalone need a plain number)
#   MODALDISH_VERSION_DISPLAY  what the window shows: "0.1.0" on the tag v0.1.0,
#                        "0.1.0-3-gabc1234" three commits after it,
#                        "0.0.0-dev" before any tag (or without git)
#
# The tag is the latest vX.Y.Z reachable from HEAD (git describe), the same
# pattern that triggers release.yml. -DMODALDISH_VERSION=X.Y.Z overrides it (both
# values then). CMake reconfigures by itself on a commit, a checkout or a new
# tag, so a local build never shows a stale version.
# ──────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)

set(MODALDISH_VERSION "" CACHE STRING "Override the version (X.Y.Z); empty: from the git tag")

if(MODALDISH_VERSION)
    if(NOT MODALDISH_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "MODALDISH_VERSION must be X.Y.Z, got '${MODALDISH_VERSION}'")
    endif()
    set(_md_version "${MODALDISH_VERSION}")
    set(_md_display "${MODALDISH_VERSION}")
else()
    set(_md_version "0.0.0")
    set(_md_display "0.0.0-dev")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*.[0-9]*.[0-9]*"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _md_describe
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _md_git_result)
        # v0.1.0, or v0.1.0-3-gabc1234 after it.
        if(_md_git_result EQUAL 0 AND _md_describe MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)(-.*)?$")
            set(_md_version "${CMAKE_MATCH_1}")
            string(SUBSTRING "${_md_describe}" 1 -1 _md_display)
        endif()

        # Reconfigure when HEAD moves (a commit, a checkout) or a tag appears.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _md_git_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _md_git_dir_result)
        if(_md_git_dir_result EQUAL 0)
            set(_md_watch "${_md_git_dir}/HEAD" "${_md_git_dir}/packed-refs" "${_md_git_dir}/refs/tags")
            file(READ "${_md_git_dir}/HEAD" _md_head)
            if(_md_head MATCHES "^ref: ([^\n]+)")
                list(APPEND _md_watch "${_md_git_dir}/${CMAKE_MATCH_1}")
            endif()
            foreach(_f IN LISTS _md_watch)
                if(EXISTS "${_f}")
                    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}")
                endif()
            endforeach()
        endif()
    endif()
endif()

set(MODALDISH_VERSION_NUMBER "${_md_version}")
set(MODALDISH_VERSION_DISPLAY "${_md_display}")
message(STATUS "ModalDish version ${MODALDISH_VERSION_DISPLAY} (hosts see ${MODALDISH_VERSION_NUMBER})")
