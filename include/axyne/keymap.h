#ifndef AXYNE_KEYMAP_H
#define AXYNE_KEYMAP_H

#include <stddef.h>
#include <stdint.h>

#include "axyne/commands.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Keyboard shortcut model shared by both UIs: key strokes, two-stroke
 * chords ("Ctrl+K Ctrl+O"), the effective key map (registry defaults plus
 * user overrides keyed by command string id), chord dispatch with a timeout,
 * conflict detection and menu display text. Pure C, no OS API; the native UI
 * translates its key events with axyne_key_from_windows_vk /
 * axyne_key_from_mac_character and feeds strokes to axyne_keymap_feed. */

/* ---- keys and strokes --------------------------------------------------- */

/* Key codes. Printable keys use their unshifted US-layout ASCII code:
 * 'A'-'Z' (upper case), '0'-'9', ' ' (Space) and ` - = [ ] \ ; ' , . /.
 * Other keys use the values below. 0 means "no key" (a modifier alone). */
enum {
    AXYNE_KEY_NONE = 0,
    AXYNE_KEY_F1 = 0x100, /* F1..F24 are consecutive: AXYNE_KEY_F1 + n - 1 */
    AXYNE_KEY_F24 = AXYNE_KEY_F1 + 23,
    AXYNE_KEY_UP = 0x120,
    AXYNE_KEY_DOWN,
    AXYNE_KEY_LEFT,
    AXYNE_KEY_RIGHT,
    AXYNE_KEY_HOME,
    AXYNE_KEY_END,
    AXYNE_KEY_PAGE_UP,
    AXYNE_KEY_PAGE_DOWN,
    AXYNE_KEY_INSERT,
    AXYNE_KEY_DELETE,
    AXYNE_KEY_BACKSPACE,
    AXYNE_KEY_TAB,
    AXYNE_KEY_ENTER,
    AXYNE_KEY_ESCAPE,
    AXYNE_KEY_BREAK
};

/* Modifier bits of a stroke. CMD is the macOS Command key (never produced on
 * Windows). ALTGR is not a modifier of a binding: the Windows UI sets it on a
 * stroke when the right Alt key (AltGr, reported as Ctrl+Alt) is down, and
 * axyne_keymap_feed then treats the stroke as text input (see
 * axyne_keymap_set_altgr_exclusion). */
enum {
    AXYNE_KEYMOD_SHIFT = 1u << 0,
    AXYNE_KEYMOD_CTRL = 1u << 1,
    AXYNE_KEYMOD_ALT = 1u << 2,
    AXYNE_KEYMOD_CMD = 1u << 3,
    AXYNE_KEYMOD_ALTGR = 1u << 4
};
#define AXYNE_KEYMOD_MASK (AXYNE_KEYMOD_SHIFT | AXYNE_KEYMOD_CTRL | \
                           AXYNE_KEYMOD_ALT | AXYNE_KEYMOD_CMD)

typedef struct AxyneKeyStroke {
    uint16_t key;  /* AXYNE_KEY_* or ASCII as above */
    uint16_t mods; /* AXYNE_KEYMOD_* */
} AxyneKeyStroke;

#define AXYNE_KEY_SEQUENCE_MAX 2

typedef struct AxyneKeySequence {
    AxyneKeyStroke strokes[AXYNE_KEY_SEQUENCE_MAX];
    size_t count; /* 1 (single stroke) or 2 (chord) */
} AxyneKeySequence;

/* Text styles.
 * CANONICAL: settings.json form, "Ctrl+Cmd+Alt+Shift+Key", arrows "Up".
 * WINDOWS:   menu text, as CANONICAL but arrows as "↑ ↓ ← →" (Figma).
 * MACOS:     menu text with ⌃⌥⇧⌘ symbols in Apple order ("⇧⌘S", "⌘K ⌘O"). */
typedef enum AxyneKeyFormat {
    AXYNE_KEY_FORMAT_CANONICAL = 0,
    AXYNE_KEY_FORMAT_WINDOWS,
    AXYNE_KEY_FORMAT_MACOS
} AxyneKeyFormat;

/* The menu style for a platform. */
AxyneKeyFormat axyne_key_format_for(AxynePlatform platform);

/* Parses one stroke such as "Ctrl+Shift+S", "F5", "Alt+Up", "Cmd+,".
 * Modifier names are case-insensitive: Ctrl/Control, Shift, Alt/Option/Opt,
 * Cmd/Command. Key names: a letter or digit, one of ` - = [ ] \ ; ' , . /,
 * F1-F24, Up, Down, Left, Right, Home, End, PageUp, PageDown, Insert,
 * Delete/Del, Backspace, Tab, Enter/Return, Escape/Esc, Space, Break/Pause,
 * and the arrow symbols ↑ ↓ ← →. Exactly one key is required; surrounding
 * blanks are ignored. INVALID_ARGUMENT on anything else. */
AxyneStatus axyne_key_stroke_parse(const char *text, AxyneKeyStroke *stroke);
/* Parses one stroke or a two-stroke chord separated by blanks. */
AxyneStatus axyne_key_sequence_parse(const char *text,
                                     AxyneKeySequence *sequence);
/* snprintf-like: writes at most capacity bytes including the NUL and returns
 * the full length the text needs (excluding the NUL). ALTGR is not shown. */
size_t axyne_key_stroke_format(const AxyneKeyStroke *stroke,
                               AxyneKeyFormat style, char *buffer,
                               size_t capacity);
size_t axyne_key_sequence_format(const AxyneKeySequence *sequence,
                                 AxyneKeyFormat style, char *buffer,
                                 size_t capacity);
/* Equality ignoring AXYNE_KEYMOD_ALTGR. */
int axyne_key_stroke_equal(AxyneKeyStroke a, AxyneKeyStroke b);
int axyne_key_sequence_equal(const AxyneKeySequence *a,
                             const AxyneKeySequence *b);

/* Translates a Windows virtual-key code (VK_*) to a key code; modifier keys
 * and unknown codes give AXYNE_KEY_NONE. Numpad digits map to digits and
 * VK_ADD / VK_SUBTRACT to '=' / '-' (zoom shortcuts accept both). OEM keys
 * assume the US layout positions. */
uint16_t axyne_key_from_windows_vk(unsigned int virtual_key);
/* Translates the first UTF-16 unit of NSEvent charactersIgnoringModifiers
 * (which still applies Shift) to a key code: lower-case letters fold to
 * upper case, shifted US punctuation folds to its base key ('?' -> '/'),
 * NS*FunctionKey values map to F-keys, arrows and navigation keys. */
uint16_t axyne_key_from_mac_character(unsigned int character);

/* ---- key map ------------------------------------------------------------ */

#define AXYNE_KEYMAP_CHORD_TIMEOUT_MS 1500u

/* Context bits for axyne_keymap_feed / axyne_keymap_lookup. */
enum { AXYNE_KEYMAP_CONTEXT_DEBUGGING = 1u << 0 };

typedef enum AxyneKeymapResult {
    AXYNE_KEYMAP_NONE = 0,  /* not a shortcut: let the focused control have it */
    AXYNE_KEYMAP_PENDING,   /* first stroke of a chord: consume it, show the hint */
    AXYNE_KEYMAP_COMMAND,   /* run *command; consume the key */
    AXYNE_KEYMAP_ABORTED    /* second stroke matched no chord: consume it and
                               clear the hint */
} AxyneKeymapResult;

typedef struct AxyneKeymapEntry {
    AxyneKeySequence sequence;
    AxyneCommandId command;
    int user; /* 1 when it came from an override, 0 for a registry default */
} AxyneKeymapEntry;

/* Treat the members as private; use the functions below. */
typedef struct AxyneKeymap {
    AxyneKeymapEntry *entries;
    size_t count;
    size_t capacity;
    AxynePlatform platform;
    int altgr_exclusion;
    int has_pending;
    AxyneKeyStroke pending;
    uint64_t pending_since_ms;
} AxyneKeymap;

/* Builds the default map of `platform` from the registry (commands that do
 * not exist on the platform are skipped). AltGr exclusion starts enabled on
 * Windows and disabled on macOS. Release with axyne_keymap_destroy. */
AxyneStatus axyne_keymap_init(AxyneKeymap *map, AxynePlatform platform);
/* Frees the entries; the map can be initialised again. Safe on a zeroed map. */
void axyne_keymap_destroy(AxyneKeymap *map);

/* Replaces every binding of `command` with `sequences` (count 0 unbinds the
 * command). Entries are marked as user bindings. INVALID_ARGUMENT for an
 * unknown command or a command unavailable on the map's platform. */
AxyneStatus axyne_keymap_set_command(AxyneKeymap *map, AxyneCommandId command,
                                     const AxyneKeySequence *sequences,
                                     size_t count);
/* Same with the command string id and key text (each element one sequence,
 * "" or NULL elements are skipped). NOT_FOUND for an unknown command id
 * (ignored ids from newer versions are the caller's choice);
 * INVALID_ARGUMENT for unparsable text, in which case nothing changes. */
AxyneStatus axyne_keymap_set_command_text(AxyneKeymap *map,
                                          const char *command_name,
                                          const char *const *keys,
                                          size_t count);

/* When enabled (default on Windows) a stroke carrying AXYNE_KEYMOD_ALTGR is
 * never a shortcut: feed returns NONE and drops a pending chord. */
void axyne_keymap_set_altgr_exclusion(AxyneKeymap *map, int enabled);

/* Dispatches one key-down stroke at monotonic time now_ms. A stroke without
 * a key (modifier alone) returns NONE and keeps a pending chord. A pending
 * chord older than AXYNE_KEYMAP_CHORD_TIMEOUT_MS is dropped before the stroke
 * is matched as a fresh one. Bindings of DEBUG_CONTEXT commands only match
 * with AXYNE_KEYMAP_CONTEXT_DEBUGGING and then win over other bindings of the
 * same keys. NATIVE_KEY commands are never reported. `command` may be NULL. */
AxyneKeymapResult axyne_keymap_feed(AxyneKeymap *map, AxyneKeyStroke stroke,
                                    uint64_t now_ms, unsigned int context,
                                    AxyneCommandId *command);
/* Non-zero while a chord waits for its second stroke at now_ms (expired
 * chords report 0); `first` (may be NULL) receives the first stroke. */
int axyne_keymap_pending(const AxyneKeymap *map, uint64_t now_ms,
                         AxyneKeyStroke *first);
/* Drops a pending chord (focus change, Escape handled elsewhere, ...). */
void axyne_keymap_cancel_pending(AxyneKeymap *map);
/* Status-bar hint while a chord is pending, e.g.
 * "(Ctrl+K) 두 번째 키를 기다리는 중…"; "" when nothing is pending.
 * snprintf-like return value. */
size_t axyne_keymap_chord_hint(const AxyneKeymap *map, uint64_t now_ms,
                               char *buffer, size_t capacity);

/* The command bound to `sequence` in `context` (same priority rules as feed
 * but including NATIVE_KEY commands); AXYNE_COMMAND_NONE when unbound. */
AxyneCommandId axyne_keymap_lookup(const AxyneKeymap *map,
                                   const AxyneKeySequence *sequence,
                                   unsigned int context);
/* Copies up to `capacity` bindings of `command` (primary first) and returns
 * how many it has. */
size_t axyne_keymap_bindings(const AxyneKeymap *map, AxyneCommandId command,
                             AxyneKeySequence *out, size_t capacity);
/* Menu text of the first binding in the platform style ("" when unbound).
 * snprintf-like return value. */
size_t axyne_keymap_display(const AxyneKeymap *map, AxyneCommandId command,
                            char *buffer, size_t capacity);

/* ---- conflicts ---------------------------------------------------------- */

typedef struct AxyneKeymapConflict {
    AxyneKeySequence sequence; /* the colliding keys (the shorter one for a
                                  prefix conflict) */
    AxyneCommandId first;
    AxyneCommandId second;
    int prefix; /* 1: a single stroke equals the first stroke of a chord */
} AxyneKeymapConflict;

/* Collects conflicts between bindings of different commands in the same
 * context class (both debug-context or both not): identical sequences, or a
 * single stroke that is the first stroke of another command's chord (the
 * chord could never be typed). Writes up to `capacity` and returns the total. */
size_t axyne_keymap_conflicts(const AxyneKeymap *map,
                              AxyneKeymapConflict *out, size_t capacity);
/* For binding editors: the first command other than `exclude` (whose context
 * class equals that of `exclude`, or plain when exclude is NONE) that
 * `sequence` would conflict with, or AXYNE_COMMAND_NONE. */
AxyneCommandId axyne_keymap_find_conflict(const AxyneKeymap *map,
                                          const AxyneKeySequence *sequence,
                                          AxyneCommandId exclude);

#ifdef __cplusplus
}
#endif

#endif
