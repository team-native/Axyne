#ifndef AXYNE_TEST_SUPPORT_H
#define AXYNE_TEST_SUPPORT_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/filesystem.h"
#include "axyne/status.h"

#ifdef _WIN32
#define AXYNE_TEST_SEPARATOR '\\'
#else
#define AXYNE_TEST_SEPARATOR '/'
#endif

static int axyne_test_path(char *destination, size_t capacity,
                           const char *directory, const char *name)
{
    int written = snprintf(destination, capacity, "%s%c%s", directory,
                            AXYNE_TEST_SEPARATOR, name);
    return written >= 0 && (size_t)written < capacity;
}

static int axyne_test_make_directory(const char *path)
{
    return axyne_fs_create_directory(path, NULL) == AXYNE_STATUS_OK;
}

static inline int axyne_test_write(const char *path, const char *contents)
{
    return axyne_fs_write_file(path, contents, strlen(contents), NULL) ==
           AXYNE_STATUS_OK;
}

static inline int axyne_test_read_equals(const char *path, const char *expected)
{
    char *contents = NULL;
    size_t length = 0;
    int matches;

    if (axyne_fs_read_file(path, &contents, &length, NULL) != AXYNE_STATUS_OK)
        return 0;
    matches = length == strlen(expected) && memcmp(contents, expected, length) == 0;
    axyne_fs_free(contents);
    return matches;
}

static void axyne_test_remove_file(const char *path)
{
    (void)axyne_fs_remove(path, NULL);
}

static void axyne_test_remove_directory(const char *path)
{
    (void)axyne_fs_remove(path, NULL);
}

static inline void axyne_test_remove_tree(const char *path)
{
    AxyneDirectoryList list = {0};

    if (axyne_fs_list_directory(path, &list, NULL) == AXYNE_STATUS_OK) {
        for (size_t i = 0; i < list.count; ++i) {
            if (list.entries[i].kind == AXYNE_FILE_KIND_DIRECTORY)
                axyne_test_remove_tree(list.entries[i].path);
            else
                axyne_test_remove_file(list.entries[i].path);
        }
        axyne_fs_free_directory_list(&list);
    }
    axyne_test_remove_directory(path);
}

static char axyne_test_cleanup_path[1024];

static void axyne_test_cleanup_at_exit(void)
{
    if (axyne_test_cleanup_path[0] != '\0')
        axyne_test_remove_tree(axyne_test_cleanup_path);
}

static inline int axyne_test_register_cleanup(const char *path)
{
    int written = snprintf(axyne_test_cleanup_path,
                           sizeof(axyne_test_cleanup_path), "%s", path);
    return written >= 0 && (size_t)written < sizeof(axyne_test_cleanup_path) &&
           atexit(axyne_test_cleanup_at_exit) == 0;
}

#define AXYNE_TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

#define AXYNE_TEST_STATUS(expression, expected) \
    do { \
        AxyneStatus axyne_test_status = (expression); \
        if (axyne_test_status != (expected)) { \
            fprintf(stderr, "FAIL %s:%d: status %d, expected %d\n", \
                    __FILE__, __LINE__, (int)axyne_test_status, (int)(expected)); \
            return 0; \
        } \
    } while (0)

/* Integer comparison that reports both values, so CI logs show the cause. */
#define AXYNE_TEST_EQ_INT(actual, expected) \
    do { \
        long long axyne_test_actual = (long long)(actual); \
        long long axyne_test_expected = (long long)(expected); \
        if (axyne_test_actual != axyne_test_expected) { \
            fprintf(stderr, "FAIL %s:%d: %s == %lld, expected %lld\n", \
                    __FILE__, __LINE__, #actual, axyne_test_actual, \
                    axyne_test_expected); \
            return 0; \
        } \
    } while (0)

/* String-contains check that reports the text that was searched. */
#define AXYNE_TEST_CONTAINS(haystack, needle) \
    do { \
        const char *axyne_test_text = (haystack); \
        if (axyne_test_text == NULL || strstr(axyne_test_text, (needle)) == NULL) { \
            fprintf(stderr, "FAIL %s:%d: %s lacks \"%s\"; actual: \"%s\"\n", \
                    __FILE__, __LINE__, #haystack, (needle), \
                    axyne_test_text != NULL ? axyne_test_text : "(null)"); \
            return 0; \
        } \
    } while (0)

#endif
