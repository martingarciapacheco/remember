# One product version for the app, VST3, installer and release scripts.
get_filename_component(REMEMBER_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(STRINGS "${REMEMBER_SOURCE_ROOT}/VERSION" product_version LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REMEMBER_SOURCE_ROOT}/VERSION")
if(NOT product_version MATCHES "^([0-9]+)\.([0-9]+)\.([0-9]+)(-(alpha|beta|rc)([0-9]+))?$")
    message(FATAL_ERROR "VERSION must contain major.minor.patch[-alphaN|-betaN|-rcN]")
endif()
set(REMEMBER_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(version_revision "${CMAKE_MATCH_6}")
if(version_revision STREQUAL "")
    set(version_revision 0)
endif()
set(REMEMBER_WINDOWS_VERSION "${REMEMBER_NUMERIC_VERSION}.${version_revision}")
if(DEFINED REMEMBER_VERSION AND NOT REMEMBER_VERSION STREQUAL product_version)
    message(FATAL_ERROR "REMEMBER_VERSION differs from VERSION. Clear the old cache override.")
endif()
set(REMEMBER_VERSION "${product_version}" CACHE STRING "Product version from VERSION" FORCE)

# Resolve Git before invoking it. A VS build may deliberately remove PATH.
find_package(Git QUIET)
if(NOT GIT_EXECUTABLE)
    find_program(GIT_EXECUTABLE NAMES git git.exe
        HINTS "$ENV{ProgramFiles}/Git/cmd" "C:/Program Files/Git/cmd")
endif()
set(REMEMBER_GIT_COMMIT "unknown")
set(REMEMBER_GIT_HASH "unknown")
set(REMEMBER_SOURCE_STATE "unknown")
if(GIT_EXECUTABLE)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${REMEMBER_SOURCE_ROOT}"
        OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result ERROR_QUIET)
    if(git_result EQUAL 0 AND commit MATCHES "^[0-9a-f]+$")
        set(REMEMBER_GIT_COMMIT "${commit}")
        string(SUBSTRING "${commit}" 0 7 REMEMBER_GIT_HASH)
        execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            WORKING_DIRECTORY "${REMEMBER_SOURCE_ROOT}"
            OUTPUT_VARIABLE status RESULT_VARIABLE status_result ERROR_QUIET)
        if(status_result EQUAL 0)
            if(status STREQUAL "")
                set(REMEMBER_SOURCE_STATE "clean")
            else()
                set(REMEMBER_SOURCE_STATE "dirty")
                string(APPEND REMEMBER_GIT_HASH "+dirty")
            endif()
        endif()
    endif()
endif()
set(REMEMBER_BUILD_ID "local" CACHE STRING "Unique release pipeline build ID")
set(REMEMBER_SOURCE_FINGERPRINT "unknown" CACHE STRING "Pipeline SHA256 of source files")
if(NOT REMEMBER_BUILD_ID MATCHES "^[A-Za-z0-9_.-]+$"
   OR NOT REMEMBER_SOURCE_FINGERPRINT MATCHES "^(unknown|[0-9a-f]+)$")
    message(FATAL_ERROR "Invalid build ID or source fingerprint")
endif()
string(TIMESTAMP REMEMBER_BUILD_DATE "%Y-%m-%d" UTC)

function(remember_generate_build_info)
    configure_file("${REMEMBER_SOURCE_ROOT}/Shared/BuildInfo.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h" @ONLY)
    configure_file("${REMEMBER_SOURCE_ROOT}/Shared/BuildInfo.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/build-info.json" @ONLY)
endfunction()
