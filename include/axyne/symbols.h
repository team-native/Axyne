#ifndef AXYNE_SYMBOLS_H
#define AXYNE_SYMBOLS_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dependency-free, heuristic symbol scanner for C, C++ and Objective-C-like
 * text. It is a lexical scan, not a parser: it is meant to feed the palette's
 * "@" mode and the explorer outline, never to drive refactoring. */

#define AXYNE_SYMBOLS_MAX 5000

typedef enum AxyneSymbolKind {
    AXYNE_SYMBOL_FUNCTION = 1, /* function definitions (not prototypes) */
    AXYNE_SYMBOL_MACRO = 2,    /* #define NAME and #define NAME(args) */
    AXYNE_SYMBOL_TYPE = 3,     /* struct/union/enum/class tags and typedef names */
    AXYNE_SYMBOL_VARIABLE = 4  /* simple file-scope variable definitions */
} AxyneSymbolKind;

typedef struct AxyneSymbol {
    char *name;            /* owned, NUL-terminated identifier */
    AxyneSymbolKind kind;
    size_t line;           /* 1-based line of the name */
    size_t column;         /* 1-based BYTE column of the name on that line */
} AxyneSymbol;

typedef struct AxyneSymbolList {
    AxyneSymbol *items;    /* in file order (line, then column) */
    size_t count;
    int truncated;         /* non-zero when AXYNE_SYMBOLS_MAX stopped the scan */
} AxyneSymbolList;

/* Scans `text` (`length` bytes, need not be NUL-terminated; NULL is allowed
 * only with length 0). Comments, string/char literals (including C++ raw
 * strings) and preprocessor continuation lines are skipped; LF, CRLF and lone
 * CR line endings and a missing final newline are handled. Function bodies are
 * skipped, so locals and member functions defined inside class bodies are not
 * reported. Unbalanced #if/#else branches can confuse brace tracking; the scan
 * then simply reports fewer symbols. Never reads outside [text, text+length).
 * On failure `out` is left empty. `out` must be released with
 * axyne_symbols_destroy. */
AxyneStatus axyne_symbols_scan(const char *text, size_t length,
                               AxyneSymbolList *out, AxyneError *error);
void axyne_symbols_destroy(AxyneSymbolList *list);

/* Non-zero when the file name (or path, either separator) has one of the
 * extensions the scanner understands: .c .h .cc .cpp .cxx .hpp .m .mm
 * (ASCII case-insensitive). */
int axyne_symbols_supports_file(const char *name_or_path);

#ifdef __cplusplus
}
#endif

#endif
