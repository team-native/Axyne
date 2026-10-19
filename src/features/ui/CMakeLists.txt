if(WIN32)
    add_library(axyne_ui STATIC
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_commands_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/terminal_view_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/syntax.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/preferences_window_windows.c"
        # One file per feature category (hooks: ui_windows_internal.h).
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_file_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_edit_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_view_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_build_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_debug_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_terminal_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_status_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_tools_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_help_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_lsp_nav_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_diag_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_release_windows.c")
    target_link_libraries(axyne_ui PRIVATE user32 gdi32 comdlg32 shell32 ole32 imm32)
elseif(APPLE)
    add_library(axyne_ui STATIC
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/terminal_view_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/syntax.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/preferences_window_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/popup_menu_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/git_panel_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_commands_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_file_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_edit_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_view_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_build_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_debug_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_terminal_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_status_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_tools_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_help_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_lsp_nav_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_diag_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_release_macos.m")
    target_link_libraries(axyne_ui PRIVATE "-framework AppKit")
    # The feature files only add categories to AxyneWorkspaceView; nothing
    # references their symbols, so every consumer of the static library must
    # load all Objective-C members.
    target_link_options(axyne_ui INTERFACE "-ObjC")
endif()

target_include_directories(axyne_ui
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${axyne_scintilla_SOURCE_DIR}/include"
)
target_link_libraries(axyne PRIVATE axyne_ui)
target_link_libraries(axyne_ui PRIVATE axyne_terminal)

if(MSVC)
    target_compile_options(axyne_ui PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()
