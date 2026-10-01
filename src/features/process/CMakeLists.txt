if(WIN32)
    set(AXYNE_PROCESS_PLATFORM_SOURCE process_windows.c)
elseif(APPLE)
    set(AXYNE_PROCESS_PLATFORM_SOURCE process_posix.c)
else()
    message(FATAL_ERROR "Axyne process feature supports Windows and macOS only")
endif()

add_library(axyne_process STATIC
    "${CMAKE_CURRENT_LIST_DIR}/process.c"
    "${CMAKE_CURRENT_LIST_DIR}/${AXYNE_PROCESS_PLATFORM_SOURCE}"
)
target_link_libraries(axyne_process PUBLIC axyne_core)
target_include_directories(axyne_process PUBLIC ${PROJECT_SOURCE_DIR}/include)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(axyne_process PRIVATE Threads::Threads)
endif()
if(MSVC)
    target_compile_options(axyne_process PRIVATE /W4)
else()
    target_compile_options(axyne_process PRIVATE -Wall -Wextra -Wpedantic)
endif()
