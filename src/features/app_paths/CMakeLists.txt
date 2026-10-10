# Per-user config/log/resource locations. The platform part is a small
# Windows (shell folders) or POSIX (HOME, bundle, /proc) source.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/app_paths.c"
)
if(WIN32)
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/app_paths_windows.c"
    )
    target_link_libraries(axyne_core PUBLIC shell32)
else()
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/app_paths_posix.c"
    )
endif()
