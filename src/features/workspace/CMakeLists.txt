if(WIN32)
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/filesystem.c"
    )
elseif(APPLE)
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/filesystem.c"
    )
else()
    target_sources(axyne_core PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/filesystem.c"
    )
endif()
