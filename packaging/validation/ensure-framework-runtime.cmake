cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED AXYNE_SCINTILLA_FRAMEWORK OR AXYNE_SCINTILLA_FRAMEWORK STREQUAL "")
    message(FATAL_ERROR
        "AXYNE_SCINTILLA_FRAMEWORK must point to the built Scintilla.framework.")
endif()

set(framework_root "${AXYNE_SCINTILLA_FRAMEWORK}")
set(runtime_path "${framework_root}/Versions/A/Scintilla")

if(EXISTS "${runtime_path}" AND NOT IS_SYMLINK "${runtime_path}")
    return()
endif()

file(GLOB_RECURSE runtime_candidates
    LIST_DIRECTORIES false
    "${framework_root}/Scintilla"
    "${framework_root}/*/Scintilla"
)
foreach(candidate IN LISTS runtime_candidates)
    if(EXISTS "${candidate}" AND NOT IS_SYMLINK "${candidate}")
        file(MAKE_DIRECTORY "${framework_root}/Versions/A")
        file(COPY_FILE "${candidate}" "${runtime_path}" ONLY_IF_DIFFERENT)
        message(STATUS "Materialized Scintilla framework runtime from ${candidate}")
        return()
    endif()
endforeach()

message(FATAL_ERROR
    "Could not find a concrete Scintilla runtime in ${framework_root}.")
