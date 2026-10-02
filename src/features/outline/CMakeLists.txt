# Platform-independent explorer outline model. Pure C, no OS API.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/outline.c"
)
if(MSVC)
    # The header and message texts are UTF-8 Korean.
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/outline.c"
        PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()
