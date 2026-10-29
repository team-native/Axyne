# Shared server configuration and the existing native blocking HTTP ABI.
add_library(axyne_server_client STATIC
    "${CMAKE_CURRENT_LIST_DIR}/server_client.c"
)
target_include_directories(axyne_server_client PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(axyne_server_client PUBLIC axyne_core)
if(WIN32)
    target_sources(axyne_server_client PRIVATE "${CMAKE_CURRENT_LIST_DIR}/http_windows.c")
    target_link_libraries(axyne_server_client PUBLIC winhttp)
elseif(APPLE)
    target_sources(axyne_server_client PRIVATE "${CMAKE_CURRENT_LIST_DIR}/http_macos.m")
    target_link_libraries(axyne_server_client PUBLIC "-framework Foundation")
endif()
if(MSVC)
    target_compile_options(axyne_server_client PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_server_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Consumers opt into this library; no account/plugin/UI dependency here.
