# Platform-independent command palette model and symbol scanner. Pure C, no
# OS API, so every platform builds the same sources.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/symbols.c"
)
