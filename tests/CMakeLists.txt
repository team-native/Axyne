add_executable(axyne_tests
    test_main.c
    test_documents_search.c
    test_settings_preferences.c
    test_runner_git_lsp.c
    test_process_stability.c
    test_git_repair.c
    test_preview_tabs.c
    test_palette_problems.c
)

target_include_directories(axyne_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(axyne_tests PRIVATE
    axyne_runner
    axyne_git
    axyne_lsp
)
if(MSVC)
    target_compile_options(axyne_tests PRIVATE /W4 /permissive-)
    # The palette and problems tests contain UTF-8 Korean literals.
    set_source_files_properties(test_palette_problems.c
        PROPERTIES COMPILE_OPTIONS "/utf-8")
else()
    target_compile_options(axyne_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
# The no-follow filesystem contract requires canonical parent components.
# macOS aliases such as /tmp must not make otherwise valid fixtures fail.
file(REAL_PATH "${CMAKE_CURRENT_BINARY_DIR}/fixtures" AXYNE_TEST_FIXTURE_DIR)
add_test(
    NAME axyne-documents-search
    COMMAND $<TARGET_FILE:axyne_tests> documents-search
            "${AXYNE_TEST_FIXTURE_DIR}/documents-search"
)
add_test(
    NAME axyne-settings-preferences
    COMMAND $<TARGET_FILE:axyne_tests> settings-preferences
            "${AXYNE_TEST_FIXTURE_DIR}/settings-preferences"
)
add_test(
    NAME axyne-runner-git-lsp
    COMMAND $<TARGET_FILE:axyne_tests> runner-git-lsp
            "${AXYNE_TEST_FIXTURE_DIR}/runner-git-lsp"
            "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME axyne-process-stability
    COMMAND $<TARGET_FILE:axyne_tests> process-stability
            "${AXYNE_TEST_FIXTURE_DIR}/process-stability"
)
add_test(
    NAME axyne-git-repair
    COMMAND $<TARGET_FILE:axyne_tests> git-repair
            "${AXYNE_TEST_FIXTURE_DIR}/git-repair"
)
add_test(
    NAME axyne-preview-tabs
    COMMAND $<TARGET_FILE:axyne_tests> preview-tabs
            "${AXYNE_TEST_FIXTURE_DIR}/preview-tabs"
)
add_test(
    NAME axyne-palette-problems
    COMMAND $<TARGET_FILE:axyne_tests> palette-problems
            "${AXYNE_TEST_FIXTURE_DIR}/palette-problems"
)
set_tests_properties(
    axyne-git-repair
    axyne-preview-tabs
    axyne-documents-search
    axyne-settings-preferences
    axyne-runner-git-lsp
    axyne-process-stability
    axyne-palette-problems
    PROPERTIES TIMEOUT 60
)

# Clear-all-breakpoints helper over the real breakpoint table (no gdb needed).
add_executable(axyne_debugger_actions_tests test_debugger_actions.c)
target_include_directories(axyne_debugger_actions_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(axyne_debugger_actions_tests PRIVATE axyne_debugger)
if(MSVC)
    target_compile_options(axyne_debugger_actions_tests PRIVATE /W4)
else()
    target_compile_options(axyne_debugger_actions_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME axyne-debugger-actions COMMAND $<TARGET_FILE:axyne_debugger_actions_tests>)
set_tests_properties(axyne-debugger-actions PROPERTIES TIMEOUT 60)

if(DEFINED axyne_scintilla_SOURCE_DIR)
    # Shared Scintilla command helpers run against an in-memory editor.
    add_executable(axyne_editor_actions_tests test_editor_actions.c)
    target_include_directories(axyne_editor_actions_tests PRIVATE
        "${axyne_scintilla_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(axyne_editor_actions_tests PRIVATE /W4)
    else()
        target_compile_options(axyne_editor_actions_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME axyne-editor-actions COMMAND $<TARGET_FILE:axyne_editor_actions_tests>)
    set_tests_properties(axyne-editor-actions PROPERTIES TIMEOUT 60)
endif()

if(NOT WIN32)
    add_executable(axyne_workspace_safety_tests test_workspace_safety_posix.c)
    target_link_libraries(axyne_workspace_safety_tests PRIVATE axyne_core)
    target_compile_options(axyne_workspace_safety_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME axyne-workspace-safety COMMAND $<TARGET_FILE:axyne_workspace_safety_tests>)
    set_tests_properties(axyne-workspace-safety PROPERTIES TIMEOUT 60)
endif()

if(APPLE)
    # Exercise the production AppKit adapter in a bundle with the same native
    # runtime as Axyne, without starting the interactive application.
    add_executable(axyne_editor_runtime_tests MACOSX_BUNDLE
        test_editor_runtime_macos.m
    )
    target_include_directories(axyne_editor_runtime_tests PRIVATE
        "${axyne_scintilla_SOURCE_DIR}/include"
    )
    target_link_libraries(axyne_editor_runtime_tests PRIVATE
        axyne_ui axyne_debugger axyne_git axyne_lsp axyne_runner axyne_core
        "-framework AppKit"
    )
    target_link_options(axyne_editor_runtime_tests PRIVATE -ObjC)
    target_compile_options(axyne_editor_runtime_tests PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_dependencies(axyne_editor_runtime_tests axyne)
    add_custom_command(TARGET axyne_editor_runtime_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne_editor_runtime_tests>/Frameworks"
        COMMAND /bin/cp -R
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks/Scintilla.framework"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne_editor_runtime_tests>/Frameworks/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Frameworks/Lexilla.dylib"
            "$<TARGET_BUNDLE_CONTENT_DIR:axyne_editor_runtime_tests>/Frameworks/Lexilla.dylib"
        VERBATIM
    )
    add_test(NAME axyne-editor-runtime
        COMMAND $<TARGET_FILE:axyne_editor_runtime_tests>
    )
    set_tests_properties(axyne-editor-runtime PROPERTIES TIMEOUT 60)
elseif(WIN32)
    add_executable(axyne_editor_runtime_tests test_editor_runtime_windows.c)
    target_include_directories(axyne_editor_runtime_tests PRIVATE
        "${axyne_scintilla_SOURCE_DIR}/include")
    target_link_libraries(axyne_editor_runtime_tests PRIVATE
        axyne_ui axyne_debugger axyne_git axyne_lsp axyne_runner axyne_core)
    if(MSVC)
        target_compile_options(axyne_editor_runtime_tests PRIVATE /W4 /utf-8)
    endif()
    add_dependencies(axyne_editor_runtime_tests axyne)
    add_custom_command(TARGET axyne_editor_runtime_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AXYNE_SCINTILLA_DLL}"
            "$<TARGET_FILE_DIR:axyne_editor_runtime_tests>/Scintilla.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:axyne_lexilla>"
            "$<TARGET_FILE_DIR:axyne_editor_runtime_tests>/Lexilla.dll"
        VERBATIM)
    add_test(NAME axyne-editor-runtime COMMAND $<TARGET_FILE:axyne_editor_runtime_tests>)
    set_tests_properties(axyne-editor-runtime PROPERTIES TIMEOUT 60)
endif()

if(APPLE)
    add_executable(axyne_workspace_layout_tests test_workspace_layout_macos.m)
    target_link_libraries(axyne_workspace_layout_tests PRIVATE
        axyne_ui axyne_debugger axyne_lsp axyne_git)
    target_link_options(axyne_workspace_layout_tests PRIVATE
        "LINKER:-force_load,$<TARGET_FILE:axyne_ui>")
    add_test(NAME axyne-workspace-layout COMMAND $<TARGET_FILE:axyne_workspace_layout_tests>)
    set_tests_properties(axyne-workspace-layout PROPERTIES TIMEOUT 60)
endif()

# Pure-C table/mapping tests; independent of the native UI adapters.
add_executable(axyne_syntax_tests
    test_syntax.c
    "${PROJECT_SOURCE_DIR}/src/features/ui/syntax.c"
)
target_include_directories(axyne_syntax_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(axyne_syntax_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(axyne_syntax_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME axyne-syntax COMMAND $<TARGET_FILE:axyne_syntax_tests>)
set_tests_properties(axyne-syntax PROPERTIES TIMEOUT 30)

add_executable(axyne_ui_design_tests test_ui_design.c)
target_include_directories(axyne_ui_design_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(axyne_ui_design_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(axyne_ui_design_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME axyne-ui-design COMMAND $<TARGET_FILE:axyne_ui_design_tests>)
set_tests_properties(axyne-ui-design PROPERTIES TIMEOUT 30)
