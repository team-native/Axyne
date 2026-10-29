# Installed plugins: manifests, enable/disable, zip install and external
# command preparation. Plugins run as external processes only.
add_library(axyne_plugins STATIC
    "${CMAKE_CURRENT_LIST_DIR}/plugins.c"
)
target_include_directories(axyne_plugins PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(axyne_plugins PUBLIC axyne_process)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(axyne_plugins PRIVATE Threads::Threads)
endif()
if(MSVC)
    target_compile_options(axyne_plugins PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_plugins PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(axyne PRIVATE axyne_plugins)
