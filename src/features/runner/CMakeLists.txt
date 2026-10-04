add_library(axyne_runner STATIC
    "${CMAKE_CURRENT_LIST_DIR}/runner.c"
    "${CMAKE_CURRENT_LIST_DIR}/build_target.c"
    "${CMAKE_CURRENT_LIST_DIR}/language.c"
    "${CMAKE_CURRENT_LIST_DIR}/build_selector.c"
)
target_include_directories(axyne_runner PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(axyne_runner PUBLIC axyne_process)
if(MSVC)
    target_compile_options(axyne_runner PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_runner PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(axyne PRIVATE axyne_runner)
