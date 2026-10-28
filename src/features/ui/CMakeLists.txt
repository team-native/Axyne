if(WIN32)
    add_library(axyne_ui STATIC
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/release_window_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/terminal_view_windows.c"
        "${CMAKE_CURRENT_LIST_DIR}/syntax.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/windows/preferences_window_windows.c")
    target_link_libraries(axyne_ui PRIVATE user32 gdi32 comdlg32 shell32 ole32 imm32)
elseif(APPLE)
    add_library(axyne_ui STATIC
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/release_window_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/terminal_view_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/syntax.c"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/preferences_window_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/popup_menu_macos.m"
        "${CMAKE_CURRENT_LIST_DIR}/platform/macos/git_panel_macos.m")
    target_link_libraries(axyne_ui PRIVATE "-framework AppKit")
endif()

target_sources(axyne_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/release_notes.c")

target_include_directories(axyne_ui
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${axyne_scintilla_SOURCE_DIR}/include"
)
target_link_libraries(axyne PRIVATE axyne_ui)
target_link_libraries(axyne_ui PRIVATE axyne_terminal axyne_server_client)

if(MSVC)
    target_compile_options(axyne_ui PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Embed exact release icons in each executable that consumes the static UI.
set(AXYNE_RELEASE_ASSET_DIR "${CMAKE_CURRENT_LIST_DIR}/assets/release")
if(WIN32)
    configure_file("${AXYNE_RELEASE_ASSET_DIR}/release_assets.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/release_assets.rc" @ONLY)
    target_sources(axyne_ui INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/release_assets.rc")
    target_link_libraries(axyne_ui PRIVATE windowscodecs msimg32)
elseif(APPLE)
    file(GLOB AXYNE_RELEASE_ASSETS "${AXYNE_RELEASE_ASSET_DIR}/*.png"
        "${AXYNE_RELEASE_ASSET_DIR}/*.svg")
    set_source_files_properties(${AXYNE_RELEASE_ASSETS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/release")
    target_sources(axyne PRIVATE ${AXYNE_RELEASE_ASSETS})
endif()