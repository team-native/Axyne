if(WIN32)
    add_library(axyne_ui STATIC "${CMAKE_CURRENT_LIST_DIR}/platform/windows/ui_windows.c")
    target_link_libraries(axyne_ui PRIVATE user32 gdi32 comdlg32)
elseif(APPLE)
    add_library(axyne_ui STATIC "${CMAKE_CURRENT_LIST_DIR}/platform/macos/ui_macos.m")
    target_include_directories(axyne_ui PRIVATE "${axyne_scintilla_SOURCE_DIR}/include")
    target_link_libraries(axyne_ui PRIVATE "-framework AppKit")
endif()

target_include_directories(axyne_ui
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(axyne PRIVATE axyne_ui)

if(MSVC)
    target_compile_options(axyne_ui PRIVATE /W4 /utf-8)
else()
    target_compile_options(axyne_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()
