#ifndef AXYNE_PALETTE_H
#define AXYNE_PALETTE_H

#include <stddef.h>
#include <stdint.h>

#include "axyne/status.h"
#include "axyne/symbols.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-independent model of the command palette (Figma node 80:70).
 * The native UIs own the window, the text field and the painting; this module
 * turns the text typed into the field plus some source data into a ranked,
 * highlighted row list. It performs no I/O and uses no OS API.
 *
 * Threading: every function is reentrant and keeps no global state, but an
 * AxynePaletteList must not be shared between threads without locking.
 * Strings are UTF-8. All positions are BYTE offsets and never split a UTF-8
 * sequence. */

#define AXYNE_PALETTE_MAX_ITEMS 50

/* ---- modes -------------------------------------------------------------- */

typedef enum AxynePaletteMode {
    AXYNE_PALETTE_MODE_FILE = 0,    /* no prefix */
    AXYNE_PALETTE_MODE_COMMAND = 1, /* ">" */
    AXYNE_PALETTE_MODE_SYMBOL = 2,  /* "@" */
    AXYNE_PALETTE_MODE_LINE = 3     /* ":" */
} AxynePaletteMode;

/* Selects the mode from the first byte of `input` (NULL counts as empty).
 * `*query` (may be NULL) receives a pointer into `input` just after the
 * prefix, with blanks (space/tab) directly after a prefix skipped; for FILE
 * mode it is `input` itself. The prefix characters are ASCII, so the pointer
 * is always on a UTF-8 boundary. */
AxynePaletteMode axyne_palette_parse_mode(const char *input, const char **query);

/* The prefix string to put in the text field for a mode: "", ">", "@", ":".
 * Used by the footer filter chips. */
const char *axyne_palette_mode_prefix(AxynePaletteMode mode);

/* ---- matching ----------------------------------------------------------- */

/* Score tiers (a higher score ranks first). Within a tier a shorter text
 * scores higher, so an exact match always beats a prefix match, which beats a
 * word-boundary match, which beats a plain substring match. A matching text
 * always scores >= 1; an empty query matches everything with score 0. */
enum {
    AXYNE_PALETTE_SCORE_EXACT = 1000,
    AXYNE_PALETTE_SCORE_PREFIX = 800,
    AXYNE_PALETTE_SCORE_BOUNDARY = 500,
    AXYNE_PALETTE_SCORE_SUBSTRING = 200
};

typedef struct AxynePaletteMatch {
    int score;
    size_t start;  /* byte offset of the highlight in the text */
    size_t length; /* byte length of the highlight; 0 means no highlight */
} AxynePaletteMatch;

/* Matches `query` as one contiguous, ASCII case-insensitive substring of
 * `text` (bytes >= 0x80 compare exactly). A "word boundary" is the start of
 * the text, a position after one of `/ \ . _ - space`, or a lower-case to
 * upper-case transition (camelCase). Returns 1 and fills `match` (may be
 * NULL) when the text matches, otherwise 0. The best occurrence is reported
 * (highest tier, then earliest). */
int axyne_palette_match(const char *text, const char *query,
                        AxynePaletteMatch *match);

/* ---- result list -------------------------------------------------------- */

typedef enum AxynePaletteItemKind {
    AXYNE_PALETTE_ITEM_FILE = 1,
    AXYNE_PALETTE_ITEM_COMMAND = 2,
    AXYNE_PALETTE_ITEM_SYMBOL = 3
} AxynePaletteItemKind;

typedef struct AxynePaletteItem {
    char *label;     /* owned; file: basename, command: Korean title, symbol: name */
    char *detail;    /* owned, may be ""; file: directory relative to the root,
                        command: shortcut text, symbol: "줄 N" */
    char *badge;     /* owned, may be ""; short text such as "C", "H", "f", "#" */
    uint32_t badge_color; /* 0xRRGGBB */
    AxynePaletteItemKind kind;
    size_t match_start;   /* highlight byte range inside `label` */
    size_t match_len;     /* 0 = no highlight */
    int payload;          /* file: index into the caller's path array;
                             command: AxynePaletteCommandId; symbol: 1-based line */
    int payload2;         /* symbol: 1-based byte column; otherwise 0 */
} AxynePaletteItem;

typedef struct AxynePaletteList {
    AxynePaletteItem *items;
    size_t count;
} AxynePaletteList;

/* Releases every item and resets the list to empty. Safe on a zeroed list
 * and on NULL. */
void axyne_palette_list_destroy(AxynePaletteList *list);

/* ---- commands ----------------------------------------------------------- */

/* Ids are stable: new commands are appended, existing values never change. */
typedef enum AxynePaletteCommandId {
    AXYNE_PALETTE_COMMAND_NONE = 0,
    AXYNE_PALETTE_COMMAND_NEW_FILE = 1,
    AXYNE_PALETTE_COMMAND_OPEN_FILE = 2,
    AXYNE_PALETTE_COMMAND_OPEN_FOLDER = 3,
    AXYNE_PALETTE_COMMAND_SAVE = 4,
    AXYNE_PALETTE_COMMAND_SAVE_AS = 5,
    AXYNE_PALETTE_COMMAND_CLOSE_TAB = 6,
    AXYNE_PALETTE_COMMAND_FIND = 7,
    AXYNE_PALETTE_COMMAND_REPLACE = 8,
    AXYNE_PALETTE_COMMAND_QUICK_FILE = 9,    /* go to file (palette file mode) */
    AXYNE_PALETTE_COMMAND_GO_TO_LINE = 10,   /* palette line mode */
    AXYNE_PALETTE_COMMAND_GO_TO_SYMBOL = 11, /* palette symbol mode */
    AXYNE_PALETTE_COMMAND_BUILD = 12,
    AXYNE_PALETTE_COMMAND_RUN = 13,
    AXYNE_PALETTE_COMMAND_START_DEBUGGING = 14,
    AXYNE_PALETTE_COMMAND_GIT_STATUS = 15,
    AXYNE_PALETTE_COMMAND_GIT_DIFF = 16,
    AXYNE_PALETTE_COMMAND_GIT_STAGE_ALL = 17,
    AXYNE_PALETTE_COMMAND_GIT_UNSTAGE_ALL = 18,
    AXYNE_PALETTE_COMMAND_PREFERENCES = 19,
    AXYNE_PALETTE_COMMAND_WORKSPACE_SETTINGS = 20,
    AXYNE_PALETTE_COMMAND_PANEL_OUTPUT = 21,
    AXYNE_PALETTE_COMMAND_PANEL_PROBLEMS = 22,
    AXYNE_PALETTE_COMMAND_PANEL_TERMINAL = 23,
    AXYNE_PALETTE_COMMAND_CLEAR_OUTPUT = 24,
    AXYNE_PALETTE_COMMAND_LAST = AXYNE_PALETTE_COMMAND_CLEAR_OUTPUT
} AxynePaletteCommandId;

/* Modifier flags of a shortcut. PRIMARY is Ctrl on Windows and Command on
 * macOS, so one descriptor serves both platforms. */
enum {
    AXYNE_PALETTE_MOD_PRIMARY = 1,
    AXYNE_PALETTE_MOD_SHIFT = 2,
    AXYNE_PALETTE_MOD_ALT = 4 /* Alt on Windows, Option on macOS */
};

/* `key` is the printable key name: an upper-case letter, a digit, a symbol
 * such as "," or a function key such as "F5"; NULL means no shortcut. When
 * `mac_key` is non-NULL the macOS shortcut is (mac_flags, mac_key) instead
 * (the Windows menu and the macOS system menu do not use identical keys for
 * every action). */
typedef struct AxynePaletteShortcut {
    unsigned flags;
    const char *key;
    unsigned mac_flags;
    const char *mac_key;
} AxynePaletteShortcut;

typedef struct AxynePaletteCommand {
    AxynePaletteCommandId id;
    const char *title;    /* Korean display title (UTF-8) */
    const char *keywords; /* English search keywords, space separated */
    AxynePaletteShortcut shortcut;
} AxynePaletteCommand;

/* The static command table in display order. `count` may be NULL. */
const AxynePaletteCommand *axyne_palette_commands(size_t *count);

/* NULL for an unknown id. */
const AxynePaletteCommand *axyne_palette_command(AxynePaletteCommandId id);

/* Writes the shortcut as text into `buffer` (NUL-terminated, truncated to
 * `capacity`) and returns the full length it needs, excluding the NUL (like
 * snprintf). Windows style: "Ctrl+Shift+S". macOS style: "⌥⇧⌘S" (UTF-8).
 * An empty string is produced when the command has no shortcut. */
size_t axyne_palette_format_shortcut(const AxynePaletteShortcut *shortcut,
                                     int mac_style, char *buffer,
                                     size_t capacity);

/* ---- builders ------------------------------------------------------------
 * Each builder filters by `query` (already stripped of the mode prefix; NULL
 * or "" matches everything), ranks by score (ties keep the input order),
 * truncates to AXYNE_PALETTE_MAX_ITEMS and returns a newly allocated list in
 * `out`, which must be released with axyne_palette_list_destroy. On failure
 * `out` is left empty. A NULL `error` is allowed. */

/* File mode. `paths` are `path_count` NUL-terminated paths (either separator;
 * NULL entries are skipped). The label is the basename and the highlight lies
 * inside it; only the basename is matched. The detail is the directory part
 * of the path relative to `root` (separators preserved, "" for a file
 * directly in the root). When `root` is NULL/empty or the path is not below
 * it, the detail is the path's own directory. The path comparison against
 * `root` is case-insensitive and slash-agnostic on Windows, exact on
 * POSIX. The badge comes from axyne_ui_file_badge. payload = index in
 * `paths`. */
AxyneStatus axyne_palette_build_files(const char *const *paths,
                                      size_t path_count, const char *root,
                                      const char *query,
                                      AxynePaletteList *out,
                                      AxyneError *error);

/* Command mode over the static table. The title and the English keywords are
 * both searched; the highlight is only reported for a title match. The detail
 * is the shortcut text (macOS style when `mac_style` is non-zero). payload =
 * AxynePaletteCommandId. */
AxyneStatus axyne_palette_build_commands(const char *query, int mac_style,
                                         AxynePaletteList *out,
                                         AxyneError *error);

/* Symbol mode over a scanner result (axyne_symbols_scan). Label = symbol name
 * with the highlight. payload = line, payload2 = column. A NULL `symbols` is
 * treated as an empty list. */
AxyneStatus axyne_palette_build_symbols(const AxyneSymbolList *symbols,
                                        const char *query,
                                        AxynePaletteList *out,
                                        AxyneError *error);

/* ---- line mode ---------------------------------------------------------- */

/* Parses "N" or "N:C" (a single optional leading ':' is accepted, so both the
 * whole palette text ":12:3" and the stripped query "12:3" work; surrounding
 * blanks are ignored). N and C are 1-based decimal numbers; a missing or
 * empty column ("12", "12:") yields 1. Returns 1 on success and 0 when the
 * text is empty, contains non-digits or signs, or N is 0. Numbers saturate at
 * 2147483647 instead of overflowing; a column of 0 becomes 1. */
int axyne_palette_parse_line(const char *text, size_t *line, size_t *column);

/* Clamps a 1-based line into [1, max(line_count, 1)]. */
size_t axyne_palette_clamp_line(size_t line, size_t line_count);

/* Clamps a 1-based column into [1, line_length + 1] (just past the last
 * byte of the line is allowed). */
size_t axyne_palette_clamp_column(size_t column, size_t line_length);

#ifdef __cplusplus
}
#endif

#endif
