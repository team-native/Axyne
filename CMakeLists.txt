cmake_minimum_required(VERSION 3.21)

set(AXYNE_DISPLAY_NAME "Axyne" CACHE STRING "Application display name")
set(AXYNE_AUTHOR "Native" CACHE STRING "Application author or organization")
set(AXYNE_VERSION "0.1.0" CACHE STRING "Application version")
project(Axyne VERSION ${AXYNE_VERSION} LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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
    set(AXYNE_SCINTILLA_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/scintilla/$<CONFIG>")
    set(AXYNE_SCINTILLA_DLL "${AXYNE_SCINTILLA_OUTPUT_DIR}/Scintilla.dll")
    add_custom_command(
        OUTPUT "${AXYNE_SCINTILLA_DLL}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AXYNE_SCINTILLA_OUTPUT_DIR}"
        COMMAND "${AXYNE_MSBUILD_EXECUTABLE}"
            "${axyne_scintilla_SOURCE_DIR}/win32/Scintilla.vcxproj"
            /m /nologo /t:Build
            /p:Configuration=Release
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
    add_dependencies(axyne axyne_scintilla)
    add_custom_command(TARGET axyne POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AXYNE_SCINTILLA_DLL}" "$<TARGET_FILE_DIR:axyne>/Scintilla.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${axyne_scintilla_SOURCE_DIR}/License.txt"
            "$<TARGET_FILE_DIR:axyne>/Scintilla-LICENSE.txt"
        VERBATIM
    )
elseif(APPLE)
    enable_language(OBJC)
    set(AXYNE_SCINTILLA_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/scintilla")
    set(AXYNE_SCINTILLA_FRAMEWORK
        "${AXYNE_SCINTILLA_OUTPUT_DIR}/Scintilla.framework")
    add_custom_command(
        OUTPUT "${AXYNE_SCINTILLA_FRAMEWORK}/Versions/A/Scintilla"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AXYNE_SCINTILLA_OUTPUT_DIR}"
        COMMAND xcodebuild
            -project "${axyne_scintilla_SOURCE_DIR}/cocoa/Scintilla/Scintilla.xcodeproj"
            -target Scintilla -configuration Release -sdk macosx
            "CONFIGURATION_BUILD_DIR=${AXYNE_SCINTILLA_OUTPUT_DIR}"
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
        MACOSX_BUNDLE_BUNDLE_NAME "${AXYNE_DISPLAY_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.axyne.ide"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) ${AXYNE_AUTHOR}"
    )
    target_link_libraries(axyne PRIVATE axyne_core "-framework AppKit")
    add_dependencies(axyne axyne_scintilla)
    add_custom_command(TARGET axyne POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${AXYNE_SCINTILLA_FRAMEWORK}"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks/Scintilla.framework"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${axyne_scintilla_SOURCE_DIR}/License.txt"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Resources/Scintilla-LICENSE.txt"
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
            RUNTIME DESTINATION .
        )
        install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/version.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/RELEASE_NOTES.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            DESTINATION share/axyne
        )
    elseif(APPLE)
        install(TARGETS axyne
            BUNDLE DESTINATION .
        )
        install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/version.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/RELEASE_NOTES.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            DESTINATION "Axyne.app/Contents/Resources"
        )
        install(PROGRAMS
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Uninstall Axyne.command"
            DESTINATION .
        )
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

if(WIN32 OR APPLE)
    set(CPACK_PACKAGE_NAME "${AXYNE_DISPLAY_NAME}")
    set(CPACK_PACKAGE_VENDOR "${AXYNE_AUTHOR}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${AXYNE_DISPLAY_NAME}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "A native offline desktop IDE for Windows and macOS")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/docs/RELEASE_NOTES.md")

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
