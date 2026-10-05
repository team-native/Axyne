#include "test_support.h"

#include <string.h>

#include "axyne/explorer.h"
#include "axyne/search.h"

/* Writes `length` bytes of 'a', optionally with one NUL at `nul_at`. */
static int axyne_test_write_filled(const char *path, size_t length,
                                   long nul_at)
{
    char *data = (char *)malloc(length == 0 ? 1 : length);
    int written;
    if (data == NULL) return 0;
    memset(data, 'a', length);
    if (nul_at >= 0 && (size_t)nul_at < length) data[nul_at] = '\0';
    written = axyne_fs_write_file(path, data, length, NULL) == AXYNE_STATUS_OK;
    free(data);
    return written;
}

static int axyne_test_nul_case(const char *path, size_t length, long nul_at,
                               int expected)
{
    int contains = -1;
    AXYNE_TEST_CHECK(axyne_test_write_filled(path, length, nul_at));
    AXYNE_TEST_STATUS(axyne_fs_file_contains_nul(path, &contains, NULL),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(contains, expected);
    return 1;
}

int axyne_test_memory_limits(const char *root)
{
    char file[512], missing[512], tree[512], nested[512], text[512], binary[512];
    AxyneExplorer explorer = {0};
    AxyneSearchResults results = {0};
    AxyneError error = {0};
    int contains = 0;
    size_t baseline, i;

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(file, sizeof(file), root, "probe.bin"));
    AXYNE_TEST_CHECK(axyne_test_path(missing, sizeof(missing), root, "none"));

    /* Streaming NUL probe: the NUL may sit anywhere, including past several
     * read chunks and exactly on a chunk boundary. */
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 0, -1, 0));
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 1, 0, 1));
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 100, -1, 0));
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 100, 99, 1));
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 200000, -1, 0));
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 200000, 32 * 1024, 1));
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 200000, 32 * 1024 - 1, 1));
    AXYNE_TEST_CHECK(axyne_test_nul_case(file, 200000, 199999, 1));
    AXYNE_TEST_STATUS(axyne_fs_file_contains_nul(missing, &contains, &error),
                      AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_STATUS(axyne_fs_file_contains_nul(NULL, &contains, NULL),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_fs_file_contains_nul(file, NULL, NULL),
                      AXYNE_STATUS_INVALID_ARGUMENT);

    /* Workspace search skips a binary file whose only NUL is far from the
     * start and never lists it. */
    AXYNE_TEST_CHECK(axyne_test_path(tree, sizeof(tree), root, "tree"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(tree));
    AXYNE_TEST_CHECK(axyne_test_path(binary, sizeof(binary), tree, "late.dat"));
    AXYNE_TEST_CHECK(axyne_test_path(text, sizeof(text), tree, "note.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(text, "plain needle text\n"));
    AXYNE_TEST_CHECK(axyne_test_write_filled(binary, 100000, 90000));
    AXYNE_TEST_STATUS(axyne_search_workspace(tree, "aaa", 0, &results, &error),
                      AXYNE_STATUS_OK);
    for (i = 0; i < results.count; ++i)
        AXYNE_TEST_CHECK(strstr(results.items[i].path, "late.dat") == NULL);
    axyne_search_results_destroy(&results);

    /* Repeated explorer reloads keep the node list stable (no growth). */
    AXYNE_TEST_CHECK(axyne_test_path(nested, sizeof(nested), tree, "inner"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(nested));
    AXYNE_TEST_STATUS(axyne_explorer_initialize(&explorer, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_explorer_set_root(&explorer, tree, &error),
                      AXYNE_STATUS_OK);
    baseline = explorer.count;
    for (i = 0; i < 200; ++i) {
        AXYNE_TEST_STATUS(axyne_explorer_reload(&explorer, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_EQ_INT(explorer.count, baseline);
    }
    AXYNE_TEST_CHECK(explorer.capacity <= 64);
    axyne_explorer_destroy(&explorer);

    axyne_test_remove_file(file);
    axyne_test_remove_file(binary);
    axyne_test_remove_file(text);
    axyne_test_remove_directory(nested);
    axyne_test_remove_directory(tree);
    return 1;
}
