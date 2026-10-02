#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/outline.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static const char sample[] =
    "#include <stdio.h>\n"   /* 1 (not a symbol) */
    "#define MAX 4\n"        /* 2 macro */
    "int g_value;\n"         /* 3 variable */
    "struct Point { int x; };\n" /* 4 type */
    "\n"
    "static int add(int a, int b)\n" /* 6 function */
    "{\n"
    "    return a + b;\n"
    "}\n"
    "int main(void)\n"       /* 10 function */
    "{\n"
    "    return 0;\n"
    "}\n";

static int scan_text(AxyneOutline *outline, const char *name, const char *text)
{
    size_t length = strlen(text);
    CHECK(axyne_outline_prepare(outline, name, length) != 0);
    CHECK(axyne_outline_scan(outline, text, length, NULL) == AXYNE_STATUS_OK);
    return 1;
}

static int test_states(void)
{
    AxyneOutline outline;
    char header[64];
    axyne_outline_init(&outline);
    CHECK(outline.state == AXYNE_OUTLINE_HIDDEN);
    CHECK(axyne_outline_row_count(&outline) == 0);
    CHECK(axyne_outline_height(&outline, 800) == 0);

    CHECK(scan_text(&outline, "main.c", sample));
    CHECK(outline.state == AXYNE_OUTLINE_SYMBOLS);
    CHECK(axyne_outline_row_count(&outline) == outline.symbols.count);
    CHECK(outline.symbols.count >= 4);
    axyne_outline_header(&outline, header, sizeof(header));
    CHECK(strcmp(header, "\xea\xb0\x9c\xec\x9a\x94 \xe2\x80\x94 main.c") == 0);

    /* an unsupported extension shows one muted row and never asks for text */
    CHECK(axyne_outline_prepare(&outline, "notes.txt", 10) == 0);
    CHECK(outline.state == AXYNE_OUTLINE_UNSUPPORTED);
    CHECK(outline.symbols.count == 0 && outline.symbols.items == NULL);
    CHECK(axyne_outline_row_count(&outline) == 1);
    CHECK(axyne_outline_message(outline.state) != NULL);
    CHECK(axyne_outline_symbol_at(&outline, 127, 40) == -1);
    CHECK(axyne_outline_height(&outline, 800) ==
          AXYNE_OUTLINE_SEPARATOR + AXYNE_OUTLINE_HEADER + AXYNE_OUTLINE_ROW);

    /* an untitled document has no file name: unsupported, not hidden */
    CHECK(axyne_outline_prepare(&outline, NULL, 0) == 0);
    CHECK(outline.state == AXYNE_OUTLINE_UNSUPPORTED);
    axyne_outline_header(&outline, header, sizeof(header));
    CHECK(strcmp(header, "\xea\xb0\x9c\xec\x9a\x94") == 0);

    /* files over the limit are not scanned */
    CHECK(axyne_outline_prepare(&outline, "big.c", AXYNE_OUTLINE_MAX_BYTES + 1) == 0);
    CHECK(outline.state == AXYNE_OUTLINE_TOO_LARGE);
    CHECK(axyne_outline_message(outline.state) != NULL);
    CHECK(axyne_outline_prepare(&outline, "big.c", AXYNE_OUTLINE_MAX_BYTES) != 0);

    /* a supported file without symbols hides the whole section */
    CHECK(scan_text(&outline, "empty.c", "/* nothing */\n"));
    CHECK(outline.symbols.count == 0);
    CHECK(axyne_outline_row_count(&outline) == 0);
    CHECK(axyne_outline_height(&outline, 800) == 0);

    axyne_outline_clear(&outline);
    CHECK(outline.state == AXYNE_OUTLINE_HIDDEN && outline.file_name == NULL);
    axyne_outline_destroy(&outline);
    return 1;
}

static int test_geometry(void)
{
    AxyneOutline outline;
    int height;
    axyne_outline_init(&outline);
    CHECK(scan_text(&outline, "main.c", sample));
    /* Figma example: 4 rows exactly as drawn (9 + 30 + 4 * 22) when 4 symbols */
    {
        size_t count = outline.symbols.count;
        height = axyne_outline_height(&outline, 800);
        CHECK(height == AXYNE_OUTLINE_SEPARATOR + AXYNE_OUTLINE_HEADER +
              (int)(count > 8 ? 8 : count) * AXYNE_OUTLINE_ROW);
        CHECK(axyne_outline_visible_rows(height) == (count > 8 ? 8 : count));
    }
    /* never more than 40% of the explorer, never more than 8 rows */
    {
        char *text = (char *)malloc(64 * 100);
        size_t used = 0;
        int i;
        CHECK(text != NULL);
        for (i = 0; i < 100; ++i)
            used += (size_t)sprintf(text + used, "int f%d(void) { return %d; }\n", i, i);
        CHECK(axyne_outline_prepare(&outline, "many.c", used) != 0);
        CHECK(axyne_outline_scan(&outline, text, used, NULL) == AXYNE_STATUS_OK);
        free(text);
    }
    CHECK(outline.symbols.count == 100);
    height = axyne_outline_height(&outline, 1000);
    CHECK(height == 9 + 30 + 8 * 22);
    height = axyne_outline_height(&outline, 300); /* cap 120 -> 4 rows */
    CHECK(height == 9 + 30 + 3 * 22 && height <= 120);
    CHECK(axyne_outline_height(&outline, 150) == 0); /* cap 60: header + 1 row does not fit */
    height = axyne_outline_height(&outline, 1000);

    /* scrolling is clamped to the last full page */
    CHECK(axyne_outline_scroll_by(&outline, 5, height) == 5);
    CHECK(axyne_outline_scroll_by(&outline, -50, height) == 0);
    CHECK(axyne_outline_scroll_by(&outline, 1000, height) == 92);
    CHECK(axyne_outline_set_scroll(&outline, -3, height) == 0);

    /* hit testing: separator and header are not rows */
    CHECK(axyne_outline_symbol_at(&outline, height, 0) == -1);
    CHECK(axyne_outline_symbol_at(&outline, height, 38) == -1);
    CHECK(axyne_outline_symbol_at(&outline, height, 39) == 0);
    CHECK(axyne_outline_symbol_at(&outline, height, 39 + 21) == 0);
    CHECK(axyne_outline_symbol_at(&outline, height, 39 + 22) == 1);
    CHECK(axyne_outline_symbol_at(&outline, height, height - 1) == 7);
    CHECK(axyne_outline_symbol_at(&outline, height, height) == -1);
    CHECK(axyne_outline_symbol_at(&outline, height, -4) == -1);
    (void)axyne_outline_set_scroll(&outline, 10, height);
    CHECK(axyne_outline_symbol_at(&outline, height, 39) == 10);

    /* reveal keeps a symbol inside the window */
    axyne_outline_reveal(&outline, 50, height);
    CHECK(outline.first_row <= 50 && 50 < outline.first_row + 8);
    axyne_outline_reveal(&outline, 2, height);
    CHECK(outline.first_row == 2);
    axyne_outline_destroy(&outline);
    return 1;
}

static int test_caret_symbol(void)
{
    AxyneOutline outline;
    long main_index = -1;
    size_t i;
    axyne_outline_init(&outline);
    CHECK(scan_text(&outline, "main.c", sample));
    CHECK(axyne_outline_symbol_for_line(&outline, 1) == -1); /* before the first symbol */
    for (i = 0; i < outline.symbols.count; ++i)
        if (strcmp(outline.symbols.items[i].name, "main") == 0) main_index = (long)i;
    CHECK(main_index > 0);
    CHECK(axyne_outline_symbol_for_line(&outline, 10) == main_index);
    CHECK(axyne_outline_symbol_for_line(&outline, 13) == main_index);
    CHECK(axyne_outline_symbol_for_line(&outline, 9) == main_index - 1);
    CHECK(axyne_outline_symbol_for_line(&outline, 100000) == main_index);
    CHECK(axyne_outline_symbol_for_line(NULL, 3) == -1);
    axyne_outline_destroy(&outline);
    return 1;
}

static int test_header_and_glyphs(void)
{
    AxyneOutline outline;
    char small[12], header[64];
    axyne_outline_init(&outline);
    CHECK(scan_text(&outline, "m\xea\xb0\x9c.c", sample));
    axyne_outline_header(&outline, small, sizeof(small));
    CHECK(strlen(small) < sizeof(small));
    /* never ends inside a UTF-8 sequence */
    CHECK(((unsigned char)small[strlen(small)] & 0xc0u) != 0x80u);
    axyne_outline_header(&outline, header, 1);
    CHECK(header[0] == '\0');
    CHECK(axyne_outline_glyph(AXYNE_SYMBOL_MACRO) == '#');
    CHECK(axyne_outline_glyph(AXYNE_SYMBOL_VARIABLE) == 'v');
    CHECK(axyne_outline_glyph(AXYNE_SYMBOL_FUNCTION) == 'f');
    CHECK(axyne_outline_glyph_color(AXYNE_SYMBOL_MACRO) == 0xc79ad9);
    CHECK(axyne_outline_glyph_color(AXYNE_SYMBOL_VARIABLE) == 0x8cc7c0);
    CHECK(axyne_outline_glyph_color(AXYNE_SYMBOL_FUNCTION) == 0xe3cf86);
    CHECK(axyne_outline_glyph_color(AXYNE_SYMBOL_TYPE) == 0x7db5e3);
    axyne_outline_destroy(&outline);
    return 1;
}

static int test_scroll_kept_per_file(void)
{
    AxyneOutline outline;
    axyne_outline_init(&outline);
    CHECK(scan_text(&outline, "a.c", sample));
    outline.first_row = 1;
    CHECK(scan_text(&outline, "a.c", sample)); /* same file: rescan keeps scroll */
    CHECK(outline.first_row == 1);
    CHECK(scan_text(&outline, "b.c", sample)); /* other file: back to the top */
    CHECK(outline.first_row == 0);
    axyne_outline_destroy(&outline);
    return 1;
}

int axyne_test_outline(const char *root)
{
    (void)root;
    CHECK(test_states());
    CHECK(test_geometry());
    CHECK(test_caret_symbol());
    CHECK(test_header_and_glyphs());
    CHECK(test_scroll_kept_per_file());
    return 1;
}
