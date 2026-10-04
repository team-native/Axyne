#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "axyne/document.h"
#include "axyne/git.h"
#include "axyne/lsp.h"
#include "axyne/process.h"
#include "axyne/runner.h"

typedef struct AxyneTestProcessResult {
    int exit_code;
#ifdef _WIN32
    HANDLE done_event;
#else
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int done;
#endif
} AxyneTestProcessResult;

static void axyne_test_process_exit(AxyneProcess *process, int exit_code,
                                    void *user_data)
{
    AxyneTestProcessResult *result = (AxyneTestProcessResult *)user_data;
    (void)process;
#ifdef _WIN32
    result->exit_code = exit_code;
    (void)SetEvent(result->done_event);
#else
    (void)pthread_mutex_lock(&result->lock);
    result->exit_code = exit_code;
    result->done = 1;
    (void)pthread_cond_signal(&result->condition);
    (void)pthread_mutex_unlock(&result->lock);
#endif
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
#else
    const char *git_executable = "git";
#endif
#ifdef _WIN32
    result.exit_code = -1;
    result.done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (result.done_event == NULL) {
        free(git_executable);
        return 0;
    }
#else
    result.exit_code = -1;
    result.done = 0;
    if (pthread_mutex_init(&result.lock, NULL) != 0) return 0;
    if (pthread_cond_init(&result.condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&result.lock);
        return 0;
    }
#endif
    spec.executable = git_executable;

    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = root;
    spec.on_exit = axyne_test_process_exit;
    spec.user_data = &result;
    status = axyne_process_start(&spec, &process, &error);
#ifdef _WIN32
    free(git_executable);
#endif
    if (status != AXYNE_STATUS_OK) {
#ifdef _WIN32
        (void)CloseHandle(result.done_event);
#else
        (void)pthread_cond_destroy(&result.condition);
        (void)pthread_mutex_destroy(&result.lock);
#endif
        fprintf(stderr, "git fixture process failed: %d %s\n", (int)status,
                error.message);
        return 0;
    }
#ifdef _WIN32
    (void)WaitForSingleObject(result.done_event, INFINITE);
#else
    (void)pthread_mutex_lock(&result.lock);
    while (!result.done)
        (void)pthread_cond_wait(&result.condition, &result.lock);
    (void)pthread_mutex_unlock(&result.lock);
#endif
    axyne_process_release(process);
#ifdef _WIN32
    (void)CloseHandle(result.done_event);
#else
    (void)pthread_cond_destroy(&result.condition);
    (void)pthread_mutex_destroy(&result.lock);
#endif
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

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
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
    /* Appended runtime kinds are accepted; one past the last is rejected. */
    {
        AxyneRunnerConfig kinds;
        AxyneRunnerSpec kind_spec = runner_spec;
        AXYNE_TEST_STATUS(axyne_runner_initialize(&kinds, &error), AXYNE_STATUS_OK);
        kind_spec.runtime_kind = AXYNE_RUNTIME_SWIFT;
        AXYNE_TEST_STATUS(axyne_runner_configure(&kinds, &kind_spec, &error),
                          AXYNE_STATUS_OK);
        axyne_runner_destroy(&kinds);
        AXYNE_TEST_STATUS(axyne_runner_initialize(&kinds, &error), AXYNE_STATUS_OK);
        kind_spec.runtime_kind = AXYNE_RUNTIME_KIND_COUNT;
        AXYNE_TEST_STATUS(axyne_runner_configure(&kinds, &kind_spec, &error),
                          AXYNE_STATUS_INVALID_ARGUMENT);
        axyne_runner_destroy(&kinds);
    }
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
