cmake_minimum_required(VERSION 3.21)

project(Axyne VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(axyne_core STATIC
    src/core/app.c
)

target_include_directories(axyne_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(axyne_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(axyne_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(axyne WIN32
        src/platform/windows/main.c
    )
    target_link_libraries(axyne PRIVATE axyne_core user32)
elseif(APPLE)
    enable_language(OBJC)
    add_executable(axyne
        src/platform/macos/main.m
    )
    target_link_libraries(axyne PRIVATE axyne_core "-framework AppKit")
else()
    add_executable(axyne
        src/platform/unsupported/main.c
    )
    target_link_libraries(axyne PRIVATE axyne_core)
endif()

target_include_directories(axyne PRIVATE include)

