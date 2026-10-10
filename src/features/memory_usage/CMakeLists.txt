# Process memory sampler for the RAM gauge (D15).
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/memory_usage.c"
)
if(WIN32)
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/memory_usage_windows.c"
    )
    target_link_libraries(axyne_core PUBLIC psapi)
else()
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/memory_usage_posix.c"
    )
endif()
