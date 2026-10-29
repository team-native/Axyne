/* Compile the production adapter into this test so its private state and
 * window procedure are exercised without exporting any application test API. */
#include "../src/features/ui/platform/windows/ui_windows.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

enum { FAULT_NONE, FAULT_CREATE, FAULT_BIND };
static int fault;
static int document_references;
static int references_at_child_destroy = -1;
static WNDPROC production_editor_proc;

static LRESULT CALLBACK test_editor_proc(HWND editor, UINT message,
                                         WPARAM w_param, LPARAM l_param)
{
    if (fault == FAULT_CREATE && message == SCI_CREATEDOCUMENT) return 0;
    if (message == WM_DESTROY) references_at_child_destroy = document_references;
    LRESULT result = CallWindowProcW(production_editor_proc, editor, message,
                                     w_param, l_param);
    if (message == SCI_CREATEDOCUMENT && result != 0) ++document_references;
    if (message == SCI_ADDREFDOCUMENT) ++document_references;
    if (message == SCI_RELEASEDOCUMENT) --document_references;
    if (fault == FAULT_BIND && message == SCI_SETDOCPOINTER) {
        fault = FAULT_NONE;
        CallWindowProcW(production_editor_proc, editor, SCI_SETSTATUS,
                        SC_STATUS_BADALLOC, 0);
    }
    return result;
}

static int editor_equals(HWND editor, const char *bytes, size_t length)
{
    if (SendMessageA(editor, SCI_GETTEXTLENGTH, 0, 0) != (LRESULT)length) return 0;
    char *actual = (char *)malloc(length + 1);
    if (actual == NULL) return 0;
    SendMessageA(editor, SCI_GETTEXT, (WPARAM)(length + 1), (LPARAM)actual);
    int equal = memcmp(actual, bytes, length) == 0;
    free(actual);
    return equal;
}

static AxyneStatus test_terminal_configure_failure(
    AxyneRunnerConfig *config, const AxyneRunnerSpec *spec,
    AxyneError *error)
{
    (void)config;
    (void)spec;
    if (error != NULL) {
        error->code = AXYNE_STATUS_OUT_OF_MEMORY;
        (void)snprintf(error->message, sizeof(error->message),
                       "injected terminal profile configuration failure");
    }
    return AXYNE_STATUS_OUT_OF_MEMORY;
}

static int test_terminal_profiles(AxyneWindowState *state)
{
    wchar_t system_directory[MAX_PATH];
    wchar_t explicit_path[MAX_PATH];
    wchar_t findstr_path[MAX_PATH];
    char *explicit_utf8;
    char *findstr_utf8;
    char *resolved;
    char *previous_runner;
    DWORD system_length = GetSystemDirectoryW(system_directory, MAX_PATH);
    DWORD path_length;
    wchar_t *previous_path;
    AxyneError error = {0};

    CHECK(system_length != 0 && system_length + 12 < MAX_PATH);
    wcscpy_s(explicit_path, MAX_PATH, system_directory);
    wcscat_s(explicit_path, MAX_PATH, L"\\cmd.exe");
    wcscpy_s(findstr_path, MAX_PATH, system_directory);
    wcscat_s(findstr_path, MAX_PATH, L"\\findstr.exe");
    explicit_utf8 = axyne_utf8(explicit_path);
    findstr_utf8 = axyne_utf8(findstr_path);
    CHECK(explicit_utf8 != NULL && findstr_utf8 != NULL);

    resolved = axyne_terminal_profile_executable(explicit_utf8);
    CHECK(resolved != NULL && _stricmp(resolved, explicit_utf8) == 0);
    free(resolved);
    CHECK(axyne_terminal_profile_executable(".\\cmd.exe") == NULL);
    CHECK(axyne_terminal_profile_executable(
              "C:\\axyne-missing-terminal-profile.exe") == NULL);
    CHECK(axyne_terminal_profile_executable(
              "axyne-missing-terminal-profile-9f82") == NULL);

    path_length = GetEnvironmentVariableW(L"PATH", NULL, 0);
    CHECK(path_length != 0);
    previous_path = (wchar_t *)malloc((size_t)path_length * sizeof(wchar_t));
    CHECK(previous_path != NULL);
    CHECK(GetEnvironmentVariableW(L"PATH", previous_path, path_length) != 0);
    CHECK(SetEnvironmentVariableW(L"PATH", system_directory));
    resolved = axyne_terminal_profile_executable("findstr");
    CHECK(SetEnvironmentVariableW(L"PATH", previous_path));
    free(previous_path);
    CHECK(resolved != NULL && _stricmp(resolved, findstr_utf8) == 0);
    free(resolved);
    free(findstr_utf8);

    strcpy_s(state->preferences.terminal.default_profile,
             sizeof(state->preferences.terminal.default_profile), explicit_utf8);
    CHECK(axyne_terminal_apply_profile(state, &error));
    CHECK(state->terminal_runner.executable != NULL &&
          _stricmp(state->terminal_runner.executable, explicit_utf8) == 0);
    previous_runner = _strdup(state->terminal_runner.executable);
    CHECK(previous_runner != NULL);

    strcpy_s(state->preferences.terminal.default_profile,
             sizeof(state->preferences.terminal.default_profile), "findstr");
    axyne_terminal_test_fail_wide_conversion = 1;
    CHECK(!axyne_terminal_apply_profile(state, &error));
    CHECK(error.code == AXYNE_STATUS_NOT_FOUND);
    axyne_terminal_test_fail_wide_conversion = 0;
    CHECK(_stricmp(state->terminal_runner.executable, previous_runner) == 0);

    strcpy_s(state->preferences.terminal.default_profile,
             sizeof(state->preferences.terminal.default_profile), explicit_utf8);
    axyne_terminal_test_fail_utf8_conversion = 1;
    CHECK(!axyne_terminal_apply_profile(state, &error));
    CHECK(error.code == AXYNE_STATUS_NOT_FOUND);
    axyne_terminal_test_fail_utf8_conversion = 0;
    CHECK(_stricmp(state->terminal_runner.executable, previous_runner) == 0);

    state->preferences.terminal.default_profile[0] = '\0';
    axyne_terminal_test_fail_utf8_conversion = 1;
    CHECK(!axyne_terminal_apply_profile(state, &error));
    CHECK(error.code == AXYNE_STATUS_OUT_OF_MEMORY);
    axyne_terminal_test_fail_utf8_conversion = 0;
    CHECK(_stricmp(state->terminal_runner.executable, previous_runner) == 0);

    strcpy_s(state->preferences.terminal.default_profile,
             sizeof(state->preferences.terminal.default_profile),
             "axyne-missing-terminal-profile-9f82");
    CHECK(!axyne_terminal_apply_profile(state, &error));
    CHECK(error.code == AXYNE_STATUS_NOT_FOUND);
    CHECK(_stricmp(state->terminal_runner.executable, previous_runner) == 0);

    strcpy_s(state->preferences.terminal.default_profile,
             sizeof(state->preferences.terminal.default_profile), "cmd");
    axyne_terminal_configure_runner = test_terminal_configure_failure;
    CHECK(!axyne_terminal_apply_profile(state, &error));
    CHECK(error.code == AXYNE_STATUS_OUT_OF_MEMORY);
    CHECK(_stricmp(state->terminal_runner.executable, previous_runner) == 0);
    axyne_terminal_configure_runner = axyne_runner_configure;

    state->preferences.terminal.default_profile[0] = '\0';
    CHECK(axyne_terminal_apply_profile(state, &error));
    CHECK(state->terminal_runner.executable != NULL &&
          _stricmp(state->terminal_runner.executable, explicit_utf8) == 0);
    free(previous_runner);
    free(explicit_utf8);
    return EXIT_SUCCESS;
}

int main(void)
{
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = axyne_window_proc;
    window_class.lpszClassName = AXYNE_WINDOW_CLASS;
    CHECK(RegisterClassExW(&window_class) != 0);
    AxyneWindowState *state = (AxyneWindowState *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    CHECK(state != NULL);
    CHECK(axyne_explorer_initialize(&state->explorer, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_documents_initialize(&state->documents, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_documents_new(&state->documents, NULL, NULL) == AXYNE_STATUS_OK);
    const char bytes[] = "before-window\r\nbytes\0tail";
    CHECK(axyne_documents_set_contents(&state->documents, 1, bytes,
        sizeof(bytes) - 1, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_documents_mark_clean(&state->documents, 1, NULL) == AXYNE_STATUS_OK);
    axyne_preferences_defaults(&state->preferences);
    state->preferences.editor.tab_width = 6;
    state->preferences.editor.insert_spaces = 1;
    /* Hidden window; no message loop or desktop interaction is needed. */
    HWND window = CreateWindowExW(0, AXYNE_WINDOW_CLASS, L"Axyne editor test",
        WS_OVERLAPPEDWINDOW, 0, 0, 1440, 900, NULL, NULL, instance, state);
    CHECK(window != NULL && state->editor != NULL);
    CHECK(test_terminal_profiles(state) == EXIT_SUCCESS);
    CHECK(state->scintilla_module != NULL && state->lexilla_module != NULL);
    CHECK(state->create_lexer != NULL);
    CHECK(editor_equals(state->editor, bytes, sizeof(bytes) - 1));
    CHECK(SendMessageA(state->editor, SCI_GETINDENT, 0, 0) == 6);
    CHECK(SendMessageA(state->editor, SCI_GETTABWIDTH, 0, 0) == 6);
    CHECK(SendMessageA(state->editor, SCI_GETUSETABS, 0, 0) == 0);
    CHECK(state->documents.documents[0].native_editor_document == NULL);
    document_references = 1; /* The first owned buffer predates the hook. */
    production_editor_proc = (WNDPROC)SetWindowLongPtrW(state->editor,
        GWLP_WNDPROC, (LONG_PTR)test_editor_proc);
    CHECK(production_editor_proc != NULL);
    void *current = state->documents.documents[1].native_editor_document;
    for (int failure = FAULT_CREATE; failure <= FAULT_BIND; ++failure) {
        fault = failure;
        axyne_close_tab(window, state, 1);
        fault = FAULT_NONE;
        CHECK(state->documents.count == 2 && state->documents.active_index == 1);
        CHECK(state->documents.documents[0].native_editor_document == NULL);
        CHECK((void *)(uintptr_t)SendMessageA(state->editor, SCI_GETDOCPOINTER, 0, 0)
            == current);
        CHECK(editor_equals(state->editor, bytes, sizeof(bytes) - 1));
        CHECK(document_references == 1);
    }
    CHECK(axyne_show_document(state, 0));
    CHECK(document_references == 2);
    fault = FAULT_BIND;
    CHECK(!axyne_show_document(state, 1));
    CHECK(state->documents.active_index == 0 && editor_equals(state->editor, "", 0));
    CHECK(document_references == 2);
    CHECK(axyne_show_document(state, 1));
    CHECK(editor_equals(state->editor, bytes, sizeof(bytes) - 1));
    CHECK(DestroyWindow(window));
    /* The child hook sees releases before the child is destroyed. */
    CHECK(references_at_child_destroy == 0 && document_references == 0);
    /* Normal startup has no document: binding preloaded contents must not
     * reintroduce an Untitled tab for the empty workspace. */
    state = (AxyneWindowState *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    CHECK(state != NULL);
    CHECK(axyne_explorer_initialize(&state->explorer, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_documents_initialize_empty(&state->documents, NULL) == AXYNE_STATUS_OK);
    axyne_preferences_defaults(&state->preferences);
    window = CreateWindowExW(0, AXYNE_WINDOW_CLASS, L"Axyne empty editor test",
        WS_OVERLAPPEDWINDOW, 0, 0, 1440, 900, NULL, NULL, instance, state);
    CHECK(window != NULL && state->editor != NULL);
    CHECK(state->documents.count == 0);
    CHECK(editor_equals(state->editor, "", 0));
    CHECK(DestroyWindow(window));
    CHECK(UnregisterClassW(AXYNE_WINDOW_CLASS, instance));
    puts("Native DLLs, exact bytes, preferences, binding/close rollback and live-child reference cleanup passed");
    return EXIT_SUCCESS;
}
