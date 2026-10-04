#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/palette.h"
#include "axyne/problems.h"
#include "axyne/symbols.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

#define CHECK_STR(actual, expected) \
    do { \
        const char *axyne_actual = (actual); \
        if (axyne_actual == NULL || strcmp(axyne_actual, (expected)) != 0) { \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, \
                    axyne_actual == NULL ? "(null)" : axyne_actual, (expected)); \
            return 0; \
        } \
    } while (0)

/* ---- palette: modes, matching, line parsing ------------------------------ */

static int test_modes(void)
{
    const char *query = NULL;
    CHECK(axyne_palette_parse_mode(NULL, &query) == AXYNE_PALETTE_MODE_FILE);
    CHECK_STR(query, "");
    CHECK(axyne_palette_parse_mode("main", &query) == AXYNE_PALETTE_MODE_FILE);
    CHECK_STR(query, "main");
    CHECK(axyne_palette_parse_mode(">save", &query) == AXYNE_PALETTE_MODE_COMMAND);
    CHECK_STR(query, "save");
    CHECK(axyne_palette_parse_mode(">  save as", &query) == AXYNE_PALETTE_MODE_COMMAND);
    CHECK_STR(query, "save as");
    CHECK(axyne_palette_parse_mode("@main", &query) == AXYNE_PALETTE_MODE_SYMBOL);
    CHECK_STR(query, "main");
    CHECK(axyne_palette_parse_mode(":12:3", &query) == AXYNE_PALETTE_MODE_LINE);
    CHECK_STR(query, "12:3");
    CHECK(axyne_palette_parse_mode(">", &query) == AXYNE_PALETTE_MODE_COMMAND);
    CHECK_STR(query, "");
    CHECK(axyne_palette_parse_mode(" >x", &query) == AXYNE_PALETTE_MODE_FILE);
    CHECK(axyne_palette_parse_mode("@x", NULL) == AXYNE_PALETTE_MODE_SYMBOL);
    CHECK_STR(axyne_palette_mode_prefix(AXYNE_PALETTE_MODE_FILE), "");
    CHECK_STR(axyne_palette_mode_prefix(AXYNE_PALETTE_MODE_COMMAND), ">");
    CHECK_STR(axyne_palette_mode_prefix(AXYNE_PALETTE_MODE_SYMBOL), "@");
    CHECK_STR(axyne_palette_mode_prefix(AXYNE_PALETTE_MODE_LINE), ":");
    return 1;
}

static int test_matching(void)
{
    AxynePaletteMatch exact, prefix, boundary, camel, substring, shorter, longer;

    CHECK(axyne_palette_match("main.c", "", &exact) && exact.score == 0 && exact.length == 0);
    CHECK(axyne_palette_match("", "", &exact) && exact.score == 0);
    CHECK(axyne_palette_match(NULL, NULL, &exact) && exact.score == 0);
    CHECK(!axyne_palette_match("", "a", &exact));
    CHECK(!axyne_palette_match("main.c", "mainx", NULL));
    CHECK(axyne_palette_match("main.c", "MAIN.C", &exact));
    CHECK(exact.start == 0 && exact.length == 6);
    CHECK(axyne_palette_match("main.c", "main", &prefix) && prefix.start == 0 && prefix.length == 4);
    CHECK(axyne_palette_match("my_main.c", "main", &boundary) && boundary.start == 3);
    CHECK(axyne_palette_match("getMainWindow", "main", &camel) && camel.start == 3);
    CHECK(axyne_palette_match("domain.c", "main", &substring) && substring.start == 2);
    CHECK(exact.score > prefix.score);
    CHECK(prefix.score > boundary.score);
    CHECK(boundary.score > substring.score);
    CHECK(camel.score == boundary.score + (int)strlen("my_main.c") - (int)strlen("getMainWindow"));
    CHECK(substring.score >= 1);
    CHECK(axyne_palette_match("main.c", "main", &shorter));
    CHECK(axyne_palette_match("mainframe.c", "main", &longer));
    CHECK(shorter.score > longer.score);
    /* the best occurrence wins, not the first one */
    CHECK(axyne_palette_match("xxmain a/main", "main", &boundary) && boundary.start == 9);
    CHECK(axyne_palette_match("a-main", "main", &boundary) && boundary.start == 2);
    CHECK(axyne_palette_match("a b", "b", &boundary) && boundary.start == 2);
    CHECK(axyne_palette_match("src\\main", "main", &boundary) && boundary.start == 4);
    CHECK(axyne_palette_match("a.main", "main", &boundary) && boundary.start == 2);
    /* lower->Upper only: HTTPServer has no boundary before S after P */
    CHECK(axyne_palette_match("HTTPServer", "server", &substring));
    CHECK(substring.score < AXYNE_PALETTE_SCORE_BOUNDARY);
    return 1;
}

static int test_matching_utf8(void)
{
    AxynePaletteMatch match;
    const char *name = "한글파일.c"; /* every Hangul syllable is 3 bytes */
    CHECK(axyne_palette_match(name, "파일", &match));
    CHECK(match.start == 6 && match.length == 6);
    CHECK(axyne_palette_match(name, "한", &match) && match.start == 0 && match.length == 3);
    /* non-ASCII bytes compare exactly, only ASCII folds */
    CHECK(axyne_palette_match("Ä.c", "Ä", &match));
    CHECK(!axyne_palette_match("Ä.c", "ä", &match));
    CHECK(axyne_palette_match("Éa.c", "éa", &match) == 0);
    /* a partial sequence must never match or produce a highlight */
    CHECK(!axyne_palette_match(name, "\xED\x8C", &match));
    CHECK(!axyne_palette_match(name, "\x8C", &match));
    CHECK(!axyne_palette_match(name, "\x8C\x8C", &match));
    CHECK(!axyne_palette_match(name, "\xED", &match));
    /* a valid query never ends mid-sequence either */
    CHECK(axyne_palette_match("파a", "a", &match) && match.start == 3);
    /* invalid UTF-8 in the text is tolerated */
    CHECK(axyne_palette_match("a\xFF" "b.c", "b", &match));
    CHECK(!axyne_palette_match("\x80\x80", "a", &match));
    return 1;
}

static int test_parse_line(void)
{
    size_t line = 0, column = 0;
    CHECK(axyne_palette_parse_line(":12", &line, &column) && line == 12 && column == 1);
    CHECK(axyne_palette_parse_line("12", &line, &column) && line == 12 && column == 1);
    CHECK(axyne_palette_parse_line(":12:5", &line, &column) && line == 12 && column == 5);
    CHECK(axyne_palette_parse_line("12:5", &line, &column) && line == 12 && column == 5);
    CHECK(axyne_palette_parse_line("12:", &line, &column) && line == 12 && column == 1);
    CHECK(axyne_palette_parse_line("  7 : 3  ", &line, &column) && line == 7 && column == 3);
    CHECK(axyne_palette_parse_line("7:0", &line, &column) && column == 1);
    CHECK(axyne_palette_parse_line("1", NULL, NULL));
    CHECK(axyne_palette_parse_line("99999999999999999999999", &line, &column) &&
          line == 2147483647u);
    CHECK(!axyne_palette_parse_line(NULL, &line, &column));
    CHECK(!axyne_palette_parse_line("", &line, &column));
    CHECK(!axyne_palette_parse_line(":", &line, &column));
    CHECK(!axyne_palette_parse_line("abc", &line, &column));
    CHECK(!axyne_palette_parse_line("12a", &line, &column));
    CHECK(!axyne_palette_parse_line("-1", &line, &column));
    CHECK(!axyne_palette_parse_line("+1", &line, &column));
    CHECK(!axyne_palette_parse_line("0", &line, &column));
    CHECK(!axyne_palette_parse_line("1:2:3", &line, &column));
    CHECK(!axyne_palette_parse_line("1:x", &line, &column));
    CHECK(!axyne_palette_parse_line("::1", &line, &column));
    CHECK(!axyne_palette_parse_line("1.5", &line, &column));
    CHECK(axyne_palette_clamp_line(0, 10) == 1);
    CHECK(axyne_palette_clamp_line(5, 10) == 5);
    CHECK(axyne_palette_clamp_line(50, 10) == 10);
    CHECK(axyne_palette_clamp_line(50, 0) == 1);
    CHECK(axyne_palette_clamp_column(0, 4) == 1);
    CHECK(axyne_palette_clamp_column(3, 4) == 3);
    CHECK(axyne_palette_clamp_column(5, 4) == 5);
    CHECK(axyne_palette_clamp_column(99, 4) == 5);
    CHECK(axyne_palette_clamp_column(99, 0) == 1);
    return 1;
}

/* ---- palette: builders ---------------------------------------------------- */

static int test_build_files(void)
{
    const char *paths[] = {
        "/proj/src/main.c", "/proj/src/domain.c", "/proj/CMakeLists.txt",
        NULL, "/proj/include/axyne/main.h", "/proj/src/", "/other/readme.md",
        "/proj/src/MainWindow.c", "/proj/한글/파일.c"
    };
    size_t count = sizeof(paths) / sizeof(paths[0]);
    AxynePaletteList list = {0};
    AxyneError error;

    CHECK(axyne_palette_build_files(paths, count, "/proj", "", &list, &error) == AXYNE_STATUS_OK);
    CHECK(list.count == 7); /* NULL and the directory entry are skipped */
    CHECK_STR(list.items[0].label, "main.c");
    CHECK_STR(list.items[0].detail, "src");
    CHECK_STR(list.items[0].badge, "C");
    CHECK(list.items[0].badge_color == 0x7db5e3);
    CHECK(list.items[0].kind == AXYNE_PALETTE_ITEM_FILE);
    CHECK(list.items[0].payload == 0 && list.items[0].match_len == 0);
    CHECK_STR(list.items[2].label, "CMakeLists.txt");
    CHECK_STR(list.items[2].detail, "");
    CHECK_STR(list.items[2].badge, "CM");
    CHECK_STR(list.items[3].detail, "include/axyne");
    CHECK_STR(list.items[3].badge, "H");
    CHECK_STR(list.items[4].detail, "/other"); /* not below the root */
    CHECK_STR(list.items[6].label, "파일.c");
    CHECK_STR(list.items[6].detail, "한글");
    axyne_palette_list_destroy(&list);
    CHECK(list.items == NULL && list.count == 0);

    CHECK(axyne_palette_build_files(paths, count, "/proj/", "main", &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 4);
    CHECK_STR(list.items[0].label, "main.c"); /* prefix, shorter */
    CHECK(list.items[0].match_start == 0 && list.items[0].match_len == 4);
    CHECK_STR(list.items[1].label, "main.h");
    CHECK_STR(list.items[2].label, "MainWindow.c");
    CHECK(list.items[2].match_start == 0 && list.items[2].match_len == 4);
    CHECK_STR(list.items[3].label, "domain.c"); /* substring ranks last */
    CHECK(list.items[0].payload == 0 && list.items[1].payload == 4 &&
          list.items[2].payload == 7 && list.items[3].payload == 1);
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_files(paths, count, "/proj", "MAIN", &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 4 && strcmp(list.items[3].label, "domain.c") == 0);
    CHECK(list.items[3].match_start == 2 && list.items[3].match_len == 4);
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_files(paths, count, "/proj", "파일", &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 1 && list.items[0].match_start == 0 && list.items[0].match_len == 6);
    axyne_palette_list_destroy(&list);

    /* only the basename is matched */
    CHECK(axyne_palette_build_files(paths, count, "/proj", "axyne", &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 0 && list.items == NULL);
    axyne_palette_list_destroy(&list);

    /* NULL root keeps the file's own directory */
    CHECK(axyne_palette_build_files(paths, 1, NULL, "", &list, NULL) == AXYNE_STATUS_OK);
    CHECK_STR(list.items[0].detail, "/proj/src");
    axyne_palette_list_destroy(&list);
    CHECK(axyne_palette_build_files(paths, 1, "/pro", "", &list, NULL) == AXYNE_STATUS_OK);
    CHECK_STR(list.items[0].detail, "/proj/src"); /* "/pro" is not a parent directory */
    axyne_palette_list_destroy(&list);

    /* separators are preserved */
    {
        const char *windows[] = { "C:\\proj\\src\\app.c", "C:\\proj\\a/b\\app.h" };
#ifdef _WIN32
        CHECK(axyne_palette_build_files(windows, 2, "c:/PROJ", "app", &list, NULL) == AXYNE_STATUS_OK);
        CHECK_STR(list.items[0].detail, "src");
        CHECK_STR(list.items[1].detail, "a/b");
#else
        CHECK(axyne_palette_build_files(windows, 2, "C:\\proj", "app", &list, NULL) == AXYNE_STATUS_OK);
        CHECK_STR(list.items[0].detail, "src");
        CHECK_STR(list.items[1].detail, "a/b");
#endif
        CHECK_STR(list.items[0].label, "app.c");
        axyne_palette_list_destroy(&list);
    }

    CHECK(axyne_palette_build_files(NULL, 0, "/proj", "x", &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 0);
    CHECK(axyne_palette_build_files(NULL, 3, "/proj", "x", &list, &error) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(error.code == AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(axyne_palette_build_files(paths, count, "/proj", "x", NULL, NULL) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    return 1;
}

static int test_build_files_large(void)
{
    enum { N = 120000 };
    char **paths = (char **)calloc(N, sizeof(*paths));
    AxynePaletteList list = {0};
    CHECK(paths != NULL);
    for (size_t i = 0; i < N; ++i) {
        paths[i] = (char *)malloc(64);
        CHECK(paths[i] != NULL);
        (void)snprintf(paths[i], 64, "/r/dir%zu/file%zu.c", i % 100, i);
    }
    CHECK(axyne_palette_build_files((const char *const *)paths, N, "/r", "", &list, NULL) ==
          AXYNE_STATUS_OK);
    CHECK(list.count == AXYNE_PALETTE_MAX_ITEMS);
    CHECK(list.items[0].payload == 0 && list.items[49].payload == 49);
    axyne_palette_list_destroy(&list);
    CHECK(axyne_palette_build_files((const char *const *)paths, N, "/r", "file1", &list, NULL) ==
          AXYNE_STATUS_OK);
    CHECK(list.count == AXYNE_PALETTE_MAX_ITEMS);
    CHECK_STR(list.items[0].label, "file1.c"); /* shortest label first */
    for (size_t i = 1; i < list.count; ++i)
        CHECK(strlen(list.items[i - 1].label) <= strlen(list.items[i].label));
    axyne_palette_list_destroy(&list);
    for (size_t i = 0; i < N; ++i) free(paths[i]);
    free(paths);
    return 1;
}

static int test_commands(void)
{
    size_t count = 0;
    const AxynePaletteCommand *table = axyne_palette_commands(&count);
    AxynePaletteList list = {0};
    char text[64];

    CHECK(table != NULL && count == AXYNE_PALETTE_COMMAND_LAST);
    for (size_t i = 0; i < count; ++i) {
        CHECK(table[i].id >= 1 && table[i].id <= AXYNE_PALETTE_COMMAND_LAST);
        CHECK(table[i].title[0] != '\0' && table[i].keywords[0] != '\0');
        CHECK(axyne_palette_command(table[i].id) == &table[i]);
        for (size_t j = 0; j < i; ++j) CHECK(table[j].id != table[i].id);
    }
    CHECK(axyne_palette_command(AXYNE_PALETTE_COMMAND_NONE) == NULL);
    CHECK(axyne_palette_command((AxynePaletteCommandId)999) == NULL);
    CHECK(axyne_palette_commands(NULL) == table);

    CHECK(axyne_palette_build_commands("", 0, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == count && list.items[0].payload == (int)table[0].id);
    CHECK(list.items[0].kind == AXYNE_PALETTE_ITEM_COMMAND && list.items[0].match_len == 0);
    CHECK_STR(list.items[0].label, "새 파일");
    CHECK_STR(list.items[0].detail, "Ctrl+N");
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_commands("save", 0, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 2);
    CHECK(list.items[0].payload == AXYNE_PALETTE_COMMAND_SAVE);
    CHECK(list.items[1].payload == AXYNE_PALETTE_COMMAND_SAVE_AS);
    CHECK(list.items[0].match_len == 0); /* keyword match: no highlight in the title */
    CHECK_STR(list.items[1].detail, "Ctrl+Shift+S");
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_commands("SAVE", 1, &list, NULL) == AXYNE_STATUS_OK);
    CHECK_STR(list.items[1].detail, "\xE2\x87\xA7\xE2\x8C\x98" "S");
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_commands("저장", 0, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 2);
    CHECK(list.items[0].payload == AXYNE_PALETTE_COMMAND_SAVE);
    CHECK(list.items[0].match_start == 0 && list.items[0].match_len == 6);
    CHECK(list.items[1].payload == AXYNE_PALETTE_COMMAND_SAVE_AS);
    CHECK(list.items[1].match_start == strlen("다른 이름으로 ") && list.items[1].match_len == 6);
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_commands("git", 0, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 4);
    CHECK(list.items[0].match_len == 3); /* title "Git ..." matches as a prefix */
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_commands("run", 0, &list, NULL) == AXYNE_STATUS_OK);
    /* "Runner 설정" matches "run" as a title prefix and ranks first; the Run
     * command is still listed with its shortcut. */
    CHECK(list.items[0].payload == AXYNE_PALETTE_COMMAND_CONFIGURE_RUNNER);
    {
        size_t run_row = list.count;
        for (size_t row = 0; row < list.count; ++row)
            if (list.items[row].payload == AXYNE_PALETTE_COMMAND_RUN) run_row = row;
        CHECK(run_row < list.count);
        CHECK_STR(list.items[run_row].detail, "F5");
    }
    axyne_palette_list_destroy(&list);
    CHECK(axyne_palette_build_commands("run", 1, &list, NULL) == AXYNE_STATUS_OK);
    {
        size_t run_row = list.count;
        for (size_t row = 0; row < list.count; ++row)
            if (list.items[row].payload == AXYNE_PALETTE_COMMAND_RUN) run_row = row;
        CHECK(run_row < list.count);
        CHECK_STR(list.items[run_row].detail, "\xE2\x8C\x98" "R");
    }
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_commands("debug", 0, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 1 && list.items[0].payload == AXYNE_PALETTE_COMMAND_START_DEBUGGING);
    CHECK_STR(list.items[0].detail, "");
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_commands("zzzz", 0, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 0 && list.items == NULL);
    CHECK(axyne_palette_build_commands("x", 0, NULL, NULL) == AXYNE_STATUS_INVALID_ARGUMENT);

    CHECK(axyne_palette_format_shortcut(&axyne_palette_command(AXYNE_PALETTE_COMMAND_PREFERENCES)->shortcut,
                                        1, text, sizeof(text)) == strlen("\xE2\x8C\x98,"));
    CHECK_STR(text, "\xE2\x8C\x98,");
    CHECK(axyne_palette_format_shortcut(&axyne_palette_command(AXYNE_PALETTE_COMMAND_PREFERENCES)->shortcut,
                                        0, text, sizeof(text)) == 0);
    CHECK_STR(text, "");
    {
        char small[5];
        CHECK(axyne_palette_format_shortcut(&axyne_palette_command(AXYNE_PALETTE_COMMAND_SAVE_AS)->shortcut,
                                            0, small, sizeof(small)) == strlen("Ctrl+Shift+S"));
        CHECK_STR(small, "Ctrl");
        CHECK(axyne_palette_format_shortcut(&axyne_palette_command(AXYNE_PALETTE_COMMAND_SAVE)->shortcut,
                                            0, NULL, 0) == strlen("Ctrl+S"));
        CHECK(axyne_palette_format_shortcut(NULL, 0, small, sizeof(small)) == 0);
    }
    return 1;
}

static const char sample[] =
    "#include <stdio.h>\n"
    "#define MAX 10\n"
    "#define SQUARE(x) ((x)*(x))\n"
    "#define LONG_MACRO(a, b) \\\n"
    "    do { a; b; } while (0)\n"
    "typedef struct Point { int x; int y; } Point;\n"
    "struct Node { struct Node *next; };\n"
    "union U { int a; float b; };\n"
    "enum Color { RED, GREEN };\n"
    "typedef enum { A, B } Mode;\n"
    "static int counter = 0;\n"
    "int table[4] = {1, 2, 3, 4};\n"
    "const char *name = \"int fake(void) { }\";\n"
    "int proto(int a);\n"
    "static int\n"
    "helper(int a,\n"
    "       int b)\n"
    "{\n"
    "    int local = 0;\n"
    "    return a + b;\n"
    "}\n"
    "// int commented(void) { }\n"
    "/* int blocked(void) { } */\n"
    "void run(void) { if (x) { y(); } }\n";

typedef struct Expected {
    const char *name;
    AxyneSymbolKind kind;
    size_t line, column;
} Expected;

static const Expected sample_expected[] = {
    { "MAX", AXYNE_SYMBOL_MACRO, 2, 9 },
    { "SQUARE", AXYNE_SYMBOL_MACRO, 3, 9 },
    { "LONG_MACRO", AXYNE_SYMBOL_MACRO, 4, 9 },
    { "Point", AXYNE_SYMBOL_TYPE, 6, 16 },
    { "Node", AXYNE_SYMBOL_TYPE, 7, 8 },
    { "U", AXYNE_SYMBOL_TYPE, 8, 7 },
    { "Color", AXYNE_SYMBOL_TYPE, 9, 6 },
    { "Mode", AXYNE_SYMBOL_TYPE, 10, 23 },
    { "counter", AXYNE_SYMBOL_VARIABLE, 11, 12 },
    { "table", AXYNE_SYMBOL_VARIABLE, 12, 5 },
    { "name", AXYNE_SYMBOL_VARIABLE, 13, 13 },
    { "helper", AXYNE_SYMBOL_FUNCTION, 16, 1 },
    { "run", AXYNE_SYMBOL_FUNCTION, 24, 6 }
};

static int expect_symbols(const AxyneSymbolList *list, const Expected *expected,
                          size_t count)
{
    if (list->count != count) {
        fprintf(stderr, "symbol count %zu, expected %zu\n", list->count, count);
        for (size_t i = 0; i < list->count; ++i)
            fprintf(stderr, "  %s kind %d %zu:%zu\n", list->items[i].name,
                    (int)list->items[i].kind, list->items[i].line, list->items[i].column);
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(list->items[i].name, expected[i].name) != 0 ||
            list->items[i].kind != expected[i].kind ||
            list->items[i].line != expected[i].line ||
            list->items[i].column != expected[i].column) {
            fprintf(stderr, "symbol %zu: %s kind %d %zu:%zu, expected %s kind %d %zu:%zu\n",
                    i, list->items[i].name, (int)list->items[i].kind,
                    list->items[i].line, list->items[i].column, expected[i].name,
                    (int)expected[i].kind, expected[i].line, expected[i].column);
            return 0;
        }
    }
    return 1;
}

static int test_build_symbols(void)
{
    AxyneSymbolList symbols = {0};
    AxynePaletteList list = {0};

    CHECK(axyne_symbols_scan(sample, sizeof(sample) - 1, &symbols, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_palette_build_symbols(&symbols, "", &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == symbols.count);
    CHECK_STR(list.items[0].label, "MAX");
    CHECK_STR(list.items[0].badge, "#");
    CHECK_STR(list.items[0].detail, "줄 2");
    CHECK(list.items[0].payload == 2 && list.items[0].payload2 == 9);
    CHECK(list.items[0].kind == AXYNE_PALETTE_ITEM_SYMBOL);
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_symbols(&symbols, "n", &list, NULL) == AXYNE_STATUS_OK);
    /* prefix "name"/"Node" first (shorter wins), then substrings */
    CHECK(list.count >= 4);
    CHECK_STR(list.items[0].label, "Node");
    CHECK_STR(list.items[1].label, "name");
    CHECK(list.items[0].match_start == 0 && list.items[0].match_len == 1);
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_symbols(&symbols, "help", &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 1 && list.items[0].payload == 16);
    CHECK_STR(list.items[0].badge, "f");
    axyne_palette_list_destroy(&list);

    CHECK(axyne_palette_build_symbols(NULL, "x", &list, NULL) == AXYNE_STATUS_OK && list.count == 0);
    axyne_symbols_destroy(&symbols);
    return 1;
}

/* ---- symbol scanner --------------------------------------------------------- */

static char *to_crlf(const char *text, size_t length, size_t *out_length)
{
    char *out = (char *)malloc(length * 2 + 1);
    size_t n = 0;
    if (out == NULL) return NULL;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') out[n++] = '\r';
        out[n++] = text[i];
    }
    out[n] = '\0';
    *out_length = n;
    return out;
}

static int test_symbols(void)
{
    AxyneSymbolList list = {0};
    AxyneError error;
    size_t crlf_length;
    char *crlf;

    CHECK(axyne_symbols_scan(sample, sizeof(sample) - 1, &list, &error) == AXYNE_STATUS_OK);
    CHECK(error.code == AXYNE_STATUS_OK && !list.truncated);
    CHECK(expect_symbols(&list, sample_expected,
                         sizeof(sample_expected) / sizeof(sample_expected[0])));
    axyne_symbols_destroy(&list);
    CHECK(list.items == NULL && list.count == 0);

    /* CRLF line endings give identical symbols and positions */
    crlf = to_crlf(sample, sizeof(sample) - 1, &crlf_length);
    CHECK(crlf != NULL);
    CHECK(axyne_symbols_scan(crlf, crlf_length, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(expect_symbols(&list, sample_expected,
                         sizeof(sample_expected) / sizeof(sample_expected[0])));
    axyne_symbols_destroy(&list);
    free(crlf);

    /* lone CR counts as a line break */
    {
        const char text[] = "int a;\rint f(void)\r{\r}\r";
        const Expected expected[] = { { "a", AXYNE_SYMBOL_VARIABLE, 1, 5 },
                                      { "f", AXYNE_SYMBOL_FUNCTION, 2, 5 } };
        CHECK(axyne_symbols_scan(text, sizeof(text) - 1, &list, NULL) == AXYNE_STATUS_OK);
        CHECK(expect_symbols(&list, expected, 2));
        axyne_symbols_destroy(&list);
    }

    /* missing final newline, no trailing semicolon/brace tricks */
    {
        const char text[] = "int f(void) { return 1; }";
        const Expected expected[] = { { "f", AXYNE_SYMBOL_FUNCTION, 1, 5 } };
        CHECK(axyne_symbols_scan(text, sizeof(text) - 1, &list, NULL) == AXYNE_STATUS_OK);
        CHECK(expect_symbols(&list, expected, 1));
        axyne_symbols_destroy(&list);
    }
    {
        const char text[] = "#define LAST 1";
        const Expected expected[] = { { "LAST", AXYNE_SYMBOL_MACRO, 1, 9 } };
        CHECK(axyne_symbols_scan(text, sizeof(text) - 1, &list, NULL) == AXYNE_STATUS_OK);
        CHECK(expect_symbols(&list, expected, 1));
        axyne_symbols_destroy(&list);
    }

    /* the text is not NUL-terminated: only `length` bytes may be read */
    {
        char *exact = (char *)malloc(15);
        const Expected expected[] = { { "g", AXYNE_SYMBOL_FUNCTION, 1, 5 } };
        CHECK(exact != NULL);
        memcpy(exact, "int g(void) {}\nXXXX", 15);
        CHECK(axyne_symbols_scan(exact, 14, &list, NULL) == AXYNE_STATUS_OK);
        CHECK(expect_symbols(&list, expected, 1));
        axyne_symbols_destroy(&list);
        free(exact);
    }

    /* C++ and Objective-C flavours */
    {
        const char text[] =
            "namespace ns {\n"
            "class Widget : public Base {\n"
            "  public:\n"
            "    void method() { call(); }\n"
            "};\n"
            "int free_fn(int a) const noexcept { return a; }\n"
            "}\n"
            "extern \"C\" {\n"
            "int c_api(void) { return 0; }\n"
            "}\n"
            "template <typename T> T identity(T v) { return v; }\n"
            "Widget::Widget(int a) : Base(a), n(0) {}\n"
            "@implementation Thing\n"
            "- (void)doIt { }\n"
            "@end\n";
        CHECK(axyne_symbols_scan(text, sizeof(text) - 1, &list, NULL) == AXYNE_STATUS_OK);
        {
            int widget = 0, free_fn = 0, c_api = 0, identity = 0, method = 0;
            for (size_t i = 0; i < list.count; ++i) {
                if (strcmp(list.items[i].name, "Widget") == 0 &&
                    list.items[i].kind == AXYNE_SYMBOL_TYPE) widget = 1;
                if (strcmp(list.items[i].name, "free_fn") == 0) free_fn = 1;
                if (strcmp(list.items[i].name, "c_api") == 0) c_api = 1;
                if (strcmp(list.items[i].name, "identity") == 0) identity = 1;
                if (strcmp(list.items[i].name, "method") == 0) method = 1;
            }
            CHECK(widget && free_fn && c_api && identity && !method);
        }
        axyne_symbols_destroy(&list);
    }

    /* comments, strings, raw strings, continuations hide fake symbols */
    {
        const char text[] =
            "/* #define HIDDEN 1\n   int fake1(void) { } */\n"
            "// #define HIDDEN2 2 \\\n int fake2(void) { }\n"
            "const char *s = \"\\\" int fake3(void) { }\";\n"
            "char c = '\"';\n"
            "const char *r = R\"x( int fake4(void) { } )x\";\n"
            "#define TWO(a) \\\n   int fake5(void) { }\n"
            "int real(void) { return 0; }\n";
        const Expected expected[] = {
            { "s", AXYNE_SYMBOL_VARIABLE, 5, 13 },
            { "c", AXYNE_SYMBOL_VARIABLE, 6, 6 },
            { "r", AXYNE_SYMBOL_VARIABLE, 7, 13 },
            { "TWO", AXYNE_SYMBOL_MACRO, 8, 9 },
            { "real", AXYNE_SYMBOL_FUNCTION, 10, 5 }
        };
        CHECK(axyne_symbols_scan(text, sizeof(text) - 1, &list, NULL) == AXYNE_STATUS_OK);
        CHECK(expect_symbols(&list, expected, 5));
        axyne_symbols_destroy(&list);
    }

    /* empty and degenerate input */
    CHECK(axyne_symbols_scan(NULL, 0, &list, NULL) == AXYNE_STATUS_OK && list.count == 0);
    CHECK(axyne_symbols_scan("", 0, &list, NULL) == AXYNE_STATUS_OK && list.count == 0);
    axyne_symbols_destroy(&list);
    CHECK(axyne_symbols_scan(NULL, 3, &list, &error) == AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(axyne_symbols_scan("x", 1, NULL, NULL) == AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_symbols_destroy(NULL);

    {
        static const char *const nasty[] = {
            "}}}}{{{{", "{{{{{{{{{{{{{{{", "/* unterminated", "\"unterminated",
            "'", "R\"(", "R\"abc", "#define", "#define \\", "#", "int (", "int f(",
            "int f(void) {", "typedef", "typedef struct {", "struct", "enum { A,",
            "\\", "int a = {", "#if 0\nint f(void) {\n#endif\n}\n", "))))((((",
            "int \xC3", "\xFF\xFE\xFD", "int \xED\x95\x9C\xEA\xB8\x80(void) { }"
        };
        for (size_t i = 0; i < sizeof(nasty) / sizeof(nasty[0]); ++i) {
            size_t n = strlen(nasty[i]);
            char *copy = (char *)malloc(n);
            CHECK(copy != NULL);
            memcpy(copy, nasty[i], n); /* exact-size heap block: ASan sees overreads */
            CHECK(axyne_symbols_scan(copy, n, &list, NULL) == AXYNE_STATUS_OK);
            axyne_symbols_destroy(&list);
            free(copy);
        }
    }

    /* every prefix of the sample scans without reading out of bounds */
    for (size_t n = 0; n <= sizeof(sample) - 1; ++n) {
        char *copy = (char *)malloc(n == 0 ? 1 : n);
        CHECK(copy != NULL);
        memcpy(copy, sample, n);
        CHECK(axyne_symbols_scan(copy, n, &list, NULL) == AXYNE_STATUS_OK);
        axyne_symbols_destroy(&list);
        free(copy);
    }
    return 1;
}

static int test_symbols_limits(void)
{
    AxyneSymbolList list = {0};
    size_t capacity = 6000 * 24, length = 0;
    char *text = (char *)malloc(capacity);
    CHECK(text != NULL);
    for (int i = 0; i < 6000; ++i)
        length += (size_t)snprintf(text + length, capacity - length,
                                   "int f%d(void){}\n", i);
    CHECK(axyne_symbols_scan(text, length, &list, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == AXYNE_SYMBOLS_MAX && list.truncated);
    CHECK_STR(list.items[0].name, "f0");
    CHECK(list.items[AXYNE_SYMBOLS_MAX - 1].line == AXYNE_SYMBOLS_MAX);
    axyne_symbols_destroy(&list);
    free(text);

    /* one very long line and very deep nesting stay linear and safe */
    {
        size_t n = 3000000;
        char *deep = (char *)malloc(n + 1);
        CHECK(deep != NULL);
        memset(deep, '{', n);
        deep[n] = '\0';
        CHECK(axyne_symbols_scan(deep, n, &list, NULL) == AXYNE_STATUS_OK && list.count == 0);
        axyne_symbols_destroy(&list);
        memset(deep, '(', n);
        CHECK(axyne_symbols_scan(deep, n, &list, NULL) == AXYNE_STATUS_OK);
        axyne_symbols_destroy(&list);
        memset(deep, 'a', n);
        CHECK(axyne_symbols_scan(deep, n, &list, NULL) == AXYNE_STATUS_OK);
        axyne_symbols_destroy(&list);
        free(deep);
    }
    return 1;
}

static int test_symbols_supports_file(void)
{
    static const char *const yes[] = {
        "a.c", "a.h", "a.cc", "a.cpp", "a.cxx", "a.hpp", "a.m", "a.mm", "A.C",
        "main.CPP", "/x/y/z.h", "C:\\x\\y.Hpp", "dir.d/file.c", "a.b.c"
    };
    static const char *const no[] = {
        "a.txt", "a", "", ".c", "a.", "a.cs", "a.cpp.bak", "a.hh", "a.py",
        "dir.c/file", "Makefile", "a.mmm", "/x/.h"
    };
    for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); ++i)
        if (!axyne_symbols_supports_file(yes[i])) {
            fprintf(stderr, "expected support: %s\n", yes[i]);
            return 0;
        }
    for (size_t i = 0; i < sizeof(no) / sizeof(no[0]); ++i)
        if (axyne_symbols_supports_file(no[i])) {
            fprintf(stderr, "expected no support: %s\n", no[i]);
            return 0;
        }
    CHECK(!axyne_symbols_supports_file(NULL));
    return 1;
}

/* ---- problems ---------------------------------------------------------------- */

static AxyneProblem make(int severity, const char *path, size_t line, size_t column,
                         const char *code, const char *message)
{
    AxyneProblem problem;
    memset(&problem, 0, sizeof(problem));
    problem.severity = severity;
    problem.path = (char *)path;
    problem.line = line;
    problem.column = column;
    problem.code = (char *)code;
    problem.message = (char *)message;
    return problem;
}

static int test_problem_list(void)
{
    AxyneProblemList list = {0};
    AxyneProblemCounts counts;
    AxyneError error;
    char text[128];
    AxyneProblem a[2], b[1], bad;

    a[0] = make(AXYNE_PROBLEM_ERROR, NULL, 1, 1, "E1", "first");
    a[1] = make(AXYNE_PROBLEM_HINT, "ignored.c", 2, 2, NULL, "hint");
    b[0] = make(AXYNE_PROBLEM_WARNING, NULL, 3, 3, "", "warn");
    axyne_problems_counts(&list, &counts);
    CHECK(counts.total == 0);
    axyne_problems_summary(&list, text, sizeof(text));
    CHECK_STR(text, "오류 0개 \xC2\xB7 경고 0개 \xC2\xB7 정보 0개");

    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", a, 2, &error) ==
          AXYNE_STATUS_OK);
    CHECK(list.count == 2 && list.items[1].origin == AXYNE_PROBLEM_ORIGIN_LSP);
    CHECK_STR(list.items[1].path, "/p/a.c"); /* the path argument wins */
    CHECK_STR(list.items[1].code, "");
    CHECK(list.items[0].path != a[0].path); /* owned copies */
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/b.c", b, 1, NULL) ==
          AXYNE_STATUS_OK);
    {
        AxyneProblem built[2];
        built[0] = make(AXYNE_PROBLEM_ERROR, "/p/a.c", 9, 1, "C1", "build error");
        built[1] = make(AXYNE_PROBLEM_INFORMATION, "/p/c.c", 4, 1, "", "build note");
        CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_BUILD, NULL, built, 2, NULL) ==
              AXYNE_STATUS_OK);
    }
    CHECK(list.count == 5);
    axyne_problems_counts(&list, &counts);
    CHECK(counts.errors == 2 && counts.warnings == 1 && counts.information == 1 &&
          counts.hints == 1 && counts.total == 5);
    CHECK(axyne_problems_summary(&list, text, sizeof(text)) == strlen(text));
    CHECK_STR(text, "오류 2개 \xC2\xB7 경고 1개 \xC2\xB7 정보 2개"); /* hint counts as information */
    {
        char tiny[8];
        size_t needed = axyne_problems_summary(&list, tiny, sizeof(tiny));
        CHECK(needed == strlen(text) && strlen(tiny) < sizeof(tiny));
        CHECK(axyne_problems_summary(&list, NULL, 0) == needed);
    }

    /* replacing LSP diagnostics of a.c keeps b.c's and the build problems */
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", b, 1, NULL) ==
          AXYNE_STATUS_OK);
    CHECK(list.count == 4);
    axyne_problems_counts(&list, &counts);
    CHECK(counts.errors == 1 && counts.warnings == 2 && counts.hints == 0);
    /* an empty replacement clears just that file */
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", NULL, 0, NULL) ==
          AXYNE_STATUS_OK);
    CHECK(list.count == 3);
    /* a new build run replaces only build problems */
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_BUILD, NULL, NULL, 0, NULL) ==
          AXYNE_STATUS_OK);
    CHECK(list.count == 1 && list.items[0].origin == AXYNE_PROBLEM_ORIGIN_LSP);
    CHECK_STR(list.items[0].path, "/p/b.c");

    /* append and clear by source */
    {
        AxyneProblem line = make(AXYNE_PROBLEM_ERROR, "/p/x.c", 1, 1, "", "streamed");
        CHECK(axyne_problems_append(&list, AXYNE_PROBLEM_ORIGIN_BUILD, &line, NULL) == AXYNE_STATUS_OK);
        CHECK(axyne_problems_append(&list, AXYNE_PROBLEM_ORIGIN_BUILD, &line, NULL) == AXYNE_STATUS_OK);
        CHECK(list.count == 3);
        axyne_problems_clear_source(&list, AXYNE_PROBLEM_ORIGIN_BUILD, "/p/other.c");
        CHECK(list.count == 3);
        axyne_problems_clear_source(&list, AXYNE_PROBLEM_ORIGIN_BUILD, NULL);
        CHECK(list.count == 1);
        axyne_problems_clear_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/b.c");
        CHECK(list.count == 0);
    }

    /* invalid input leaves the list untouched */
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", a, 2, NULL) ==
          AXYNE_STATUS_OK);
    bad = make(0, "/p/z.c", 1, 1, "", "bad severity");
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", &bad, 1, &error) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(error.code == AXYNE_STATUS_INVALID_ARGUMENT && list.count == 2);
    bad = make(5, "/p/z.c", 1, 1, "", "bad severity");
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", &bad, 1, NULL) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    bad = make(1, NULL, 1, 1, "", "no path");
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_BUILD, NULL, &bad, 1, NULL) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(axyne_problems_set_source(&list, (AxyneProblemOrigin)7, "/p/a.c", a, 2, NULL) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(axyne_problems_set_source(NULL, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", a, 2, NULL) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", NULL, 2, NULL) ==
          AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(list.count == 2);
    CHECK(axyne_problems_append(&list, AXYNE_PROBLEM_ORIGIN_BUILD, &bad, NULL) ==
          AXYNE_STATUS_INVALID_ARGUMENT);

    axyne_problems_clear_all(&list);
    CHECK(list.count == 0);
    axyne_problems_destroy(&list);
    CHECK(list.items == NULL);
    axyne_problems_destroy(NULL);
    axyne_problem_destroy(NULL);
    return 1;
}

static int test_problem_limit(void)
{
    AxyneProblemList list = {0};
    AxyneProblem item = make(AXYNE_PROBLEM_WARNING, "/p/a.c", 1, 1, "", "m");
    AxyneProblem *many = (AxyneProblem *)calloc(AXYNE_PROBLEMS_MAX + 10, sizeof(*many));
    CHECK(many != NULL);
    for (size_t i = 0; i < AXYNE_PROBLEMS_MAX + 10; ++i) many[i] = item;
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", many,
                                    AXYNE_PROBLEMS_MAX + 10, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == AXYNE_PROBLEMS_MAX && list.truncated);
    CHECK(axyne_problems_append(&list, AXYNE_PROBLEM_ORIGIN_BUILD, &item, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == AXYNE_PROBLEMS_MAX);
    axyne_problems_clear_all(&list);
    CHECK(!list.truncated);
    axyne_problems_destroy(&list);
    free(many);
    return 1;
}

static int test_path_helpers(void)
{
    char *resolved;
    CHECK(axyne_problems_path_equal(NULL, NULL));
    CHECK(!axyne_problems_path_equal(NULL, "a"));
    CHECK(!axyne_problems_path_equal("a", NULL));
    CHECK(axyne_problems_path_equal("/a/b.c", "/a/b.c"));
    CHECK(!axyne_problems_path_equal("/a/b.c", "/a/b.cc"));
    CHECK(!axyne_problems_path_equal("/a/b.c", "/a/b"));
#ifdef _WIN32
    CHECK(axyne_problems_path_equal("C:\\A\\b.c", "c:/a/B.C"));
#else
    CHECK(!axyne_problems_path_equal("/a/B.c", "/a/b.c"));
    CHECK(!axyne_problems_path_equal("/a/b.c", "\\a\\b.c"));
#endif

    resolved = axyne_problems_resolve_path("src/a.c", "/home/u/proj");
    CHECK(resolved != NULL); CHECK_STR(resolved, "/home/u/proj/src/a.c"); free(resolved);
    resolved = axyne_problems_resolve_path("../b.c", "/home/u/proj/build/");
    CHECK(resolved != NULL); CHECK_STR(resolved, "/home/u/proj/b.c"); free(resolved);
    resolved = axyne_problems_resolve_path("./x/../y//z.c", "/w");
    CHECK(resolved != NULL); CHECK_STR(resolved, "/w/y/z.c"); free(resolved);
    resolved = axyne_problems_resolve_path("/abs/./p.c", "/w");
    CHECK(resolved != NULL); CHECK_STR(resolved, "/abs/p.c"); free(resolved);
    resolved = axyne_problems_resolve_path("../../../x.c", "/w");
    CHECK(resolved != NULL); CHECK_STR(resolved, "/x.c"); free(resolved);
    resolved = axyne_problems_resolve_path("a.c", NULL);
    CHECK(resolved != NULL); CHECK_STR(resolved, "a.c"); free(resolved);
    resolved = axyne_problems_resolve_path("../a/../../b.c", "");
    CHECK(resolved != NULL); CHECK_STR(resolved, "../../b.c"); free(resolved);
    resolved = axyne_problems_resolve_path("C:\\x\\y.c", "/w");
    CHECK(resolved != NULL); CHECK_STR(resolved, "C:\\x\\y.c"); free(resolved);
    resolved = axyne_problems_resolve_path("C:/x/../y.c", "/w");
    CHECK(resolved != NULL); CHECK_STR(resolved, "C:/y.c"); free(resolved);
    resolved = axyne_problems_resolve_path("..\\src\\a.c", "C:\\proj\\build");
    CHECK(resolved != NULL); CHECK_STR(resolved, "C:\\proj\\src\\a.c"); free(resolved);
    resolved = axyne_problems_resolve_path("a.c", "D:\\");
    CHECK(resolved != NULL); CHECK_STR(resolved, "D:\\a.c"); free(resolved);
    resolved = axyne_problems_resolve_path(".", "/w");
    CHECK(resolved != NULL); CHECK_STR(resolved, "/w"); free(resolved);
    resolved = axyne_problems_resolve_path("..", "/");
    CHECK(resolved != NULL); CHECK_STR(resolved, "/"); free(resolved);
    resolved = axyne_problems_resolve_path("\\\\srv\\share\\a.c", "/w");
    CHECK(resolved != NULL); CHECK_STR(resolved, "\\\\srv\\share\\a.c"); free(resolved);
    CHECK(axyne_problems_resolve_path(NULL, "/w") == NULL);
    CHECK(axyne_problems_resolve_path("", "/w") == NULL);
    return 1;
}

static int test_filter(void)
{
    AxyneProblem problem = make(AXYNE_PROBLEM_ERROR, "/Proj/Src/Main.c", 1, 1,
                                "C2065", "'Value' undeclared 한글");
    CHECK(axyne_problem_matches(&problem, NULL));
    CHECK(axyne_problem_matches(&problem, ""));
    CHECK(axyne_problem_matches(&problem, "UNDECLARED"));
    CHECK(axyne_problem_matches(&problem, "value'"));
    CHECK(axyne_problem_matches(&problem, "c2065"));
    CHECK(axyne_problem_matches(&problem, "src/main"));
    CHECK(axyne_problem_matches(&problem, "한글"));
    CHECK(!axyne_problem_matches(&problem, "C2066"));
    CHECK(!axyne_problem_matches(&problem, "nothing"));
    CHECK(!axyne_problem_matches(&problem, "UNDECLARED 한글 and more"));
    CHECK(!axyne_problem_matches(NULL, "x"));
    return 1;
}

static int row_is(const AxyneProblemRow *row, AxyneProblemRowKind kind, int indent,
                  const char *file, size_t line, size_t column)
{
    if (row->kind != kind || row->indent != indent || strcmp(row->file_name, file) != 0 ||
        row->line != line || row->column != column) {
        fprintf(stderr, "row mismatch: kind %d indent %d file %s %zu:%zu (wanted %d %d %s %zu:%zu)\n",
                (int)row->kind, row->indent, row->file_name, row->line, row->column,
                (int)kind, indent, file, line, column);
        return 0;
    }
    return 1;
}

static int test_rows(void)
{
    AxyneProblemList list = {0};
    AxyneProblemRow *rows = NULL;
    size_t count = 0;
    AxyneProblemCollapsed collapsed = {0};
    AxyneError error;
    AxyneProblem a[3], b[2], c[1], built[1];

    a[0] = make(AXYNE_PROBLEM_ERROR, NULL, 5, 1, "", "e5");
    a[1] = make(AXYNE_PROBLEM_WARNING, NULL, 2, 4, "", "w2");
    a[2] = make(AXYNE_PROBLEM_ERROR, NULL, 3, 9, "-Wx", "e3");
    b[0] = make(AXYNE_PROBLEM_WARNING, NULL, 1, 1, "", "b-warn");
    b[1] = make(AXYNE_PROBLEM_INFORMATION, NULL, 1, 1, "", "b-info same position");
    c[0] = make(AXYNE_PROBLEM_INFORMATION, NULL, 2, 2, "", "c-info");
    built[0] = make(AXYNE_PROBLEM_ERROR, "/p/sub/B.c", 9, 3, "C1", "b-build-error");
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/a.c", a, 3, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/sub/B.c", b, 2, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_LSP, "/p/c.c", c, 1, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_problems_set_source(&list, AXYNE_PROBLEM_ORIGIN_BUILD, NULL, built, 1, NULL) == AXYNE_STATUS_OK);

    CHECK(axyne_problems_rows(&list, NULL, "/p/a.c", NULL, &rows, &count, &error) == AXYNE_STATUS_OK);
    CHECK(count == 3 + 4 + 2);
    CHECK(row_is(&rows[0], AXYNE_PROBLEM_ROW_PROBLEM, 0, "a.c", 3, 9));
    CHECK_STR(rows[0].message, "e3");
    CHECK_STR(rows[0].code, "-Wx");
    CHECK(rows[0].severity == AXYNE_PROBLEM_ERROR);
    CHECK(row_is(&rows[1], AXYNE_PROBLEM_ROW_PROBLEM, 0, "a.c", 5, 1));
    CHECK(row_is(&rows[2], AXYNE_PROBLEM_ROW_PROBLEM, 0, "a.c", 2, 4));
    CHECK_STR(rows[2].path, "/p/a.c");
    /* B.c has an error through the build, so its group comes first */
    CHECK(row_is(&rows[3], AXYNE_PROBLEM_ROW_GROUP, 0, "B.c", 0, 0));
    CHECK(rows[3].count == 3 && rows[3].expanded == 1 && rows[3].severity == AXYNE_PROBLEM_ERROR);
    CHECK(rows[3].message == NULL);
    CHECK_STR(rows[3].path, "/p/sub/B.c");
    CHECK(row_is(&rows[4], AXYNE_PROBLEM_ROW_PROBLEM, 1, "B.c", 9, 3));
    CHECK(row_is(&rows[5], AXYNE_PROBLEM_ROW_PROBLEM, 1, "B.c", 1, 1));
    CHECK(rows[5].severity == AXYNE_PROBLEM_WARNING);
    CHECK(rows[6].severity == AXYNE_PROBLEM_INFORMATION && rows[6].indent == 1);
    CHECK(row_is(&rows[7], AXYNE_PROBLEM_ROW_GROUP, 0, "c.c", 0, 0));
    CHECK(rows[7].count == 1);
    CHECK(row_is(&rows[8], AXYNE_PROBLEM_ROW_PROBLEM, 1, "c.c", 2, 2));
    CHECK(list.items[rows[8].problem_index].line == 2);
    axyne_problems_rows_destroy(rows, count);

    /* collapsing a group hides its problems but keeps the group row */
    CHECK(axyne_problems_collapsed_set(&collapsed, "/p/sub/B.c", 0, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_problems_collapsed_contains(&collapsed, "/p/sub/B.c"));
    CHECK(!axyne_problems_collapsed_contains(&collapsed, "/p/c.c"));
    CHECK(axyne_problems_rows(&list, "", "/p/a.c", &collapsed, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 3 + 1 + 2);
    CHECK(rows[3].kind == AXYNE_PROBLEM_ROW_GROUP && rows[3].expanded == 0 && rows[3].count == 3);
    CHECK(rows[4].kind == AXYNE_PROBLEM_ROW_GROUP && rows[4].expanded == 1);
    axyne_problems_rows_destroy(rows, count);
    CHECK(axyne_problems_collapsed_set(&collapsed, "/p/sub/B.c", 0, NULL) == AXYNE_STATUS_OK);
    CHECK(collapsed.count == 1);
    CHECK(axyne_problems_collapsed_set(&collapsed, "/p/sub/B.c", 1, NULL) == AXYNE_STATUS_OK);
    CHECK(collapsed.count == 0 && !axyne_problems_collapsed_contains(&collapsed, "/p/sub/B.c"));
    CHECK(axyne_problems_collapsed_set(&collapsed, "/p/x.c", 1, NULL) == AXYNE_STATUS_OK);
    CHECK(collapsed.count == 0);
    CHECK(axyne_problems_collapsed_set(NULL, "/p/x.c", 1, &error) == AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_problems_collapsed_destroy(&collapsed);

    /* no active file: everything is grouped */
    CHECK(axyne_problems_rows(&list, NULL, NULL, NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 10);
    CHECK(rows[0].kind == AXYNE_PROBLEM_ROW_GROUP && strcmp(rows[0].file_name, "a.c") == 0);
    /* a.c and B.c both have errors: file name decides, case-insensitively */
    CHECK(strcmp(rows[4].file_name, "B.c") == 0 && rows[4].kind == AXYNE_PROBLEM_ROW_GROUP);
    axyne_problems_rows_destroy(rows, count);

    /* filter: message, code and path, case-insensitive */
    CHECK(axyne_problems_rows(&list, "B-BUILD", "/p/a.c", NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 2 && rows[0].kind == AXYNE_PROBLEM_ROW_GROUP && rows[0].count == 1);
    axyne_problems_rows_destroy(rows, count);
    CHECK(axyne_problems_rows(&list, "-wx", "/p/a.c", NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 1 && rows[0].line == 3);
    axyne_problems_rows_destroy(rows, count);
    CHECK(axyne_problems_rows(&list, "SUB/", "/p/a.c", NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 1 + 3);
    axyne_problems_rows_destroy(rows, count);
    CHECK(axyne_problems_rows(&list, "no such text", "/p/a.c", NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 0 && rows == NULL);
    axyne_problems_rows_destroy(rows, count);

    /* empty list and invalid arguments */
    axyne_problems_clear_all(&list);
    CHECK(axyne_problems_rows(&list, NULL, "/p/a.c", NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 0 && rows == NULL);
    CHECK(axyne_problems_rows(NULL, NULL, NULL, NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(axyne_problems_rows(&list, NULL, NULL, NULL, NULL, &count, &error) == AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_problems_destroy(&list);
    return 1;
}

static int test_rows_many(void)
{
    AxyneProblemList list = {0};
    AxyneProblemRow *rows = NULL;
    size_t count = 0;
    for (int i = 0; i < 3000; ++i) {
        char path[32], message[32];
        AxyneProblem item;
        (void)snprintf(path, sizeof(path), "/p/f%d.c", i % 300);
        (void)snprintf(message, sizeof(message), "m%d", i);
        item = make(1 + i % 4, path, (size_t)(i % 97) + 1, 1, "", message);
        CHECK(axyne_problems_append(&list, AXYNE_PROBLEM_ORIGIN_BUILD, &item, NULL) == AXYNE_STATUS_OK);
    }
    CHECK(axyne_problems_rows(&list, NULL, "/p/f7.c", NULL, &rows, &count, NULL) == AXYNE_STATUS_OK);
    CHECK(count == 3000 + 299);
    for (size_t i = 1; i < 10; ++i) { /* the active file's rows are sorted */
        CHECK(rows[i - 1].severity < rows[i].severity ||
              (rows[i - 1].severity == rows[i].severity && rows[i - 1].line <= rows[i].line));
    }
    axyne_problems_rows_destroy(rows, count);
    axyne_problems_destroy(&list);
    return 1;
}

static int test_set_lsp(void)
{
    AxyneProblemList list = {0};
    char code1[] = "E100", source[] = "clangd", message1[] = "msg one", message2[] = "msg two";
    AxyneLspDiagnostic diagnostics[2];
    memset(diagnostics, 0, sizeof(diagnostics));
    diagnostics[0].range.start.line = 4;
    diagnostics[0].range.start.character = 2;
    diagnostics[0].severity = AXYNE_LSP_DIAGNOSTIC_WARNING;
    diagnostics[0].code = code1;
    diagnostics[0].source = source;
    diagnostics[0].message = message1;
    diagnostics[1].severity = (AxyneLspDiagnosticSeverity)0; /* unspecified */
    diagnostics[1].message = message2;
    CHECK(axyne_problems_set_lsp(&list, "/p/a.c", diagnostics, 2, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 2);
    CHECK(list.items[0].line == 5 && list.items[0].column == 3);
    CHECK(list.items[0].severity == AXYNE_PROBLEM_WARNING);
    CHECK_STR(list.items[0].code, "E100");
    CHECK_STR(list.items[0].source, "clangd");
    CHECK(list.items[0].message != message1);
    CHECK(list.items[1].line == 1 && list.items[1].column == 1);
    CHECK(list.items[1].severity == AXYNE_PROBLEM_ERROR);
    CHECK_STR(list.items[1].code, "");
    CHECK(axyne_problems_set_lsp(&list, "/p/a.c", NULL, 0, NULL) == AXYNE_STATUS_OK);
    CHECK(list.count == 0);
    CHECK(axyne_problems_set_lsp(&list, NULL, NULL, 0, NULL) == AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_problems_destroy(&list);
    return 1;
}

/* ---- build output parser ---------------------------------------------------------- */

static int parse(const char *line, int severity, const char *path, size_t line_number,
                 size_t column, const char *code, const char *message,
                 const char *source)
{
    AxyneProblem problem;
    int ok = axyne_problems_parse_build_line(line, strlen(line), &problem);
    if (!ok) {
        fprintf(stderr, "not parsed: %s\n", line);
        return 0;
    }
    ok = problem.severity == severity && problem.origin == AXYNE_PROBLEM_ORIGIN_BUILD &&
         strcmp(problem.path, path) == 0 && problem.line == line_number &&
         problem.column == column && strcmp(problem.code, code) == 0 &&
         strcmp(problem.message, message) == 0 && strcmp(problem.source, source) == 0;
    if (!ok)
        fprintf(stderr, "wrong parse of: %s\n  got sev %d path %s %zu:%zu code %s msg %s src %s\n",
                line, problem.severity, problem.path, problem.line, problem.column,
                problem.code, problem.message, problem.source);
    axyne_problem_destroy(&problem);
    return ok;
}

static int not_parsed(const char *line)
{
    AxyneProblem problem;
    if (axyne_problems_parse_build_line(line, strlen(line), &problem)) {
        fprintf(stderr, "should not parse: %s\n", line);
        axyne_problem_destroy(&problem);
        return 0;
    }
    return problem.path == NULL && problem.message == NULL;
}

static int test_build_parser(void)
{
    AxyneProblem problem;

    CHECK(parse("src/main.c:12:5: error: 'x' undeclared (first use in this function)",
                1, "src/main.c", 12, 5, "", "'x' undeclared (first use in this function)", "gnu"));
    CHECK(parse("main.c:3:10: warning: unused variable 'a' [-Wunused-variable]",
                2, "main.c", 3, 10, "-Wunused-variable", "unused variable 'a'", "gnu"));
    CHECK(parse("main.c:3:10: warning: unused variable 'a' [-Wunused-variable]\r",
                2, "main.c", 3, 10, "-Wunused-variable", "unused variable 'a'", "gnu"));
    CHECK(parse("main.c:3:10: warning: unused variable 'a' [-Wunused-variable]\r\n",
                2, "main.c", 3, 10, "-Wunused-variable", "unused variable 'a'", "gnu"));
    CHECK(parse("foo.c:7: warning: implicit declaration", 2, "foo.c", 7, 1, "",
                "implicit declaration", "gnu"));
    CHECK(parse("a.c:1:1: note: declared here", 3, "a.c", 1, 1, "", "declared here", "gnu"));
    CHECK(parse("a.c:2:3: fatal error: stdio.h: No such file or directory", 1, "a.c", 2, 3, "",
                "stdio.h: No such file or directory", "gnu"));
    CHECK(parse("a.c:2:3: remark: loop vectorized", 3, "a.c", 2, 3, "", "loop vectorized", "gnu"));
    CHECK(parse("/abs/dir with space/a.c:20:7: error: expected ';' before '}' token", 1,
                "/abs/dir with space/a.c", 20, 7, "", "expected ';' before '}' token", "gnu"));
    CHECK(parse("a.c:5:1: error: array subscript [i] is bad", 1, "a.c", 5, 1, "",
                "array subscript [i] is bad", "gnu")); /* "[i]" is not a warning flag */
    CHECK(parse("a.c:5:1: error:", 1, "a.c", 5, 1, "", "", "gnu"));
    CHECK(parse("a.c:0:0: error: odd", 1, "a.c", 1, 1, "", "odd", "gnu"));
    CHECK(parse("a.c:5:1: error: 한글 메시지 '값'", 1, "a.c", 5, 1, "", "한글 메시지 '값'", "gnu"));
    CHECK(parse("C:/x/y.c:12:3: error: boom", 1, "C:/x/y.c", 12, 3, "", "boom", "gnu"));
    CHECK(parse("C:\\x\\y.c:12:3: warning: boom [-Wall]", 2, "C:\\x\\y.c", 12, 3, "-Wall", "boom", "gnu"));
    CHECK(parse("C:\\x\\y.c(12,3): error C2065: 'z': undeclared identifier", 1, "C:\\x\\y.c", 12, 3,
                "C2065", "'z': undeclared identifier", "msvc"));
    CHECK(parse("y.c(9): warning C4244: conversion from 'int' to 'char'", 2, "y.c", 9, 1, "C4244",
                "conversion from 'int' to 'char'", "msvc"));
    CHECK(parse("y.c(9) : error C2143: syntax error : missing ';'", 1, "y.c", 9, 1, "C2143",
                "syntax error : missing ';'", "msvc"));
    CHECK(parse("src\\y.cpp(120,15): fatal error C1083: Cannot open include file: 'x.h'", 1,
                "src\\y.cpp", 120, 15, "C1083", "Cannot open include file: 'x.h'", "msvc"));
    CHECK(parse("y.c(3,4): note: see declaration of 'f'", 3, "y.c", 3, 4, "", "see declaration of 'f'", "msvc"));
    CHECK(parse("y.c(3,4): error C2065: 'q': undeclared identifier [C:\\p\\app.vcxproj]", 1, "y.c", 3, 4,
                "C2065", "'q': undeclared identifier", "msvc"));
    CHECK(parse("   12>C:\\p\\y.c(3,4): warning C4100: 'a': unreferenced parameter", 2, "C:\\p\\y.c", 3, 4,
                "C4100", "'a': unreferenced parameter", "msvc"));
    CHECK(parse("12>C:\\p\\y.c(3,4): warning C4100: 'a': unreferenced parameter", 2, "C:\\p\\y.c", 3, 4,
                "C4100", "'a': unreferenced parameter", "msvc"));
    CHECK(parse("\x1b[1m\x1b[Kfoo.c:1:2: \x1b[1;31merror: \x1b[0m\x1b[Kbad thing\x1b[m\x1b[K", 1, "foo.c", 1,
                2, "", "bad thing", "gnu"));
    CHECK(parse("\x1b[01;35m\x1b[Kfoo.c:4:2: \x1b[01;35m\x1b[Kwarning: \x1b[m\x1b[Kmaybe [\x1b[01;35m\x1b[K-Wmaybe\x1b[m\x1b[K]",
                2, "foo.c", 4, 2, "-Wmaybe", "maybe", "gnu"));

    CHECK(not_parsed(""));
    CHECK(not_parsed("error: something failed"));
    CHECK(not_parsed("make: *** [all] Error 1"));
    CHECK(not_parsed("In file included from a.h:3,"));
    CHECK(not_parsed("                 from b.c:4:"));
    CHECK(not_parsed("gcc: error: unrecognized command-line option '-x'"));
    CHECK(not_parsed("this line has error: in the middle"));
    CHECK(not_parsed("foo.c:12: error"));
    CHECK(not_parsed("foo.c:12:5: errors: nope"));
    CHECK(not_parsed("foo.c:12:5: errorx: nope"));
    CHECK(not_parsed("foo.c:abc: error: x"));
    CHECK(not_parsed("foo.c:12:5 error: x"));
    CHECK(not_parsed("    12 |   int x = 1;"));
    CHECK(not_parsed("      |       ^~~~"));
    CHECK(not_parsed("x.obj : error LNK2019: unresolved external symbol main"));
    CHECK(not_parsed("LINK : fatal error LNK1104: cannot open file 'a.exe'"));
    CHECK(not_parsed("cl : Command line warning D9002 : ignoring unknown option"));
    CHECK(parse("y.c(9): warning: no code given", 2, "y.c", 9, 1, "", "no code given", "msvc"));
    CHECK(not_parsed("y.c(9): error C: bad code"));
    CHECK(not_parsed("y.c(x): error C2065: nope"));
    CHECK(not_parsed("y.c(9,): error C2065: nope"));
    CHECK(not_parsed(":12:3: error: no path"));
    CHECK(not_parsed("\x1b[31m\x1b[0m"));
    CHECK(not_parsed("a.c:1:1: error \xC3"));
    CHECK(!axyne_problems_parse_build_line(NULL, 5, &problem));
    CHECK(!axyne_problems_parse_build_line("a.c:1:1: error: x", 0, &problem));
    CHECK(!axyne_problems_parse_build_line("a.c:1:1: error: x", 17, NULL));
    /* only `length` bytes are looked at */
    {
        char exact[] = "a.c:1:1: error: xyz";
        AxyneProblem p;
        CHECK(axyne_problems_parse_build_line(exact, 17, &p));
        CHECK_STR(p.message, "x");
        axyne_problem_destroy(&p);
    }
    /* huge lines are truncated, never overflow */
    {
        size_t n = 200000;
        char *big = (char *)malloc(n + 32);
        AxyneProblem p;
        CHECK(big != NULL);
        memcpy(big, "a.c:1:1: error: ", 16);
        memset(big + 16, 'x', n);
        CHECK(axyne_problems_parse_build_line(big, n + 16, &p));
        CHECK(strlen(p.message) < 16384);
        axyne_problem_destroy(&p);
        memset(big, 'x', n);
        CHECK(!axyne_problems_parse_build_line(big, n, &p));
        memset(big, ':', n);
        CHECK(!axyne_problems_parse_build_line(big, n, &p));
        memset(big, '(', n);
        CHECK(!axyne_problems_parse_build_line(big, n, &p));
        for (size_t i = 0; i + 1 < n; i += 2) { big[i] = 'a'; big[i + 1] = ':'; }
        CHECK(!axyne_problems_parse_build_line(big, n, &p));
        free(big);
    }
    return 1;
}

int axyne_test_palette_problems(const char *root)
{
    (void)root;
    CHECK(test_modes());
    CHECK(test_matching());
    CHECK(test_matching_utf8());
    CHECK(test_parse_line());
    CHECK(test_build_files());
    CHECK(test_build_files_large());
    CHECK(test_commands());
    CHECK(test_build_symbols());
    CHECK(test_symbols());
    CHECK(test_symbols_limits());
    CHECK(test_symbols_supports_file());
    CHECK(test_problem_list());
    CHECK(test_problem_limit());
    CHECK(test_path_helpers());
    CHECK(test_filter());
    CHECK(test_rows());
    CHECK(test_rows_many());
    CHECK(test_set_lsp());
    CHECK(test_build_parser());
    return 1;
}

#ifdef AXYNE_PALETTE_PROBLEMS_STANDALONE
int main(void)
{
    return axyne_test_palette_problems("") ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
