# Axyne account (sign-up / sign-in) and plugin marketplace client over the
# server API. HTTP uses WinHTTP on Windows and NSURLSession on macOS.
if(WIN32)
    set(AXYNE_HTTP_SOURCE "${CMAKE_CURRENT_LIST_DIR}/http_windows.c")
elseif(APPLE)
    set(AXYNE_HTTP_SOURCE "${CMAKE_CURRENT_LIST_DIR}/http_macos.m")
else()
    set(AXYNE_HTTP_SOURCE "")
endif()
add_library(axyne_account STATIC
    "${CMAKE_CURRENT_LIST_DIR}/account.c"
    ${AXYNE_HTTP_SOURCE}
)
target_include_directories(axyne_account PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(axyne_account PUBLIC axyne_core)
if(WIN32)
    target_link_libraries(axyne_account PUBLIC winhttp)
elseif(APPLE)
    target_link_libraries(axyne_account PUBLIC "-framework Foundation")
endif()
if(MSVC)
    target_compile_options(axyne_account PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_account PRIVATE -Wall -Wextra)
endif()
target_link_libraries(axyne PRIVATE axyne_account)
