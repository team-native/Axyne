#include "axyne/symbols.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TK_IDENT, TK_NUMBER, TK_LITERAL, TK_PUNCT, TK_OP };
enum { FRAME_TRANSPARENT = 0, FRAME_RESET = 1, FRAME_CONTINUE = 2, FRAME_NESTED = 3 };
enum { STATEMENT_MAX = 2048, STACK_MAX = 256 };

typedef struct Token {
    unsigned char kind;
    unsigned char ch; /* PUNCT/OP: first byte; LITERAL: opening quote */
    size_t start, length, line, column;
} Token;

typedef struct Scanner {
    const char *s;
    size_t n, i, line, line_start;
    int at_line_start;
    AxyneSymbol *items;
    size_t count, capacity;
    int truncated, out_of_memory;
    Token statement[STATEMENT_MAX];
    size_t statement_count;
    int statement_overflow;
    long tag_index;
    unsigned char stack[STACK_MAX];
    size_t depth, opaque;
} Scanner;

static size_t eol_length(const Scanner *c, size_t i)
{
    if (i >= c->n) return 0;
    if (c->s[i] == '\n') return 1;
    if (c->s[i] == '\r') return (i + 1 < c->n && c->s[i + 1] == '\n') ? 2 : 1;
    return 0;
}

static void consume_eol(Scanner *c, size_t length, int line_start)
{
    c->i += length;
    ++c->line;
    c->line_start = c->i;
    c->at_line_start = line_start;
}

static int ident_start(unsigned char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' ||
           ch == '$' || ch >= 0x80;
}

static int ident_part(unsigned char ch)
{
    return ident_start(ch) || (ch >= '0' && ch <= '9');
}

static int is_digit(unsigned char ch) { return ch >= '0' && ch <= '9'; }

static void skip_line_comment(Scanner *c)
{
    c->i += 2;
    while (c->i < c->n) {
        size_t e = eol_length(c, c->i);
        if (e != 0) {
            if (c->s[c->i - 1] == '\\') { consume_eol(c, e, 0); continue; }
            break;
        }
        ++c->i;
    }
}

static void skip_block_comment(Scanner *c)
{
    c->i += 2;
    while (c->i < c->n) {
        size_t e;
        if (c->s[c->i] == '*' && c->i + 1 < c->n && c->s[c->i + 1] == '/') {
            c->i += 2;
            return;
        }
        e = eol_length(c, c->i);
        if (e != 0) consume_eol(c, e, c->at_line_start);
        else ++c->i;
    }
}

/* Skips a string or character literal; an unterminated literal ends at the
 * end of its line so one stray quote cannot swallow the file. */
static void skip_quoted(Scanner *c)
{
    char quote = c->s[c->i++];
    while (c->i < c->n) {
        size_t e = eol_length(c, c->i);
        char ch = c->s[c->i];
        if (e != 0) return;
        if (ch == '\\') {
            ++c->i;
            if (c->i < c->n) {
                e = eol_length(c, c->i);
                if (e != 0) consume_eol(c, e, 0);
                else ++c->i;
            }
            continue;
        }
        ++c->i;
        if (ch == quote) return;
    }
}

/* C++ raw string R"delim( ... )delim"; c->i is at the opening quote. */
static int skip_raw_string(Scanner *c)
{
    char delimiter[17];
    size_t d = 0, j = c->i + 1;
    while (j < c->n && c->s[j] != '(' && d < 16) {
        char ch = c->s[j];
        if (ch == ' ' || ch == ')' || ch == '\\' || ch == '"' ||
            ch == '\t' || ch == '\n' || ch == '\r') return 0;
        delimiter[d++] = ch;
        ++j;
    }
    if (j >= c->n || c->s[j] != '(') return 0;
    c->i = j + 1;
    while (c->i < c->n) {
        size_t e = eol_length(c, c->i);
        if (e != 0) { consume_eol(c, e, 0); continue; }
        if (c->s[c->i] == ')' && c->n - c->i > d &&
            memcmp(c->s + c->i + 1, delimiter, d) == 0 &&
            c->i + 1 + d < c->n && c->s[c->i + 1 + d] == '"') {
            c->i += d + 2;
            return 1;
        }
        ++c->i;
    }
    return 1;
}

static int emit(Scanner *c, AxyneSymbolKind kind, const char *name,
                size_t length, size_t line, size_t column)
{
    AxyneSymbol *symbol;
    char *copy;
    if (c->out_of_memory || c->truncated) return 0;
    if (c->count >= AXYNE_SYMBOLS_MAX) { c->truncated = 1; return 0; }
    if (c->count == c->capacity) {
        size_t capacity = c->capacity == 0 ? 64 : c->capacity * 2;
        AxyneSymbol *grown = (AxyneSymbol *)realloc(c->items,
                                                   capacity * sizeof(*grown));
        if (grown == NULL) { c->out_of_memory = 1; return 0; }
        c->items = grown;
        c->capacity = capacity;
    }
    copy = (char *)malloc(length + 1);
    if (copy == NULL) { c->out_of_memory = 1; return 0; }
    memcpy(copy, name, length);
    copy[length] = '\0';
    symbol = &c->items[c->count++];
    symbol->name = copy;
    symbol->kind = kind;
    symbol->line = line;
    symbol->column = column;
    return 1;
}

static int emit_token(Scanner *c, AxyneSymbolKind kind, const Token *t)
{
    return emit(c, kind, c->s + t->start, t->length, t->line, t->column);
}

static void skip_horizontal_space(Scanner *c)
{
    while (c->i < c->n && (c->s[c->i] == ' ' || c->s[c->i] == '\t' ||
                           c->s[c->i] == '\f' || c->s[c->i] == '\v'))
        ++c->i;
}

static void skip_directive_rest(Scanner *c)
{
    while (c->i < c->n) {
        char ch = c->s[c->i];
        size_t e = eol_length(c, c->i);
        if (e != 0) return;
        if (ch == '\\' && eol_length(c, c->i + 1) != 0) {
            ++c->i;
            consume_eol(c, eol_length(c, c->i), 0);
        } else if (ch == '/' && c->i + 1 < c->n && c->s[c->i + 1] == '*') {
            skip_block_comment(c);
        } else if (ch == '/' && c->i + 1 < c->n && c->s[c->i + 1] == '/') {
            skip_line_comment(c);
        } else if (ch == '"' || ch == '\'') {
            skip_quoted(c);
        } else {
            ++c->i;
        }
    }
}

/* c->i is at a '#' that starts a line. Reports #define names, then skips the
 * whole logical directive line (continuations and comments included). */
static void directive(Scanner *c)
{
    size_t start;
    c->at_line_start = 0;
    ++c->i;
    skip_horizontal_space(c);
    start = c->i;
    while (c->i < c->n && ident_part((unsigned char)c->s[c->i])) ++c->i;
    if (c->i - start == 6 && memcmp(c->s + start, "define", 6) == 0) {
        skip_horizontal_space(c);
        if (c->i < c->n && ident_start((unsigned char)c->s[c->i])) {
            size_t name = c->i;
            while (c->i < c->n && ident_part((unsigned char)c->s[c->i])) ++c->i;
            (void)emit(c, AXYNE_SYMBOL_MACRO, c->s + name, c->i - name,
                       c->line, name - c->line_start + 1);
        }
    }
    skip_directive_rest(c);
}

static int is_raw_prefix(const char *s, size_t length)
{
    return (length == 1 && s[0] == 'R') ||
           (length == 2 && (s[0] == 'L' || s[0] == 'u' || s[0] == 'U') && s[1] == 'R') ||
           (length == 3 && s[0] == 'u' && s[1] == '8' && s[2] == 'R');
}

static int next_token(Scanner *c, Token *t)
{
    unsigned char ch, next;
    for (;;) {
        size_t e;
        if (c->i >= c->n) return 0;
        ch = (unsigned char)c->s[c->i];
        e = eol_length(c, c->i);
        if (e != 0) { consume_eol(c, e, 1); continue; }
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') { ++c->i; continue; }
        if (ch == '/' && c->i + 1 < c->n) {
            if (c->s[c->i + 1] == '/') { skip_line_comment(c); continue; }
            if (c->s[c->i + 1] == '*') { skip_block_comment(c); continue; }
        }
        if (ch == '\\' && (e = eol_length(c, c->i + 1)) != 0) {
            ++c->i;
            consume_eol(c, e, 0);
            continue;
        }
        if (ch == '#' && c->at_line_start) { directive(c); continue; }
        break;
    }
    t->start = c->i;
    t->line = c->line;
    t->column = c->i - c->line_start + 1;
    t->ch = ch;
    c->at_line_start = 0;
    next = c->i + 1 < c->n ? (unsigned char)c->s[c->i + 1] : 0;
    if (ident_start(ch)) {
        t->kind = TK_IDENT;
        while (c->i < c->n && ident_part((unsigned char)c->s[c->i])) ++c->i;
        if (c->i < c->n && c->s[c->i] == '"' &&
            is_raw_prefix(c->s + t->start, c->i - t->start) &&
            skip_raw_string(c)) {
            t->kind = TK_LITERAL;
            t->ch = '"';
        }
    } else if (is_digit(ch) || (ch == '.' && is_digit(next))) {
        t->kind = TK_NUMBER;
        ++c->i;
        while (c->i < c->n) {
            unsigned char d = (unsigned char)c->s[c->i];
            unsigned char after = c->i + 1 < c->n ? (unsigned char)c->s[c->i + 1] : 0;
            if (ident_part(d) || d == '.') {
                ++c->i;
                if ((d == 'e' || d == 'E' || d == 'p' || d == 'P') &&
                    (after == '+' || after == '-')) ++c->i;
            } else if (d == '\'' && ident_part(after)) {
                ++c->i;
            } else break;
        }
    } else if (ch == '"' || ch == '\'') {
        t->kind = TK_LITERAL;
        skip_quoted(c);
    } else if ((next == '=' && (ch == '=' || ch == '!' || ch == '<' || ch == '>')) ||
               (ch == '-' && next == '>')) {
        t->kind = TK_OP;
        c->i += 2;
    } else {
        t->kind = TK_PUNCT;
        ++c->i;
    }
    t->length = c->i - t->start;
    return 1;
}

/* ---- statement analysis ------------------------------------------------- */

static int is_punct(const Token *t, char ch)
{
    return t->kind == TK_PUNCT && t->ch == (unsigned char)ch;
}

static int is_word(const Scanner *c, const Token *t, const char *word)
{
    size_t length = strlen(word);
    return t->kind == TK_IDENT && t->length == length &&
           memcmp(c->s + t->start, word, length) == 0;
}

static int word_in(const Scanner *c, const Token *t, const char *const *words)
{
    for (; *words != NULL; ++words)
        if (is_word(c, t, *words)) return 1;
    return 0;
}

static const char *const control_words[] = {
    "if", "while", "for", "switch", "catch", "return", "sizeof", "defined",
    "do", "else", NULL
};
static const char *const attribute_words[] = {
    "__attribute__", "__declspec", "alignas", "_Alignas", "noexcept", "throw",
    "decltype", "__asm__", "asm", "__asm", "requires", NULL
};
static const char *const tag_words[] = { "struct", "union", "enum", "class", NULL };
static const char *const declaration_blockers[] = {
    "extern", "typedef", "return", "using", "namespace", "template", "friend",
    "static_assert", "_Static_assert", "goto", "public", "private",
    "protected", "case", "default", NULL
};
static const char *const qualifier_words[] = {
    "static", "const", "volatile", "register", "inline", "extern", "struct",
    "union", "enum", "class", "_Thread_local", "thread_local", "constexpr",
    "__extension__", "typedef", NULL
};

static void statement_reset(Scanner *c)
{
    c->statement_count = 0;
    c->statement_overflow = 0;
    c->tag_index = -1;
}

static void statement_add(Scanner *c, const Token *t)
{
    if (c->statement_count < STATEMENT_MAX) c->statement[c->statement_count++] = *t;
    else c->statement_overflow = 1;
}

/* Index of the token that closes the group opened at `open`, or n - 1. */
static size_t group_end(const Token *t, size_t n, size_t open, char o, char close)
{
    size_t depth = 0;
    for (size_t i = open; i < n; ++i) {
        if (is_punct(&t[i], o)) ++depth;
        else if (is_punct(&t[i], close) && --depth == 0) return i;
    }
    return n - 1;
}

static int is_trailing_token(const Token *t)
{
    return t->kind == TK_IDENT || is_punct(t, '*') || is_punct(t, '&') ||
           is_punct(t, ':') || is_punct(t, '<') || is_punct(t, '>') ||
           (t->kind == TK_OP && t->ch == '-');
}

/* Returns the index of the function name token when the statement tail looks
 * like `declarators name ( params ) qualifiers`, otherwise -1. */
static long find_function_name(const Scanner *c, size_t n)
{
    const Token *t = c->statement;
    size_t e = n;
    for (int guard = 0; guard < 64; ++guard) {
        size_t depth = 0, k;
        int matched = 0;
        while (e > 0 && is_trailing_token(&t[e - 1])) --e;
        if (e == 0 || !is_punct(&t[e - 1], ')')) return -1;
        for (k = e; k > 0;) {
            --k;
            if (is_punct(&t[k], ')')) ++depth;
            else if (is_punct(&t[k], '(') && --depth == 0) { matched = 1; break; }
        }
        if (!matched || k == 0 || t[k - 1].kind != TK_IDENT) return -1;
        if (word_in(c, &t[k - 1], attribute_words)) { e = k - 1; continue; }
        if (k >= 2 && (is_punct(&t[k - 2], ',') ||
                       (is_punct(&t[k - 2], ':') &&
                        !(k >= 3 && is_punct(&t[k - 3], ':'))))) {
            /* C++ constructor initializer list: continue before its ':' */
            size_t nesting = 0, j = k - 1;
            int found = 0;
            while (j > 0) {
                --j;
                if (is_punct(&t[j], ')')) ++nesting;
                else if (is_punct(&t[j], '(')) {
                    if (nesting == 0) break;
                    --nesting;
                } else if (nesting == 0 && is_punct(&t[j], ':') &&
                           !(j > 0 && is_punct(&t[j - 1], ':')) &&
                           !(j + 1 < n && is_punct(&t[j + 1], ':'))) {
                    found = 1;
                    break;
                }
            }
            if (!found) return -1;
            e = j;
            continue;
        }
        if (word_in(c, &t[k - 1], control_words)) return -1;
        return (long)(k - 1);
    }
    return -1;
}

static int has_top_level_assignment(const Scanner *c, size_t n)
{
    const Token *t = c->statement;
    size_t depth = 0;
    for (size_t i = 0; i < n; ++i) {
        if (is_punct(&t[i], '(') || is_punct(&t[i], '[')) ++depth;
        else if ((is_punct(&t[i], ')') || is_punct(&t[i], ']')) && depth > 0) --depth;
        else if (depth == 0 && is_punct(&t[i], '=')) {
            int operator_name = 0;
            for (size_t back = 1; back <= 3 && back <= i; ++back)
                if (is_word(c, &t[i - back], "operator")) operator_name = 1;
            if (!operator_name) return 1;
        }
    }
    return 0;
}

static size_t paren_depth(const Scanner *c, size_t n)
{
    size_t depth = 0;
    for (size_t i = 0; i < n; ++i) {
        if (is_punct(&c->statement[i], '(') || is_punct(&c->statement[i], '[')) ++depth;
        else if ((is_punct(&c->statement[i], ')') || is_punct(&c->statement[i], ']')) &&
                 depth > 0) --depth;
    }
    return depth;
}

static int find_tag_name(const Scanner *c, size_t n, size_t *name)
{
    const Token *t = c->statement;
    size_t keyword = (size_t)-1, depth = 0, candidate = (size_t)-1;
    for (size_t i = 0; i < n; ++i) {
        if (is_punct(&t[i], '(')) ++depth;
        else if (is_punct(&t[i], ')') && depth > 0) --depth;
        else if (depth == 0 && word_in(c, &t[i], tag_words)) keyword = i;
    }
    if (keyword == (size_t)-1) return 0;
    for (size_t j = keyword + 1; j < n; ++j) {
        if (t[j].kind == TK_IDENT) {
            if (is_word(c, &t[j], "__attribute__") || is_word(c, &t[j], "__declspec") ||
                is_word(c, &t[j], "alignas") || is_word(c, &t[j], "_Alignas")) {
                if (j + 1 < n && is_punct(&t[j + 1], '('))
                    j = group_end(t, n, j + 1, '(', ')');
                continue;
            }
            if (word_in(c, &t[j], tag_words) || is_word(c, &t[j], "final")) continue;
            candidate = j;
        } else if (is_punct(&t[j], '[')) {
            j = group_end(t, n, j, '[', ']');
        } else {
            break;
        }
    }
    if (candidate == (size_t)-1) return 0;
    *name = candidate;
    return 1;
}

/* Decides what an opening brace at file scope is, reporting functions and tag
 * names along the way. */
static unsigned char classify_brace(Scanner *c)
{
    size_t n = c->statement_count, name;
    const Token *t = c->statement;
    long function;
    if (n == 0) return FRAME_RESET;
    if (paren_depth(c, n) > 0 || has_top_level_assignment(c, n)) return FRAME_CONTINUE;
    function = find_function_name(c, n);
    if (function >= 0) {
        (void)emit_token(c, AXYNE_SYMBOL_FUNCTION, &t[function]);
        return FRAME_RESET;
    }
    if ((n >= 2 && is_word(c, &t[0], "extern") && t[1].kind == TK_LITERAL &&
         t[1].ch == '"') ||
        is_word(c, &t[0], "namespace") ||
        (n >= 2 && is_word(c, &t[1], "namespace")))
        return FRAME_TRANSPARENT;
    if (find_tag_name(c, n, &name)) {
        if (emit_token(c, AXYNE_SYMBOL_TYPE, &t[name])) c->tag_index = (long)c->count - 1;
        return FRAME_CONTINUE;
    }
    {
        size_t depth = 0;
        for (size_t i = 0; i < n; ++i) {
            if (is_punct(&t[i], '(')) ++depth;
            else if (is_punct(&t[i], ')') && depth > 0) --depth;
            else if (depth == 0 && word_in(c, &t[i], tag_words)) return FRAME_CONTINUE;
        }
    }
    return FRAME_RESET;
}

static void emit_type_name(Scanner *c, const Token *t)
{
    if (c->tag_index >= 0 && (size_t)c->tag_index < c->count) {
        const char *tag = c->items[c->tag_index].name;
        if (strlen(tag) == t->length && memcmp(tag, c->s + t->start, t->length) == 0)
            return;
    }
    (void)emit_token(c, AXYNE_SYMBOL_TYPE, t);
}

static void finish_typedef(Scanner *c, size_t n)
{
    const Token *t = c->statement;
    size_t body = (size_t)-1, end;
    for (size_t i = n; i > 0; --i)
        if (is_punct(&t[i - 1], '}')) { body = i - 1; break; }
    if (body != (size_t)-1) {
        for (size_t i = body + 1; i < n; ++i)
            if (t[i].kind == TK_IDENT &&
                (i + 1 == n || is_punct(&t[i + 1], ',') || is_punct(&t[i + 1], '[')))
                emit_type_name(c, &t[i]);
        return;
    }
    for (size_t i = 1; i + 2 < n; ++i)
        if ((is_punct(&t[i], '*') || is_punct(&t[i], '^')) &&
            t[i + 1].kind == TK_IDENT && is_punct(&t[i + 2], ')')) {
            emit_type_name(c, &t[i + 1]);
            return;
        }
    for (size_t i = 1; i < n; ++i)
        if (is_punct(&t[i], '(')) {
            if (t[i - 1].kind == TK_IDENT) emit_type_name(c, &t[i - 1]);
            return;
        }
    end = n;
    for (size_t i = 0; i < n; ++i)
        if (is_punct(&t[i], '[')) { end = i; break; }
    while (end > 1) {
        --end;
        if (t[end].kind == TK_IDENT) { emit_type_name(c, &t[end]); return; }
    }
}

static void finish_variable(Scanner *c, size_t n)
{
    const Token *t = c->statement;
    size_t region = n, name, bracket = (size_t)-1;
    int has_type = 0;
    if (t[0].kind == TK_IDENT && word_in(c, &t[0], declaration_blockers)) return;
    for (size_t i = 0; i < n; ++i)
        if (is_punct(&t[i], '=')) { region = i; break; }
    for (size_t i = 0; i < region; ++i) {
        if (is_punct(&t[i], '(') || is_punct(&t[i], '{') || is_punct(&t[i], '}') ||
            is_punct(&t[i], ':') || is_punct(&t[i], ',') ||
            is_word(c, &t[i], "extern") || is_word(c, &t[i], "typedef"))
            return;
        if (bracket == (size_t)-1 && is_punct(&t[i], '[')) bracket = i;
    }
    name = (bracket != (size_t)-1 ? bracket : region);
    if (name < 2) return;
    --name;
    if (t[name].kind != TK_IDENT || word_in(c, &t[name], qualifier_words)) return;
    for (size_t i = 0; i < name; ++i)
        if (t[i].kind == TK_IDENT && !word_in(c, &t[i], qualifier_words)) has_type = 1;
    if (!has_type) return;
    (void)emit_token(c, AXYNE_SYMBOL_VARIABLE, &t[name]);
}

static void finish_statement(Scanner *c)
{
    size_t n = c->statement_count;
    const Token *t = c->statement;
    if (n == 0 || c->statement_overflow) return;
    if (is_word(c, &t[0], "typedef")) {
        if (n >= 2) finish_typedef(c, n);
    } else if (n >= 4 && is_word(c, &t[0], "using") && t[1].kind == TK_IDENT &&
               is_punct(&t[2], '=')) {
        (void)emit_token(c, AXYNE_SYMBOL_TYPE, &t[1]);
    } else {
        finish_variable(c, n);
    }
}

static void push_frame(Scanner *c, unsigned char type)
{
    if (c->depth < STACK_MAX) c->stack[c->depth] = type;
    ++c->depth;
    if (type != FRAME_TRANSPARENT) ++c->opaque;
}

static void open_brace(Scanner *c, const Token *brace)
{
    unsigned char type;
    if (c->opaque > 0) { push_frame(c, FRAME_NESTED); return; }
    type = c->statement_overflow ? FRAME_RESET : classify_brace(c);
    if (type == FRAME_CONTINUE) statement_add(c, brace);
    else statement_reset(c);
    push_frame(c, type);
}

static void close_brace(Scanner *c, const Token *brace)
{
    unsigned char type;
    if (c->depth == 0) {
        if (c->opaque == 0) statement_reset(c);
        return;
    }
    --c->depth;
    type = c->depth < STACK_MAX ? c->stack[c->depth] : FRAME_NESTED;
    if (type == FRAME_TRANSPARENT) { statement_reset(c); return; }
    if (c->opaque > 0) --c->opaque;
    if (c->opaque == 0) {
        if (type == FRAME_CONTINUE) statement_add(c, brace);
        else statement_reset(c);
    }
}

static int compare_symbols(const void *left, const void *right)
{
    const AxyneSymbol *a = (const AxyneSymbol *)left;
    const AxyneSymbol *b = (const AxyneSymbol *)right;
    if (a->line != b->line) return a->line < b->line ? -1 : 1;
    if (a->column != b->column) return a->column < b->column ? -1 : 1;
    return 0;
}

static AxyneStatus fail(AxyneError *error, AxyneStatus status, const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

void axyne_symbols_destroy(AxyneSymbolList *list)
{
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; ++i) free(list->items[i].name);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->truncated = 0;
}

AxyneStatus axyne_symbols_scan(const char *text, size_t length,
                               AxyneSymbolList *out, AxyneError *error)
{
    Scanner *c;
    Token t;
    if (out == NULL || (text == NULL && length != 0))
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid symbol scan arguments");
    out->items = NULL;
    out->count = 0;
    out->truncated = 0;
    c = (Scanner *)calloc(1, sizeof(*c));
    if (c == NULL)
        return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to allocate symbol scanner");
    c->s = text == NULL ? "" : text;
    c->n = length;
    c->line = 1;
    c->at_line_start = 1;
    c->tag_index = -1;
    while (!c->truncated && !c->out_of_memory && next_token(c, &t)) {
        if (c->opaque > 0) {
            if (is_punct(&t, '{')) open_brace(c, &t);
            else if (is_punct(&t, '}')) close_brace(c, &t);
        } else if (is_punct(&t, '{')) {
            open_brace(c, &t);
        } else if (is_punct(&t, '}')) {
            close_brace(c, &t);
        } else if (is_punct(&t, ';')) {
            finish_statement(c);
            statement_reset(c);
        } else {
            statement_add(c, &t);
        }
    }
    if (c->out_of_memory) {
        for (size_t i = 0; i < c->count; ++i) free(c->items[i].name);
        free(c->items);
        free(c);
        return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to allocate symbols");
    }
    if (c->count > 1) qsort(c->items, c->count, sizeof(*c->items), compare_symbols);
    out->items = c->items;
    out->count = c->count;
    out->truncated = c->truncated;
    free(c);
    if (error != NULL) {
        error->code = AXYNE_STATUS_OK;
        error->message[0] = '\0';
    }
    return AXYNE_STATUS_OK;
}

int axyne_symbols_supports_file(const char *name_or_path)
{
    static const char *const extensions[] = {
        "c", "h", "cc", "cpp", "cxx", "hpp", "m", "mm"
    };
    const char *base, *dot, *p;
    if (name_or_path == NULL) return 0;
    base = name_or_path;
    for (p = name_or_path; *p != '\0'; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    dot = strrchr(base, '.');
    if (dot == NULL || dot == base) return 0;
    ++dot;
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        const char *a = dot, *b = extensions[i];
        while (*a != '\0' && *b != '\0') {
            char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + ('a' - 'A')) : *a;
            if (ca != *b) break;
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') return 1;
    }
    return 0;
}
