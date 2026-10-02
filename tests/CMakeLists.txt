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
    add_executable(axyne_workspace_layout_tests test_workspace_layout_macos.m)
    target_link_libraries(axyne_workspace_layout_tests PRIVATE
        axyne_ui axyne_debugger axyne_lsp axyne_git)
    target_link_options(axyne_workspace_layout_tests PRIVATE
        "LINKER:-force_load,$<TARGET_FILE:axyne_ui>")
    add_test(NAME axyne-workspace-layout COMMAND $<TARGET_FILE:axyne_workspace_layout_tests>)
    set_tests_properties(axyne-workspace-layout PROPERTIES TIMEOUT 60)
endif()
