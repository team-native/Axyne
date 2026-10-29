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
#else
#include <time.h>
static void test_sleep(void)
{
    const struct timespec delay = {0, 10 * 1000 * 1000};
    (void)nanosleep(&delay, NULL);
}
#endif

static void completion_received(AxyneLspClient *client, uint64_t request_id,
                                const AxyneLspCompletionItem *items,
                                size_t item_count, void *user_data)
{
    const char *marker = (const char *)user_data;
    FILE *file;
    (void)client;
    file = fopen(marker, "wb");
    if (file == NULL) return;
    if (request_id != 0 && item_count == 1 && items != NULL &&
        items[0].label != NULL && strcmp(items[0].label, "probeCompletion") == 0)
        fputs("ok", file);
    else
        fputs("bad", file);
    fclose(file);
}

static int marker_is_ready(const char *path)
{
    FILE *file = fopen(path, "rb");
    char result[4] = {0};
    int ok;
    if (file == NULL) return 0;
    (void)fread(result, 1, sizeof(result) - 1, file);
    fclose(file);
    ok = strcmp(result, "ok") == 0;
    if (!ok && strcmp(result, "bad") == 0) return -1;
    return ok;
}

int main(int argc, char **argv)
{
    const char *arguments[2];
    AxyneLspConfig config = {0};
    AxyneLspClient *client = NULL;
    AxyneDocument document = {0};
    AxyneError error;
    AxyneLspPosition position = {0, 7};
    uint64_t request_id = 0;
    char path[1024], marker[1100];
    int ready = 0;

    if (argc != 4) return 2;
    if (snprintf(path, sizeof(path), "%s/probe.c", argv[3]) < 0 ||
        snprintf(marker, sizeof(marker), "%s/result.txt", argv[3]) < 0)
        return 2;
    (void)remove(marker);
    arguments[0] = argv[2];
    arguments[1] = "--server";
    config.command = argv[1];
    config.arguments = arguments;
    config.argument_count = 2;
    config.root_path = argv[3];
    config.language_id = "c";
    config.on_completion = completion_received;
    config.user_data = marker;
    if (axyne_lsp_create(&config, &client, &error) != AXYNE_STATUS_OK ||
        client == NULL)
        return 3;

    document.path = path;
    document.contents = "int item;";
    document.length = strlen(document.contents);
    if (axyne_lsp_did_open(client, &document, &error) != AXYNE_STATUS_OK ||
        axyne_lsp_completion(client, &document, position, &request_id, &error) !=
            AXYNE_STATUS_OK || request_id == 0) {
        axyne_lsp_destroy(client);
        return 4;
    }

    for (int i = 0; i < 500; ++i) {
        ready = marker_is_ready(marker);
        if (ready != 0) break;
        test_sleep();
    }
    axyne_lsp_destroy(client);
    (void)remove(marker);
    return ready == 1 ? 0 : 5;
}
