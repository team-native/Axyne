#ifndef AXYNE_SYNTAX_H
#define AXYNE_SYNTAX_H

#include <stddef.h>
#include <stdint.h>

/* Shared, platform-independent syntax description used by the macOS and
 * Windows editor adapters. A language names the Lexilla lexer to create, the
 * keyword sets to send with SCI_SETKEYWORDS and the {style, colour} pairs to
 * send with SCI_STYLESETFORE after SCI_SETILEXER. Colours are 0xRRGGBB. */

#define AXYNE_SYNTAX_KEYWORD_SETS 4

/* Palette entries equal to this colour are the "plain text" colour; adapters
 * substitute the theme's editor text colour for it on non-reference themes. */
#define AXYNE_SYNTAX_PLAIN 0xd5d8dd

typedef struct AxyneSyntaxStyle {
    unsigned int style;
    uint32_t color;
} AxyneSyntaxStyle;

typedef struct AxyneSyntaxLanguage {
    const char *id;       /* stable language id, e.g. "python" */
    const char *lexer;    /* Lexilla lexer name, e.g. "cpp", "hypertext" */
    const char *keywords[AXYNE_SYNTAX_KEYWORD_SETS]; /* NULL = unused set */
    const AxyneSyntaxStyle *styles;
    size_t style_count;
} AxyneSyntaxLanguage;

/* Resolves a document path (or bare title) to a language. A NULL or empty path
 * is an untitled document and maps to C-family highlighting. Never returns
 * NULL: unknown files map to the "text" language (lexer "null", no styles).
 * Both '/' and '\\' separate directories; matching is case-insensitive. */
const AxyneSyntaxLanguage *axyne_syntax_for_path(const char *path);

/* Looks a language up by its id; NULL when unknown. */
const AxyneSyntaxLanguage *axyne_syntax_by_id(const char *id);

#endif
