#include "axyne/ui_design.h"

#include <stdio.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

static void expect(const char *name, const char *label)
{
    AxyneFileBadge b = axyne_ui_file_badge(name);
    if (strcmp(b.label, label) != 0)
        fprintf(stderr, "%s -> '%s', expected '%s'\n",
                name == NULL ? "(null)" : name, b.label, label);
    CHECK(strcmp(b.label, label) == 0);
    CHECK(b.label[0] != '\0');
    CHECK(strlen(b.label) <= 3);
}

static void check_menu_titles(void)
{
    static const char *const expected[] = {
        "\xed\x8c\x8c\xec\x9d\xbc", "\xed\x8e\xb8\xec\xa7\x91",
        "\xeb\xb3\xb4\xea\xb8\xb0", "\xeb\xb9\x8c\xeb\x93\x9c",
        "\xeb\x94\x94\xeb\xb2\x84\xea\xb7\xb8", "\xeb\x8f\x84\xea\xb5\xac",
        "\xeb\x8f\x84\xec\x9b\x80\xeb\xa7\x90" };
    static const char letters[] = "FEVBDTH";
    size_t i;
    CHECK(AXYNE_UI_MENU == 26);
    CHECK(axyne_ui_menu_title(AXYNE_UI_MENU_COUNT) == NULL);
    for (i = 0; i < AXYNE_UI_MENU_COUNT; ++i) {
        const AxyneMenuTitle *title = axyne_ui_menu_title(i);
        size_t base = strlen(expected[i]);
        CHECK(title != NULL);
        if (title == NULL) continue;
        /* "<name>(X)": the name, then the mnemonic in parentheses. */
        CHECK(strncmp(title->label, expected[i], base) == 0);
        CHECK(strlen(title->label) == base + 3);
        CHECK(title->label[base] == '(' && title->label[base + 2] == ')');
        CHECK(title->mnemonic == letters[i]);
        CHECK(title->label[base + 1] == title->mnemonic);
    }
}

int main(void)
{
    check_menu_titles();
    static const char *names[] = { "a.c", "a.h", "a.cpp", "a.m", "a.mm",
        "a.md", "a.txt", "a.html", "a.css", "a.sh", "a.java", "a.tsx",
        "a.jsx", "a.yml", "a.yaml", "a.xml", "a.kt", "a.kts", "a.slint", "a.swift", "a.go", "a.rs",
        "a.json", "a.py", "a.ts", "a.js", "a.cmake", "a.zzzzz", "noext" };
    size_t i;
    expect("main.c", "C");
    expect("MAIN.C", "C");
    expect("a.m", "M");
    expect("a.mm", "MM");
    expect("a.md", "MD");
    expect("a.txt", "TXT");
    expect("a.html", "HTM");
    expect("a.sh", "SH");
    expect("a.java", "JV");
    expect("a.tsx", "TSX");
    expect("a.yml", "YML");
    expect("a.yaml", "YML");
    expect("a.xml", "XML");
    expect("a.swift", "SW");
    expect("Main.kt", "KT");
    expect("b.kts", "KT");
    expect("app.slint", "SL");
    expect("a.go", "GO");
    expect("a.rs", "RS");
    expect("CMakeLists.txt", "CM");
    expect("/x/CMakeLists.txt", "CM");
    expect("/x/y.d/Makefile", "MK");
    /* fallback: upper-cased extension truncated to three characters */
    expect("data.proto", "PRO");
    expect("a.ab", "AB");
    expect(".gitignore", "GIT");
    CHECK(axyne_ui_file_badge(".gitignore").color == 0xc79ad9);
    expect("/repo/LICENSE", "LIC");
    expect("LICENSE.md", "MD");
    expect("pyproject.toml", "TML");
    /* configure templates take the generated file's badge */
    expect("config.h.in", "H");
    expect("/x/version.c.in", "C");
    expect("gen.CPP.in", "C++");
    expect("bridge.mm.in", "MM");
    expect("Makefile.in", "IN");
    expect("notes.zzz.in", "IN");
    expect(".h.in", "IN");
    /* media */
    expect("logo.png", "IMG");
    expect("photo.JPEG", "IMG");
    expect("icon.svg", "IMG");
    expect("scan.tiff", "IMG");
    expect("app.ico", "IMG");
    expect("pic.webp", "IMG");
    expect("manual.pdf", "PDF");
    expect("song.flac", "AUD");
    expect("clip.m4a", "AUD");
    expect("movie.mov", "VID");
    expect("clip.webm", "VID");
    /* a dot in a directory must not become the extension */
    expect("/dir.v2/noext", "TXT");
    expect("C:\\dir.v2\\noext", "TXT");
    expect("trailing.", "TXT");
    expect("", "TXT");
    expect(NULL, "TXT");
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        CHECK(axyne_ui_file_badge(names[i]).label[0] != '\0');
    /* document badge: path first, title otherwise */
    CHECK(strcmp(axyne_ui_document_badge("/p/a.rs", "Untitled").label, "RS") == 0);
    CHECK(strcmp(axyne_ui_document_badge("", "b.py").label, "PY") == 0);
    CHECK(strcmp(axyne_ui_document_badge(NULL, "b.py").label, "PY") == 0);
    CHECK(strcmp(axyne_ui_document_badge(NULL, NULL).label, "TXT") == 0);
    if (failures == 0) puts("ui_design: ok");
    return failures == 0 ? 0 : 1;
}
