#include "test_support.h"

#include <string.h>

#include "axyne/document.h"

#define NONE ((size_t)-1)

static size_t preview_count(const AxyneDocumentSet *set)
{
    size_t count = 0;
    for (size_t i = 0; i < set->count; ++i) count += set->documents[i].preview != 0;
    return count;
}

/* Virtual read-only documents (Git diff tabs) in the preview slot. */
static int check_virtual_documents(const char *a, const char *b)
{
    static const char diff1[] = "diff --git a/x b/x\n@@ -1 +1 @@\n-old\n+new\n";
    static const char diff2[] = "diff --git a/y b/y\n+added\n";
    AxyneDocumentSet set = {0};
    AxyneDocument evicted;
    AxyneError error = {0};
    size_t index = 0;
    int replaced = 0;
    int marker = 7;
    size_t path_count = 0;

    AXYNE_TEST_STATUS(axyne_documents_initialize(&set, &error), AXYNE_STATUS_OK);
    /* Opens appended (no preview yet), not dirty, not savable, never recent. */
    AXYNE_TEST_STATUS(axyne_documents_open_virtual(&set, "변경: x", diff1,
                                                   sizeof(diff1) - 1, &index,
                                                   &evicted, &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && set.count == 2 && replaced == 0 &&
                     set.active_index == 1);
    AXYNE_TEST_CHECK(set.documents[1].is_virtual && set.documents[1].preview &&
                     !set.documents[1].is_dirty && !set.documents[1].is_untitled &&
                     set.documents[1].path == NULL &&
                     set.documents[1].length == sizeof(diff1) - 1 &&
                     strcmp(set.documents[1].contents, diff1) == 0 &&
                     strcmp(set.documents[1].title, "변경: x") == 0);
    AXYNE_TEST_CHECK(set.recent_count == 0);
    AXYNE_TEST_CHECK(!axyne_document_tab_hidden(&set.documents[1]));
    AXYNE_TEST_CHECK(!axyne_document_can_save(&set.documents[1]) &&
                     !axyne_document_has_file(&set.documents[1]) &&
                     axyne_document_can_save(&set.documents[0]));
    AXYNE_TEST_CHECK(!axyne_documents_empty_state(&set));

    /* Saving, editing and dirtying are refused and leave it clean. */
    AXYNE_TEST_CHECK(axyne_documents_save(&set, 1, &error) == AXYNE_STATUS_UNSUPPORTED);
    AXYNE_TEST_CHECK(axyne_documents_save_as(&set, 1, a, &error) ==
                     AXYNE_STATUS_UNSUPPORTED);
    AXYNE_TEST_CHECK(axyne_documents_mark_dirty(&set, 1, &error) ==
                     AXYNE_STATUS_UNSUPPORTED);
    AXYNE_TEST_CHECK(axyne_documents_set_contents(&set, 1, "edited", 6, &error) ==
                     AXYNE_STATUS_UNSUPPORTED);
    AXYNE_TEST_CHECK(!set.documents[1].is_dirty && set.documents[1].preview &&
                     set.documents[1].path == NULL &&
                     strcmp(set.documents[1].contents, diff1) == 0);

    /* Palette / file lists keep only documents backed by a file. */
    for (size_t i = 0; i < set.count; ++i)
        if (axyne_document_has_file(&set.documents[i]))
            ++path_count;
    AXYNE_TEST_CHECK(path_count == 0);

    /* Another click replaces the virtual document in place and hands the old
     * native handle back for release. */
    set.documents[1].native_editor_document = &marker;
    set.documents[1].owns_native_editor_document = 1;
    AXYNE_TEST_STATUS(axyne_documents_open_virtual(&set, "abc1234: y", diff2,
                                                   sizeof(diff2) - 1, &index,
                                                   &evicted, &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && set.count == 2 && replaced == 1 &&
                     strcmp(set.documents[1].title, "abc1234: y") == 0 &&
                     set.documents[1].native_editor_document == NULL &&
                     evicted.is_virtual && evicted.native_editor_document == &marker &&
                     strcmp(evicted.title, "변경: x") == 0);
    /* A failed native load puts the previous diff back. */
    axyne_documents_revert_preview_open(&set, index, &evicted, replaced);
    AXYNE_TEST_CHECK(set.count == 2 && strcmp(set.documents[1].title, "변경: x") == 0 &&
                     set.documents[1].native_editor_document == &marker &&
                     set.documents[1].is_virtual);
    set.documents[1].native_editor_document = NULL;
    set.documents[1].owns_native_editor_document = 0;

    /* An Explorer preview replaces the diff, and a diff replaces a file preview. */
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, a, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && replaced == 1 && evicted.is_virtual &&
                     !set.documents[1].is_virtual && set.documents[1].path != NULL);
    axyne_document_dispose(&evicted);
    AXYNE_TEST_STATUS(axyne_documents_open_virtual(&set, "변경: z", "", 0, &index,
                                                   &evicted, &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && replaced == 1 && evicted.path != NULL &&
                     strcmp(evicted.title, "a.txt") == 0 &&
                     set.documents[1].is_virtual && set.documents[1].length == 0 &&
                     set.documents[1].contents[0] == '\0');
    axyne_document_dispose(&evicted);
    /* A normal tab is never replaced; the diff keeps the preview slot. */
    AXYNE_TEST_STATUS(axyne_documents_open(&set, b, &index, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 2 && set.documents[1].is_virtual &&
                     set.documents[1].preview && !set.documents[2].preview);
    AXYNE_TEST_STATUS(axyne_documents_open_virtual(&set, "abc1234: y", diff2,
                                                   sizeof(diff2) - 1, &index, NULL,
                                                   NULL, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && set.count == 3 &&
                     strcmp(set.documents[1].title, "abc1234: y") == 0);

    /* Closing needs no prompt state (never dirty) and leaves the rest. */
    AXYNE_TEST_CHECK(!set.documents[1].is_dirty);
    AXYNE_TEST_STATUS(axyne_documents_close(&set, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.count == 2 && axyne_documents_preview_index(&set) == (size_t)-1);
    AXYNE_TEST_CHECK(set.recent_count == 2); /* only a.txt and b.txt, the real opens */

    AXYNE_TEST_CHECK(axyne_documents_open_virtual(&set, "", diff1, 1, &index, NULL,
                                                  NULL, &error) ==
                     AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_documents_open_virtual(&set, "t", NULL, 3, &index, NULL,
                                                  NULL, &error) ==
                     AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_documents_open_virtual(NULL, "t", "", 0, &index, NULL,
                                                  NULL, &error) ==
                     AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_documents_destroy(&set);
    return 1;
}

int axyne_test_preview_tabs(const char *root)
{
    char a[512], b[512], c[512], e[512], missing[512];
    AxyneDocumentSet set = {0};
    AxyneDocument evicted;
    AxyneError error = {0};
    size_t index = 0;
    int replaced = 0;
    int marker_old = 1, marker_new = 2;

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(a, sizeof(a), root, "a.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(b, sizeof(b), root, "b.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(c, sizeof(c), root, "c.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(missing, sizeof(missing), root, "nope.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(a, "alpha\n"));
    AXYNE_TEST_CHECK(axyne_test_write(b, "beta\n"));
    AXYNE_TEST_CHECK(axyne_test_write(c, "gamma\n"));

    /* Startup: one untitled buffer (hidden placeholder once merged with the
     * workspace rules). The first preview is appended after it. */
    AXYNE_TEST_STATUS(axyne_documents_initialize(&set, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.count == 1 && set.documents[0].preview == 0);
    AXYNE_TEST_CHECK(axyne_documents_preview_index(&set) == NONE);
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, a, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && set.count == 2 && replaced == 0);
    AXYNE_TEST_CHECK(set.documents[1].preview == 1 && set.active_index == 1);
    AXYNE_TEST_CHECK(set.documents[0].is_untitled && set.documents[0].preview == 0);
    AXYNE_TEST_CHECK(axyne_documents_preview_index(&set) == 1);

    /* A second Explorer open replaces the clean preview in place. */
    set.documents[1].native_editor_document = &marker_old;
    set.documents[1].owns_native_editor_document = 1;
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, b, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && set.count == 2 && replaced == 1);
    AXYNE_TEST_CHECK(strcmp(set.documents[1].title, "b.txt") == 0 &&
                     set.documents[1].preview == 1 &&
                     set.documents[1].native_editor_document == NULL &&
                     set.documents[1].owns_native_editor_document == 0);
    AXYNE_TEST_CHECK(strcmp(evicted.title, "a.txt") == 0 &&
                     evicted.native_editor_document == &marker_old &&
                     evicted.owns_native_editor_document == 1);
    axyne_document_dispose(&evicted);
    AXYNE_TEST_CHECK(preview_count(&set) == 1 && set.active_index == 1);

    /* Native load failure: revert puts the old preview back. */
    set.documents[1].native_editor_document = &marker_new;
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, c, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(replaced == 1 && strcmp(set.documents[1].title, "c.txt") == 0);
    axyne_documents_revert_preview_open(&set, index, &evicted, replaced);
    AXYNE_TEST_CHECK(set.count == 2 && strcmp(set.documents[1].title, "b.txt") == 0 &&
                     set.documents[1].native_editor_document == &marker_new &&
                     set.documents[1].preview == 1 && evicted.title == NULL);
    set.documents[1].native_editor_document = NULL;
    set.documents[1].owns_native_editor_document = 0;

    /* Missing file: nothing changes. */
    AXYNE_TEST_CHECK(axyne_documents_open_preview(&set, missing, &index,
                                                  &evicted, &replaced,
                                                  &error) != AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.count == 2 && strcmp(set.documents[1].title, "b.txt") == 0 &&
                     preview_count(&set) == 1 && replaced == 0);

    /* Reopening the preview file only activates it and keeps it preview. */
    AXYNE_TEST_STATUS(axyne_documents_set_active(&set, 0, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, b, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && set.count == 2 && replaced == 0 &&
                     set.active_index == 1 && set.documents[1].preview == 1);
    AXYNE_TEST_STATUS(axyne_documents_open(&set, b, &index, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 1 && set.count == 2 && set.documents[1].preview == 1);

    /* A normal open never replaces the preview and is itself not preview. */
    AXYNE_TEST_STATUS(axyne_documents_open(&set, a, &index, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 2 && set.count == 3 && set.documents[2].preview == 0 &&
                     set.documents[1].preview == 1 && preview_count(&set) == 1);
    /* Explorer-opening a file that is already a normal tab does not make it
     * a preview and does not replace the existing preview. */
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, a, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 2 && set.count == 3 && replaced == 0 &&
                     set.documents[2].preview == 0 && set.documents[1].preview == 1);

    /* Edit promotes (set_contents), preview then no longer exists. */
    AXYNE_TEST_STATUS(axyne_documents_set_contents(&set, 1, "edited", 6, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.documents[1].preview == 0 && set.documents[1].is_dirty &&
                     preview_count(&set) == 0);
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, c, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 3 && set.count == 4 && replaced == 0 &&
                     set.documents[3].preview == 1 && set.documents[1].is_dirty);
    AXYNE_TEST_CHECK(strcmp(set.documents[1].title, "b.txt") == 0);

    /* mark_dirty promotes; mark_clean does not restore preview. */
    AXYNE_TEST_STATUS(axyne_documents_mark_dirty(&set, 3, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.documents[3].preview == 0);
    AXYNE_TEST_STATUS(axyne_documents_mark_clean(&set, 3, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.documents[3].preview == 0 && preview_count(&set) == 0);

    /* Save promotes. */
    AXYNE_TEST_STATUS(axyne_documents_close(&set, 3, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_documents_close(&set, 2, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.count == 2);
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, c, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 2 && set.documents[2].preview == 1);
    AXYNE_TEST_STATUS(axyne_documents_save(&set, 2, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.documents[2].preview == 0);

    /* Save As promotes (untitled preview-less and a preview renamed). */
    {
        char renamed[512];
        AXYNE_TEST_CHECK(axyne_test_path(renamed, sizeof(renamed), root, "r.txt"));
        AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, a, &index, &evicted,
                                                       &replaced, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(index == 3 && set.documents[3].preview == 1);
        AXYNE_TEST_STATUS(axyne_documents_save_as(&set, 3, renamed, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(set.documents[3].preview == 0 &&
                         strcmp(set.documents[3].title, "r.txt") == 0);
    }

    /* Replacement keeps index/active when the preview is not the active tab
     * and not the last tab. */
    AXYNE_TEST_CHECK(axyne_test_path(e, sizeof(e), root, "e.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(e, "epsilon\n"));
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, e,
                                                   &index, &evicted, &replaced,
                                                   &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 4 && set.count == 5 && set.documents[4].preview == 1);
    AXYNE_TEST_STATUS(axyne_documents_set_active(&set, 3, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_documents_new(&set, &index, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 5 && set.documents[5].preview == 0);
    AXYNE_TEST_STATUS(axyne_documents_set_active(&set, 0, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_test_write(c, "gamma changed on disk\n"));
    {
        char d[512];
        AXYNE_TEST_CHECK(axyne_test_path(d, sizeof(d), root, "d.txt"));
        AXYNE_TEST_CHECK(axyne_test_write(d, "delta\n"));
        AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, d, &index, &evicted,
                                                       &replaced, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(index == 4 && replaced == 1 && set.count == 6 &&
                         set.active_index == 4 && preview_count(&set) == 1 &&
                         strcmp(set.documents[4].title, "d.txt") == 0);
        axyne_document_dispose(&evicted);
    }

    /* Caller without native state may pass NULL; the old preview is freed. */
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, e, &index, NULL, NULL,
                                                   &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(index == 4 && set.count == 6 && preview_count(&set) == 1 &&
                     strcmp(set.documents[4].title, "e.txt") == 0);

    /* Closing the preview leaves none; the next Explorer open appends. */
    AXYNE_TEST_STATUS(axyne_documents_close(&set, 4, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_documents_preview_index(&set) == NONE);
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, e, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(replaced == 0 && preview_count(&set) == 1);

    /* Invalid arguments. */
    AXYNE_TEST_CHECK(axyne_documents_open_preview(NULL, a, &index, &evicted,
                                                  &replaced, &error) ==
                     AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_documents_open_preview(&set, "", &index, &evicted,
                                                  &replaced, &error) ==
                     AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_documents_promote(&set, 999);
    axyne_documents_promote(NULL, 0);

    AXYNE_TEST_CHECK(check_virtual_documents(a, b));

    axyne_documents_destroy(&set);
    axyne_test_remove_tree(root);
    return 1;
}
