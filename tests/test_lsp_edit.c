#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/lsp.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static AxyneLspTextEdit edit(size_t sl, size_t sc, size_t el, size_t ec, const char *text)
{
    AxyneLspTextEdit e;
    e.range.start.line = sl; e.range.start.character = sc;
    e.range.end.line = el; e.range.end.character = ec;
    e.new_text = (char *)text;
    return e;
}

static int test_offsets(void)
{
    const char *text = "ab\r\ncd\n\xf0\x9f\x98\x80x\nlast";
    AxyneLspPosition p;
    size_t length = strlen(text);
    p.line = 0; p.character = 1; CHECK(axyne_lsp_offset_for_position(text, length, p) == 1);
    p.line = 0; p.character = 99; CHECK(axyne_lsp_offset_for_position(text, length, p) == 2); /* clamps to the line end */
    p.line = 1; p.character = 0; CHECK(axyne_lsp_offset_for_position(text, length, p) == 4); /* after CRLF */
    p.line = 2; p.character = 2; CHECK(axyne_lsp_offset_for_position(text, length, p) == 11); /* emoji = 2 UTF-16 units */
    p.line = 2; p.character = 3; CHECK(axyne_lsp_offset_for_position(text, length, p) == 12);
    p.line = 3; p.character = 2; CHECK(axyne_lsp_offset_for_position(text, length, p) == 15);
    p.line = 9; p.character = 0; CHECK(axyne_lsp_offset_for_position(text, length, p) == length);
    CHECK(axyne_lsp_offset_for_position(NULL, 0, p) == 0);
    return 1;
}

static int test_apply(void)
{
    char *out = NULL;
    size_t out_length = 0;
    AxyneLspTextEdit edits[3];
    /* two edits given out of order, one insertion */
    edits[0] = edit(0, 6, 0, 9, "world");
    edits[1] = edit(0, 0, 0, 0, "// hi\n");
    edits[2] = edit(0, 3, 0, 5, "");
    CHECK(axyne_lsp_apply_text_edits("foo bar baz\n", 12, edits, 3, &out, &out_length) == AXYNE_STATUS_OK);
    CHECK(strcmp(out, "// hi\nfooaworldaz\n") == 0 && out_length == strlen(out));
    free(out);
    edits[0] = edit(0, 0, 0, 3, "X");
    edits[1] = edit(0, 4, 0, 7, "Y");
    CHECK(axyne_lsp_apply_text_edits("foo bar baz", 11, edits, 2, &out, &out_length) == AXYNE_STATUS_OK);
    CHECK(strcmp(out, "X Y baz") == 0 && out_length == 7);
    free(out);
    /* overlapping edits are rejected */
    edits[0] = edit(0, 0, 0, 5, "A");
    edits[1] = edit(0, 3, 0, 7, "B");
    CHECK(axyne_lsp_apply_text_edits("foo bar baz", 11, edits, 2, &out, &out_length) ==
          AXYNE_STATUS_INVALID_ARGUMENT && out == NULL);
    /* no edits: copy; multi-line replacement */
    CHECK(axyne_lsp_apply_text_edits("same", 4, NULL, 0, &out, NULL) == AXYNE_STATUS_OK);
    CHECK(strcmp(out, "same") == 0);
    free(out);
    edits[0] = edit(0, 1, 2, 1, "-");
    CHECK(axyne_lsp_apply_text_edits("a1\nb2\nc3", 8, edits, 1, &out, &out_length) == AXYNE_STATUS_OK);
    CHECK(strcmp(out, "a-3") == 0);
    free(out);
    return 1;
}

static int test_markdown(void)
{
    char *plain = axyne_lsp_markdown_to_plain(
        "```ts\nconst version: string\n```\n\n**Bold** and `code` with [a link](http://x.y)\n## Title\nsnake\\_case");
    CHECK(plain != NULL);
    CHECK(strcmp(plain, "const version: string\n\nBold and code with a link\nTitle\nsnake_case") == 0);
    free(plain);
    plain = axyne_lsp_markdown_to_plain("");
    CHECK(plain != NULL && plain[0] == '\0');
    free(plain);
    CHECK(axyne_lsp_markdown_to_plain(NULL) == NULL);
    return 1;
}

static int test_uri(void)
{
    char *path = axyne_lsp_uri_to_path("file:///C:/Users/a%20b/x.js");
    CHECK(path != NULL && strcmp(path, "C:/Users/a b/x.js") == 0);
    free(path);
    path = axyne_lsp_uri_to_path("file:///home/me/x.py");
    CHECK(path != NULL && strcmp(path, "/home/me/x.py") == 0);
    free(path);
    path = axyne_lsp_uri_to_path("file:///a/%zz%4");
    CHECK(path != NULL && strcmp(path, "/a/%zz%4") == 0);
    free(path);
    CHECK(axyne_lsp_uri_to_path("https://example.com/x") == NULL);
    CHECK(axyne_lsp_uri_to_path(NULL) == NULL);
    return 1;
}

int axyne_test_lsp_edit(const char *root)
{
    (void)root;
    CHECK(test_offsets());
    CHECK(test_apply());
    CHECK(test_markdown());
    CHECK(test_uri());
    return 1;
}
