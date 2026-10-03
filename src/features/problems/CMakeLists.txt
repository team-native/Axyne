# Platform-independent problems list model and build-output parser. Pure C,
# no OS API, so every platform builds the same source.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/problems.c"
)
if(MSVC)
    # The summary text contains UTF-8 Korean.
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/problems.c"
        PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()
