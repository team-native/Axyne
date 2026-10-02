if(WIN32)
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/explorer.c"
        "${CMAKE_CURRENT_LIST_DIR}/filesystem.c"
        "${CMAKE_CURRENT_LIST_DIR}/search.c"
        "${CMAKE_CURRENT_LIST_DIR}/watcher_windows.c"
    )
elseif(APPLE)
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/explorer.c"
        "${CMAKE_CURRENT_LIST_DIR}/filesystem.c"
        "${CMAKE_CURRENT_LIST_DIR}/search.c"
        "${CMAKE_CURRENT_LIST_DIR}/watcher_macos.c"
    )
    target_link_libraries(axyne_core PUBLIC "-framework CoreServices")
else()
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/explorer.c"
        "${CMAKE_CURRENT_LIST_DIR}/filesystem.c"
        "${CMAKE_CURRENT_LIST_DIR}/search.c"
    )
endif()
