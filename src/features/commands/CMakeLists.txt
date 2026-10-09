# Platform-independent command registry. Pure C, no OS API.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/commands.c"
)
if(MSVC)
    # The command table contains UTF-8 Korean titles.
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/commands.c"
        PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()
