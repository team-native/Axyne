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

int main(void)
{
    static const char *names[] = { "a.c", "a.h", "a.cpp", "a.m", "a.mm",
        "a.md", "a.txt", "a.html", "a.css", "a.sh", "a.java", "a.tsx",
        "a.jsx", "a.yml", "a.yaml", "a.xml", "a.swift", "a.go", "a.rs",
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
    expect("a.go", "GO");
    expect("a.rs", "RS");
    expect("CMakeLists.txt", "CM");
    expect("/x/CMakeLists.txt", "CM");
    expect("/x/y.d/Makefile", "MK");
    /* fallback: upper-cased extension truncated to three characters */
    expect("data.proto", "PRO");
    expect("a.ab", "AB");
    expect(".gitignore", "GIT");
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
