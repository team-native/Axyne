#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/palette_controller.h"
#include "test_support.h"

/* The controller is the shared flow behind the Windows and macOS palettes:
 * open/set_input/walk_step/move/activate. These checks run without any UI. */

typedef struct FakeDocument {
    const char *path;
    const char *text;
    size_t lines;
    int available;
} FakeDocument;

static char *dup_text(const char *text)
{
    size_t n = strlen(text);
    char *copy = (char *)malloc(n + 1);
    if (copy != NULL) memcpy(copy, text, n + 1);
    return copy;
}

static int fake_document(void *user, char **path, char **text, size_t *length,
                         size_t *line_count)
{
    FakeDocument *document = (FakeDocument *)user;
    size_t n;
    if (!document->available) return 0;
    n = strlen(document->text);
    *path = document->path == NULL ? NULL : dup_text(document->path);
    *text = (char *)malloc(n + 1);
    if (*text == NULL) return 0;
    memcpy(*text, document->text, n + 1);
    *length = n;
    *line_count = document->lines;
    return 1;
}

static int run_walk(AxynePaletteController *c)
{
    int guard = 0;
    while (axyne_palette_ctl_walk_step(c, 2, NULL)) {
        if (++guard > 10000) return 0;
    }
    return 1;
}

static int has_label(const AxynePaletteController *c, const char *label)
{
    size_t i;
    for (i = 0; i < c->list.count; ++i)
        if (strcmp(c->list.items[i].label, label) == 0) return 1;
    return 0;
}

static int test_file_mode(const char *root)
{
    char sub[1024], deep[1024], a[1024], b[1024], d[1024], z[1024];
    const char *open_paths[1];
    AxynePaletteController c;
    AxynePaletteAction action;

    AXYNE_TEST_CHECK(axyne_test_path(sub, sizeof(sub), root, "sub"));
    AXYNE_TEST_CHECK(axyne_test_path(deep, sizeof(deep), sub, "deeper"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(sub));
    AXYNE_TEST_CHECK(axyne_test_make_directory(deep));
    AXYNE_TEST_CHECK(axyne_test_path(a, sizeof(a), root, "alpha.c"));
    AXYNE_TEST_CHECK(axyne_test_path(b, sizeof(b), sub, "beta.h"));
    AXYNE_TEST_CHECK(axyne_test_path(d, sizeof(d), deep, "delta.txt"));
    AXYNE_TEST_CHECK(axyne_test_path(z, sizeof(z), deep, "alphabet.md"));
    AXYNE_TEST_CHECK(axyne_test_write(a, "int x;\n"));
    AXYNE_TEST_CHECK(axyne_test_write(b, "int y;\n"));
    AXYNE_TEST_CHECK(axyne_test_write(d, "d\n"));
    AXYNE_TEST_CHECK(axyne_test_write(z, "z\n"));

    axyne_palette_ctl_init(&c, 0, NULL, NULL);
    open_paths[0] = b;
    AXYNE_TEST_STATUS(axyne_palette_ctl_open(&c, root, open_paths, 1, ""),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.active && c.mode == AXYNE_PALETTE_MODE_FILE);
    AXYNE_TEST_CHECK(axyne_palette_ctl_walk_running(&c));
    /* before the walk only the open document is listed */
    AXYNE_TEST_CHECK(c.list.count == 1);
    AXYNE_TEST_CHECK(run_walk(&c));
    AXYNE_TEST_CHECK(!axyne_palette_ctl_walk_running(&c));
    /* the open document is listed once and first; the rest follow */
    AXYNE_TEST_CHECK(c.path_count == 4);
    AXYNE_TEST_CHECK(c.list.count == 4);
    AXYNE_TEST_CHECK(strcmp(c.list.items[0].label, "beta.h") == 0);
    AXYNE_TEST_CHECK(has_label(&c, "alpha.c") && has_label(&c, "delta.txt") &&
                     has_label(&c, "alphabet.md"));

    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, "alpha"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count == 2);
    AXYNE_TEST_CHECK(strcmp(c.list.items[0].label, "alpha.c") == 0);
    AXYNE_TEST_CHECK(c.selection == 0);
    axyne_palette_ctl_move(&c, 1);
    AXYNE_TEST_CHECK(c.selection == 1);
    axyne_palette_ctl_move(&c, 1);
    AXYNE_TEST_CHECK(c.selection == 0); /* wraps */
    axyne_palette_ctl_move(&c, -1);
    AXYNE_TEST_CHECK(c.selection == 1);
    AXYNE_TEST_CHECK(axyne_palette_ctl_activate(&c, (size_t)-1, &action));
    AXYNE_TEST_CHECK(action.kind == AXYNE_PALETTE_ACTION_OPEN_FILE);
    AXYNE_TEST_CHECK(action.path != NULL && strstr(action.path, "alphabet.md") != NULL);
    axyne_palette_action_destroy(&action);
    AXYNE_TEST_CHECK(axyne_palette_ctl_activate(&c, 0, &action));
    AXYNE_TEST_CHECK(action.path != NULL && strstr(action.path, "alpha.c") != NULL);
    axyne_palette_action_destroy(&action);

    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, "nomatchhere"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count == 0 && c.message[0] != '\0');
    AXYNE_TEST_CHECK(axyne_palette_ctl_row_count(&c) == 1);
    AXYNE_TEST_CHECK(!axyne_palette_ctl_activate(&c, (size_t)-1, &action));
    AXYNE_TEST_CHECK(action.kind == AXYNE_PALETTE_ACTION_NONE);

    axyne_palette_ctl_close(&c);
    AXYNE_TEST_CHECK(!c.active && c.paths == NULL && c.list.count == 0);

    /* no workspace: only open documents */
    open_paths[0] = a;
    AXYNE_TEST_STATUS(axyne_palette_ctl_open(&c, NULL, open_paths, 1, ""),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!axyne_palette_ctl_walk_running(&c));
    AXYNE_TEST_CHECK(c.list.count == 1 && strcmp(c.list.items[0].label, "alpha.c") == 0);
    axyne_palette_ctl_destroy(&c);
    return 1;
}

static int test_command_mode(void)
{
    AxynePaletteController c;
    AxynePaletteAction action;
    size_t i;

    axyne_palette_ctl_init(&c, 1, NULL, NULL);
    AXYNE_TEST_STATUS(axyne_palette_ctl_open(&c, NULL, NULL, 0, ">"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.mode == AXYNE_PALETTE_MODE_COMMAND);
    AXYNE_TEST_CHECK(c.list.count > 0);
    AXYNE_TEST_CHECK(strcmp(axyne_palette_ctl_title(c.mode), "명령 실행") == 0);

    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, ">save"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count > 0);
    AXYNE_TEST_CHECK(axyne_palette_ctl_activate(&c, 0, &action));
    AXYNE_TEST_CHECK(action.kind == AXYNE_PALETTE_ACTION_COMMAND);
    AXYNE_TEST_CHECK(action.command == AXYNE_PALETTE_COMMAND_SAVE ||
                     action.command == AXYNE_PALETTE_COMMAND_SAVE_AS);

    /* the three commands that only switch the palette mode */
    for (i = 0; i < 3; ++i) {
        static const char *queries[] = {">go to file", ">go to line", ">go to symbol"};
        static const char *texts[] = {"", ":", "@"};
        size_t row;
        AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, queries[i]), AXYNE_STATUS_OK);
        for (row = 0; row < c.list.count; ++row) {
            AxynePaletteCommandId id = (AxynePaletteCommandId)c.list.items[row].payload;
            if ((i == 0 && id == AXYNE_PALETTE_COMMAND_QUICK_FILE) ||
                (i == 1 && id == AXYNE_PALETTE_COMMAND_GO_TO_LINE) ||
                (i == 2 && id == AXYNE_PALETTE_COMMAND_GO_TO_SYMBOL)) break;
        }
        AXYNE_TEST_CHECK(row < c.list.count);
        AXYNE_TEST_CHECK(axyne_palette_ctl_activate(&c, row, &action));
        AXYNE_TEST_CHECK(action.kind == AXYNE_PALETTE_ACTION_SET_INPUT);
        AXYNE_TEST_CHECK(strcmp(action.text, texts[i]) == 0);
    }

    /* scrolling keeps the selection visible */
    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, ">"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count > AXYNE_PALETTE_VISIBLE_ROWS);
    axyne_palette_ctl_move(&c, -1);
    AXYNE_TEST_CHECK(c.selection == c.list.count - 1);
    AXYNE_TEST_CHECK(c.scroll == c.list.count - AXYNE_PALETTE_VISIBLE_ROWS);
    axyne_palette_ctl_move(&c, 1);
    AXYNE_TEST_CHECK(c.selection == 0 && c.scroll == 0);
    axyne_palette_ctl_scroll(&c, 3);
    AXYNE_TEST_CHECK(c.scroll == 3);
    axyne_palette_ctl_scroll(&c, -100);
    AXYNE_TEST_CHECK(c.scroll == 0);
    axyne_palette_ctl_destroy(&c);
    return 1;
}

static int test_symbol_and_line_mode(void)
{
    FakeDocument document = {"/proj/main.c",
                             "int add(int a, int b)\n{\n    return a + b;\n}\n\n"
                             "struct point { int x; };\n", 6, 1};
    AxynePaletteController c;
    AxynePaletteAction action;

    axyne_palette_ctl_init(&c, 0, fake_document, &document);
    AXYNE_TEST_STATUS(axyne_palette_ctl_open(&c, NULL, NULL, 0, "@"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.mode == AXYNE_PALETTE_MODE_SYMBOL);
    AXYNE_TEST_CHECK(c.list.count >= 2 && has_label(&c, "add") && has_label(&c, "point"));
    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, "@poi"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count == 1);
    AXYNE_TEST_CHECK(axyne_palette_ctl_activate(&c, (size_t)-1, &action));
    AXYNE_TEST_CHECK(action.kind == AXYNE_PALETTE_ACTION_GOTO && action.line == 6);
    AXYNE_TEST_CHECK(action.column >= 1);

    /* line mode: clamped, live hint */
    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, ":3"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.mode == AXYNE_PALETTE_MODE_LINE && c.line_valid);
    AXYNE_TEST_CHECK(strcmp(c.message, "3줄로 이동") == 0 && c.message_actionable);
    AXYNE_TEST_CHECK(axyne_palette_ctl_row_count(&c) == 1);
    AXYNE_TEST_CHECK(axyne_palette_ctl_activate(&c, (size_t)-1, &action));
    AXYNE_TEST_CHECK(action.kind == AXYNE_PALETTE_ACTION_GOTO &&
                     action.line == 3 && action.column == 1);
    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, ":999:4"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_palette_ctl_activate(&c, (size_t)-1, &action));
    AXYNE_TEST_CHECK(action.line == 6 && action.column == 4);
    AXYNE_TEST_CHECK(strcmp(c.message, "6줄 4열로 이동") == 0);
    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, ":abc"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!c.line_valid && !c.message_actionable);
    AXYNE_TEST_CHECK(!axyne_palette_ctl_activate(&c, (size_t)-1, &action));
    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, ":"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!c.line_valid);
    axyne_palette_ctl_close(&c);

    /* unsupported file type */
    document.path = "/proj/notes.txt";
    AXYNE_TEST_STATUS(axyne_palette_ctl_open(&c, NULL, NULL, 0, "@"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count == 0);
    AXYNE_TEST_CHECK(strcmp(c.message, "지원되지 않는 파일 형식") == 0);
    AXYNE_TEST_CHECK(!axyne_palette_ctl_activate(&c, (size_t)-1, &action));
    axyne_palette_ctl_close(&c);

    /* untitled document: no path, no symbols */
    document.path = NULL;
    AXYNE_TEST_STATUS(axyne_palette_ctl_open(&c, NULL, NULL, 0, "@"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count == 0 && c.message[0] != '\0');
    axyne_palette_ctl_close(&c);

    /* no document at all */
    document.available = 0;
    AXYNE_TEST_STATUS(axyne_palette_ctl_open(&c, NULL, NULL, 0, "@"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.list.count == 0 && c.message[0] != '\0');
    AXYNE_TEST_STATUS(axyne_palette_ctl_set_input(&c, ":5"), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(c.line_valid && c.target_line == 1);
    axyne_palette_ctl_destroy(&c);
    return 1;
}

int axyne_test_palette_controller(const char *root)
{
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(test_file_mode(root));
    AXYNE_TEST_CHECK(test_command_mode());
    AXYNE_TEST_CHECK(test_symbol_and_line_mode());
    return 1;
}
