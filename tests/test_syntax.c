#include "axyne/syntax.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

static void expect(const char *path, const char *id, const char *lexer)
{
    const AxyneSyntaxLanguage *l = axyne_syntax_for_path(path);
    CHECK(l != NULL);
    if (l == NULL) return;
    if (strcmp(l->id, id) != 0 || strcmp(l->lexer, lexer) != 0)
        fprintf(stderr, "path %s -> %s/%s, expected %s/%s\n",
                path == NULL ? "(null)" : path, l->id, l->lexer, id, lexer);
    CHECK(strcmp(l->id, id) == 0);
    CHECK(strcmp(l->lexer, lexer) == 0);
}

int main(void)
{
    size_t i, k;
    static const char *ids[] = { "cpp", "java", "javascript", "typescript",
        "swift", "kotlin", "slint", "go", "rust", "python", "json", "css", "html", "xml", "bash",
        "markdown", "cmake", "yaml", "diff", "text" };

    expect(NULL, "cpp", "cpp");
    expect("", "cpp", "cpp");
    expect("/a/b/main.C", "cpp", "cpp");
    expect("x.mm", "cpp", "cpp");
    expect("Main.java", "java", "cpp");
    expect("a.swift", "swift", "cpp");
    expect("Main.kt", "kotlin", "cpp");
    expect("build.gradle.KTS", "kotlin", "cpp");
    expect("ui/app.slint", "slint", "cpp");
    expect("a.tsx", "typescript", "cpp");
    expect("a.jsx", "javascript", "cpp");
    expect("a.py", "python", "python");
    expect("a.json", "json", "json");
    expect("a.css", "css", "css");
    expect("a.html", "html", "hypertext");
    expect("a.xml", "xml", "xml");
    expect("run.SH", "bash", "bash");
    expect("README.md", "markdown", "markdown");
    expect("/p/CMakeLists.txt", "cmake", "cmake");
    expect("C:\\p\\CMakeLists.txt", "cmake", "cmake");
    expect("x.cmake", "cmake", "cmake");
    expect("ci.yml", "yaml", "yaml");
    expect("ci.YAML", "yaml", "yaml");
    expect("fix.diff", "diff", "diff");
    expect("0001-x.PATCH", "diff", "diff");
    expect("notes.txt", "text", "null");
    expect("/dir.d/Makefile", "text", "null");
    expect("file.", "text", "null");
    expect("weird.zzz", "text", "null");
    expect("C:\\dir.x\\noext", "text", "null");
    /* configure templates use the generated file's language */
    expect("config.h.in", "cpp", "cpp");
    expect("/p/version.CPP.in", "cpp", "cpp");
    expect("bridge.mm.in", "cpp", "cpp");
    expect("settings.json.in", "json", "json");
    expect("Makefile.in", "text", "null");
    expect("notes.zzz.in", "text", "null");
    expect(".h.in", "text", "null");

    {
        const AxyneSyntaxLanguage *md = axyne_syntax_by_id("markdown");
        const AxyneSyntaxLanguage *cmake = axyne_syntax_by_id("cmake");
        unsigned int style;
        for (style = 0; style < 32; ++style)
            CHECK(axyne_syntax_style_bold(md, style) == (style >= 6 && style <= 11));
        CHECK(!axyne_syntax_style_bold(axyne_syntax_by_id("cpp"), 6));
        CHECK(!axyne_syntax_style_bold(NULL, 6));
        CHECK(cmake != NULL && cmake->keywords[0] != NULL &&
              strstr(cmake->keywords[0], "target_link_options") != NULL);
        CHECK(cmake != NULL && cmake->keywords[2] != NULL &&
              strstr(cmake->keywords[2], "CMAKE_BUILD_TYPE") != NULL);
    }

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        const AxyneSyntaxLanguage *l = axyne_syntax_by_id(ids[i]);
        CHECK(l != NULL);
        if (l == NULL) continue;
        CHECK(l->lexer != NULL && l->lexer[0] != '\0');
        CHECK(strcmp(l->id, "text") == 0 || l->style_count > 0);
        for (k = 0; k < l->style_count; ++k) {
            CHECK(l->styles[k].style < 32 || (l->styles[k].style >= 40 && l->styles[k].style < 128));
            CHECK(l->styles[k].color <= 0xffffff);
        }
        for (k = 0; k + 1 < l->style_count; ++k) {
            size_t j;
            for (j = k + 1; j < l->style_count; ++j)
                CHECK(l->styles[k].style != l->styles[j].style);
        }
    }
    {
        /* The established C palette must stay exact. */
        static const unsigned s[] = { 1, 2, 3, 15, 4, 5, 6, 7, 9, 10, 11, 16 };
        static const uint32_t c[] = { 0x7a828e, 0x7a828e, 0x7a828e, 0x7a828e,
            0xd9b36c, 0xd98e73, 0xa3c98a, 0xa3c98a, 0xc79ad9, 0xd5d8dd,
            0xd5d8dd, 0x7db5e3 };
        const AxyneSyntaxLanguage *l = axyne_syntax_by_id("cpp");
        CHECK(l != NULL && l->style_count >= 12);
        for (i = 0; l != NULL && i < 12; ++i) {
            CHECK(l->styles[i].style == s[i]);
            CHECK(l->styles[i].color == c[i]);
        }
        CHECK(l != NULL && l->keywords[0] != NULL && l->keywords[1] != NULL);
    }
    {
        static const char *const kw[][2] = { { "kotlin", "suspend" },
            { "slint", "in-out" } };
        for (i = 0; i < 2; ++i) {
            const AxyneSyntaxLanguage *l = axyne_syntax_by_id(kw[i][0]);
            CHECK(l != NULL && l->keywords[0] != NULL &&
                  strstr(l->keywords[0], kw[i][1]) != NULL);
            CHECK(l != NULL && l->keywords[1] != NULL && l->keywords[3] != NULL);
        }
    }
    CHECK(axyne_syntax_by_id("nope") == NULL);
    CHECK(axyne_syntax_by_id(NULL) == NULL);
    if (failures == 0) puts("syntax: ok");
    return failures == 0 ? 0 : 1;
}
