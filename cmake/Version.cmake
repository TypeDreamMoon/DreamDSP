# The version the binary reports, taken from git rather than typed twice.
#
# A number written in CMakeLists and a tag written in git are two places to
# state the same fact, and they drift the first time someone tags without
# editing. Here the tag is the fact: `v0.2.0` produces 0.2.0, and a build from
# an untagged tree says so out loud rather than claiming to be a release.
#
# Sets, in the parent scope:
#   DREAMDSP_VERSION        0.2.0        -- three numbers, for the installer
#   DREAMDSP_VERSION_FULL   0.2.0+7.ab12cd3-dirty  -- what the About box shows
#   DREAMDSP_IS_RELEASE     TRUE when built from exactly a tag, with no edits

function(dreamdsp_version_from_git fallback)
    set(version "${fallback}")
    set(full "${fallback}+unknown")
    set(isRelease FALSE)

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        set(DREAMDSP_VERSION "${version}" PARENT_SCOPE)
        set(DREAMDSP_VERSION_FULL "${full}" PARENT_SCOPE)
        set(DREAMDSP_IS_RELEASE ${isRelease} PARENT_SCOPE)
        return()
    endif()

    # --dirty marks a working tree with uncommitted changes, which is exactly
    # the build you do not want mistaken for a release.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --long --dirty --match "v[0-9]*"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE described
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE describeResult)

    if(describeResult EQUAL 0 AND described MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
        set(version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
        set(commitsSince "${CMAKE_MATCH_4}")
        set(hash "${CMAKE_MATCH_5}")
        set(dirty "${CMAKE_MATCH_6}")

        if(commitsSince STREQUAL "0" AND dirty STREQUAL "")
            set(full "${version}")
            set(isRelease TRUE)
        else()
            set(full "${version}+${commitsSince}.${hash}${dirty}")
        endif()
    else()
        # No tag yet. Say which commit it is rather than inventing a version.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(hash)
            set(full "${fallback}+dev.${hash}")
        endif()
    endif()

    set(DREAMDSP_VERSION "${version}" PARENT_SCOPE)
    set(DREAMDSP_VERSION_FULL "${full}" PARENT_SCOPE)
    set(DREAMDSP_IS_RELEASE ${isRelease} PARENT_SCOPE)
endfunction()
