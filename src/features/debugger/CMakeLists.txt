if(WIN32)
    set(AXYNE_DEBUGGER_PLATFORM_SOURCE debugger_platform_windows.c)
else()
    set(AXYNE_DEBUGGER_PLATFORM_SOURCE debugger_platform_posix.c)
endif()

add_library(axyne_debugger STATIC
    "${CMAKE_CURRENT_LIST_DIR}/debugger.c"
    "${CMAKE_CURRENT_LIST_DIR}/${AXYNE_DEBUGGER_PLATFORM_SOURCE}"
)
target_include_directories(axyne_debugger PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(axyne_debugger PUBLIC axyne_process axyne_runner)
if(MSVC)
    target_compile_options(axyne_debugger PRIVATE /W4)
else()
    target_compile_options(axyne_debugger PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(axyne PRIVATE axyne_debugger)
