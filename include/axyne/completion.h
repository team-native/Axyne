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

/* The byte range [*begin, *end) of a document of `length` bytes that is worth
 * scanning for a caret at `caret` (the whole document up to
 * AXYNE_COMPLETION_MAX_SCAN bytes, else a window around the caret). Adapters
 * fetch only this range (SCI_GETRANGEPOINTER) so big files are never made
 * contiguous on each keystroke. */
void axyne_completion_window(size_t length, size_t caret, size_t *begin,
                             size_t *end);

/* Like axyne_completion_build for a slice of the document: `caret` is relative
 * to the slice, and cut_head / cut_tail say that the slice begins / ends in
 * the middle of the document, so a word touching that edge may be incomplete
 * and is not suggested. */
char *axyne_completion_build_slice(const char *text, size_t length,
                                   size_t caret, int cut_head, int cut_tail,
                                   const char *const *keyword_sets,
                                   size_t keyword_set_count,
                                   size_t *prefix_bytes);

#endif
