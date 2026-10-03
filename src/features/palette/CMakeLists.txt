# Platform-independent command palette model and symbol scanner. Pure C, no
# OS API, so every platform builds the same sources.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/palette.c"
    "${CMAKE_CURRENT_LIST_DIR}/symbols.c"
)
if(MSVC)
    # The command table contains UTF-8 Korean titles.
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/palette.c"
        PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()
