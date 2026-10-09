#include "axyne/keymap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- key names ---------------------------------------------------------- */

typedef struct AxyneKeyName {
    uint16_t key;
    const char *canonical;
    const char *windows;
    const char *macos;
} AxyneKeyName;

static const AxyneKeyName key_names[] = {
    { AXYNE_KEY_UP, "Up", "\xe2\x86\x91", "\xe2\x86\x91" },
    { AXYNE_KEY_DOWN, "Down", "\xe2\x86\x93", "\xe2\x86\x93" },
    { AXYNE_KEY_LEFT, "Left", "\xe2\x86\x90", "\xe2\x86\x90" },
    { AXYNE_KEY_RIGHT, "Right", "\xe2\x86\x92", "\xe2\x86\x92" },
    { AXYNE_KEY_HOME, "Home", "Home", "\xe2\x86\x96" },
    { AXYNE_KEY_END, "End", "End", "\xe2\x86\x98" },
    { AXYNE_KEY_PAGE_UP, "PageUp", "PageUp", "\xe2\x87\x9e" },
    { AXYNE_KEY_PAGE_DOWN, "PageDown", "PageDown", "\xe2\x87\x9f" },
    { AXYNE_KEY_INSERT, "Insert", "Insert", "Insert" },
    { AXYNE_KEY_DELETE, "Delete", "Delete", "\xe2\x8c\xa6" },
    { AXYNE_KEY_BACKSPACE, "Backspace", "Backspace", "\xe2\x8c\xab" },
    { AXYNE_KEY_TAB, "Tab", "Tab", "\xe2\x87\xa5" },
    { AXYNE_KEY_ENTER, "Enter", "Enter", "\xe2\x86\xa9" },
    { AXYNE_KEY_ESCAPE, "Escape", "Esc", "\xe2\x8e\x8b" },
    { AXYNE_KEY_BREAK, "Break", "Break", "Break" },
    { ' ', "Space", "Space", "Space" }
};

/* Extra spellings accepted by the parser. */
static const struct { const char *name; uint16_t key; } key_aliases[] = {
    { "Del", AXYNE_KEY_DELETE }, { "Return", AXYNE_KEY_ENTER },
    { "Esc", AXYNE_KEY_ESCAPE }, { "Pause", AXYNE_KEY_BREAK },
    { "PgUp", AXYNE_KEY_PAGE_UP }, { "PgDn", AXYNE_KEY_PAGE_DOWN },
    { "\xe2\x86\x91", AXYNE_KEY_UP }, { "\xe2\x86\x93", AXYNE_KEY_DOWN },
    { "\xe2\x86\x90", AXYNE_KEY_LEFT }, { "\xe2\x86\x92", AXYNE_KEY_RIGHT }
};

static const char punctuation[] = "`-=[]\\;',./";

static int token_equals(const char *token, size_t length, const char *literal)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        char a = token[i], b = literal[i];
        if (b == '\0') return 0;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
    }
    return literal[length] == '\0';
}

static uint16_t parse_key(const char *token, size_t length)
{
    size_t i;
    if (length == 1) {
        char c = token[0];
        if (c >= 'a' && c <= 'z') return (uint16_t)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (uint16_t)c;
        if (strchr(punctuation, c) != NULL) return (uint16_t)(unsigned char)c;
        return AXYNE_KEY_NONE;
    }
    if ((token[0] == 'F' || token[0] == 'f') && length <= 3) {
        unsigned value = 0;
        for (i = 1; i < length; ++i) {
            if (token[i] < '0' || token[i] > '9') return AXYNE_KEY_NONE;
            value = value * 10u + (unsigned)(token[i] - '0');
        }
        if (token[1] != '0' && value >= 1 && value <= 24)
            return (uint16_t)(AXYNE_KEY_F1 + value - 1);
        return AXYNE_KEY_NONE;
    }
    for (i = 0; i < sizeof(key_names) / sizeof(key_names[0]); ++i)
        if (token_equals(token, length, key_names[i].canonical)) return key_names[i].key;
    for (i = 0; i < sizeof(key_aliases) / sizeof(key_aliases[0]); ++i)
        if (token_equals(token, length, key_aliases[i].name)) return key_aliases[i].key;
    return AXYNE_KEY_NONE;
}

static unsigned parse_modifier(const char *token, size_t length)
{
    if (token_equals(token, length, "Ctrl") || token_equals(token, length, "Control"))
        return AXYNE_KEYMOD_CTRL;
    if (token_equals(token, length, "Shift")) return AXYNE_KEYMOD_SHIFT;
    if (token_equals(token, length, "Alt") || token_equals(token, length, "Option") ||
        token_equals(token, length, "Opt"))
        return AXYNE_KEYMOD_ALT;
    if (token_equals(token, length, "Cmd") || token_equals(token, length, "Command"))
        return AXYNE_KEYMOD_CMD;
    return 0;
}

static int is_blank(char c) { return c == ' ' || c == '\t'; }

static AxyneStatus parse_stroke_range(const char *text, size_t length,
                                      AxyneKeyStroke *stroke)
{
    size_t start = 0;
    unsigned mods = 0;
    uint16_t key = AXYNE_KEY_NONE;
    while (start < length && is_blank(text[start])) ++start;
    while (length > start && is_blank(text[length - 1])) --length;
    if (start >= length) return AXYNE_STATUS_INVALID_ARGUMENT;
    while (start < length) {
        size_t end = start;
        unsigned modifier;
        while (end < length && text[end] != '+') ++end;
        if (end == start) return AXYNE_STATUS_INVALID_ARGUMENT; /* empty token */
        modifier = end < length ? parse_modifier(text + start, end - start) : 0;
        if (end < length) {
            /* Every token before the last '+' must be a distinct modifier. */
            if (modifier == 0 || (mods & modifier) != 0)
                return AXYNE_STATUS_INVALID_ARGUMENT;
            mods |= modifier;
            start = end + 1;
            if (start >= length) return AXYNE_STATUS_INVALID_ARGUMENT;
        } else {
            key = parse_key(text + start, end - start);
            if (key == AXYNE_KEY_NONE) return AXYNE_STATUS_INVALID_ARGUMENT;
            start = end;
        }
    }
    if (stroke != NULL) {
        stroke->key = key;
        stroke->mods = (uint16_t)mods;
    }
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_key_stroke_parse(const char *text, AxyneKeyStroke *stroke)
{
    if (text == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    return parse_stroke_range(text, strlen(text), stroke);
}

AxyneStatus axyne_key_sequence_parse(const char *text, AxyneKeySequence *sequence)
{
    AxyneKeySequence parsed;
    size_t length, position = 0;
    if (text == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(&parsed, 0, sizeof(parsed));
    length = strlen(text);
    while (position < length) {
        size_t start, end;
        AxyneStatus status;
        while (position < length && is_blank(text[position])) ++position;
        if (position >= length) break;
        start = position;
        while (position < length && !is_blank(text[position])) ++position;
        end = position;
        if (parsed.count >= AXYNE_KEY_SEQUENCE_MAX) return AXYNE_STATUS_INVALID_ARGUMENT;
        status = parse_stroke_range(text + start, end - start,
                                    &parsed.strokes[parsed.count]);
        if (status != AXYNE_STATUS_OK) return status;
        ++parsed.count;
    }
    if (parsed.count == 0) return AXYNE_STATUS_INVALID_ARGUMENT;
    if (sequence != NULL) *sequence = parsed;
    return AXYNE_STATUS_OK;
}

AxyneKeyFormat axyne_key_format_for(AxynePlatform platform)
{
    return platform == AXYNE_PLATFORM_MACOS ? AXYNE_KEY_FORMAT_MACOS
                                            : AXYNE_KEY_FORMAT_WINDOWS;
}

typedef struct AxyneTextOut {
    char *buffer;
    size_t capacity;
    size_t length;
} AxyneTextOut;

static void out_append(AxyneTextOut *out, const char *text)
{
    size_t add = strlen(text);
    if (out->capacity != 0 && out->length < out->capacity - 1) {
        size_t room = out->capacity - 1 - out->length;
        size_t copy = add < room ? add : room;
        memcpy(out->buffer + out->length, text, copy);
        out->buffer[out->length + copy] = '\0';
    }
    out->length += add;
}

static void format_key(uint16_t key, AxyneKeyFormat style, AxyneTextOut *out)
{
    char single[8];
    size_t i;
    if (key >= AXYNE_KEY_F1 && key <= AXYNE_KEY_F24) {
        (void)snprintf(single, sizeof(single), "F%u", (unsigned)(key - AXYNE_KEY_F1 + 1));
        out_append(out, single);
        return;
    }
    for (i = 0; i < sizeof(key_names) / sizeof(key_names[0]); ++i) {
        if (key_names[i].key != key) continue;
        out_append(out, style == AXYNE_KEY_FORMAT_MACOS ? key_names[i].macos
                        : style == AXYNE_KEY_FORMAT_WINDOWS ? key_names[i].windows
                        : key_names[i].canonical);
        return;
    }
    if (key > ' ' && key < 0x7f) {
        single[0] = (char)key;
        single[1] = '\0';
        out_append(out, single);
        return;
    }
    out_append(out, "?");
}

static void format_stroke(const AxyneKeyStroke *stroke, AxyneKeyFormat style,
                          AxyneTextOut *out)
{
    unsigned mods = stroke->mods;
    if (style == AXYNE_KEY_FORMAT_MACOS) {
        if (mods & AXYNE_KEYMOD_CTRL) out_append(out, "\xe2\x8c\x83");
        if (mods & AXYNE_KEYMOD_ALT) out_append(out, "\xe2\x8c\xa5");
        if (mods & AXYNE_KEYMOD_SHIFT) out_append(out, "\xe2\x87\xa7");
        if (mods & AXYNE_KEYMOD_CMD) out_append(out, "\xe2\x8c\x98");
    } else {
        if (mods & AXYNE_KEYMOD_CTRL) out_append(out, "Ctrl+");
        if (mods & AXYNE_KEYMOD_CMD) out_append(out, "Cmd+");
        if (mods & AXYNE_KEYMOD_ALT) out_append(out, "Alt+");
        if (mods & AXYNE_KEYMOD_SHIFT) out_append(out, "Shift+");
    }
    format_key(stroke->key, style, out);
}

size_t axyne_key_stroke_format(const AxyneKeyStroke *stroke, AxyneKeyFormat style,
                               char *buffer, size_t capacity)
{
    AxyneTextOut out = { buffer, buffer != NULL ? capacity : 0, 0 };
    if (out.capacity != 0) buffer[0] = '\0';
    if (stroke != NULL) format_stroke(stroke, style, &out);
    return out.length;
}

size_t axyne_key_sequence_format(const AxyneKeySequence *sequence,
                                 AxyneKeyFormat style, char *buffer,
                                 size_t capacity)
{
    AxyneTextOut out = { buffer, buffer != NULL ? capacity : 0, 0 };
    if (out.capacity != 0) buffer[0] = '\0';
    if (sequence == NULL) return 0;
    for (size_t i = 0; i < sequence->count && i < AXYNE_KEY_SEQUENCE_MAX; ++i) {
        if (i != 0) out_append(&out, " ");
        format_stroke(&sequence->strokes[i], style, &out);
    }
    return out.length;
}

int axyne_key_stroke_equal(AxyneKeyStroke a, AxyneKeyStroke b)
{
    return a.key == b.key &&
           (a.mods & AXYNE_KEYMOD_MASK) == (b.mods & AXYNE_KEYMOD_MASK);
}

int axyne_key_sequence_equal(const AxyneKeySequence *a, const AxyneKeySequence *b)
{
    if (a == NULL || b == NULL || a->count != b->count) return 0;
    for (size_t i = 0; i < a->count && i < AXYNE_KEY_SEQUENCE_MAX; ++i)
        if (!axyne_key_stroke_equal(a->strokes[i], b->strokes[i])) return 0;
    return 1;
}

uint16_t axyne_key_from_windows_vk(unsigned int vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return (uint16_t)vk;
    if (vk >= 0x60 && vk <= 0x69) return (uint16_t)('0' + (vk - 0x60)); /* numpad */
    if (vk >= 0x70 && vk <= 0x87) return (uint16_t)(AXYNE_KEY_F1 + (vk - 0x70));
    switch (vk) {
    case 0x03: case 0x13: return AXYNE_KEY_BREAK; /* VK_CANCEL, VK_PAUSE */
    case 0x08: return AXYNE_KEY_BACKSPACE;
    case 0x09: return AXYNE_KEY_TAB;
    case 0x0D: return AXYNE_KEY_ENTER;
    case 0x1B: return AXYNE_KEY_ESCAPE;
    case 0x20: return ' ';
    case 0x21: return AXYNE_KEY_PAGE_UP;
    case 0x22: return AXYNE_KEY_PAGE_DOWN;
    case 0x23: return AXYNE_KEY_END;
    case 0x24: return AXYNE_KEY_HOME;
    case 0x25: return AXYNE_KEY_LEFT;
    case 0x26: return AXYNE_KEY_UP;
    case 0x27: return AXYNE_KEY_RIGHT;
    case 0x28: return AXYNE_KEY_DOWN;
    case 0x2D: return AXYNE_KEY_INSERT;
    case 0x2E: return AXYNE_KEY_DELETE;
    case 0x6B: return '=';  /* VK_ADD */
    case 0x6D: return '-';  /* VK_SUBTRACT */
    case 0xBA: return ';';
    case 0xBB: return '=';
    case 0xBC: return ',';
    case 0xBD: return '-';
    case 0xBE: return '.';
    case 0xBF: return '/';
    case 0xC0: return '`';
    case 0xDB: return '[';
    case 0xDC: return '\\';
    case 0xDD: return ']';
    case 0xDE: return '\'';
    default: return AXYNE_KEY_NONE;
    }
}

uint16_t axyne_key_from_mac_character(unsigned int c)
{
    static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
    static const char base[] = "`1234567890-=[]\\;',./";
    if (c >= 'a' && c <= 'z') return (uint16_t)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (uint16_t)c;
    if (c >= 0xF704 && c <= 0xF71B) return (uint16_t)(AXYNE_KEY_F1 + (c - 0xF704));
    switch (c) {
    case 0xF700: return AXYNE_KEY_UP;
    case 0xF701: return AXYNE_KEY_DOWN;
    case 0xF702: return AXYNE_KEY_LEFT;
    case 0xF703: return AXYNE_KEY_RIGHT;
    case 0xF727: return AXYNE_KEY_INSERT;
    case 0xF728: return AXYNE_KEY_DELETE;    /* forward delete */
    case 0xF729: return AXYNE_KEY_HOME;
    case 0xF72B: return AXYNE_KEY_END;
    case 0xF72C: return AXYNE_KEY_PAGE_UP;
    case 0xF72D: return AXYNE_KEY_PAGE_DOWN;
    case 0xF730: case 0xF732: return AXYNE_KEY_BREAK; /* pause, break */
    case 0x7F: case 0x08: return AXYNE_KEY_BACKSPACE;
    case 0x09: case 0x19: return AXYNE_KEY_TAB;      /* 0x19: Shift+Tab */
    case 0x0D: case 0x03: return AXYNE_KEY_ENTER;    /* return, keypad enter */
    case 0x1B: return AXYNE_KEY_ESCAPE;
    case 0x20: return ' ';
    default: break;
    }
    if (c > 0 && c < 0x80) {
        const char *found = strchr(shifted, (int)c);
        if (found != NULL) return (uint16_t)(unsigned char)base[found - shifted];
        if (strchr(punctuation, (int)c) != NULL) return (uint16_t)c;
    }
    return AXYNE_KEY_NONE;
}

/* ---- key map ------------------------------------------------------------ */

static int command_is_debug(AxyneCommandId command)
{
    const AxyneCommandInfo *info = axyne_command_info(command);
    return info != NULL && (info->flags & AXYNE_COMMAND_FLAG_DEBUG_CONTEXT) != 0;
}

static int command_is_native(AxyneCommandId command)
{
    const AxyneCommandInfo *info = axyne_command_info(command);
    return info != NULL && (info->flags & AXYNE_COMMAND_FLAG_NATIVE_KEY) != 0;
}

static int sequence_valid(const AxyneKeySequence *sequence)
{
    if (sequence == NULL || sequence->count < 1 ||
        sequence->count > AXYNE_KEY_SEQUENCE_MAX) return 0;
    for (size_t i = 0; i < sequence->count; ++i)
        if (sequence->strokes[i].key == AXYNE_KEY_NONE) return 0;
    return 1;
}

static AxyneStatus keymap_append(AxyneKeymap *map, const AxyneKeySequence *sequence,
                                 AxyneCommandId command, int user)
{
    AxyneKeymapEntry *entry;
    if (map->count == map->capacity) {
        size_t next = map->capacity == 0 ? 64 : map->capacity * 2;
        AxyneKeymapEntry *grown = (AxyneKeymapEntry *)realloc(map->entries,
                                                              next * sizeof(*grown));
        if (grown == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        map->entries = grown;
        map->capacity = next;
    }
    entry = &map->entries[map->count++];
    memset(entry, 0, sizeof(*entry));
    entry->sequence = *sequence;
    for (size_t i = 0; i < entry->sequence.count; ++i)
        entry->sequence.strokes[i].mods &= AXYNE_KEYMOD_MASK;
    entry->command = command;
    entry->user = user;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_keymap_init(AxyneKeymap *map, AxynePlatform platform)
{
    size_t count = axyne_command_count();
    if (map == NULL || (platform != AXYNE_PLATFORM_WINDOWS &&
                        platform != AXYNE_PLATFORM_MACOS))
        return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(map, 0, sizeof(*map));
    map->platform = platform;
    map->altgr_exclusion = platform == AXYNE_PLATFORM_WINDOWS;
    for (size_t i = 0; i < count; ++i) {
        const AxyneCommandInfo *info = axyne_command_at(i);
        for (size_t k = 0; k < AXYNE_COMMAND_DEFAULT_KEYS; ++k) {
            const char *text = axyne_command_default_keys(info->id, platform, k);
            AxyneKeySequence sequence;
            AxyneStatus status;
            if (text == NULL) continue;
            status = axyne_key_sequence_parse(text, &sequence);
            if (status == AXYNE_STATUS_OK)
                status = keymap_append(map, &sequence, info->id, 0);
            if (status != AXYNE_STATUS_OK) {
                axyne_keymap_destroy(map);
                return status;
            }
        }
    }
    return AXYNE_STATUS_OK;
}

void axyne_keymap_destroy(AxyneKeymap *map)
{
    if (map == NULL) return;
    free(map->entries);
    memset(map, 0, sizeof(*map));
}

AxyneStatus axyne_keymap_set_command(AxyneKeymap *map, AxyneCommandId command,
                                     const AxyneKeySequence *sequences, size_t count)
{
    size_t kept = 0;
    if (map == NULL || !axyne_command_available(command, map->platform) ||
        (count != 0 && sequences == NULL))
        return AXYNE_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (!sequence_valid(&sequences[i])) return AXYNE_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0; i < map->count; ++i)
        if (map->entries[i].command != command) map->entries[kept++] = map->entries[i];
    map->count = kept;
    for (size_t i = 0; i < count; ++i) {
        int duplicate = 0;
        AxyneStatus status;
        for (size_t j = 0; j < i; ++j)
            if (axyne_key_sequence_equal(&sequences[i], &sequences[j])) duplicate = 1;
        if (duplicate) continue;
        status = keymap_append(map, &sequences[i], command, 1);
        if (status != AXYNE_STATUS_OK) return status;
    }
    map->has_pending = 0;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_keymap_set_command_text(AxyneKeymap *map, const char *command_name,
                                          const char *const *keys, size_t count)
{
    AxyneKeySequence parsed[AXYNE_COMMAND_DEFAULT_KEYS * 4];
    size_t parsed_count = 0;
    AxyneCommandId command;
    if (map == NULL || command_name == NULL || (count != 0 && keys == NULL))
        return AXYNE_STATUS_INVALID_ARGUMENT;
    command = axyne_command_id(command_name);
    if (command == AXYNE_COMMAND_NONE) return AXYNE_STATUS_NOT_FOUND;
    for (size_t i = 0; i < count; ++i) {
        AxyneStatus status;
        if (keys[i] == NULL || keys[i][0] == '\0') continue;
        if (parsed_count >= sizeof(parsed) / sizeof(parsed[0]))
            return AXYNE_STATUS_INVALID_ARGUMENT;
        status = axyne_key_sequence_parse(keys[i], &parsed[parsed_count]);
        if (status != AXYNE_STATUS_OK) return status;
        ++parsed_count;
    }
    return axyne_keymap_set_command(map, command, parsed, parsed_count);
}

void axyne_keymap_set_altgr_exclusion(AxyneKeymap *map, int enabled)
{
    if (map != NULL) map->altgr_exclusion = enabled != 0;
}

/* Best entry for `sequence`: a debug-context binding wins while debugging;
 * debug-context bindings never match outside a debug session. Returns NULL
 * when nothing matches. */
static const AxyneKeymapEntry *keymap_match(const AxyneKeymap *map,
                                            const AxyneKeySequence *sequence,
                                            unsigned int context)
{
    const AxyneKeymapEntry *plain = NULL;
    int debugging = (context & AXYNE_KEYMAP_CONTEXT_DEBUGGING) != 0;
    for (size_t i = 0; i < map->count; ++i) {
        const AxyneKeymapEntry *entry = &map->entries[i];
        if (!axyne_key_sequence_equal(&entry->sequence, sequence)) continue;
        if (command_is_debug(entry->command)) {
            if (debugging) return entry;
        } else if (plain == NULL) {
            plain = entry;
        }
    }
    return plain;
}

static int keymap_is_prefix(const AxyneKeymap *map, AxyneKeyStroke stroke,
                            unsigned int context)
{
    int debugging = (context & AXYNE_KEYMAP_CONTEXT_DEBUGGING) != 0;
    for (size_t i = 0; i < map->count; ++i) {
        const AxyneKeymapEntry *entry = &map->entries[i];
        if (entry->sequence.count < 2) continue;
        if (command_is_debug(entry->command) && !debugging) continue;
        if (command_is_native(entry->command)) continue;
        if (axyne_key_stroke_equal(entry->sequence.strokes[0], stroke)) return 1;
    }
    return 0;
}

static int pending_alive(const AxyneKeymap *map, uint64_t now_ms)
{
    return map->has_pending && now_ms >= map->pending_since_ms &&
           now_ms - map->pending_since_ms <= AXYNE_KEYMAP_CHORD_TIMEOUT_MS;
}

AxyneKeymapResult axyne_keymap_feed(AxyneKeymap *map, AxyneKeyStroke stroke,
                                    uint64_t now_ms, unsigned int context,
                                    AxyneCommandId *command)
{
    AxyneKeySequence sequence;
    const AxyneKeymapEntry *entry;
    if (command != NULL) *command = AXYNE_COMMAND_NONE;
    if (map == NULL || stroke.key == AXYNE_KEY_NONE) return AXYNE_KEYMAP_NONE;
    if (map->altgr_exclusion && (stroke.mods & AXYNE_KEYMOD_ALTGR) != 0) {
        map->has_pending = 0;
        return AXYNE_KEYMAP_NONE;
    }
    stroke.mods &= AXYNE_KEYMOD_MASK;
    memset(&sequence, 0, sizeof(sequence));
    if (map->has_pending) {
        int alive = pending_alive(map, now_ms);
        map->has_pending = 0;
        if (alive) {
            sequence.strokes[0] = map->pending;
            sequence.strokes[1] = stroke;
            sequence.count = 2;
            entry = keymap_match(map, &sequence, context);
            if (entry == NULL || command_is_native(entry->command))
                return AXYNE_KEYMAP_ABORTED;
            if (command != NULL) *command = entry->command;
            return AXYNE_KEYMAP_COMMAND;
        }
    }
    sequence.strokes[0] = stroke;
    sequence.count = 1;
    entry = keymap_match(map, &sequence, context);
    if (entry != NULL) {
        if (command_is_native(entry->command)) return AXYNE_KEYMAP_NONE;
        if (command != NULL) *command = entry->command;
        return AXYNE_KEYMAP_COMMAND;
    }
    if (keymap_is_prefix(map, stroke, context)) {
        map->has_pending = 1;
        map->pending = stroke;
        map->pending_since_ms = now_ms;
        return AXYNE_KEYMAP_PENDING;
    }
    return AXYNE_KEYMAP_NONE;
}

int axyne_keymap_pending(const AxyneKeymap *map, uint64_t now_ms, AxyneKeyStroke *first)
{
    if (map == NULL || !pending_alive(map, now_ms)) return 0;
    if (first != NULL) *first = map->pending;
    return 1;
}

void axyne_keymap_cancel_pending(AxyneKeymap *map)
{
    if (map != NULL) map->has_pending = 0;
}

size_t axyne_keymap_chord_hint(const AxyneKeymap *map, uint64_t now_ms,
                               char *buffer, size_t capacity)
{
    AxyneTextOut out = { buffer, buffer != NULL ? capacity : 0, 0 };
    AxyneKeyStroke first;
    if (out.capacity != 0) buffer[0] = '\0';
    if (!axyne_keymap_pending(map, now_ms, &first)) return 0;
    out_append(&out, "(");
    format_stroke(&first, axyne_key_format_for(map->platform), &out);
    out_append(&out, ") 두 번째 키를 기다리는 중…");
    return out.length;
}

AxyneCommandId axyne_keymap_lookup(const AxyneKeymap *map,
                                   const AxyneKeySequence *sequence,
                                   unsigned int context)
{
    const AxyneKeymapEntry *entry;
    if (map == NULL || sequence == NULL) return AXYNE_COMMAND_NONE;
    entry = keymap_match(map, sequence, context);
    return entry != NULL ? entry->command : AXYNE_COMMAND_NONE;
}

size_t axyne_keymap_bindings(const AxyneKeymap *map, AxyneCommandId command,
                             AxyneKeySequence *out, size_t capacity)
{
    size_t found = 0;
    if (map == NULL) return 0;
    for (size_t i = 0; i < map->count; ++i) {
        if (map->entries[i].command != command) continue;
        if (out != NULL && found < capacity) out[found] = map->entries[i].sequence;
        ++found;
    }
    return found;
}

size_t axyne_keymap_display(const AxyneKeymap *map, AxyneCommandId command,
                            char *buffer, size_t capacity)
{
    AxyneKeySequence first;
    if (buffer != NULL && capacity != 0) buffer[0] = '\0';
    if (axyne_keymap_bindings(map, command, &first, 1) == 0) return 0;
    return axyne_key_sequence_format(&first, axyne_key_format_for(map->platform),
                                     buffer, capacity);
}

/* 0: no conflict, 1: identical, 2: `a` is a single stroke prefixing chord `b`. */
static int sequences_conflict(const AxyneKeySequence *a, const AxyneKeySequence *b)
{
    if (axyne_key_sequence_equal(a, b)) return 1;
    if (a->count == 1 && b->count == 2 &&
        axyne_key_stroke_equal(a->strokes[0], b->strokes[0])) return 2;
    return 0;
}

size_t axyne_keymap_conflicts(const AxyneKeymap *map, AxyneKeymapConflict *out,
                              size_t capacity)
{
    size_t found = 0;
    if (map == NULL) return 0;
    for (size_t i = 0; i < map->count; ++i) {
        for (size_t j = i + 1; j < map->count; ++j) {
            const AxyneKeymapEntry *a = &map->entries[i], *b = &map->entries[j];
            int kind;
            if (a->command == b->command ||
                command_is_debug(a->command) != command_is_debug(b->command))
                continue;
            kind = sequences_conflict(&a->sequence, &b->sequence);
            if (kind == 0) {
                kind = sequences_conflict(&b->sequence, &a->sequence);
                if (kind != 0) { const AxyneKeymapEntry *t = a; a = b; b = t; }
            }
            if (kind == 0) continue;
            if (out != NULL && found < capacity) {
                out[found].sequence = a->sequence;
                out[found].first = a->command;
                out[found].second = b->command;
                out[found].prefix = kind == 2;
            }
            ++found;
        }
    }
    return found;
}

AxyneCommandId axyne_keymap_find_conflict(const AxyneKeymap *map,
                                          const AxyneKeySequence *sequence,
                                          AxyneCommandId exclude)
{
    int debug = command_is_debug(exclude);
    if (map == NULL || !sequence_valid(sequence)) return AXYNE_COMMAND_NONE;
    for (size_t i = 0; i < map->count; ++i) {
        const AxyneKeymapEntry *entry = &map->entries[i];
        if (entry->command == exclude || command_is_debug(entry->command) != debug)
            continue;
        if (sequences_conflict(sequence, &entry->sequence) != 0 ||
            sequences_conflict(&entry->sequence, sequence) != 0)
            return entry->command;
    }
    return AXYNE_COMMAND_NONE;
}
