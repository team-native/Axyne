#include "axyne/symbols.h"

#include <stdlib.h>
#include <string.h>

/* Heuristic outline scanners for the non-C languages. A shared lexer skips
 * comments and string literals and feeds statement tokens to a per-language
 * classifier; braces are tracked as frames so that only declarations at the
 * top level (or directly inside a type body) are considered. Python is
 * indentation based and has its own pass. Nothing here parses a grammar: an
 * unusual layout simply yields fewer symbols. */

typedef enum Family {
    FAM_NONE = 0, FAM_C, FAM_JS, FAM_TS, FAM_PY, FAM_GO, FAM_RUST,
    FAM_JAVA, FAM_KOTLIN, FAM_SWIFT, FAM_SLINT
} Family;

typedef struct ExtensionEntry { const char *extension; Family family; } ExtensionEntry;

static const ExtensionEntry extension_table[] = {
    { "c", FAM_C }, { "h", FAM_C }, { "cc", FAM_C }, { "cpp", FAM_C },
    { "cxx", FAM_C }, { "hpp", FAM_C }, { "m", FAM_C }, { "mm", FAM_C },
    { "js", FAM_JS }, { "mjs", FAM_JS }, { "cjs", FAM_JS }, { "jsx", FAM_JS },
    { "ts", FAM_TS }, { "tsx", FAM_TS }, { "mts", FAM_TS }, { "cts", FAM_TS },
    { "py", FAM_PY }, { "go", FAM_GO }, { "rs", FAM_RUST },
    { "java", FAM_JAVA }, { "kt", FAM_KOTLIN }, { "kts", FAM_KOTLIN },
    { "swift", FAM_SWIFT }, { "slint", FAM_SLINT }
};

static Family family_for_file(const char *name_or_path)
{
    const char *base, *dot, *p;
    size_t i;
    if (name_or_path == NULL) return FAM_NONE;
    base = name_or_path;
    for (p = name_or_path; *p != '\0'; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    dot = strrchr(base, '.');
    if (dot == NULL || dot == base) return FAM_NONE;
    ++dot;
    for (i = 0; i < sizeof(extension_table) / sizeof(extension_table[0]); ++i) {
        const char *a = dot, *b = extension_table[i].extension;
        while (*a != '\0' && *b != '\0') {
            char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + ('a' - 'A')) : *a;
            if (ca != *b) break;
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') return extension_table[i].family;
    }
    return FAM_NONE;
}

int axyne_symbols_supports_file(const char *name_or_path)
{
    return family_for_file(name_or_path) != FAM_NONE;
}

/* ---- shared scanner state ---------------------------------------------- */

#define MAX_TOKENS 96
#define MAX_FRAMES 256

typedef struct Tok {
    char c;            /* 'w' word, 's' string, 'n' number, 'A' "=>", else the punctuation char */
    const char *p;
    size_t len;
    size_t line, col;
} Tok;

enum { FR_TYPE = 1, FR_SCOPE = 2, FR_OPAQUE = 3 };

typedef struct Frame {
    unsigned char kind;
    unsigned char in_paren; /* opened inside (...): never starts a statement */
} Frame;

typedef struct Scan {
    Family family;
    const char *s;
    size_t n, pos, line, line_start;
    AxyneSymbol *items;
    size_t count, capacity;
    int truncated, out_of_memory;
    Frame frames[MAX_FRAMES];
    int depth;
    int overflow_depth;     /* braces nested deeper than MAX_FRAMES */
    Tok toks[MAX_TOKENS];
    int ntoks;
    int paren;
    int group_active;       /* Go: const/var/type ( ... ) group */
    Tok group_tok;
    int next_frame;         /* frame kind chosen by the classifier */
} Scan;

static int is_ident_start(unsigned char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' ||
           ch == '$' || ch >= 0x80;
}

static int is_ident_char(unsigned char ch)
{
    return is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

static int tok_is(const Tok *t, const char *word)
{
    size_t n = strlen(word);
    return t->c == 'w' && t->len == n && memcmp(t->p, word, n) == 0;
}

static int tok_in(const Tok *t, const char *const *words)
{
    size_t i;
    if (t->c != 'w') return 0;
    for (i = 0; words[i] != NULL; ++i)
        if (tok_is(t, words[i])) return 1;
    return 0;
}

static void add_symbol(Scan *c, const Tok *name, AxyneSymbolKind kind)
{
    char *copy;
    if (name == NULL || name->c != 'w' || name->len == 0) return;
    if (c->count >= AXYNE_SYMBOLS_MAX) { c->truncated = 1; return; }
    if (c->count == c->capacity) {
        size_t next = c->capacity == 0 ? 64 : c->capacity * 2;
        AxyneSymbol *grown = (AxyneSymbol *)realloc(c->items, next * sizeof(*grown));
        if (grown == NULL) { c->out_of_memory = 1; return; }
        c->items = grown;
        c->capacity = next;
    }
    copy = (char *)malloc(name->len + 1);
    if (copy == NULL) { c->out_of_memory = 1; return; }
    memcpy(copy, name->p, name->len);
    copy[name->len] = '\0';
    c->items[c->count].name = copy;
    c->items[c->count].kind = kind;
    c->items[c->count].line = name->line;
    c->items[c->count].column = name->col;
    ++c->count;
}

/* ---- token helpers ------------------------------------------------------ */

static int word_at(const Tok *t, int n, int i)
{
    return i >= 0 && i < n && t[i].c == 'w';
}

static int punct_at(const Tok *t, int n, int i, char ch)
{
    return i >= 0 && i < n && t[i].c == ch;
}

/* Index just past the group opened by t[i] ('(' or '['); n when unbalanced. */
static int skip_group(const Tok *t, int n, int i)
{
    char open = t[i].c, close = open == '(' ? ')' : ']';
    int depth = 0;
    for (; i < n; ++i) {
        if (t[i].c == open) ++depth;
        else if (t[i].c == close && --depth == 0) return i + 1;
    }
    return n;
}

/* Skips modifier words, annotations/attributes and `pub(crate)` style groups.
 * A modifier followed by '(' '=' ':' '<' is really a name and is kept. */
static int skip_mods(const Tok *t, int n, int i, const char *const *mods)
{
    while (i < n) {
        if (t[i].c == '@' && word_at(t, n, i + 1) && !tok_is(&t[i + 1], "interface")) {
            i += 2;
            while (punct_at(t, n, i, ':') && word_at(t, n, i + 1)) i += 2;
            if (punct_at(t, n, i, '(')) i = skip_group(t, n, i);
            continue;
        }
        if (t[i].c == '#' && (punct_at(t, n, i + 1, '[') ||
                              (punct_at(t, n, i + 1, '!') && punct_at(t, n, i + 2, '[')))) {
            i = skip_group(t, n, punct_at(t, n, i + 1, '[') ? i + 1 : i + 2);
            continue;
        }
        if (tok_in(&t[i], mods)) {
            if (i + 1 < n && (t[i + 1].c == '(' || t[i + 1].c == '=' ||
                              t[i + 1].c == ':' || t[i + 1].c == '<') &&
                !tok_is(&t[i], "pub"))
                break;
            ++i;
            if (tok_is(&t[i - 1], "pub") && punct_at(t, n, i, '(')) i = skip_group(t, n, i);
            continue;
        }
        break;
    }
    return i;
}

static int has_arrow_or_function(const Tok *t, int n, int from)
{
    int i;
    for (i = from; i < n; ++i)
        if (t[i].c == 'A' || tok_is(&t[i], "function")) return 1;
    return 0;
}

static int index_of_punct(const Tok *t, int n, int from, char ch)
{
    int i;
    for (i = from; i < n; ++i)
        if (t[i].c == ch) return i;
    return -1;
}

/* ---- classifiers: one per family --------------------------------------- */

static const char *const js_mods[] = {
    "export", "default", "declare", "abstract", "public", "private", "protected",
    "static", "readonly", "async", "override", "get", "set", "accessor", NULL };

static void classify_js(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    int i = skip_mods(t, n, 0, js_mods);
    if (i >= n) return;
    if (ctx == FR_TYPE) {
        int j = i;
        if (punct_at(t, n, j, '*')) ++j;
        if (word_at(t, n, j) && (punct_at(t, n, j + 1, '(') || punct_at(t, n, j + 1, '<'))) {
            add_symbol(c, &t[j], AXYNE_SYMBOL_METHOD);
            c->next_frame = FR_OPAQUE;
        } else if (word_at(t, n, j) && punct_at(t, n, j + 1, '=') &&
                   has_arrow_or_function(t, n, j + 2)) {
            add_symbol(c, &t[j], AXYNE_SYMBOL_METHOD);
            c->next_frame = FR_OPAQUE;
        }
        return;
    }
    if (tok_is(&t[i], "function")) {
        int j = i + 1;
        if (punct_at(t, n, j, '*')) ++j;
        if (word_at(t, n, j)) add_symbol(c, &t[j], AXYNE_SYMBOL_FUNCTION);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "class")) {
        if (word_at(t, n, i + 1) && !tok_is(&t[i + 1], "extends") &&
            !tok_is(&t[i + 1], "implements"))
            add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_TYPE;
    } else if (tok_is(&t[i], "const") && word_at(t, n, i + 1) && tok_is(&t[i + 1], "enum")) {
        if (word_at(t, n, i + 2)) add_symbol(c, &t[i + 2], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "interface") || tok_is(&t[i], "enum")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "type") && word_at(t, n, i + 1) &&
               (punct_at(t, n, i + 2, '=') || punct_at(t, n, i + 2, '<'))) {
        add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "namespace") || tok_is(&t[i], "module")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        if (braces) c->next_frame = FR_SCOPE;
    } else if ((tok_is(&t[i], "const") || tok_is(&t[i], "let") || tok_is(&t[i], "var")) &&
               word_at(t, n, i + 1)) {
        int eq = index_of_punct(t, n, i + 2, '=');
        AxyneSymbolKind kind = tok_is(&t[i], "const") ? AXYNE_SYMBOL_CONSTANT
                                                      : AXYNE_SYMBOL_VARIABLE;
        if (eq >= 0 && has_arrow_or_function(t, n, eq + 1)) kind = AXYNE_SYMBOL_FUNCTION;
        add_symbol(c, &t[i + 1], kind);
        c->next_frame = FR_OPAQUE;
    }
}

static void classify_go(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    (void)braces;
    if (ctx != FR_SCOPE || n == 0) return;
    if (tok_is(&t[0], "func")) {
        int j = 1;
        AxyneSymbolKind kind = AXYNE_SYMBOL_FUNCTION;
        if (punct_at(t, n, j, '(')) { j = skip_group(t, n, j); kind = AXYNE_SYMBOL_METHOD; }
        if (word_at(t, n, j)) add_symbol(c, &t[j], kind);
    } else if (tok_is(&t[0], "type") && word_at(t, n, 1)) {
        add_symbol(c, &t[1], AXYNE_SYMBOL_TYPE);
    } else if ((tok_is(&t[0], "const") || tok_is(&t[0], "var")) && word_at(t, n, 1)) {
        add_symbol(c, &t[1], tok_is(&t[0], "const") ? AXYNE_SYMBOL_CONSTANT
                                                    : AXYNE_SYMBOL_VARIABLE);
    }
    c->next_frame = FR_OPAQUE;
}

static const char *const rust_mods[] = {
    "pub", "async", "unsafe", "extern", "default", NULL };

static void classify_rust(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    int i = 0;
    for (;;) {
        i = skip_mods(t, n, i, rust_mods);
        /* `const fn` / `const unsafe fn`: const is a qualifier here. */
        if (i + 1 < n && tok_is(&t[i], "const") && word_at(t, n, i + 1) &&
            (tok_is(&t[i + 1], "fn") || tok_is(&t[i + 1], "unsafe") ||
             tok_is(&t[i + 1], "async") || tok_is(&t[i + 1], "extern"))) {
            ++i;
            continue;
        }
        break;
    }
    if (i >= n) return;
    if (tok_is(&t[i], "fn")) {
        if (word_at(t, n, i + 1))
            add_symbol(c, &t[i + 1], ctx == FR_TYPE ? AXYNE_SYMBOL_METHOD
                                                    : AXYNE_SYMBOL_FUNCTION);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "struct") || tok_is(&t[i], "enum") || tok_is(&t[i], "union")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "trait")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_TYPE;
    } else if (tok_is(&t[i], "type")) {
        if (ctx != FR_TYPE && word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "mod")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        if (braces) c->next_frame = FR_SCOPE;
    } else if (tok_is(&t[i], "impl")) {
        c->next_frame = FR_TYPE;
    } else if (tok_is(&t[i], "const") || tok_is(&t[i], "static")) {
        int j = i + 1;
        if (word_at(t, n, j) && tok_is(&t[j], "mut")) ++j;
        if (ctx != FR_TYPE && word_at(t, n, j)) add_symbol(c, &t[j], AXYNE_SYMBOL_CONSTANT);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "macro_rules") && punct_at(t, n, i + 1, '!') &&
               word_at(t, n, i + 2)) {
        add_symbol(c, &t[i + 2], AXYNE_SYMBOL_MACRO);
        c->next_frame = FR_OPAQUE;
    }
}

static const char *const java_mods[] = {
    "public", "private", "protected", "static", "final", "abstract", "native",
    "synchronized", "transient", "volatile", "strictfp", "default", "sealed",
    "non", NULL };
static const char *const java_not_methods[] = {
    "if", "for", "while", "switch", "catch", "synchronized", "return", "new",
    "throw", "else", "do", "try", "assert", NULL };

static void classify_java(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    int i, k, eq, is_static = 0, is_final = 0;
    (void)braces;
    for (i = 0; i < n; ++i) {
        if (tok_is(&t[i], "static")) is_static = 1;
        if (tok_is(&t[i], "final")) is_final = 1;
    }
    i = 0;
    for (;;) {
        int before = i;
        i = skip_mods(t, n, i, java_mods);
        /* `non-sealed`: "non" "-" "sealed" */
        if (i + 2 < n && tok_is(&t[i], "non") && punct_at(t, n, i + 1, '-')) i += 3;
        if (i == before) break;
    }
    if (i >= n) return;
    if (tok_is(&t[i], "class") || tok_is(&t[i], "interface") || tok_is(&t[i], "enum") ||
        tok_is(&t[i], "record") || (t[i].c == '@' && word_at(t, n, i + 1) &&
                                    tok_is(&t[i + 1], "interface"))) {
        int name = t[i].c == '@' ? i + 2 : i + 1;
        if (word_at(t, n, name)) add_symbol(c, &t[name], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_TYPE;
        return;
    }
    if (ctx != FR_TYPE) return;
    k = index_of_punct(t, n, i, '(');
    eq = index_of_punct(t, n, i, '=');
    if (k > i && word_at(t, n, k - 1) && !tok_in(&t[k - 1], java_not_methods) &&
        (eq < 0 || eq > k)) {
        add_symbol(c, &t[k - 1], AXYNE_SYMBOL_METHOD);
        c->next_frame = FR_OPAQUE;
    } else if (is_static && is_final && eq > i && word_at(t, n, eq - 1)) {
        add_symbol(c, &t[eq - 1], AXYNE_SYMBOL_CONSTANT);
    }
}

static const char *const kotlin_mods[] = {
    "public", "private", "protected", "internal", "open", "final", "abstract",
    "sealed", "data", "inline", "value", "inner", "enum", "annotation", "override",
    "suspend", "operator", "infix", "tailrec", "external", "actual", "expect",
    "const", "lateinit", "companion", "vararg", "noinline", "crossinline", NULL };

static void classify_kotlin(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    int i = 0, k, has_const = 0;
    (void)braces;
    for (k = 0; k < n && tok_in(&t[k], kotlin_mods); ++k)
        if (tok_is(&t[k], "const")) has_const = 1;
    i = skip_mods(t, n, 0, kotlin_mods);
    if (i >= n) return;
    if (tok_is(&t[i], "fun") && word_at(t, n, i + 1) && tok_is(&t[i + 1], "interface")) {
        if (word_at(t, n, i + 2)) add_symbol(c, &t[i + 2], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_TYPE;
    } else if (tok_is(&t[i], "fun")) {
        int paren = index_of_punct(t, n, i + 1, '(');
        if (paren > i + 1 && word_at(t, n, paren - 1))
            add_symbol(c, &t[paren - 1], ctx == FR_TYPE ? AXYNE_SYMBOL_METHOD
                                                        : AXYNE_SYMBOL_FUNCTION);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "class") || tok_is(&t[i], "interface") || tok_is(&t[i], "object")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = punct_at(t, n, i + 1, ':') ? FR_OPAQUE : FR_TYPE;
    } else if (tok_is(&t[i], "typealias")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if ((tok_is(&t[i], "val") || tok_is(&t[i], "var")) && ctx != FR_TYPE) {
        if (word_at(t, n, i + 1) && !punct_at(t, n, i + 2, '.'))
            add_symbol(c, &t[i + 1], has_const ? AXYNE_SYMBOL_CONSTANT : AXYNE_SYMBOL_VARIABLE);
        c->next_frame = FR_OPAQUE;
    }
}

static const char *const swift_mods[] = {
    "public", "private", "fileprivate", "internal", "open", "final", "static",
    "mutating", "nonmutating", "override", "required", "convenience", "lazy",
    "weak", "unowned", "indirect", "dynamic", "optional", "nonisolated",
    "distributed", "package", NULL };

static void classify_swift(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    int i = 0;
    (void)braces;
    for (;;) {
        i = skip_mods(t, n, i, swift_mods);
        /* `class func` / `class var`: class is a modifier there. */
        if (i + 1 < n && tok_is(&t[i], "class") && word_at(t, n, i + 1) &&
            (tok_is(&t[i + 1], "func") || tok_is(&t[i + 1], "var") ||
             tok_is(&t[i + 1], "let") || tok_is(&t[i + 1], "subscript"))) {
            ++i;
            continue;
        }
        break;
    }
    if (i >= n) return;
    if (tok_is(&t[i], "func")) {
        if (word_at(t, n, i + 1))
            add_symbol(c, &t[i + 1], ctx == FR_TYPE ? AXYNE_SYMBOL_METHOD
                                                    : AXYNE_SYMBOL_FUNCTION);
        c->next_frame = FR_OPAQUE;
    } else if (ctx == FR_TYPE && (tok_is(&t[i], "init") || tok_is(&t[i], "deinit") ||
                                  tok_is(&t[i], "subscript"))) {
        add_symbol(c, &t[i], AXYNE_SYMBOL_METHOD);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "class") || tok_is(&t[i], "struct") || tok_is(&t[i], "enum") ||
               tok_is(&t[i], "protocol") || tok_is(&t[i], "actor") ||
               tok_is(&t[i], "extension")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_TYPE;
    } else if (tok_is(&t[i], "typealias")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if ((tok_is(&t[i], "let") || tok_is(&t[i], "var")) && ctx != FR_TYPE) {
        if (word_at(t, n, i + 1))
            add_symbol(c, &t[i + 1], tok_is(&t[i], "let") ? AXYNE_SYMBOL_CONSTANT
                                                           : AXYNE_SYMBOL_VARIABLE);
        c->next_frame = FR_OPAQUE;
    }
}

static const char *const slint_mods[] = { "export", "pure", "public", "protected", NULL };

static void classify_slint(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    int i = skip_mods(t, n, 0, slint_mods);
    (void)braces;
    if (i >= n) return;
    if (tok_is(&t[i], "component") || tok_is(&t[i], "global")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_TYPE;
    } else if (tok_is(&t[i], "struct") || tok_is(&t[i], "enum")) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_TYPE);
        c->next_frame = FR_OPAQUE;
    } else if (tok_is(&t[i], "function") && ctx == FR_TYPE) {
        if (word_at(t, n, i + 1)) add_symbol(c, &t[i + 1], AXYNE_SYMBOL_METHOD);
        c->next_frame = FR_OPAQUE;
    }
}

static void classify(Scan *c, const Tok *t, int n, int ctx, int braces)
{
    c->next_frame = FR_OPAQUE;
    if (n == 0) return;
    switch (c->family) {
    case FAM_JS: case FAM_TS: classify_js(c, t, n, ctx, braces); break;
    case FAM_GO: classify_go(c, t, n, ctx, braces); break;
    case FAM_RUST: classify_rust(c, t, n, ctx, braces); break;
    case FAM_JAVA: classify_java(c, t, n, ctx, braces); break;
    case FAM_KOTLIN: classify_kotlin(c, t, n, ctx, braces); break;
    case FAM_SWIFT: classify_swift(c, t, n, ctx, braces); break;
    case FAM_SLINT: classify_slint(c, t, n, ctx, braces); break;
    default: break;
    }
}

/* ---- statement and frame handling -------------------------------------- */

static int current_context(const Scan *c)
{
    if (c->depth == 0) return FR_SCOPE;
    return c->frames[c->depth - 1].kind;
}

static int declarations_allowed(const Scan *c)
{
    return c->overflow_depth == 0 && current_context(c) != FR_OPAQUE;
}

static void statement_reset(Scan *c)
{
    c->ntoks = 0;
}

/* Classifies the statement tokens collected so far. Inside a Go group the
 * group keyword is prepended to each entry. */
static void classify_statement(Scan *c, int braces)
{
    Tok buffer[MAX_TOKENS + 1];
    int count = c->ntoks;
    const Tok *tokens = c->toks;
    if (c->group_active) {
        buffer[0] = c->group_tok;
        memcpy(buffer + 1, c->toks, (size_t)count * sizeof(Tok));
        tokens = buffer;
        ++count;
    }
    c->next_frame = FR_OPAQUE;
    if (declarations_allowed(c)) classify(c, tokens, count, current_context(c), braces);
}

static void end_statement(Scan *c)
{
    int top = c->paren == 0 || (c->group_active && c->paren == 1);
    if (c->ntoks > 0 && top) classify_statement(c, 0);
    if (top) statement_reset(c);
}

static void open_brace(Scan *c)
{
    int kind;
    if (c->paren > 0) {
        /* A literal/closure inside an argument list: opaque, statement stays. */
        if (c->depth < MAX_FRAMES) {
            c->frames[c->depth].kind = FR_OPAQUE;
            c->frames[c->depth].in_paren = 1;
            ++c->depth;
        } else {
            ++c->overflow_depth;
        }
        return;
    }
    classify_statement(c, 1);
    kind = c->next_frame;
    if (!declarations_allowed(c)) kind = FR_OPAQUE;
    if (c->depth < MAX_FRAMES) {
        c->frames[c->depth].kind = (unsigned char)kind;
        c->frames[c->depth].in_paren = 0;
        ++c->depth;
    } else {
        ++c->overflow_depth;
    }
    statement_reset(c);
}

static void close_brace(Scan *c)
{
    int was_in_paren = 0;
    if (c->overflow_depth > 0) { --c->overflow_depth; return; }
    if (c->depth > 0) {
        --c->depth;
        was_in_paren = c->frames[c->depth].in_paren;
    }
    if (!was_in_paren) statement_reset(c);
}

static void push_token(Scan *c, char kind, const char *p, size_t len, size_t col)
{
    Tok *t;
    if (c->ntoks >= MAX_TOKENS) return;
    t = &c->toks[c->ntoks++];
    t->c = kind;
    t->p = p;
    t->len = len;
    t->line = c->line;
    t->col = col;
}

/* ---- lexer -------------------------------------------------------------- */

static int starts_with(const Scan *c, const char *text)
{
    size_t len = strlen(text);
    return c->pos + len <= c->n && memcmp(c->s + c->pos, text, len) == 0;
}

static void newline(Scan *c)
{
    ++c->line;
    c->line_start = c->pos;
}

static void skip_line_comment(Scan *c)
{
    while (c->pos < c->n && c->s[c->pos] != '\n' && c->s[c->pos] != '\r') ++c->pos;
}

static void skip_block_comment(Scan *c, int nested)
{
    int depth = 1;
    c->pos += 2;
    while (c->pos < c->n && depth > 0) {
        char ch = c->s[c->pos];
        if (ch == '\r') {
            ++c->pos;
            if (c->pos < c->n && c->s[c->pos] == '\n') ++c->pos;
            newline(c);
        } else if (ch == '\n') {
            ++c->pos;
            newline(c);
        } else if (ch == '*' && c->pos + 1 < c->n && c->s[c->pos + 1] == '/') {
            c->pos += 2;
            --depth;
        } else if (nested && ch == '/' && c->pos + 1 < c->n && c->s[c->pos + 1] == '*') {
            c->pos += 2;
            ++depth;
        } else {
            ++c->pos;
        }
    }
}

/* Skips a quoted literal that starts at the opening quote. */
static void skip_quoted(Scan *c, char quote, int escapes, int multiline)
{
    ++c->pos;
    while (c->pos < c->n) {
        char ch = c->s[c->pos];
        if (escapes && ch == '\\' && c->pos + 1 < c->n) {
            if (c->s[c->pos + 1] == '\r' || c->s[c->pos + 1] == '\n') {
                c->pos += 2;
                if (c->s[c->pos - 1] == '\r' && c->pos < c->n && c->s[c->pos] == '\n') ++c->pos;
                newline(c);
            } else {
                c->pos += 2;
            }
            continue;
        }
        if (ch == quote) { ++c->pos; return; }
        if (ch == '\n' || ch == '\r') {
            if (!multiline) return; /* unterminated: stop at the line end */
            ++c->pos;
            if (ch == '\r' && c->pos < c->n && c->s[c->pos] == '\n') ++c->pos;
            newline(c);
            continue;
        }
        ++c->pos;
    }
}

static void skip_triple(Scan *c)
{
    c->pos += 3;
    while (c->pos < c->n) {
        if (c->s[c->pos] == '\\' && c->pos + 1 < c->n) { c->pos += 2; continue; }
        if (starts_with(c, "\"\"\"")) { c->pos += 3; return; }
        if (c->s[c->pos] == '\r' || c->s[c->pos] == '\n') {
            char ch = c->s[c->pos++];
            if (ch == '\r' && c->pos < c->n && c->s[c->pos] == '\n') ++c->pos;
            newline(c);
        } else {
            ++c->pos;
        }
    }
}

/* JavaScript template literal with ${...} nesting. */
static void skip_template(Scan *c)
{
    ++c->pos;
    while (c->pos < c->n) {
        char ch = c->s[c->pos];
        if (ch == '\\' && c->pos + 1 < c->n) { c->pos += 2; continue; }
        if (ch == '`') { ++c->pos; return; }
        if (ch == '$' && c->pos + 1 < c->n && c->s[c->pos + 1] == '{') {
            int depth = 1;
            c->pos += 2;
            while (c->pos < c->n && depth > 0) {
                char inner = c->s[c->pos];
                if (inner == '{') { ++depth; ++c->pos; }
                else if (inner == '}') { --depth; ++c->pos; }
                else if (inner == '`') skip_template(c);
                else if (inner == '"' || inner == '\'') skip_quoted(c, inner, 1, 0);
                else if (inner == '\r' || inner == '\n') {
                    ++c->pos;
                    if (inner == '\r' && c->pos < c->n && c->s[c->pos] == '\n') ++c->pos;
                    newline(c);
                } else ++c->pos;
            }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            ++c->pos;
            if (ch == '\r' && c->pos < c->n && c->s[c->pos] == '\n') ++c->pos;
            newline(c);
            continue;
        }
        ++c->pos;
    }
}

/* Rust raw string r"..", r#".."#, br"..": `c->pos` is on the first quote or '#'. */
static int skip_rust_raw(Scan *c)
{
    size_t hashes = 0, p = c->pos;
    while (p < c->n && c->s[p] == '#') { ++hashes; ++p; }
    if (p >= c->n || c->s[p] != '"') return 0;
    ++p;
    while (p < c->n) {
        if (c->s[p] == '"') {
            size_t k = 0;
            while (k < hashes && p + 1 + k < c->n && c->s[p + 1 + k] == '#') ++k;
            if (k == hashes) { c->pos = p + 1 + hashes; return 1; }
        }
        if (c->s[p] == '\n') { c->pos = p + 1; newline(c); p = c->pos; continue; }
        ++p;
    }
    c->pos = c->n;
    return 1;
}

static int newline_ends_statement(const Scan *c)
{
    return c->family == FAM_JS || c->family == FAM_TS || c->family == FAM_GO ||
           c->family == FAM_KOTLIN || c->family == FAM_SWIFT;
}

/* After a newline: flush a declaration that has no body, unless the line
 * continues (operator at the end) or the next line opens the body. */
static void maybe_flush_at_newline(Scan *c)
{
    size_t p = c->pos;
    const Tok *last;
    if (!newline_ends_statement(c) || c->ntoks == 0) return;
    if (c->paren != 0 && !(c->group_active && c->paren == 1)) return;
    last = &c->toks[c->ntoks - 1];
    if (last->c == '=' || last->c == ',' || last->c == '(' || last->c == '.' ||
        last->c == 'A' || last->c == '+' || last->c == '-' || last->c == '*' ||
        last->c == '/' || last->c == '&' || last->c == '|' || last->c == '?' ||
        last->c == ':' || last->c == '<' || last->c == '>')
        return;
    while (p < c->n && (c->s[p] == ' ' || c->s[p] == '\t' || c->s[p] == '\r' || c->s[p] == '\n')) ++p;
    if (p < c->n && (c->s[p] == '{' || c->s[p] == '.')) return;
    if (c->family == FAM_GO && c->group_active) {
        classify_statement(c, 0);
        statement_reset(c);
        return;
    }
    if (c->paren == 0) {
        classify_statement(c, 0);
        statement_reset(c);
    }
}

static void scan_braces(Scan *c)
{
    int nested_comments = c->family == FAM_RUST || c->family == FAM_SWIFT ||
                          c->family == FAM_KOTLIN;
    while (c->pos < c->n && !c->out_of_memory && !c->truncated) {
        unsigned char ch = (unsigned char)c->s[c->pos];
        size_t col = c->pos - c->line_start + 1;
        if (ch == '\r' || ch == '\n') {
            ++c->pos;
            if (ch == '\r' && c->pos < c->n && c->s[c->pos] == '\n') ++c->pos;
            newline(c);
            maybe_flush_at_newline(c);
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') { ++c->pos; continue; }
        if (ch == '/' && c->pos + 1 < c->n && c->s[c->pos + 1] == '/') { skip_line_comment(c); continue; }
        if (ch == '/' && c->pos + 1 < c->n && c->s[c->pos + 1] == '*') { skip_block_comment(c, nested_comments); continue; }
        if (ch == '"') {
            if ((c->family == FAM_JAVA || c->family == FAM_KOTLIN || c->family == FAM_SWIFT) &&
                starts_with(c, "\"\"\"")) skip_triple(c);
            else skip_quoted(c, '"', 1, 0);
            push_token(c, 's', NULL, 0, col);
            continue;
        }
        if (ch == '`' && (c->family == FAM_JS || c->family == FAM_TS)) {
            skip_template(c);
            push_token(c, 's', NULL, 0, col);
            continue;
        }
        if (ch == '`' && c->family == FAM_GO) {
            ++c->pos;
            while (c->pos < c->n && c->s[c->pos] != '`') {
                if (c->s[c->pos] == '\n') { ++c->pos; newline(c); }
                else ++c->pos;
            }
            if (c->pos < c->n) ++c->pos;
            push_token(c, 's', NULL, 0, col);
            continue;
        }
        if (ch == '\'' && c->family != FAM_RUST && c->family != FAM_SWIFT && c->family != FAM_SLINT) {
            skip_quoted(c, '\'', 1, 0);
            push_token(c, 's', NULL, 0, col);
            continue;
        }
        if (ch == '\'' && c->family == FAM_RUST) {
            /* char literal 'x' / '\n'; otherwise a lifetime tick */
            if (c->pos + 2 < c->n && (c->s[c->pos + 1] == '\\' ||
                                      c->s[c->pos + 2] == '\'')) {
                skip_quoted(c, '\'', 1, 0);
                push_token(c, 's', NULL, 0, col);
            } else {
                ++c->pos;
            }
            continue;
        }
        if (is_ident_start(ch) ||
            (ch == '#' && (c->family == FAM_JS || c->family == FAM_TS) &&
             c->pos + 1 < c->n && is_ident_start((unsigned char)c->s[c->pos + 1]))) {
            size_t start = c->pos;
            ++c->pos;
            while (c->pos < c->n && is_ident_char((unsigned char)c->s[c->pos])) ++c->pos;
            if (c->family == FAM_RUST && (c->pos - start == 1 || c->pos - start == 2) &&
                (c->s[start] == 'r' || (c->s[start] == 'b' && c->s[start + 1] == 'r')) &&
                c->pos < c->n && (c->s[c->pos] == '"' || c->s[c->pos] == '#') &&
                skip_rust_raw(c)) {
                push_token(c, 's', NULL, 0, col);
                continue;
            }
            push_token(c, 'w', c->s + start, c->pos - start, col);
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            while (c->pos < c->n && (is_ident_char((unsigned char)c->s[c->pos]) || c->s[c->pos] == '.')) ++c->pos;
            push_token(c, 'n', NULL, 0, col);
            continue;
        }
        ++c->pos;
        switch (ch) {
        case '(': case '[':
            if (ch == '(' && c->ntoks == 1 && c->family == FAM_GO && c->paren == 0 &&
                (tok_is(&c->toks[0], "const") || tok_is(&c->toks[0], "var") ||
                 tok_is(&c->toks[0], "type"))) {
                c->group_tok = c->toks[0];
                c->group_active = 1;
                ++c->paren;
                statement_reset(c);
                break;
            }
            ++c->paren;
            push_token(c, (char)ch, NULL, 0, col);
            break;
        case ')': case ']':
            if (c->paren > 0) --c->paren;
            if (c->group_active && c->paren == 0 && ch == ')') {
                c->group_active = 0;
                statement_reset(c);
                break;
            }
            push_token(c, (char)ch, NULL, 0, col);
            break;
        case '{': open_brace(c); break;
        case '}': close_brace(c); break;
        case ';': end_statement(c); break;
        case '=':
            if (c->pos < c->n && c->s[c->pos] == '>') { ++c->pos; push_token(c, 'A', NULL, 0, col); }
            else if (c->pos < c->n && c->s[c->pos] == '=') { ++c->pos; push_token(c, 'o', NULL, 0, col); }
            else push_token(c, '=', NULL, 0, col);
            break;
        default:
            push_token(c, (char)ch, NULL, 0, col);
            break;
        }
    }
    if (!c->out_of_memory && c->paren == 0) end_statement(c);
}

/* ---- Python ------------------------------------------------------------- */

typedef struct PyBlock { int indent; int is_class; } PyBlock;

static int py_word(const char *s, size_t n, size_t *pos, const char *kw)
{
    size_t len = strlen(kw);
    if (*pos + len < n && memcmp(s + *pos, kw, len) == 0 &&
        (s[*pos + len] == ' ' || s[*pos + len] == '\t')) {
        *pos += len;
        while (*pos < n && (s[*pos] == ' ' || s[*pos] == '\t')) ++(*pos);
        return 1;
    }
    return 0;
}

static void scan_python(Scan *c)
{
    PyBlock blocks[64];
    int nblocks = 0;
    char triple = 0;
    int paren = 0, continued = 0;
    size_t pos = 0;
    c->line = 0;
    while (pos < c->n && !c->out_of_memory && !c->truncated) {
        size_t line_begin = pos, end = pos, p;
        int clean = triple == 0 && paren == 0 && !continued;
        int indent = 0;
        ++c->line;
        while (end < c->n && c->s[end] != '\n' && c->s[end] != '\r') ++end;
        p = line_begin;
        while (p < end && (c->s[p] == ' ' || c->s[p] == '\t')) {
            indent += c->s[p] == '\t' ? 8 : 1;
            ++p;
        }
        if (clean && p < end && c->s[p] != '#') {
            size_t q = p;
            while (nblocks > 0 && blocks[nblocks - 1].indent >= indent) --nblocks;
            if (nblocks == 0 || blocks[nblocks - 1].is_class) {
                int in_class = nblocks > 0;
                int is_async = py_word(c->s, end, &q, "async");
                size_t name_start;
                if (py_word(c->s, end, &q, "def")) {
                    name_start = q;
                    while (q < end && is_ident_char((unsigned char)c->s[q])) ++q;
                    if (q > name_start) {
                        Tok t;
                        t.c = 'w'; t.p = c->s + name_start; t.len = q - name_start;
                        t.line = c->line; t.col = name_start - line_begin + 1;
                        add_symbol(c, &t, in_class ? AXYNE_SYMBOL_METHOD : AXYNE_SYMBOL_FUNCTION);
                        if (nblocks < 64) { blocks[nblocks].indent = indent; blocks[nblocks].is_class = 0; ++nblocks; }
                    }
                } else if (!is_async && py_word(c->s, end, &q, "class")) {
                    name_start = q;
                    while (q < end && is_ident_char((unsigned char)c->s[q])) ++q;
                    if (q > name_start) {
                        Tok t;
                        t.c = 'w'; t.p = c->s + name_start; t.len = q - name_start;
                        t.line = c->line; t.col = name_start - line_begin + 1;
                        add_symbol(c, &t, AXYNE_SYMBOL_TYPE);
                        if (nblocks < 64) { blocks[nblocks].indent = indent; blocks[nblocks].is_class = 1; ++nblocks; }
                    }
                } else if (!in_class && indent == 0 && is_ident_start((unsigned char)c->s[q])) {
                    size_t name_end = q, r;
                    int upper = 1;
                    while (name_end < end && is_ident_char((unsigned char)c->s[name_end])) ++name_end;
                    r = name_end;
                    while (r < end && (c->s[r] == ' ' || c->s[r] == '\t')) ++r;
                    if (r < end && c->s[r] == ':') {
                        /* annotated assignment: NAME: type = value */
                        size_t k = r + 1;
                        int depth = 0, assigned = 0;
                        for (; k < end; ++k) {
                            if (c->s[k] == '[' || c->s[k] == '(') ++depth;
                            else if (c->s[k] == ']' || c->s[k] == ')') --depth;
                            else if (c->s[k] == '=' && depth == 0 &&
                                     (k + 1 >= end || c->s[k + 1] != '=')) { assigned = 1; break; }
                        }
                        r = assigned ? r : end;
                        if (assigned) r = k;
                    }
                    if (r < end && c->s[r] == '=' && (r + 1 >= end || c->s[r + 1] != '=')) {
                        size_t k;
                        Tok t;
                        for (k = q; k < name_end; ++k)
                            if ((c->s[k] >= 'a' && c->s[k] <= 'z')) { upper = 0; break; }
                        t.c = 'w'; t.p = c->s + q; t.len = name_end - q;
                        t.line = c->line; t.col = q - line_begin + 1;
                        add_symbol(c, &t, upper ? AXYNE_SYMBOL_CONSTANT : AXYNE_SYMBOL_VARIABLE);
                    }
                }
            }
        }
        /* Lex the line to track strings, brackets and continuation. */
        continued = 0;
        {
            size_t k = line_begin;
            char single = 0;
            while (k < end) {
                char ch = c->s[k];
                if (triple != 0) {
                    if (ch == '\\') { k += 2; continue; }
                    if (ch == triple && k + 2 < end && c->s[k + 1] == triple &&
                        c->s[k + 2] == triple) {
                        triple = 0;
                        k += 3;
                        continue;
                    }
                    ++k;
                    continue;
                }
                if (single != 0) {
                    if (ch == '\\') { if (k + 1 >= end) continued = 1; k += 2; continue; }
                    if (ch == single) single = 0;
                    ++k;
                    continue;
                }
                if (ch == '#') break;
                if (ch == '"' || ch == '\'') {
                    if (k + 2 < end && c->s[k + 1] == ch && c->s[k + 2] == ch) { triple = ch; k += 3; continue; }
                    single = ch;
                    ++k;
                    continue;
                }
                if (ch == '(' || ch == '[' || ch == '{') ++paren;
                else if ((ch == ')' || ch == ']' || ch == '}') && paren > 0) --paren;
                else if (ch == '\\' && k + 1 >= end) continued = 1;
                ++k;
            }
        }
        pos = end;
        if (pos < c->n) {
            if (c->s[pos] == '\r' && pos + 1 < c->n && c->s[pos + 1] == '\n') pos += 2;
            else ++pos;
        }
    }
}

/* ---- entry point -------------------------------------------------------- */

static int compare_symbols(const void *left, const void *right)
{
    const AxyneSymbol *a = (const AxyneSymbol *)left;
    const AxyneSymbol *b = (const AxyneSymbol *)right;
    if (a->line != b->line) return a->line < b->line ? -1 : 1;
    if (a->column != b->column) return a->column < b->column ? -1 : 1;
    return 0;
}

AxyneStatus axyne_symbols_scan_file(const char *name_or_path, const char *text,
                                    size_t length, AxyneSymbolList *out,
                                    AxyneError *error)
{
    Family family = family_for_file(name_or_path);
    Scan *c;
    size_t i;
    if (out != NULL) { out->items = NULL; out->count = 0; out->truncated = 0; }
    if (out == NULL || (text == NULL && length != 0)) {
        if (error != NULL) { error->code = AXYNE_STATUS_INVALID_ARGUMENT; strcpy(error->message, "Invalid symbol scan arguments"); }
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    if (family == FAM_NONE) {
        if (error != NULL) { error->code = AXYNE_STATUS_UNSUPPORTED; strcpy(error->message, "No symbol scanner for this file type"); }
        return AXYNE_STATUS_UNSUPPORTED;
    }
    if (family == FAM_C) return axyne_symbols_scan(text, length, out, error);
    c = (Scan *)calloc(1, sizeof(*c));
    if (c == NULL) {
        if (error != NULL) { error->code = AXYNE_STATUS_OUT_OF_MEMORY; strcpy(error->message, "Unable to allocate symbol scanner"); }
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    c->family = family;
    c->s = text == NULL ? "" : text;
    c->n = length;
    c->line = 1;
    if (family == FAM_PY) scan_python(c);
    else scan_braces(c);
    if (c->out_of_memory) {
        for (i = 0; i < c->count; ++i) free(c->items[i].name);
        free(c->items);
        free(c);
        if (error != NULL) { error->code = AXYNE_STATUS_OUT_OF_MEMORY; strcpy(error->message, "Unable to allocate symbols"); }
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    if (c->count > 1) qsort(c->items, c->count, sizeof(*c->items), compare_symbols);
    out->items = c->items;
    out->count = c->count;
    out->truncated = c->truncated;
    free(c);
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}
