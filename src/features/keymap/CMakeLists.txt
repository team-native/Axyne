# Platform-independent key strokes, chords and the effective key map.
# Pure C, no OS API.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/keymap.c"
)
if(MSVC)
    # The chord hint and the macOS key symbols are UTF-8.
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/keymap.c"
        PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()
