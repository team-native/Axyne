#include "test_support.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "axyne/document.h"
#include "axyne/git.h"
#include "axyne/lsp.h"
#include "axyne/process.h"
#include "axyne/runner.h"

typedef struct AxyneTestProcessResult {
    int exit_code;
    atomic_int done;
} AxyneTestProcessResult;

static void axyne_test_process_exit(AxyneProcess *process, int exit_code,
                                    void *user_data)
{
    AxyneTestProcessResult *result = (AxyneTestProcessResult *)user_data;
    (void)process;
    result->exit_code = exit_code;
    atomic_store_explicit(&result->done, 1, memory_order_release);
}

#ifdef _WIN32
static char *axyne_test_find_git(void)
{
    wchar_t *wide_path = NULL;
    DWORD capacity = MAX_PATH;
    DWORD length;
    int utf8_length;
    char *utf8_path;

    for (;;) {
        wide_path = (wchar_t *)malloc((size_t)capacity * sizeof(*wide_path));
        if (wide_path == NULL) return NULL;
        length = SearchPathW(NULL, L"git.exe", NULL, capacity, wide_path,
                             NULL);
        if (length == 0) {
            free(wide_path);
            return NULL;
        }
        if (length < capacity) break;
        free(wide_path);
        capacity = length + 1;
    }
    utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                      wide_path, -1, NULL, 0, NULL, NULL);
    if (utf8_length <= 0) {
        free(wide_path);
        return NULL;
    }
    utf8_path = (char *)malloc((size_t)utf8_length);
    if (utf8_path == NULL || WideCharToMultiByte(
                                 CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, -1,
                                 utf8_path, utf8_length, NULL, NULL) <= 0) {
        free(utf8_path);
        free(wide_path);
        return NULL;
    }
    free(wide_path);
    return utf8_path;
}
#endif

static int axyne_test_git_command(const char *root, const char *const *arguments,
                                  size_t argument_count)
{
    AxyneProcessSpec spec = {0};
    AxyneProcess *process = NULL;
    AxyneTestProcessResult result;
    AxyneError error = {0};
    AxyneStatus status;
#ifdef _WIN32
    char *git_executable = axyne_test_find_git();
    if (git_executable == NULL) {
        fprintf(stderr, "git.exe was not found on PATH\n");
        return 0;
    }
    spec.executable = git_executable;
#else
    const char *git_executable = "git";
    spec.executable = git_executable;
#endif

    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = root;
    spec.on_exit = axyne_test_process_exit;
    spec.user_data = &result;
    result.exit_code = -1;
    atomic_init(&result.done, 0);
    status = axyne_process_start(&spec, &process, &error);
#ifdef _WIN32
    free(git_executable);
#endif
    if (status != AXYNE_STATUS_OK) {
        fprintf(stderr, "git fixture process failed: %d %s\n", (int)status,
                error.message);
        return 0;
    }
    while (atomic_load_explicit(&result.done, memory_order_acquire) == 0) {
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay = {0, 1000000L};
        (void)nanosleep(&delay, NULL);
#endif
    }
    axyne_process_release(process);
    if (result.exit_code != 0)
        fprintf(stderr, "git fixture command exited with %d\n", result.exit_code);
    return result.exit_code == 0;
}

int axyne_test_runner_git_lsp(const char *root, const char *source_root)
{
    char tracked_path[512];
    const char *arguments[] = {"--flag"};
    const char *environment[] = {"Axyne_Test=one"};
    const char *duplicate_environment[] = {"PATH=one", "path=two"};
    const char *lsp_arguments[] = {"--stdio"};
    const char *lsp_environment[] = {"Axyne_Lsp_Test=one"};
    AxyneRunnerConfig runner = {0};
    AxyneRunnerSpec runner_spec = {0};
    AxyneProcessSpec process_spec = {0};
    AxyneLspClient *client = NULL;
    AxyneLspConfig lsp_config = {0};
    AxyneGitResult git_result = {0};
    AxyneError error = {0};
    char utf8_line[] = "A\xF0\x9F\x98\x80" "B";

    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(tracked_path, sizeof(tracked_path), root,
                                     "tracked.txt"));

    AXYNE_TEST_STATUS(axyne_runner_initialize(&runner, &error), AXYNE_STATUS_OK);
    runner_spec.executable = "echo";
    runner_spec.arguments = arguments;
    runner_spec.argument_count = 1;
    runner_spec.working_directory = root;
    runner_spec.environment = environment;
    runner_spec.environment_count = 1;
    runner_spec.has_runtime = 1;
    runner_spec.runtime_kind = AXYNE_RUNTIME_C;
    AXYNE_TEST_STATUS(axyne_runner_configure(&runner, &runner_spec, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(runner.executable, "echo") == 0 &&
                     strcmp(runner.arguments[0], "--flag") == 0 &&
                     strcmp(runner.environment[0], "Axyne_Test=one") == 0);
    AXYNE_TEST_STATUS(axyne_runner_process_spec(&runner, NULL, NULL, NULL,
                                                &process_spec, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(process_spec.argument_count == 1 &&
                     process_spec.environment_count == 1 &&
                     process_spec.arguments[0] == runner.arguments[0]);
    runner_spec.environment = duplicate_environment;
    runner_spec.environment_count = 2;
    AXYNE_TEST_STATUS(axyne_runner_configure(&runner, &runner_spec, &error),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    runner_spec.environment = NULL;
    runner_spec.environment_count = 1;
    AXYNE_TEST_STATUS(axyne_runner_configure(&runner, &runner_spec, &error),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_runner_destroy(&runner);

    {
        static const char *const git_init[] = {"init", "-q", "."};
        AXYNE_TEST_CHECK(axyne_test_git_command(
            root, git_init, sizeof(git_init) / sizeof(git_init[0])));
    }
    AXYNE_TEST_CHECK(axyne_test_write(tracked_path, "tracked\n"));
    AXYNE_TEST_STATUS(axyne_git_status(root, &git_result, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strstr(git_result.output, "tracked.txt") != NULL);
    axyne_git_result_free(&git_result);
    AXYNE_TEST_STATUS(axyne_git_stage_all(root, &git_result, &error),
                      AXYNE_STATUS_OK);
    axyne_git_result_free(&git_result);
    AXYNE_TEST_STATUS(axyne_git_status(root, &git_result, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strstr(git_result.output, "A  tracked.txt") != NULL);
    axyne_git_result_free(&git_result);
    AXYNE_TEST_STATUS(axyne_git_diff(root, &git_result, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(git_result.length == 0);
    axyne_git_result_free(&git_result);

    {
        static const char *const git_commit[] = {
            "-c", "user.name=Axyne Tests", "-c", "user.email=tests@example.invalid",
            "commit", "-m", "fixture"
        };
        AXYNE_TEST_CHECK(axyne_test_git_command(
            root, git_commit, sizeof(git_commit) / sizeof(git_commit[0])));
    }
    AXYNE_TEST_CHECK(axyne_test_write(tracked_path, "changed\n"));
    AXYNE_TEST_STATUS(axyne_git_stage_all(root, &git_result, &error),
                      AXYNE_STATUS_OK);
    axyne_git_result_free(&git_result);
    AXYNE_TEST_STATUS(axyne_git_unstage_all(root, &git_result, &error),
                      AXYNE_STATUS_OK);
    axyne_git_result_free(&git_result);
    AXYNE_TEST_STATUS(axyne_git_status(root, &git_result, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strstr(git_result.output, " M tracked.txt") != NULL);
    axyne_git_result_free(&git_result);

    AXYNE_TEST_CHECK(axyne_lsp_utf16_character(utf8_line, 6, 0) == 0);
    AXYNE_TEST_CHECK(axyne_lsp_utf16_character(utf8_line, 6, 1) == 1);
    AXYNE_TEST_CHECK(axyne_lsp_utf16_character(utf8_line, 6, 5) == 3);
    AXYNE_TEST_CHECK(axyne_lsp_utf16_character(utf8_line, 6, 99) == 4);
    AXYNE_TEST_CHECK(axyne_lsp_utf16_character("\xF0\x28", 2, 2) == 2);

    lsp_config.command = "language-server";
    lsp_config.arguments = lsp_arguments;
    lsp_config.argument_count = 1;
    lsp_config.environment = lsp_environment;
    lsp_config.environment_count = 1;
    lsp_config.root_path = source_root;
    lsp_config.language_id = "c";
    lsp_config.initialization_options_json = "{\"trace\":\"off\"}";
    AXYNE_TEST_STATUS(axyne_lsp_create(&lsp_config, &client, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(client != NULL);
    axyne_lsp_destroy(client);
    client = NULL;
    lsp_config.initialization_options_json = "[]";
    AXYNE_TEST_STATUS(axyne_lsp_create(&lsp_config, &client, &error),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(client == NULL);
    lsp_config.initialization_options_json = "{}";
    lsp_config.arguments = NULL;
    lsp_config.argument_count = 1;
    AXYNE_TEST_STATUS(axyne_lsp_create(&lsp_config, &client, &error),
                      AXYNE_STATUS_INVALID_ARGUMENT);

    axyne_test_remove_tree(root);
    return 1;
}
