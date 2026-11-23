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
static volatile LONG hover_seen;
#define MARK_SEEN() InterlockedExchange(&hover_seen, 1)
#define SEEN() InterlockedCompareExchange(&hover_seen, 0, 0)
#else
#include <time.h>
static void test_sleep(void)
{
    const struct timespec delay = {0, 10 * 1000 * 1000};
    (void)nanosleep(&delay, NULL);
}
static volatile int hover_seen;
#define MARK_SEEN() __sync_lock_test_and_set(&hover_seen, 1)
#define SEEN() __sync_fetch_and_add(&hover_seen, 0)
#endif

static void result_received(AxyneLspClient *client, AxyneLspResult *result, void *user_data)
{
    (void)client; (void)user_data;
    if (result->kind == AXYNE_LSP_RESULT_HOVER && !result->failed && result->text != NULL &&
        strcmp(result->text, "flood survived") == 0)
        MARK_SEEN();
}

/* Regression: a server that floods stdout while the client writes large
 * didChange messages used to deadlock (the writer held the client mutex the
 * reader needed). The writes now go through their own thread. */
int main(int argc, char **argv)
{
    const char *arguments[2];
    AxyneLspConfig config = {0};
    AxyneLspClient *client = NULL;
    AxyneDocument document = {0};
    AxyneError error;
    AxyneLspPosition position = {0, 1};
    uint64_t id;
    char path[1024];
    size_t size = 300 * 1024;
    char *text;
    if (argc != 4) return 2;
    if (snprintf(path, sizeof(path), "%s/flood.js", argv[3]) < 0) return 2;
    arguments[0] = argv[2];
    arguments[1] = "--server";
    config.command = argv[1];
    config.arguments = arguments;
    config.argument_count = 2;
    config.root_path = argv[3];
    config.language_id = "javascript";
    config.on_result = result_received;
    if (axyne_lsp_create(&config, &client, &error) != AXYNE_STATUS_OK) return 3;
    text = (char *)malloc(size + 1);
    if (text == NULL) return 3;
    memset(text, 'a', size);
    text[size] = '\0';
    document.path = path;
    document.contents = text;
    document.length = size;
    if (axyne_lsp_did_open(client, &document, &error) != AXYNE_STATUS_OK) return 4;
    for (int i = 0; i < 15; ++i) {
        text[i % 100] = (char)('a' + i % 26); /* a different document every time */
        if (axyne_lsp_did_change(client, &document, &error) != AXYNE_STATUS_OK) return 5;
    }
    if (axyne_lsp_hover(client, &document, position, &id, &error) != AXYNE_STATUS_OK) return 6;
    for (int i = 0; i < 2500 && !SEEN(); ++i) test_sleep();
    axyne_lsp_destroy(client);
    free(text);
    return SEEN() ? 0 : 1;
}
