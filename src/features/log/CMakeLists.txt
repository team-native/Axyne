# Lazy, rotating local log file (D22).
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/log.c"
)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(axyne_core PUBLIC Threads::Threads)
endif()
