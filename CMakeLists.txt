cmake_minimum_required(VERSION 3.21)

set(AXYNE_DISPLAY_NAME "Axyne" CACHE STRING "Application display name")
set(AXYNE_AUTHOR "Native" CACHE STRING "Application author or organization")
set(AXYNE_VERSION "0.1.0" CACHE STRING "Application version")

project(Axyne VERSION ${AXYNE_VERSION} LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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
elseif(APPLE)
    enable_language(OBJC)
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
