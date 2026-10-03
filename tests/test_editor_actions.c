/* Exercises the shared Scintilla command helpers against a small in-memory
 * editor so the behaviour is verified without a native window. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/features/ui/editor_actions.h"

typedef struct FakeEditor {
    char text[512];
    size_t length;
    intptr_t selection_start;
    intptr_t selection_end;
    int read_only;
    int undo_depth;
    int undo_groups;
    intptr_t goto_line;
} FakeEditor;

static intptr_t line_start(const FakeEditor *e, intptr_t line)
{
    intptr_t current = 0;
    size_t i = 0;
    while (current < line && i < e->length) {
        if (e->text[i] == '\n') ++current;
        ++i;
    }
    return (intptr_t)i;
}

static intptr_t line_of(const FakeEditor *e, intptr_t position)
{
    intptr_t line = 0;
    intptr_t i;
    for (i = 0; i < position && (size_t)i < e->length; ++i)
        if (e->text[i] == '\n') ++line;
    return line;
}

static intptr_t fake_send(void *editor, unsigned int message, uintptr_t w,
                          intptr_t l)
{
    FakeEditor *e = (FakeEditor *)editor;
    switch (message) {
    case SCI_GETLINECOUNT: return line_of(e, (intptr_t)e->length) + 1;
    case SCI_LINEFROMPOSITION: return line_of(e, (intptr_t)w);
    case SCI_POSITIONFROMLINE: return line_start(e, (intptr_t)w);
    case SCI_GETLINEINDENTPOSITION: {
        size_t i = (size_t)line_start(e, (intptr_t)w);
        while (i < e->length && (e->text[i] == ' ' || e->text[i] == '\t')) ++i;
        return (intptr_t)i;
    }
    case SCI_GETLINEENDPOSITION: {
        size_t i = (size_t)line_start(e, (intptr_t)w);
        while (i < e->length && e->text[i] != '\n' && e->text[i] != '\r') ++i;
        return (intptr_t)i;
    }
    case SCI_GETCHARAT: return (size_t)w < e->length ? e->text[w] : 0;
    case SCI_GETSELECTIONSTART: return e->selection_start;
    case SCI_GETSELECTIONEND: return e->selection_end;
    case SCI_GETCURRENTPOS: return e->selection_end;
    case SCI_GETREADONLY: return e->read_only;
    case SCI_GETLENGTH: return (intptr_t)e->length;
    case SCI_BEGINUNDOACTION: ++e->undo_depth; ++e->undo_groups; return 0;
    case SCI_ENDUNDOACTION: --e->undo_depth; return 0;
    case SCI_SETSEL: e->selection_start = (intptr_t)w; e->selection_end = l; return 0;
    case SCI_GOTOLINE: e->goto_line = (intptr_t)w; return 0;
    case SCI_INSERTTEXT: {
        const char *insert = (const char *)l;
        size_t n = strlen(insert);
        memmove(e->text + w + n, e->text + w, e->length - w + 1);
        memcpy(e->text + w, insert, n);
        e->length += n;
        return 0;
    }
    case SCI_DELETERANGE:
        memmove(e->text + w, e->text + w + l, e->length - w - (size_t)l + 1);
        e->length -= (size_t)l;
        return 0;
    default: return 0;
    }
}

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
    ++failures; } } while (0)

static void set(FakeEditor *e, const char *text, intptr_t start, intptr_t end)
{
    memset(e, 0, sizeof(*e));
    e->length = strlen(text);
    memcpy(e->text, text, e->length + 1);
    e->selection_start = start;
    e->selection_end = end;
}

int main(void)
{
    FakeEditor e;
    size_t line = 0;

    CHECK(strcmp(axyne_editor_comment_token("a/b/main.c"), "//") == 0);
    CHECK(strcmp(axyne_editor_comment_token("x.TSX"), "//") == 0);
    CHECK(strcmp(axyne_editor_comment_token("run.py"), "#") == 0);
    CHECK(strcmp(axyne_editor_comment_token("C:\\p\\CMakeLists.txt"), "#") == 0);
    CHECK(strcmp(axyne_editor_comment_token(NULL), "//") == 0);
    CHECK(axyne_editor_comment_token("data.json") == NULL);
    CHECK(axyne_editor_comment_token("notes.txt") == NULL);
    CHECK(axyne_editor_comment_token("/dir.c/noext") == NULL);
    CHECK(axyne_editor_comment_token(".c") == NULL);

    CHECK(axyne_editor_parse_line_number("12", 20, &line) && line == 12);
    CHECK(axyne_editor_parse_line_number("  3 ", 3, &line) && line == 3);
    CHECK(!axyne_editor_parse_line_number("0", 20, &line));
    CHECK(!axyne_editor_parse_line_number("21", 20, &line));
    CHECK(!axyne_editor_parse_line_number("-1", 20, &line));
    CHECK(!axyne_editor_parse_line_number("4x", 20, &line));
    CHECK(!axyne_editor_parse_line_number("", 20, &line));
    CHECK(!axyne_editor_parse_line_number("99999999999999999999999", 20, &line));

    set(&e, "a\nb\nc", 0, 0);
    CHECK(axyne_editor_go_to_line(fake_send, &e, 3) && e.goto_line == 2);
    CHECK(!axyne_editor_go_to_line(fake_send, &e, 4));
    CHECK(!axyne_editor_go_to_line(fake_send, &e, 0));

    set(&e, "ab\ncd\nef", 4, 4);
    axyne_editor_select_line(fake_send, &e);
    CHECK(e.selection_start == 3 && e.selection_end == 6);
    set(&e, "ab\ncd\nef", 7, 7);
    axyne_editor_select_line(fake_send, &e);
    CHECK(e.selection_start == 6 && e.selection_end == 8);

    /* Single line comment and uncomment keep indentation. */
    set(&e, "  int x;\nint y;\n", 3, 3);
    CHECK(axyne_editor_toggle_line_comment(fake_send, &e, "//"));
    CHECK(strcmp(e.text, "  // int x;\nint y;\n") == 0);
    CHECK(e.undo_depth == 0 && e.undo_groups == 1);
    e.selection_start = e.selection_end = 5;
    CHECK(axyne_editor_toggle_line_comment(fake_send, &e, "//"));
    CHECK(strcmp(e.text, "  int x;\nint y;\n") == 0);

    /* Selection ending at a line start excludes that line; blank lines skipped. */
    set(&e, "a\n\nb\nc\n", 0, 5);
    CHECK(axyne_editor_toggle_line_comment(fake_send, &e, "#"));
    CHECK(strcmp(e.text, "# a\n\n# b\nc\n") == 0);
    CHECK(e.selection_start == 0);
    CHECK(axyne_editor_toggle_line_comment(fake_send, &e, "#"));
    CHECK(strcmp(e.text, "a\n\nb\nc\n") == 0);

    /* Mixed lines are all commented, not toggled individually. */
    set(&e, "// a\nb\n", 0, 7);
    CHECK(axyne_editor_toggle_line_comment(fake_send, &e, "//"));
    CHECK(strcmp(e.text, "// // a\n// b\n") == 0);

    /* Uncomment tolerates a token without trailing space. */
    set(&e, "//a\n", 0, 0);
    CHECK(axyne_editor_toggle_line_comment(fake_send, &e, "//"));
    CHECK(strcmp(e.text, "a\n") == 0);

    /* CRLF, read-only and blank-only buffers are left alone or handled. */
    set(&e, "a\r\nb\r\n", 0, 6);
    CHECK(axyne_editor_toggle_line_comment(fake_send, &e, "//"));
    CHECK(strcmp(e.text, "// a\r\n// b\r\n") == 0);
    set(&e, "a\n", 0, 0);
    e.read_only = 1;
    CHECK(!axyne_editor_toggle_line_comment(fake_send, &e, "//"));
    CHECK(strcmp(e.text, "a\n") == 0 && e.undo_groups == 0);
    set(&e, "\n\n", 0, 2);
    CHECK(!axyne_editor_toggle_line_comment(fake_send, &e, "//"));
    set(&e, "x", 0, 0);
    CHECK(!axyne_editor_toggle_line_comment(fake_send, &e, ""));

    if (failures == 0) puts("editor actions ok");
    return failures == 0 ? 0 : 1;
}
