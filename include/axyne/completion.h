#ifndef AXYNE_COMPLETION_H
#define AXYNE_COMPLETION_H

#include <stddef.h>

/* Platform-independent word completion for the code editor. The adapters pass
 * the document text, the caret byte offset and the language's keyword sets;
 * they receive a Scintilla autocompletion list (space separated, sorted
 * case-insensitively) and issue SCI_AUTOCSHOW themselves. */

/* A prefix shorter than this many characters shows no suggestions. */
#define AXYNE_COMPLETION_MIN_PREFIX 2
/* At most this many suggestions are returned. */
#define AXYNE_COMPLETION_MAX_ITEMS 200
/* Only this many bytes around the caret are scanned for document words. */
#define AXYNE_COMPLETION_MAX_SCAN 1048576
/* Words longer than this many bytes are never suggested. */
#define AXYNE_COMPLETION_MAX_WORD 128

/* Length in bytes of the identifier prefix that ends at `caret` (letters,
 * digits, '_' and any non-ASCII byte). Returns 0 when there is no prefix. */
size_t axyne_completion_prefix_length(const char *text, size_t length,
                                      size_t caret);

/* Builds the suggestion list for the identifier typed before `caret`.
 * Candidates are the identifiers found in `text` (the occurrence at the caret
 * is ignored) plus the tokens of `keyword_sets` (entries may be NULL), kept
 * when they start with the prefix (case-insensitive for ASCII), are longer
 * than it and are unique. Returns a malloc'd, NUL-terminated list to release
 * with free(), or NULL when the prefix is too short, nothing matches or memory
 * runs out. *prefix_bytes receives the prefix length for SCI_AUTOCSHOW. */
char *axyne_completion_build(const char *text, size_t length, size_t caret,
                             const char *const *keyword_sets,
                             size_t keyword_set_count, size_t *prefix_bytes);

#endif
