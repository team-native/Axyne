#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "axyne/lsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void test_sleep(void) { Sleep(10); }
static volatile LONG diagnostics_seen;
static volatile LONG results_seen;
#define ATOMIC_SET(v) InterlockedExchange(&(v), 1)
#define ATOMIC_GET(v) InterlockedCompareExchange(&(v), 0, 0)
#define ATOMIC_ADD(v) InterlockedIncrement(&(v))
#else
#include <time.h>
static void test_sleep(void)
{
    const struct timespec delay = {0, 10 * 1000 * 1000};
    (void)nanosleep(&delay, NULL);
}
static volatile int diagnostics_seen;
static volatile int results_seen;
#define ATOMIC_SET(v) __sync_lock_test_and_set(&(v), 1)
#define ATOMIC_GET(v) __sync_fetch_and_add(&(v), 0)
#define ATOMIC_ADD(v) __sync_add_and_fetch(&(v), 1)
#endif

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static AxyneLspResult *latest;

static char *copy_string(const char *value)
{
    size_t length = strlen(value) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

static void diagnostics_received(AxyneLspClient *client, const char *path,
                                 const AxyneLspDiagnostic *items, size_t count,
                                 void *user_data)
{
    (void)client; (void)path; (void)items; (void)user_data;
    if (count == 2) ATOMIC_SET(diagnostics_seen);
}

static void result_received(AxyneLspClient *client, AxyneLspResult *result, void *user_data)
{
    (void)client; (void)user_data;
    result->detached = 1;
    latest = result;
    ATOMIC_ADD(results_seen);
}

/* Waits for the next result and returns it (caller frees). */
static AxyneLspResult *wait_result(int expected_count)
{
    AxyneLspResult *result;
    for (int i = 0; i < 1000; ++i) {
        if (ATOMIC_GET(results_seen) >= expected_count) break;
        test_sleep();
    }
    if (ATOMIC_GET(results_seen) < expected_count) return NULL;
    result = latest;
    latest = NULL;
    return result;
}

static int run(AxyneLspClient *client, AxyneDocument *document)
{
    AxyneError error;
    AxyneLspResult *result;
    uint64_t id = 0;
    AxyneLspPosition position = { 0, 8 };
    AxyneLspRange range = { { 0, 0 }, { 0, 3 } };
    int expected = 0;

    /* hover: markdown contents and range */
    CHECK(axyne_lsp_hover(client, document, position, &id, &error) == AXYNE_STATUS_OK && id != 0);
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_HOVER && !result->failed);
    CHECK(result->request_id == id && result->has_range && result->range.start.character == 6);
    CHECK(result->text != NULL && strstr(result->text, "const version: string") != NULL);
    {
        char *plain = axyne_lsp_markdown_to_plain(result->text);
        CHECK(plain != NULL && strcmp(plain, "const version: string\nNode version") == 0);
        free(plain);
    }
    axyne_lsp_result_free(result);

    /* signature help: active signature picked, parameter ranges in bytes */
    CHECK(axyne_lsp_signature_help(client, document, position, &id, &error) == AXYNE_STATUS_OK);
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_SIGNATURE && result->has_signature);
    CHECK(strcmp(result->signature.label, "f(a: number, b: string)") == 0);
    CHECK(result->signature_count == 2 && result->signature.parameter_count == 2);
    CHECK(result->signature.parameter_starts[0] == 2 && result->signature.parameter_ends[0] == 11);
    CHECK(result->signature.parameter_starts[1] == 13 && result->signature.parameter_ends[1] == 22);
    CHECK(result->signature.has_active_parameter && result->signature.active_parameter == 1);
    CHECK(result->signature.documentation != NULL &&
          strcmp(result->signature.documentation, "Does f") == 0);
    axyne_lsp_result_free(result);

    /* rename: workspace edit, then a server error */
    CHECK(axyne_lsp_rename(client, document, position, "renamed", &id, &error) == AXYNE_STATUS_OK);
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_RENAME && result->has_workspace_edit);
    CHECK(result->workspace_edit.file_count == 1 && result->workspace_edit.files[0].edit_count == 1);
    CHECK(strcmp(result->workspace_edit.files[0].edits[0].new_text, "renamed") == 0);
    axyne_lsp_result_free(result);
    CHECK(axyne_lsp_rename(client, document, position, "bad name", &id, &error) == AXYNE_STATUS_OK);
    result = wait_result(++expected);
    CHECK(result != NULL && result->failed && result->error_message != NULL &&
          strcmp(result->error_message, "Cannot rename") == 0);
    axyne_lsp_result_free(result);
    CHECK(axyne_lsp_rename(client, document, position, "", &id, &error) ==
          AXYNE_STATUS_INVALID_ARGUMENT);

    /* code actions: overlapping diagnostics sent as context (numeric code kept) */
    for (int i = 0; i < 1000 && !ATOMIC_GET(diagnostics_seen); ++i) test_sleep();
    CHECK(ATOMIC_GET(diagnostics_seen));
    CHECK(axyne_lsp_code_actions(client, document, range, &id, &error) == AXYNE_STATUS_OK);
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_CODE_ACTIONS && result->action_count == 3);
    CHECK(strcmp(result->actions[0].title, "Add import") == 0 && result->actions[0].preferred);
    CHECK(result->actions[0].has_edit && result->actions[0].edit.file_count == 1);
    CHECK(strcmp(result->actions[0].kind, "quickfix") == 0);
    CHECK(!result->actions[1].has_edit && result->actions[1].raw_json != NULL);
    CHECK(result->actions[2].has_command && !result->actions[2].has_edit);
    {
        char *raw = copy_string(result->actions[1].raw_json);
        axyne_lsp_result_free(result);
        CHECK(axyne_lsp_resolve_code_action(client, raw, &id, &error) == AXYNE_STATUS_OK);
        free(raw);
    }
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_RESOLVED_ACTION &&
          result->action_count == 1 && result->actions[0].has_edit);
    CHECK(strcmp(result->actions[0].edit.files[0].edits[0].new_text, "R") == 0);
    axyne_lsp_result_free(result);

    /* formatting: options forwarded */
    CHECK(axyne_lsp_format(client, document, 4, 1, &id, &error) == AXYNE_STATUS_OK);
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_FORMAT && result->edit_count == 2);
    CHECK(strcmp(result->edits[0].new_text, "  ") == 0 && strcmp(result->edits[1].new_text, "4") == 0);
    axyne_lsp_result_free(result);

    /* document symbols: hierarchy flattened and sorted by position */
    CHECK(axyne_lsp_document_symbols(client, document, &id, &error) == AXYNE_STATUS_OK);
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_SYMBOLS && result->symbol_count == 3);
    CHECK(strcmp(result->symbols[0].name, "top") == 0 && result->symbols[0].kind == 12 &&
          strcmp(result->symbols[0].container, "mod") == 0);
    CHECK(strcmp(result->symbols[1].name, "Foo") == 0 && result->symbols[1].kind == 5 &&
          result->symbols[1].position.line == 1 && result->symbols[1].position.character == 6);
    CHECK(strcmp(result->symbols[2].name, "bar") == 0 && result->symbols[2].container != NULL &&
          strcmp(result->symbols[2].container, "Foo") == 0);
    axyne_lsp_result_free(result);

    /* completion resolve */
    CHECK(axyne_lsp_resolve_completion(client, "{\"label\":\"x\",\"kind\":6}", &id, &error) ==
          AXYNE_STATUS_OK);
    result = wait_result(++expected);
    CHECK(result != NULL && result->kind == AXYNE_LSP_RESULT_RESOLVED_COMPLETION &&
          result->has_completion_item);
    CHECK(strcmp(result->completion_item.documentation, "resolved docs") == 0);
    CHECK(result->completion_item.additional_edit_count == 1);
    axyne_lsp_result_free(result);
    CHECK(axyne_lsp_resolve_completion(client, "not json", &id, &error) ==
          AXYNE_STATUS_INVALID_ARGUMENT);

    /* didSave is a notification: it must simply succeed */
    CHECK(axyne_lsp_did_save(client, document, &error) == AXYNE_STATUS_OK);
    return 1;
}

int main(int argc, char **argv)
{
    const char *arguments[2];
    AxyneLspConfig config = {0};
    AxyneLspClient *client = NULL;
    AxyneDocument document = {0};
    AxyneError error;
    char path[1024];
    int ok;
    if (argc != 4) return 2;
    if (snprintf(path, sizeof(path), "%s/probe.js", argv[3]) < 0) return 2;
    arguments[0] = argv[2];
    arguments[1] = "--server";
    config.command = argv[1];
    config.arguments = arguments;
    config.argument_count = 2;
    config.root_path = argv[3];
    config.language_id = "javascript";
    config.on_diagnostics = diagnostics_received;
    config.on_result = result_received;
    if (axyne_lsp_create(&config, &client, &error) != AXYNE_STATUS_OK || client == NULL) return 3;
    document.path = path;
    document.contents = "const version = 1;\nfoo\n";
    document.length = strlen(document.contents);
    if (axyne_lsp_did_open(client, &document, &error) != AXYNE_STATUS_OK) {
        axyne_lsp_destroy(client);
        return 4;
    }
    ok = run(client, &document);
    axyne_lsp_destroy(client);
    return ok ? 0 : 1;
}
