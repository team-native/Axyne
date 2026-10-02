cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED AXYNE_INSTALL_ROOT OR AXYNE_INSTALL_ROOT STREQUAL "")
    message(FATAL_ERROR
        "AXYNE_INSTALL_ROOT must point to the staged Release install tree.")
endif()

if(NOT DEFINED AXYNE_PLATFORM OR AXYNE_PLATFORM STREQUAL "")
    message(FATAL_ERROR
        "AXYNE_PLATFORM must be Windows or macOS.")
endif()

if(NOT DEFINED AXYNE_CONFIGURATION OR AXYNE_CONFIGURATION STREQUAL "")
    set(AXYNE_CONFIGURATION "Release")
endif()

if(NOT AXYNE_CONFIGURATION STREQUAL "Release")
    message(FATAL_ERROR
        "Axyne packaging is Release-only; validation received configuration "
        "'${AXYNE_CONFIGURATION}'.")
endif()

function(require_path path description)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "Missing ${description}: ${path}")
    endif()
endfunction()

function(require_non_empty_file path description)
    require_path("${path}" "${description}")
    file(SIZE "${path}" file_size)
    if(file_size EQUAL 0)
        message(FATAL_ERROR
            "${description} is empty: ${path}")
    endif()
endfunction()

function(require_path_or_symlink path description)
    if(NOT EXISTS "${path}" AND NOT IS_SYMLINK "${path}")
        message(FATAL_ERROR
            "Missing ${description}: ${path}")
    endif()
endfunction()

set(install_root "${AXYNE_INSTALL_ROOT}")

if(AXYNE_PLATFORM STREQUAL "Windows")
    require_path(
        "${install_root}/axyne.exe"
        "the Windows application executable")
    require_path(
        "${install_root}/Scintilla.dll"
        "the Windows Scintilla runtime")
    require_non_empty_file(
        "${install_root}/Scintilla-LICENSE.txt"
        "the Windows Scintilla license")
    set(metadata_root "${install_root}/share/axyne")
elseif(AXYNE_PLATFORM STREQUAL "macOS")
    if(NOT DEFINED AXYNE_APP_BUNDLE OR AXYNE_APP_BUNDLE STREQUAL "")
        set(AXYNE_APP_BUNDLE "Axyne.app")
    endif()
    if(NOT DEFINED AXYNE_APP_EXECUTABLE OR AXYNE_APP_EXECUTABLE STREQUAL "")
        set(AXYNE_APP_EXECUTABLE "Axyne")
    endif()
    set(app_root "${install_root}/${AXYNE_APP_BUNDLE}")
    require_path(
        "${app_root}"
        "the macOS application bundle")
    require_path(
        "${app_root}/Contents/MacOS/${AXYNE_APP_EXECUTABLE}"
        "the macOS application executable")
    require_path_or_symlink(
        "${app_root}/Contents/Frameworks/Scintilla.framework/Versions/A/Scintilla"
        "the macOS Scintilla framework runtime")
    require_non_empty_file(
        "${app_root}/Contents/Resources/Scintilla-LICENSE.txt"
        "the macOS Scintilla license")
    set(metadata_root "${app_root}/Contents/Resources")
else()
    message(FATAL_ERROR
        "Unsupported AXYNE_PLATFORM '${AXYNE_PLATFORM}'; expected Windows or macOS.")
endif()

require_non_empty_file(
    "${metadata_root}/version.json"
    "the offline version metadata")
require_non_empty_file(
    "${metadata_root}/LICENSE"
    "the bundled Axyne license")
require_non_empty_file(
    "${metadata_root}/RELEASE_NOTES.md"
    "the bundled release notes")

file(READ "${metadata_root}/version.json" version_metadata)
foreach(required_metadata_marker
    "\"name\""
    "\"version\""
    "\"release_notes\""
    "\"offline\": true")
    string(FIND "${version_metadata}" "${required_metadata_marker}" marker_index)
    if(marker_index EQUAL -1)
        message(FATAL_ERROR
            "Version metadata is missing '${required_metadata_marker}': "
            "${metadata_root}/version.json")
    endif()
endforeach()

message(STATUS
    "Axyne ${AXYNE_CONFIGURATION} ${AXYNE_PLATFORM} package install "
    "contains the application, metadata, licenses, release notes, and "
    "Scintilla runtime.")
