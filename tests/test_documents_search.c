#include "test_support.h"

#include <string.h>

#include "axyne/document.h"
#include "axyne/search.h"

int axyne_test_documents_search(const char *root)
{
    char alpha[512], beta_directory[512], beta[512], binary[512];
    char saved[512];
    AxyneDocumentSet documents = {0};
    AxyneSearchResults results = {0};
    char **paths = NULL;
    size_t path_count = 0, index = 0, match = 0;
    char *replacement = NULL;
    size_t replacement_length = 0, replacement_count = 0;
    AxyneError error = {0};

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(alpha, sizeof(alpha), root, "alpha.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(beta_directory, sizeof(beta_directory), root,
                                     "nested"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(beta_directory));
    AXYNE_TEST_CHECK(axyne_test_path(beta, sizeof(beta), beta_directory,
                                     "Beta.c"));
    AXYNE_TEST_CHECK(axyne_test_path(binary, sizeof(binary), root,
                                     "binary.dat"));
    AXYNE_TEST_CHECK(axyne_test_path(saved, sizeof(saved), root,
                                     "saved.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(alpha, "first needle\nsecond NEEDLE\n"));
    AXYNE_TEST_CHECK(axyne_test_write(beta, "nested needle\n"));
    AXYNE_TEST_CHECK(axyne_fs_write_file(binary, "needle\0hidden", 13, NULL) ==
                     AXYNE_STATUS_OK);

    AXYNE_TEST_CHECK(axyne_search_find("Abc abc", 7, "ABC", 3, 0, 0,
                                       &match));
    AXYNE_TEST_CHECK(match == 0);
    AXYNE_TEST_CHECK(axyne_search_find("one two one", 11, "one", 3, 4, 1,
                                       &match));
    AXYNE_TEST_CHECK(match == 8);
    AXYNE_TEST_CHECK(axyne_search_find("one", 3, "one", 3, 3, 1, &match));
    AXYNE_TEST_CHECK(match == 0);
    AXYNE_TEST_CHECK(!axyne_search_find("abc", 3, "", 0, 0, 1, &match));

    AXYNE_TEST_STATUS(axyne_search_replace_all(
                          "ababa", 5, "aba", 3, "X", 1, 1, &replacement,
                          &replacement_length, &replacement_count, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(replacement_count == 1 && replacement_length == 3 &&
                     strcmp(replacement, "Xba") == 0);
    axyne_fs_free(replacement);

    AXYNE_TEST_STATUS(axyne_search_workspace(root, "needle", 0, &results,
                                             &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(results.count <= 3);
    for (size_t result_index = 0; result_index < results.count; ++result_index) {
        AXYNE_TEST_CHECK(strstr(results.items[result_index].preview, "needle") !=
                         NULL ||
                         strstr(results.items[result_index].preview, "NEEDLE") !=
                         NULL);
        AXYNE_TEST_CHECK(strstr(results.items[result_index].path, "binary.dat") ==
                         NULL);
    }
    axyne_search_results_destroy(&results);

    AXYNE_TEST_STATUS(axyne_search_files(root, "beta", &paths, &path_count,
                                          &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(path_count == 1 && strstr(paths[0], "Beta.c") != NULL);
    axyne_search_paths_destroy(paths, path_count);

    AXYNE_TEST_STATUS(axyne_documents_initialize(&documents, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(documents.count == 1 && documents.documents[0].is_untitled);
    AXYNE_TEST_STATUS(axyne_documents_set_contents(&documents, 0, "draft", 5,
                                                   &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(documents.documents[0].is_dirty == 1);
    AXYNE_TEST_STATUS(axyne_documents_save_as(&documents, 0, saved, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!documents.documents[0].is_untitled &&
                     documents.documents[0].is_dirty == 0 &&
                     strcmp(documents.documents[0].title, "saved.txt") == 0);
    AXYNE_TEST_CHECK(axyne_test_read_equals(saved, "draft"));
    AXYNE_TEST_STATUS(axyne_documents_open(&documents, saved, &index, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 0 && documents.count == 1 &&
                     documents.recent_count == 1);
    AXYNE_TEST_STATUS(axyne_documents_close(&documents, 0, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(documents.count == 1 && documents.documents[0].is_untitled);
    axyne_documents_destroy(&documents);

    axyne_test_remove_tree(root);
    return 1;
}
