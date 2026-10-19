#ifndef AXYNE_TERMINAL_H
#define AXYNE_TERMINAL_H

#include <stddef.h>
#include <stdint.h>
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneTerminal AxyneTerminal;
typedef void (*AxyneTerminalChangedFn)(AxyneTerminal *, void *);
typedef void (*AxyneTerminalExitFn)(AxyneTerminal *, int, void *);

typedef struct AxyneTerminalSpec {
    const char *executable;
    const char *const *arguments;
    size_t argument_count;
    const char *working_directory;
    AxyneTerminalChangedFn on_changed;
    AxyneTerminalExitFn on_exit;
    void *user_data;
} AxyneTerminalSpec;

typedef enum AxyneTerminalKey {
    AXYNE_TERMINAL_ENTER = 1, AXYNE_TERMINAL_TAB,
    AXYNE_TERMINAL_BACKSPACE, AXYNE_TERMINAL_ESCAPE,
    AXYNE_TERMINAL_UP, AXYNE_TERMINAL_DOWN,
    AXYNE_TERMINAL_LEFT, AXYNE_TERMINAL_RIGHT,
    AXYNE_TERMINAL_INSERT, AXYNE_TERMINAL_DELETE,
    AXYNE_TERMINAL_HOME, AXYNE_TERMINAL_END,
    AXYNE_TERMINAL_PAGEUP, AXYNE_TERMINAL_PAGEDOWN,
    AXYNE_TERMINAL_F1 = 257
} AxyneTerminalKey;
enum { AXYNE_TERMINAL_SHIFT = 1, AXYNE_TERMINAL_ALT = 2,
       AXYNE_TERMINAL_CTRL = 4 };
enum { AXYNE_TERMINAL_BOLD = 1, AXYNE_TERMINAL_ITALIC = 2,
       AXYNE_TERMINAL_UNDERLINE = 4, AXYNE_TERMINAL_REVERSE = 8 };

typedef struct AxyneTerminalCell {
    uint32_t chars[6];
    uint32_t foreground, background; /* RGB, default palette already resolved */
    unsigned attributes;
    int width; /* 0: wide-character continuation; 1 or 2: leading cell */
} AxyneTerminalCell;

typedef struct AxyneTerminalSnapshot {
    int rows, columns;
    int cursor_row, cursor_column, cursor_visible;
    int alternate_screen;
    int scroll_offset;
    size_t history_lines;
    AxyneTerminalCell *cells;
} AxyneTerminalSnapshot;

/* The model supports deterministic VT tests without starting a child.
 * Feed/snapshot/key/resize are internally synchronized. Callbacks notify only;
 * native adapters marshal repaint/exit to their UI thread. Session output
 * changes and exit callbacks run serially on the session reader thread, with
 * no model locks held. Exit is delivered once, after output is drained; no
 * further session notifications follow it. Callbacks may begin before start
 * returns, so create/initialize user_data before calling start. They may query
 * snapshots or enqueue input but must not start or release a session. Manual
 * model operations (feed without a child, resize, scroll, clear) do not deliver
 * notifications: callers schedule their own repaint. Inputs are copied by
 * start. user_data stays valid until release finishes. Release terminates the
 * session and joins workers; no callbacks can arrive after it returns. */
AxyneStatus axyne_terminal_create(int rows, int columns, size_t history_limit,
                                  AxyneTerminal **out, AxyneError *error);
AxyneStatus axyne_terminal_start(AxyneTerminal *, const AxyneTerminalSpec *, AxyneError *);
AxyneStatus axyne_terminal_write(AxyneTerminal *, const char *, size_t, AxyneError *);
AxyneStatus axyne_terminal_key(AxyneTerminal *, AxyneTerminalKey, unsigned, AxyneError *);
AxyneStatus axyne_terminal_unichar(AxyneTerminal *, uint32_t, unsigned, AxyneError *);
AxyneStatus axyne_terminal_paste(AxyneTerminal *, const char *, size_t, AxyneError *);
AxyneStatus axyne_terminal_resize(AxyneTerminal *, int rows, int columns, AxyneError *);
void axyne_terminal_feed(AxyneTerminal *, const char *, size_t);
void axyne_terminal_scroll(AxyneTerminal *, int lines);
void axyne_terminal_clear(AxyneTerminal *);
void axyne_terminal_set_colors(AxyneTerminal *, uint32_t foreground, uint32_t background);
int axyne_terminal_running(AxyneTerminal *);
AxyneStatus axyne_terminal_snapshot(AxyneTerminal *, AxyneTerminalSnapshot *, AxyneError *);
void axyne_terminal_snapshot_free(AxyneTerminalSnapshot *);
AxyneStatus axyne_terminal_terminate(AxyneTerminal *, AxyneError *);
void axyne_terminal_release(AxyneTerminal *);

#ifdef __cplusplus
}
#endif
#endif
