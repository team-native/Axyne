#include "test_support.h"

#include <string.h>

#include "axyne/log.h"

#ifndef _WIN32
#include <pthread.h>
#endif

static int read_text(const char *path, char **text, size_t *length)
{
    return axyne_fs_read_file(path, text, length, NULL) == AXYNE_STATUS_OK;
}

static size_t count_lines(const char *text, size_t length)
{
    size_t lines = 0;
    for (size_t i = 0; i < length; ++i) if (text[i] == '\n') ++lines;
    return lines;
}

#ifndef _WIN32
static void *log_worker(void *argument)
{
    int id = *(const int *)argument;
    for (int i = 0; i < 200; ++i) axyne_log_info("thread", "worker %d line %d", id, i);
    return NULL;
}
#endif

int axyne_test_log(const char *root)
{
    char nested[512], directory[512], file[512], rotated[512];
    char *text = NULL;
    size_t length = 0;
    AxyneDirectoryList list = {0};

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(nested, sizeof(nested), root, "nested"));
    AXYNE_TEST_CHECK(axyne_test_path(directory, sizeof(directory), nested, "logs"));
    AXYNE_TEST_CHECK(axyne_test_path(file, sizeof(file), directory, "axyne.log"));
    AXYNE_TEST_CHECK(axyne_test_path(rotated, sizeof(rotated), directory, "axyne.log.1"));

    /* Lazy: nothing exists before the first entry. */
    axyne_log_set_directory(directory);
    AXYNE_TEST_CHECK(axyne_fs_list_directory(nested, &list, NULL) != AXYNE_STATUS_OK);

    axyne_log_error("lsp", "clangd exited with code %d", 1);
    axyne_log_warn(NULL, "two\nlines\r\nhere");
    axyne_log_info("settings", "%s", "\xed\x95\x9c\xea\xb8\x80 ok");
    {
        AxyneDirectoryList log_directory = {0};
        AxyneError directory_error = {0};
        AxyneStatus directory_status = axyne_fs_list_directory(
            directory, &log_directory, &directory_error);
        if (directory_status != AXYNE_STATUS_OK) {
            fprintf(stderr, "log directory missing: path=%s status=%d message=%s\n",
                    directory, (int)directory_status, directory_error.message);
        } else {
            axyne_fs_free_directory_list(&log_directory);
        }
    }
    {
        AxyneError read_error = {0};
        AxyneStatus read_status = axyne_fs_read_file(file, &text, &length, &read_error);
        if (read_status != AXYNE_STATUS_OK) {
            fprintf(stderr, "log read failed: path=%s status=%d message=%s\\n",
                    file, (int)read_status, read_error.message);
            return 0;
        }
    }
    AXYNE_TEST_EQ_INT(count_lines(text, length), 3);
    AXYNE_TEST_CONTAINS(text, " ERROR [lsp] clangd exited with code 1\n");
    AXYNE_TEST_CONTAINS(text, " WARN [app] two lines  here\n");
    AXYNE_TEST_CONTAINS(text, " INFO [settings] \xed\x95\x9c\xea\xb8\x80 ok\n");
    AXYNE_TEST_CHECK(length > 24 && text[4] == '-' && text[10] == 'T' && text[23] == 'Z');
    axyne_fs_free(text);
    text = NULL;

    /* Long messages are truncated to one line. */
    {
        char big[4000];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        axyne_log_info("big", "%s", big);
        AXYNE_TEST_CHECK(read_text(file, &text, &length));
        AXYNE_TEST_EQ_INT(count_lines(text, length), 4);
        AXYNE_TEST_CHECK(length < 1200 * 2);
        axyne_fs_free(text);
        text = NULL;
    }

    /* Rotation keeps one previous file. */
    axyne_log_set_max_bytes(600);
    for (int i = 0; i < 40; ++i) axyne_log_info("rotate", "entry %02d padding padding padding", i);
    AXYNE_TEST_CHECK(read_text(file, &text, &length));
    AXYNE_TEST_CHECK(length <= 600);
    AXYNE_TEST_CONTAINS(text, "entry 39");
    axyne_fs_free(text);
    text = NULL;
    AXYNE_TEST_CHECK(read_text(rotated, &text, &length));
    AXYNE_TEST_CHECK(length <= 600 && length > 0);
    axyne_fs_free(text);
    text = NULL;
    AXYNE_TEST_STATUS(axyne_fs_list_directory(directory, &list, NULL), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(list.count, 2);
    axyne_fs_free_directory_list(&list);

    /* Reopening appends to the existing file. */
    axyne_log_set_max_bytes(0);
    axyne_log_set_directory(directory);
    axyne_test_remove_file(file);
    axyne_test_remove_file(rotated);
    axyne_log_info("a", "first");
    axyne_log_shutdown();
    axyne_log_set_directory(directory);
    axyne_log_info("a", "second");
    AXYNE_TEST_CHECK(read_text(file, &text, &length));
    AXYNE_TEST_EQ_INT(count_lines(text, length), 2);
    axyne_fs_free(text);
    text = NULL;

#ifndef _WIN32
    /* Concurrent writers never interleave or lose lines. */
    {
        pthread_t threads[4];
        int ids[4] = {0, 1, 2, 3};
        for (int i = 0; i < 4; ++i)
            AXYNE_TEST_CHECK(pthread_create(&threads[i], NULL, log_worker, &ids[i]) == 0);
        for (int i = 0; i < 4; ++i) (void)pthread_join(threads[i], NULL);
        AXYNE_TEST_CHECK(read_text(file, &text, &length));
        AXYNE_TEST_EQ_INT(count_lines(text, length), 2 + 4 * 200);
        AXYNE_TEST_CONTAINS(text, "worker 3 line 199\n");
        axyne_fs_free(text);
        text = NULL;
    }
#endif

    /* A rotation that cannot rename (axyne.log.1 is a non-empty directory)
     * stops writing instead of growing past the limit. */
    {
        char blocker[512];
        axyne_log_shutdown();
        axyne_test_remove_file(file);
        AXYNE_TEST_CHECK(axyne_test_make_directory(rotated));
        AXYNE_TEST_CHECK(axyne_test_path(blocker, sizeof(blocker), rotated, "keep"));
        AXYNE_TEST_CHECK(axyne_test_write(blocker, "x"));
        axyne_log_set_directory(directory);
        axyne_log_set_max_bytes(400);
        for (int i = 0; i < 60; ++i) axyne_log_info("rotate", "blocked %02d padding padding", i);
        AXYNE_TEST_CHECK(read_text(file, &text, &length));
        AXYNE_TEST_CHECK(length > 0 && length <= 400);
        axyne_fs_free(text);
        text = NULL;
        axyne_log_shutdown();
        axyne_test_remove_file(blocker);
        axyne_test_remove_directory(rotated);
    }

    /* An unusable directory drops entries without failing. */
    AXYNE_TEST_CHECK(axyne_test_write(rotated, "not a directory"));
    axyne_log_set_directory(rotated);
    axyne_log_error("x", "dropped");
    axyne_log_shutdown();
    axyne_test_remove_tree(root);
    return 1;
}
