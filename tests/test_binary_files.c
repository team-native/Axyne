#include "test_support.h"

#include <string.h>

#include "axyne/document.h"
#include "axyne/filesystem.h"

static int write_bytes(const char *path, const void *data, size_t length)
{
    return axyne_fs_write_file(path, (const char *)data, length, NULL) ==
           AXYNE_STATUS_OK;
}

static int sniff(const char *path)
{
    int binary = -1;
    AxyneError error = {0};
    if (axyne_document_file_is_binary(path, &binary, &error) != AXYNE_STATUS_OK)
        return -1;
    return binary;
}

int axyne_test_binary_files(const char *root)
{
    char text[512], korean[512], empty[512], nul[512], bad[512], png[512];
    char late_nul[512], big_text[512], cut_utf8[512], missing[512], dir[512];
    char utf8_name[512];
    AxyneDocumentSet set = {0};
    AxyneError error = {0};
    AxyneDocument evicted;
    size_t index = 0;
    int replaced = 0;
    int binary = 0;

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));

    /* Pure head heuristic. */
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary("", 0, 0));
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary(NULL, 0, 0));
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary("hello\n\tworld\r\n", 14, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("ab\0cd", 5, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("\0", 1, 1));
    /* U+D55C U+AE00 and a 4-byte emoji are valid. */
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary("\xed\x95\x9c\xea\xb8\x80", 6, 0));
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary("\xf0\x9f\x98\x80", 4, 0));
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary("\xef\xbb\xbf" "abc", 6, 0));
    /* Invalid: lone continuation, 0xFF, overlong, surrogate, > U+10FFFF,
     * bad continuation, truncated sequence at the real end of the file. */
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("a\x80z", 3, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("a\xff", 2, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("\xc0\xaf", 2, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("\xed\xa0\x80", 3, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("\xf4\x90\x80\x80", 4, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("\xe2\x28\xa1", 3, 0));
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("ab\xed\x95", 4, 0));
    /* The same cut-off is fine when the buffer is only a prefix. */
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary("ab\xed\x95", 4, 1));
    AXYNE_TEST_CHECK(!axyne_bytes_look_binary("ab\xed", 3, 1));
    /* A complete bad byte is still invalid inside a prefix. */
    AXYNE_TEST_CHECK(axyne_bytes_look_binary("\xed\x28\x9c", 3, 1));

    /* Files. */
    AXYNE_TEST_CHECK(axyne_test_path(text, sizeof(text), root, "a.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(korean, sizeof(korean), root, "k.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(empty, sizeof(empty), root, "e.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(nul, sizeof(nul), root, "n.bin"));
    AXYNE_TEST_CHECK(axyne_test_path(bad, sizeof(bad), root, "bad.dat"));
    AXYNE_TEST_CHECK(axyne_test_path(png, sizeof(png), root, "i.png"));
    AXYNE_TEST_CHECK(axyne_test_path(late_nul, sizeof(late_nul), root, "late.dat"));
    AXYNE_TEST_CHECK(axyne_test_path(big_text, sizeof(big_text), root, "big.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(cut_utf8, sizeof(cut_utf8), root, "cut.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(missing, sizeof(missing), root, "nope"));
    AXYNE_TEST_CHECK(axyne_test_path(dir, sizeof(dir), root, "sub"));
    AXYNE_TEST_CHECK(axyne_test_path(utf8_name, sizeof(utf8_name), root,
                                     "\xed\x95\x9c\xea\xb8\x80.bin"));
    AXYNE_TEST_CHECK(axyne_test_write(text, "int main(void) {}\n"));
    AXYNE_TEST_CHECK(axyne_test_write(korean, "\xed\x95\x9c\xea\xb8\x80\n"));
    AXYNE_TEST_CHECK(axyne_test_write(empty, ""));
    AXYNE_TEST_CHECK(write_bytes(nul, "MZ\x90\0\x03\0\0\0", 8));
    AXYNE_TEST_CHECK(write_bytes(bad, "ok\n\xff\xfe\x01", 6));
    AXYNE_TEST_CHECK(write_bytes(png, "\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16));
    AXYNE_TEST_CHECK(write_bytes(utf8_name, "\0\1\2", 3));
    AXYNE_TEST_CHECK(axyne_test_make_directory(dir));

    {   /* A NUL beyond the sniff window is not looked at (documented). */
        char *buf = (char *)malloc(AXYNE_BINARY_SNIFF_BYTES + 10);
        AXYNE_TEST_CHECK(buf != NULL);
        memset(buf, 'x', AXYNE_BINARY_SNIFF_BYTES + 10);
        buf[AXYNE_BINARY_SNIFF_BYTES + 5] = '\0';
        AXYNE_TEST_CHECK(write_bytes(late_nul, buf, AXYNE_BINARY_SNIFF_BYTES + 10));
        /* ASCII filling the whole window and more. */
        memset(buf, 'y', AXYNE_BINARY_SNIFF_BYTES + 10);
        AXYNE_TEST_CHECK(write_bytes(big_text, buf, AXYNE_BINARY_SNIFF_BYTES + 10));
        /* A 3-byte character straddling the window edge. */
        memset(buf, 'z', AXYNE_BINARY_SNIFF_BYTES + 10);
        memcpy(buf + AXYNE_BINARY_SNIFF_BYTES - 1, "\xed\x95\x9c", 3);
        AXYNE_TEST_CHECK(write_bytes(cut_utf8, buf, AXYNE_BINARY_SNIFF_BYTES + 10));
        free(buf);
    }

    AXYNE_TEST_CHECK(sniff(text) == 0);
    AXYNE_TEST_CHECK(sniff(korean) == 0);
    AXYNE_TEST_CHECK(sniff(empty) == 0);
    AXYNE_TEST_CHECK(sniff(nul) == 1);
    AXYNE_TEST_CHECK(sniff(bad) == 1);
    AXYNE_TEST_CHECK(sniff(png) == 1);
    AXYNE_TEST_CHECK(sniff(utf8_name) == 1);
    AXYNE_TEST_CHECK(sniff(late_nul) == 0);
    AXYNE_TEST_CHECK(sniff(big_text) == 0);
    AXYNE_TEST_CHECK(sniff(cut_utf8) == 0);
    AXYNE_TEST_STATUS(axyne_document_file_is_binary(missing, &binary, &error),
                      AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(axyne_document_file_is_binary(NULL, &binary, NULL) ==
                     AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_document_file_is_binary(text, NULL, NULL) ==
                     AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_document_file_is_binary(dir, &binary, NULL) !=
                     AXYNE_STATUS_OK);

    /* Head reader contract. */
    {
        char *head = NULL;
        size_t length = 0;
        int truncated = 0;
        AXYNE_TEST_STATUS(axyne_fs_read_head(text, 4, &head, &length,
                                             &truncated, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(length == 4 && truncated == 1 &&
                         memcmp(head, "int ", 4) == 0 && head[4] == '\0');
        axyne_fs_free(head);
        AXYNE_TEST_STATUS(axyne_fs_read_head(text, 1000, &head, &length,
                                             &truncated, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(length == strlen("int main(void) {}\n") && truncated == 0);
        axyne_fs_free(head);
        AXYNE_TEST_STATUS(axyne_fs_read_head(empty, 10, &head, &length, NULL,
                                             &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(length == 0 && head != NULL && head[0] == '\0');
        axyne_fs_free(head);
    }

    /* Opening refuses binary files and leaves the set untouched. */
    AXYNE_TEST_STATUS(axyne_documents_initialize(&set, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_documents_empty_state(&set));
    AXYNE_TEST_STATUS(axyne_documents_open(&set, nul, &index, &error),
                      AXYNE_STATUS_BINARY);
    AXYNE_TEST_CHECK(error.code == AXYNE_STATUS_BINARY && error.message[0] != '\0');
    AXYNE_TEST_CHECK(set.count == 1 && set.recent_count == 0 &&
                     set.active_index == 0 && axyne_documents_empty_state(&set));
    AXYNE_TEST_STATUS(axyne_documents_open(&set, bad, &index, &error),
                      AXYNE_STATUS_BINARY);
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, png, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_BINARY);
    AXYNE_TEST_CHECK(set.count == 1 && replaced == 0 && set.recent_count == 0);
    AXYNE_TEST_STATUS(axyne_documents_open(&set, missing, &index, &error),
                      AXYNE_STATUS_NOT_FOUND);

    /* A text file still opens, and a preview is not replaced by a binary. */
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, text, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.count == 2 && index == 1 && set.documents[1].preview == 1 &&
                     !axyne_documents_empty_state(&set));
    AXYNE_TEST_STATUS(axyne_documents_open_preview(&set, nul, &index, &evicted,
                                                   &replaced, &error),
                      AXYNE_STATUS_BINARY);
    AXYNE_TEST_CHECK(set.count == 2 && replaced == 0 &&
                     strcmp(set.documents[1].title, "a.txt") == 0 &&
                     set.documents[1].preview == 1 && set.active_index == 1);
    AXYNE_TEST_STATUS(axyne_documents_open(&set, korean, &index, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(set.count == 3 && !axyne_documents_empty_state(&set));

    /* Empty state follows the active document. */
    AXYNE_TEST_STATUS(axyne_documents_set_active(&set, 0, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_documents_empty_state(&set));
    AXYNE_TEST_STATUS(axyne_documents_set_active(&set, 2, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!axyne_documents_empty_state(&set));
    AXYNE_TEST_STATUS(axyne_documents_new(&set, &index, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!axyne_documents_empty_state(&set));
    AXYNE_TEST_CHECK(axyne_documents_empty_state(NULL));
    axyne_documents_destroy(&set);
    AXYNE_TEST_CHECK(axyne_documents_empty_state(&set));
    return 1;
}
