#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/problems_feed.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static int test_feed_chunks(void)
{
    AxyneProblemList list = {0};
    AxyneBuildFeed feed = {0};
    const char *a = "src/main.c:4:";
    const char *b = "9: error: boom\nnote: nothing\nsrc/u.c:1:2: warn";
    const char *c = "ing: careful\n";
    CHECK(axyne_build_feed_push(&feed, &list, 0, a, strlen(a)) == 0);
    CHECK(list.count == 0);
    axyne_build_feed_begin(&feed, &list, "/work/proj");
    CHECK(axyne_build_feed_push(&feed, &list, 0, a, strlen(a)) == 0);
    CHECK(axyne_build_feed_push(&feed, &list, 0, b, strlen(b)) == 1);
    CHECK(list.count == 1);
    CHECK(strcmp(list.items[0].path, "/work/proj/src/main.c") == 0);
    CHECK(list.items[0].line == 4 && list.items[0].column == 9);
    CHECK(list.items[0].origin == AXYNE_PROBLEM_ORIGIN_BUILD);
    CHECK(axyne_build_feed_push(&feed, &list, 0, c, strlen(c)) == 1);
    CHECK(list.count == 2);
    CHECK(list.items[1].severity == AXYNE_PROBLEM_WARNING);
    CHECK(strcmp(list.items[1].message, "careful") == 0);
    axyne_build_feed_destroy(&feed);
    axyne_problems_destroy(&list);
    return 1;
}

static int test_feed_streams_and_finish(void)
{
    AxyneProblemList list = {0};
    AxyneBuildFeed feed = {0};
    AxyneProblem lsp = {0};
    axyne_build_feed_begin(&feed, &list, NULL);
    /* Interleaved partial lines of two streams must not merge. */
    CHECK(axyne_build_feed_push(&feed, &list, 0, "/a/x.c:1:1: err", 15) == 0);
    CHECK(axyne_build_feed_push(&feed, &list, 1, "/a/y.c:2:2: warning: w", 22) == 0);
    CHECK(axyne_build_feed_push(&feed, &list, 0, "or: e1\r\n", 8) == 1);
    CHECK(list.count == 1 && strcmp(list.items[0].path, "/a/x.c") == 0);
    CHECK(axyne_build_feed_finish(&feed, &list) == 1);
    CHECK(list.count == 2 && strcmp(list.items[1].path, "/a/y.c") == 0);
    CHECK(axyne_build_feed_finish(&feed, &list) == 0);
    /* An LSP problem survives the start of the next build run. */
    lsp.severity = AXYNE_PROBLEM_ERROR; lsp.line = 1; lsp.column = 1;
    lsp.path = (char *)"/a/z.c"; lsp.message = (char *)"lsp";
    CHECK(axyne_problems_append(&list, AXYNE_PROBLEM_ORIGIN_LSP, &lsp, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 3);
    CHECK(axyne_build_feed_begin(&feed, &list, "/b") == 1);
    CHECK(list.count == 1 && list.items[0].origin == AXYNE_PROBLEM_ORIGIN_LSP);
    CHECK(axyne_build_feed_begin(&feed, &list, "/b") == 0);
    axyne_build_feed_destroy(&feed);
    axyne_problems_destroy(&list);
    return 1;
}

static int test_feed_long_line(void)
{
    AxyneProblemList list = {0};
    AxyneBuildFeed feed = {0};
    size_t n = AXYNE_BUILD_FEED_MAX_LINE * 3;
    char *big = (char *)malloc(n + 2);
    CHECK(big != NULL);
    memset(big, 'x', n);
    big[n] = '\n';
    axyne_build_feed_begin(&feed, &list, "/w");
    CHECK(axyne_build_feed_push(&feed, &list, 0, big, n / 2) == 0);
    CHECK(axyne_build_feed_push(&feed, &list, 0, big + n / 2, n - n / 2 + 1) == 0);
    /* The buffer is empty again: a normal line still parses. */
    CHECK(axyne_build_feed_push(&feed, &list, 0, "m.c:3:4: error: z\n", 18) == 1);
    CHECK(list.count == 1);
    free(big);
    axyne_build_feed_destroy(&feed);
    axyne_problems_destroy(&list);
    return 1;
}

static int test_diagnostics_copy(void)
{
    AxyneLspDiagnostic source[2];
    AxyneLspDiagnostic *copy = NULL;
    AxyneProblemList list = {0};
    char message[] = "unused";
    memset(source, 0, sizeof(source));
    source[0].severity = AXYNE_LSP_DIAGNOSTIC_WARNING;
    source[0].range.start.line = 6; source[0].range.start.character = 2;
    source[0].message = message;
    source[1].severity = AXYNE_LSP_DIAGNOSTIC_ERROR;
    source[1].message = message; source[1].code = (char *)"E1";
    CHECK(axyne_problems_diagnostics_copy(source, 2, &copy) == AXYNE_STATUS_OK);
    CHECK(copy != NULL && copy[0].message != message);
    message[0] = 'X'; /* the copy is independent of the original */
    CHECK(strcmp(copy[0].message, "unused") == 0);
    CHECK(copy[0].code != NULL && copy[0].code[0] == '\0');
    CHECK(strcmp(copy[1].code, "E1") == 0);
    CHECK(axyne_problems_set_lsp(&list, "/p/a.c", copy, 2, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 2 && list.items[0].line == 7 && list.items[0].column == 3);
    axyne_problems_diagnostics_free(copy, 2);
    CHECK(strcmp(list.items[0].message, "unused") == 0);
    axyne_problems_destroy(&list);
    copy = (AxyneLspDiagnostic *)1;
    CHECK(axyne_problems_diagnostics_copy(NULL, 0, &copy) == AXYNE_STATUS_OK && copy == NULL);
    CHECK(axyne_problems_diagnostics_copy(NULL, 1, &copy) == AXYNE_STATUS_INVALID_ARGUMENT);
    return 1;
}

static int test_column_offset(void)
{
    /* "a" + U+00E9 (2 bytes) + U+20AC (3 bytes) + U+1F600 (4 bytes, 2 units) + "z" */
    const char *line = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z\r\n";
    size_t length = strlen(line);
    CHECK(axyne_problems_column_offset(line, length, 1, 1) == 0);
    CHECK(axyne_problems_column_offset(line, length, 0, 1) == 0);
    CHECK(axyne_problems_column_offset(line, length, 2, 1) == 1);
    CHECK(axyne_problems_column_offset(line, length, 3, 1) == 3);
    CHECK(axyne_problems_column_offset(line, length, 4, 1) == 6);
    CHECK(axyne_problems_column_offset(line, length, 5, 1) == 6); /* mid pair */
    CHECK(axyne_problems_column_offset(line, length, 6, 1) == 10);
    CHECK(axyne_problems_column_offset(line, length, 7, 1) == 11);
    CHECK(axyne_problems_column_offset(line, length, 99, 1) == 11); /* clamped, no EOL */
    /* byte columns never split a sequence */
    CHECK(axyne_problems_column_offset(line, length, 3, 0) == 1);
    CHECK(axyne_problems_column_offset(line, length, 4, 0) == 3);
    CHECK(axyne_problems_column_offset(line, length, 99, 0) == 11);
    CHECK(axyne_problems_column_offset("", 0, 5, 1) == 0);
    CHECK(axyne_problems_column_offset(NULL, 0, 5, 0) == 0);
    return 1;
}

int axyne_test_problems_feed(const char *root)
{
    (void)root;
    CHECK(test_feed_chunks());
    CHECK(test_feed_streams_and_finish());
    CHECK(test_feed_long_line());
    CHECK(test_diagnostics_copy());
    CHECK(test_column_offset());
    return 1;
}

#ifdef AXYNE_PROBLEMS_FEED_STANDALONE
int main(void)
{
    return axyne_test_problems_feed("") ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
