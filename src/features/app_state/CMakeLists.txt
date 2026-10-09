# state.json: recent files/folders, last seen version, global run
# configuration. Pure C over the shared settings JSON module.
target_sources(axyne_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/app_state.c"
)
