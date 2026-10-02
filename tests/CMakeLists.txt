add_executable(axyne_tests
    test_main.c
    test_documents_search.c
    test_settings_preferences.c
    test_runner_git_lsp.c
    test_process_stability.c
)

target_include_directories(axyne_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(axyne_tests PRIVATE
    axyne_runner
    axyne_git
    axyne_lsp
)
if(MSVC)
    target_compile_options(axyne_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(axyne_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
add_test(
    NAME axyne-documents-search
    COMMAND $<TARGET_FILE:axyne_tests> documents-search
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/documents-search"
)
add_test(
    NAME axyne-settings-preferences
    COMMAND $<TARGET_FILE:axyne_tests> settings-preferences
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/settings-preferences"
)
add_test(
    NAME axyne-runner-git-lsp
    COMMAND $<TARGET_FILE:axyne_tests> runner-git-lsp
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/runner-git-lsp"
            "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME axyne-process-stability
    COMMAND $<TARGET_FILE:axyne_tests> process-stability
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/process-stability"
)
set_tests_properties(
    axyne-documents-search
    axyne-settings-preferences
    axyne-runner-git-lsp
    axyne-process-stability
    PROPERTIES TIMEOUT 60
)

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
endif()
