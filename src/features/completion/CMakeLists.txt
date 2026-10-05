# Platform-independent editor word completion. Pure C, no OS API.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/completion.c"
)
