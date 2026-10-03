#include "test_support.h"

#include <string.h>

#include "axyne/document.h"
#include "axyne/explorer.h"
#include "axyne/search.h"

int axyne_test_documents_search(const char *root)
{
    char alpha[512], beta_directory[512], beta[512], binary[512];
    char git_directory[512], git_file[512];
    char saved[512], build_directory[512], nested_git[512];
    AxyneExplorer explorer = {0};
    size_t node_index = 0;
    int saw_build = 0, saw_nested = 0;
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
    AXYNE_TEST_CHECK(axyne_test_path(git_directory, sizeof(git_directory), root,
                                     ".git"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(git_directory));
    AXYNE_TEST_CHECK(axyne_test_path(git_file, sizeof(git_file), git_directory,
                                     "config"));
    AXYNE_TEST_CHECK(axyne_test_write(git_file, "needle\n"));
    AXYNE_TEST_CHECK(axyne_test_path(build_directory, sizeof(build_directory),
                                     root, "build"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(build_directory));
    /* A linked worktree stores .git as a plain file; it is metadata too. */
    AXYNE_TEST_CHECK(axyne_test_path(nested_git, sizeof(nested_git),
                                     beta_directory, ".git"));
    AXYNE_TEST_CHECK(axyne_test_write(nested_git, "gitdir: elsewhere needle\n"));
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
        /* Only the part below the fixture root counts: the root itself
         * lives in an arbitrary checkout path. */
        {
            const char *below_root = results.items[result_index].path;
            size_t root_length = strlen(root);
            if (strncmp(below_root, root, root_length) == 0)
                below_root += root_length;
            if (strstr(below_root, ".git") != NULL)
                fprintf(stderr, "FAIL search listed repository metadata: %s\n",
                        results.items[result_index].path);
            AXYNE_TEST_CHECK(strstr(below_root, ".git") == NULL);
        }
    }
    axyne_search_results_destroy(&results);

    AXYNE_TEST_STATUS(axyne_search_files(root, "beta", &paths, &path_count,
                                          &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(path_count == 1 && strstr(paths[0], "Beta.c") != NULL);
    axyne_search_paths_destroy(paths, path_count);

    AXYNE_TEST_CHECK(axyne_explorer_is_hidden_name(".git"));
    AXYNE_TEST_CHECK(!axyne_explorer_is_hidden_name(".gitignore") &&
                     !axyne_explorer_is_hidden_name("git") &&
                     !axyne_explorer_is_hidden_name(NULL));
    AXYNE_TEST_STATUS(axyne_explorer_initialize(&explorer, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_explorer_set_root(&explorer, root, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(explorer.count >= 1 && explorer.nodes[0].depth == 0);
    /* The root row shows the folder name, not the full path. */
    if (strchr(explorer.nodes[0].name, '/') != NULL ||
        strchr(explorer.nodes[0].name, '\\') != NULL ||
        strstr(root, explorer.nodes[0].name) == NULL)
        fprintf(stderr, "FAIL root row name \"%s\" for root \"%s\"\n",
                explorer.nodes[0].name, root);
    AXYNE_TEST_CHECK(strchr(explorer.nodes[0].name, '/') == NULL &&
                     strchr(explorer.nodes[0].name, '\\') == NULL &&
                     strstr(root, explorer.nodes[0].name) != NULL);
    AXYNE_TEST_STREQ(explorer.nodes[0].path, root);
    AXYNE_TEST_CHECK(!axyne_explorer_is_dimmed(&explorer.nodes[0]));
    for (node_index = 0; node_index < explorer.count; ++node_index) {
        AxyneExplorerNode *node = &explorer.nodes[node_index];
        if (strcmp(node->name, ".git") == 0)
            fprintf(stderr, "FAIL explorer row %zu lists %s\n", node_index,
                    node->path);
        AXYNE_TEST_CHECK(strcmp(node->name, ".git") != 0);
        if (strcmp(node->name, "build") == 0) {
            saw_build = 1;
            AXYNE_TEST_CHECK(axyne_explorer_is_dimmed(node));
        } else if (strcmp(node->name, "nested") == 0) {
            saw_nested = 1;
            AXYNE_TEST_CHECK(!axyne_explorer_is_dimmed(node));
        }
    }
    if (!saw_build || !saw_nested)
        fprintf(stderr, "FAIL explorer rows: count=%zu saw_build=%d "
                "saw_nested=%d\n", explorer.count, saw_build, saw_nested);
    AXYNE_TEST_CHECK(saw_build && saw_nested);
    AXYNE_TEST_CHECK(!axyne_explorer_is_dimmed(NULL));
    axyne_explorer_destroy(&explorer);

    AXYNE_TEST_STATUS(axyne_documents_initialize(&documents, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(documents.count == 1 && documents.documents[0].is_untitled);
    /* The startup buffer exists but has no tab until it is touched. */
    AXYNE_TEST_CHECK(axyne_document_tab_hidden(&documents.documents[0]) &&
                     axyne_documents_visible_count(&documents) == 0);
    AXYNE_TEST_CHECK(!axyne_document_tab_hidden(NULL));
    /* An explicit New is a visible tab even though it is empty and clean. */
    AXYNE_TEST_STATUS(axyne_documents_new(&documents, &index, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && documents.active_index == 1 &&
                     !axyne_document_tab_hidden(&documents.documents[1]) &&
                     axyne_documents_visible_count(&documents) == 1);
    AXYNE_TEST_STATUS(axyne_documents_close(&documents, 1, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(documents.count == 1 && documents.active_index == 0 &&
                     axyne_documents_visible_count(&documents) == 0);
    /* Editing reveals the placeholder; Save As turns it into a named tab. */
    documents.documents[0].is_dirty = 1;
    AXYNE_TEST_CHECK(!axyne_document_tab_hidden(&documents.documents[0]));
    documents.documents[0].is_dirty = 0;
    AXYNE_TEST_CHECK(axyne_document_tab_hidden(&documents.documents[0]));
    AXYNE_TEST_STATUS(axyne_documents_set_contents(&documents, 0, "draft", 5,
                                                   &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(documents.documents[0].is_dirty == 1 &&
                     !axyne_document_tab_hidden(&documents.documents[0]));
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
    /* Closing the last tab leaves a fresh hidden buffer, not a dead state. */
    AXYNE_TEST_CHECK(documents.count == 1 && documents.documents[0].is_untitled);
    AXYNE_TEST_CHECK(axyne_document_tab_hidden(&documents.documents[0]) &&
                     axyne_documents_visible_count(&documents) == 0);
    AXYNE_TEST_STATUS(axyne_documents_save(&documents, 0, &error),
                      AXYNE_STATUS_UNSUPPORTED);
    AXYNE_TEST_STATUS(axyne_documents_save_as(&documents, 0, saved, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!axyne_document_tab_hidden(&documents.documents[0]) &&
                     axyne_documents_visible_count(&documents) == 1);
    axyne_documents_destroy(&documents);

    axyne_test_remove_tree(root);
    return 1;
}
