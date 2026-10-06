cmake_minimum_required(VERSION 3.21)

set(AXYNE_DISPLAY_NAME "Axyne" CACHE STRING "Application display name")
set(AXYNE_AUTHOR "Native" CACHE STRING "Application author or organization")
set(AXYNE_VERSION "0.1.0" CACHE STRING "Application version")
project(Axyne VERSION ${AXYNE_VERSION} LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(APPLE)
    # Keep the native package reproducible while allowing an explicit override
    # for a host-specific release build.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING
            "macOS package architecture" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
            "Minimum macOS deployment target" FORCE)
    endif()
endif()

# Scintilla is part of the editor's required runtime, not an optional SDK.
# Pin the upstream release to its immutable commit so configure-time fetches
# are repeatable and do not depend on a user-provided binary or environment.
include(FetchContent)
FetchContent_Declare(axyne_scintilla
    GIT_REPOSITORY https://github.com/mirror/scintilla.git
    GIT_TAG        a1c86144eed9e3d2187e3a8b391d11ca909f00d2
    GIT_PROGRESS  TRUE
)
FetchContent_GetProperties(axyne_scintilla)
if(NOT axyne_scintilla_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(axyne_scintilla)
    cmake_policy(POP)
endif()

# Scintilla 5 keeps language lexers in the companion Lexilla library. Build
# only the platform-native shared library so the editor can load lexers on
# demand without moving any C++ editor dependency into the shared C core.
if(WIN32 OR APPLE)
    FetchContent_Declare(axyne_lexilla
        GIT_REPOSITORY https://github.com/ScintillaOrg/lexilla.git
        GIT_TAG        3e6f317eed854bc312b4c4453206189d381c7ae7
        GIT_PROGRESS   TRUE
    )
    FetchContent_GetProperties(axyne_lexilla)
    if(NOT axyne_lexilla_POPULATED)
        cmake_policy(PUSH)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(axyne_lexilla)
        cmake_policy(POP)
    endif()
    enable_language(CXX)
    file(GLOB AXYNE_LEXILLA_SOURCES CONFIGURE_DEPENDS
        "${axyne_lexilla_SOURCE_DIR}/src/*.cxx"
        "${axyne_lexilla_SOURCE_DIR}/lexlib/*.cxx"
        "${axyne_lexilla_SOURCE_DIR}/lexers/*.cxx"
    )
    add_library(axyne_lexilla SHARED ${AXYNE_LEXILLA_SOURCES})
    set_target_properties(axyne_lexilla PROPERTIES
        OUTPUT_NAME "Lexilla"
        PREFIX ""
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_include_directories(axyne_lexilla SYSTEM PRIVATE
        "${axyne_lexilla_SOURCE_DIR}/include"
        "${axyne_lexilla_SOURCE_DIR}/lexlib"
        "${axyne_scintilla_SOURCE_DIR}/include"
    )
    if(MSVC)
        target_compile_options(axyne_lexilla PRIVATE /utf-8)
        target_link_options(axyne_lexilla PRIVATE
            "/DEF:${axyne_lexilla_SOURCE_DIR}/src/Lexilla.def")
    else()
        target_compile_options(axyne_lexilla PRIVATE -fvisibility=hidden)
    endif()
endif()

add_library(axyne_core STATIC
    src/core/app.c
)

set(AXYNE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${AXYNE_GENERATED_INCLUDE_DIR}/axyne")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/axyne/build_config.h.in"
    "${AXYNE_GENERATED_INCLUDE_DIR}/axyne/build_config.h"
    @ONLY
)

target_include_directories(axyne_core
    PUBLIC
        ${AXYNE_GENERATED_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(axyne_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(axyne_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    if(CMAKE_VS_MSBUILD_COMMAND)
        set(AXYNE_MSBUILD_EXECUTABLE "${CMAKE_VS_MSBUILD_COMMAND}")
    else()
        find_program(AXYNE_MSBUILD_EXECUTABLE NAMES MSBuild.exe msbuild REQUIRED)
    endif()
    # Scintilla is deliberately a Release-only dependency.  Keep its staging
    # path independent of the consuming multi-config Axyne target so packaging
    # cannot accidentally look for a Debug binary that was never built.
    set(AXYNE_SCINTILLA_CONFIGURATION "Release" CACHE STRING
        "Configuration used to build the staged Scintilla Win32 component")
    if(NOT AXYNE_SCINTILLA_CONFIGURATION STREQUAL "Release")
        message(FATAL_ERROR
            "Axyne packaging requires AXYNE_SCINTILLA_CONFIGURATION=Release; "
            "got '${AXYNE_SCINTILLA_CONFIGURATION}'.")
    endif()
    set(AXYNE_SCINTILLA_OUTPUT_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/scintilla/${AXYNE_SCINTILLA_CONFIGURATION}")
    set(AXYNE_SCINTILLA_DLL "${AXYNE_SCINTILLA_OUTPUT_DIR}/Scintilla.dll")
    add_custom_command(
        OUTPUT "${AXYNE_SCINTILLA_DLL}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AXYNE_SCINTILLA_OUTPUT_DIR}"
        COMMAND "${AXYNE_MSBUILD_EXECUTABLE}"
            "${axyne_scintilla_SOURCE_DIR}/win32/Scintilla.vcxproj"
            /m /nologo /t:Build
            "/p:Configuration=${AXYNE_SCINTILLA_CONFIGURATION}"
            /p:Platform=$<IF:$<BOOL:${CMAKE_GENERATOR_PLATFORM}>,$<LOWER_CASE:${CMAKE_GENERATOR_PLATFORM}>,x64>
            "/p:OutDir=${AXYNE_SCINTILLA_OUTPUT_DIR}/"
        WORKING_DIRECTORY "${axyne_scintilla_SOURCE_DIR}/win32"
        DEPENDS
            "${axyne_scintilla_SOURCE_DIR}/win32/Scintilla.vcxproj"
        COMMENT "Building pinned upstream Scintilla Win32 component"
        VERBATIM
    )
    add_custom_target(axyne_scintilla DEPENDS "${AXYNE_SCINTILLA_DLL}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/axyne_version.rc"
        @ONLY
    )
    add_executable(axyne WIN32
        src/platform/windows/main.c
        "${CMAKE_CURRENT_BINARY_DIR}/axyne_version.rc"
    )
    target_link_libraries(axyne PRIVATE axyne_core user32)
    add_dependencies(axyne axyne_scintilla axyne_lexilla)
    add_custom_command(TARGET axyne POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AXYNE_SCINTILLA_DLL}" "$<TARGET_FILE_DIR:axyne>/Scintilla.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:axyne_lexilla>" "$<TARGET_FILE_DIR:axyne>/Lexilla.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${axyne_scintilla_SOURCE_DIR}/License.txt"
            "$<TARGET_FILE_DIR:axyne>/Scintilla-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${axyne_lexilla_SOURCE_DIR}/License.txt"
            "$<TARGET_FILE_DIR:axyne>/Lexilla-LICENSE.txt"
        VERBATIM
    )
elseif(APPLE)
    enable_language(OBJC)
    set(AXYNE_SCINTILLA_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}")
    if(AXYNE_SCINTILLA_ARCHITECTURES STREQUAL "")
        set(AXYNE_SCINTILLA_ARCHITECTURES "arm64")
    endif()
    set(AXYNE_SCINTILLA_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(AXYNE_SCINTILLA_DEPLOYMENT_TARGET STREQUAL "")
        set(AXYNE_SCINTILLA_DEPLOYMENT_TARGET "12.0")
    endif()
    set(AXYNE_SCINTILLA_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/scintilla")
    set(AXYNE_SCINTILLA_FRAMEWORK
        "${AXYNE_SCINTILLA_OUTPUT_DIR}/Scintilla.framework")
    add_custom_command(
        OUTPUT "${AXYNE_SCINTILLA_FRAMEWORK}/Versions/A/Scintilla"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AXYNE_SCINTILLA_OUTPUT_DIR}"
        COMMAND xcodebuild
            -project "${axyne_scintilla_SOURCE_DIR}/cocoa/Scintilla/Scintilla.xcodeproj"
            -target Scintilla -configuration Release -sdk macosx
            "ARCHS=${AXYNE_SCINTILLA_ARCHITECTURES}"
            "MACOSX_DEPLOYMENT_TARGET=${AXYNE_SCINTILLA_DEPLOYMENT_TARGET}"
            "CONFIGURATION_BUILD_DIR=${AXYNE_SCINTILLA_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND}
            "-DAXYNE_SCINTILLA_FRAMEWORK=${AXYNE_SCINTILLA_FRAMEWORK}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/validation/ensure-framework-runtime.cmake"
        WORKING_DIRECTORY "${axyne_scintilla_SOURCE_DIR}/cocoa/Scintilla"
        DEPENDS
            "${axyne_scintilla_SOURCE_DIR}/cocoa/Scintilla/Scintilla.xcodeproj/project.pbxproj"
        COMMENT "Building pinned upstream Scintilla Cocoa framework"
        VERBATIM
    )
    add_custom_target(axyne_scintilla DEPENDS
        "${AXYNE_SCINTILLA_FRAMEWORK}/Versions/A/Scintilla")
    add_executable(axyne
        src/platform/macos/main.m
    )
    set_target_properties(axyne PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "${AXYNE_DISPLAY_NAME}"
        MACOSX_BUNDLE_BUNDLE_NAME "${AXYNE_DISPLAY_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.axyne.ide"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "Axyne.png"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) ${AXYNE_AUTHOR}"
    )
    get_target_property(AXYNE_MACOS_BUNDLE_OUTPUT_NAME axyne OUTPUT_NAME)
    set(AXYNE_MACOS_BUNDLE_NAME "${AXYNE_MACOS_BUNDLE_OUTPUT_NAME}.app")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/install-scintilla-framework.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/install-scintilla-framework.cmake"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Uninstall Axyne.command.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Uninstall ${AXYNE_DISPLAY_NAME}.command"
        @ONLY
    )
    target_link_libraries(axyne PRIVATE axyne_core "-framework AppKit")
    add_dependencies(axyne axyne_scintilla axyne_lexilla)
    add_custom_command(TARGET axyne POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks/Scintilla.framework"
        COMMAND /bin/cp -R
            "${AXYNE_SCINTILLA_FRAMEWORK}"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:axyne_lexilla>"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks/Lexilla.dylib"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/default.png"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Resources/Axyne.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${axyne_scintilla_SOURCE_DIR}/License.txt"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Resources/Scintilla-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${axyne_lexilla_SOURCE_DIR}/License.txt"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Resources/Lexilla-LICENSE.txt"
        VERBATIM
    )
else()
    add_executable(axyne
        src/platform/unsupported/main.c
    )
    target_link_libraries(axyne PRIVATE axyne_core)
endif()

target_include_directories(axyne PRIVATE include)

target_compile_definitions(axyne PRIVATE
    AXYNE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    AXYNE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    AXYNE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(WIN32 OR APPLE)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/version.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.json"
        @ONLY
    )

    if(WIN32)
        install(TARGETS axyne
            CONFIGURATIONS Release
            RUNTIME DESTINATION .
        )
        install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/version.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/RELEASE_NOTES.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            DESTINATION share/axyne
            CONFIGURATIONS Release
        )
        install(FILES
            "${AXYNE_SCINTILLA_DLL}"
            DESTINATION .
            CONFIGURATIONS Release
        )
        install(TARGETS axyne_lexilla
            RUNTIME DESTINATION .
            CONFIGURATIONS Release
        )
        install(FILES
            "${axyne_scintilla_SOURCE_DIR}/License.txt"
            RENAME Scintilla-LICENSE.txt
            DESTINATION .
            CONFIGURATIONS Release
        )
        install(FILES
            "${axyne_lexilla_SOURCE_DIR}/License.txt"
            RENAME Lexilla-LICENSE.txt
            DESTINATION .
            CONFIGURATIONS Release
        )
    elseif(APPLE)
        install(TARGETS axyne
            CONFIGURATIONS Release
            BUNDLE DESTINATION .
        )
        install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/version.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/RELEASE_NOTES.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/default.png"
            DESTINATION "${AXYNE_MACOS_BUNDLE_NAME}/Contents/Resources"
            CONFIGURATIONS Release
        )
        install(PROGRAMS
            "${CMAKE_CURRENT_BINARY_DIR}/Uninstall ${AXYNE_DISPLAY_NAME}.command"
            DESTINATION .
            CONFIGURATIONS Release
        )
        install(TARGETS axyne_lexilla
            LIBRARY DESTINATION
                "${AXYNE_MACOS_BUNDLE_NAME}/Contents/Frameworks"
            CONFIGURATIONS Release
        )
        install(FILES
            "${axyne_lexilla_SOURCE_DIR}/License.txt"
            RENAME Lexilla-LICENSE.txt
            DESTINATION "${AXYNE_MACOS_BUNDLE_NAME}/Contents/Resources"
            CONFIGURATIONS Release
        )
        # CMake's generic bundle traversal can flatten framework symlinks.
        # Re-copy the complete framework with macOS cp -R at install time so
        # Versions/Current and the root framework links remain intact.
        install(SCRIPT
            "${CMAKE_CURRENT_BINARY_DIR}/install-scintilla-framework.cmake"
        )
    endif()
endif()

if(WIN32)
    set(AXYNE_MSVC_RUNTIME_NSIS_FILES "")
    if(MSVC)
        foreach(AXYNE_MSVC_RUNTIME_NAME
                msvcp140.dll
                vcruntime140.dll
                vcruntime140_1.dll)
            find_file(AXYNE_MSVC_RUNTIME_FILE
                NAMES "${AXYNE_MSVC_RUNTIME_NAME}"
                PATHS
                    "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT"
                    "$ENV{SystemRoot}/System32"
                NO_DEFAULT_PATH)
            if(AXYNE_MSVC_RUNTIME_FILE)
                configure_file(
                    "${AXYNE_MSVC_RUNTIME_FILE}"
                    "${CMAKE_CURRENT_BINARY_DIR}/${AXYNE_MSVC_RUNTIME_NAME}"
                    COPYONLY
                )
                string(APPEND AXYNE_MSVC_RUNTIME_NSIS_FILES
                    "    File \"${CMAKE_CURRENT_BINARY_DIR}\\${AXYNE_MSVC_RUNTIME_NAME}\"\n")
            endif()
            unset(AXYNE_MSVC_RUNTIME_FILE CACHE)
        endforeach()
    endif()
    find_program(AXYNE_NSIS_EXECUTABLE
        NAMES makensis.exe makensis
        DOC "NSIS compiler used for the custom Windows installer")
    if(AXYNE_NSIS_EXECUTABLE)
        set(AXYNE_WINDOWS_INSTALLER_SCRIPT
            "${CMAKE_CURRENT_BINARY_DIR}/Axyne-Installer.nsi")
        set(AXYNE_WINDOWS_INSTALLER_BACKEND
            "${CMAKE_CURRENT_BINARY_DIR}/Axyne-${PROJECT_VERSION}-Windows-backend.exe")
        set(AXYNE_WINDOWS_INSTALLER_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/Axyne-${PROJECT_VERSION}-Windows.exe")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/Axyne-Installer.nsi.in"
            "${AXYNE_WINDOWS_INSTALLER_SCRIPT}"
            @ONLY
        )
        add_custom_command(
            OUTPUT "${AXYNE_WINDOWS_INSTALLER_BACKEND}"
            COMMAND "${AXYNE_NSIS_EXECUTABLE}" /V2
                "${AXYNE_WINDOWS_INSTALLER_SCRIPT}"
            DEPENDS axyne
                "${CMAKE_CURRENT_BINARY_DIR}/Axyne-Uninstaller.exe"
            COMMENT "Building Axyne installer backend"
            VERBATIM
        )
        add_custom_target(axyne-windows-installer-backend
            DEPENDS "${AXYNE_WINDOWS_INSTALLER_BACKEND}")
        set(AXYNE_WINDOWS_UNINSTALLER_RESOURCE
            "${CMAKE_CURRENT_BINARY_DIR}/Axyne-Uninstaller.rc")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/uninstaller_ui.rc.in"
            "${AXYNE_WINDOWS_UNINSTALLER_RESOURCE}"
            @ONLY
        )
        add_executable(axyne-windows-uninstaller WIN32 EXCLUDE_FROM_ALL
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/uninstaller_ui.c"
            "${AXYNE_WINDOWS_UNINSTALLER_RESOURCE}")
        target_compile_options(axyne-windows-uninstaller PRIVATE /utf-8)
        set_target_properties(axyne-windows-uninstaller PROPERTIES
            OUTPUT_NAME "Axyne-Uninstaller"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        add_dependencies(axyne-windows-installer-backend
            axyne-windows-uninstaller)
        set(AXYNE_WINDOWS_INSTALLER_RESOURCE
            "${CMAKE_CURRENT_BINARY_DIR}/Axyne-Installer.rc")
        set(AXYNE_INSTALLER_CONFIG_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/generated-installer")
        set(AXYNE_INSTALLER_CONFIG_HEADER
            "${AXYNE_INSTALLER_CONFIG_DIR}/installer_ui_config.h")
        add_custom_command(
            OUTPUT "${AXYNE_INSTALLER_CONFIG_HEADER}"
            COMMAND "${CMAKE_COMMAND}"
                -DAXYNE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DAXYNE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DAXYNE_OUTPUT=${AXYNE_INSTALLER_CONFIG_HEADER}
                -DAXYNE_VERSION=${PROJECT_VERSION}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/generate-installer-config.cmake"
            DEPENDS axyne
            VERBATIM
        )
        add_custom_target(axyne-windows-installer-config
            DEPENDS "${AXYNE_INSTALLER_CONFIG_HEADER}")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/installer_ui.rc.in"
            "${AXYNE_WINDOWS_INSTALLER_RESOURCE}"
            @ONLY
        )
        set_source_files_properties("${AXYNE_WINDOWS_INSTALLER_RESOURCE}"
            PROPERTIES OBJECT_DEPENDS "${AXYNE_WINDOWS_INSTALLER_BACKEND}")
        add_executable(axyne-windows-installer-ui WIN32 EXCLUDE_FROM_ALL
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/installer_ui.c"
            "${AXYNE_WINDOWS_INSTALLER_RESOURCE}")
        add_dependencies(axyne-windows-installer-ui
            axyne axyne-windows-installer-backend axyne-windows-installer-config)
        target_include_directories(axyne-windows-installer-ui PRIVATE
            "${AXYNE_INSTALLER_CONFIG_DIR}")
        add_custom_target(axyne-windows-installer
            DEPENDS axyne-windows-installer-ui
            COMMENT "Building custom Axyne Windows installer"
        )
        set_target_properties(axyne-windows-installer-ui PROPERTIES
            OUTPUT_NAME "Axyne-${PROJECT_VERSION}-Windows"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_options(axyne-windows-installer-ui PRIVATE /utf-8)
    else()
        message(STATUS
            "NSIS was not found; axyne-windows-installer target is unavailable")
    endif()
endif()

# Each optional feature owns its CMakeLists.txt so feature branches can add
# modules without editing the shared root build file.
file(GLOB AXYNE_FEATURE_MODULES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/features/*/CMakeLists.txt"
)
foreach(AXYNE_FEATURE_MODULE IN LISTS AXYNE_FEATURE_MODULES)
    include("${AXYNE_FEATURE_MODULE}")
endforeach()

include(CTest)
option(AXYNE_BUILD_PERF_BENCHMARK
    "Build the opt-in startup and memory budget benchmark" OFF)
if(AXYNE_BUILD_PERF_BENCHMARK AND (WIN32 OR APPLE))
    add_subdirectory(tools/perf/benchmarks)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(WIN32 OR APPLE)
    set(CPACK_PACKAGE_NAME "${AXYNE_DISPLAY_NAME}")
    set(CPACK_PACKAGE_VENDOR "${AXYNE_AUTHOR}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    if(APPLE)
        string(REPLACE ";" "-" AXYNE_MACOS_ARCHITECTURE_LABEL
            "${CMAKE_OSX_ARCHITECTURES}")
        set(CPACK_PACKAGE_FILE_NAME
            "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${AXYNE_MACOS_ARCHITECTURE_LABEL}")
    else()
        set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")
    endif()
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${AXYNE_DISPLAY_NAME}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "A native offline desktop IDE for Windows and macOS")
    # ProductBuild accepts only a small set of resource extensions. Keep the
    # installed files unchanged while giving CPack portable .txt resources.
    set(AXYNE_CPACK_LICENSE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/Axyne-License.txt")
    set(AXYNE_CPACK_README_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/Axyne-Release-Notes.txt")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${AXYNE_CPACK_LICENSE_FILE}"
        COPYONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/RELEASE_NOTES.md"
        "${AXYNE_CPACK_README_FILE}"
        COPYONLY
    )
    set(CPACK_RESOURCE_FILE_LICENSE "${AXYNE_CPACK_LICENSE_FILE}")
    set(CPACK_RESOURCE_FILE_README "${AXYNE_CPACK_README_FILE}")
    set(AXYNE_PACKAGING_CONFIGURED_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    if(CMAKE_CONFIGURATION_TYPES)
        set(AXYNE_PACKAGING_MULTI_CONFIG TRUE)
    else()
        set(AXYNE_PACKAGING_MULTI_CONFIG FALSE)
    endif()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/CPackProjectConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CPackProjectConfig.cmake"
        @ONLY
    )
    set(CPACK_PROJECT_CONFIG_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/CPackProjectConfig.cmake")

    if(WIN32)
        set(CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_DISPLAY_NAME "${AXYNE_DISPLAY_NAME} ${PROJECT_VERSION}")
        set(CPACK_NSIS_PACKAGE_NAME "${AXYNE_DISPLAY_NAME}")
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        set(CPACK_NSIS_MODIFY_PATH OFF)
        set(CPACK_NSIS_MENU_LINKS "axyne.exe" "Axyne")
    elseif(APPLE)
        set(CPACK_GENERATOR "productbuild;DragNDrop")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/Applications")
        set(CPACK_PRODUCTBUILD_IDENTIFIER "com.axyne.ide")
        set(CPACK_PRODUCTBUILD_INSTALL_PATH "/Applications")
        set(CPACK_DMG_VOLUME_NAME "${AXYNE_DISPLAY_NAME} ${PROJECT_VERSION}")
    endif()

    include(CPack)
endif()
