# Axyne account (sign-up / sign-in) and plugin marketplace client over the
# server API. HTTP and the server address come from axyne_server_client.
add_library(axyne_account STATIC
    "${CMAKE_CURRENT_LIST_DIR}/account.c"
)
target_include_directories(axyne_account PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(axyne_account PUBLIC axyne_core axyne_server_client)
if(MSVC)
    target_compile_options(axyne_account PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_account PRIVATE -Wall -Wextra)
endif()
target_link_libraries(axyne PRIVATE axyne_account)
