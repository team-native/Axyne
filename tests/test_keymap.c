#include "test_support.h"

#include <string.h>

#include "axyne/keymap.h"

static AxyneKeyStroke stroke_of(const char *text)
{
    AxyneKeyStroke stroke = {0, 0};
    (void)axyne_key_stroke_parse(text, &stroke);
    return stroke;
}

static int canonical_is(const char *text, const char *expected)
{
    AxyneKeySequence sequence;
    char buffer[64];
    if (axyne_key_sequence_parse(text, &sequence) != AXYNE_STATUS_OK) {
        fprintf(stderr, "cannot parse \"%s\"\n", text);
        return 0;
    }
    (void)axyne_key_sequence_format(&sequence, AXYNE_KEY_FORMAT_CANONICAL, buffer,
                                    sizeof(buffer));
    if (strcmp(buffer, expected) != 0) {
        fprintf(stderr, "\"%s\" formats as \"%s\", expected \"%s\"\n", text, buffer,
                expected);
        return 0;
    }
    return 1;
}

static int feed_is(AxyneKeymap *map, const char *stroke, uint64_t now,
                   unsigned context, AxyneKeymapResult result,
                   AxyneCommandId command)
{
    AxyneCommandId got = AXYNE_COMMAND_NONE;
    AxyneKeymapResult actual = axyne_keymap_feed(map, stroke_of(stroke), now,
                                                 context, &got);
    if (actual != result || got != command) {
        fprintf(stderr, "feed %s: result %d command %s, expected %d %s\n", stroke,
                (int)actual, axyne_command_name(got), (int)result,
                axyne_command_name(command));
        return 0;
    }
    return 1;
}

static int test_parse_format(void)
{
    AxyneKeySequence sequence;
    AxyneKeyStroke stroke;
    char buffer[64];
    static const char *const invalid[] = {
        "", "   ", "Ctrl+", "+S", "Ctrl+Ctrl+S", "Ctrl+Foo", "A B C", "F0", "F25",
        "F05", "Ctrl++", "Ctrl+S+T", "Hyper+S", "Ctrl+ S"
    };
    AXYNE_TEST_CHECK(canonical_is("Ctrl+Shift+S", "Ctrl+Shift+S"));
    AXYNE_TEST_CHECK(canonical_is("shift+ctrl+s", "Ctrl+Shift+S"));
    AXYNE_TEST_CHECK(canonical_is("Control+Option+Command+Shift+d", "Ctrl+Cmd+Alt+Shift+D"));
    AXYNE_TEST_CHECK(canonical_is("  Ctrl+K   ctrl+o ", "Ctrl+K Ctrl+O"));
    AXYNE_TEST_CHECK(canonical_is("F5", "F5"));
    AXYNE_TEST_CHECK(canonical_is("ctrl+f24", "Ctrl+F24"));
    AXYNE_TEST_CHECK(canonical_is("Alt+\xe2\x86\x91", "Alt+Up"));
    AXYNE_TEST_CHECK(canonical_is("Ctrl+`", "Ctrl+`"));
    AXYNE_TEST_CHECK(canonical_is("Ctrl+Shift+/", "Ctrl+Shift+/"));
    AXYNE_TEST_CHECK(canonical_is("Cmd+.", "Cmd+."));
    AXYNE_TEST_CHECK(canonical_is("ctrl+pause", "Ctrl+Break"));
    AXYNE_TEST_CHECK(canonical_is("Esc", "Escape"));
    AXYNE_TEST_CHECK(canonical_is("Ctrl+Space", "Ctrl+Space"));
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        if (axyne_key_sequence_parse(invalid[i], &sequence) == AXYNE_STATUS_OK) {
            fprintf(stderr, "\"%s\" should not parse\n", invalid[i]);
            return 0;
        }
    }
    AXYNE_TEST_STATUS(axyne_key_stroke_parse("Ctrl+K Ctrl+O", &stroke),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_key_sequence_parse(NULL, &sequence),
                      AXYNE_STATUS_INVALID_ARGUMENT);

    AXYNE_TEST_STATUS(axyne_key_sequence_parse("Alt+Down", &sequence), AXYNE_STATUS_OK);
    (void)axyne_key_sequence_format(&sequence, AXYNE_KEY_FORMAT_WINDOWS, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "Alt+\xe2\x86\x93");
    AXYNE_TEST_STATUS(axyne_key_sequence_parse("Cmd+Shift+S", &sequence), AXYNE_STATUS_OK);
    (void)axyne_key_sequence_format(&sequence, AXYNE_KEY_FORMAT_MACOS, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "\xe2\x87\xa7\xe2\x8c\x98S");
    AXYNE_TEST_STATUS(axyne_key_sequence_parse("Ctrl+Alt+Shift+Cmd+F5", &sequence), AXYNE_STATUS_OK);
    (void)axyne_key_sequence_format(&sequence, AXYNE_KEY_FORMAT_MACOS, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "\xe2\x8c\x83\xe2\x8c\xa5\xe2\x87\xa7\xe2\x8c\x98" "F5");
    AXYNE_TEST_STATUS(axyne_key_sequence_parse("Cmd+K Cmd+O", &sequence), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(sequence.count, 2);
    (void)axyne_key_sequence_format(&sequence, AXYNE_KEY_FORMAT_MACOS, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "\xe2\x8c\x98K \xe2\x8c\x98O");
    /* Truncation keeps snprintf semantics. */
    AXYNE_TEST_STATUS(axyne_key_sequence_parse("Ctrl+Shift+S", &sequence), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_key_sequence_format(&sequence, AXYNE_KEY_FORMAT_CANONICAL,
                                                buffer, 5), 12);
    AXYNE_TEST_STREQ(buffer, "Ctrl");
    /* AltGr is not part of equality. */
    stroke = stroke_of("Ctrl+Alt+Q");
    {
        AxyneKeyStroke altgr = stroke;
        altgr.mods |= AXYNE_KEYMOD_ALTGR;
        AXYNE_TEST_CHECK(axyne_key_stroke_equal(stroke, altgr));
    }

    /* Native key translation. */
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0x74), AXYNE_KEY_F1 + 4); /* VK_F5 */
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk('S'), 'S');
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0xBF), '/');
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0xC0), '`');
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0x60), '0');
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0x26), AXYNE_KEY_UP);
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0x03), AXYNE_KEY_BREAK);
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0x10), AXYNE_KEY_NONE); /* VK_SHIFT */
    AXYNE_TEST_EQ_INT(axyne_key_from_windows_vk(0xA5), AXYNE_KEY_NONE); /* VK_RMENU */
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character('a'), 'A');
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character('?'), '/');
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character('~'), '`');
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character('!'), '1');
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character(0xF708), AXYNE_KEY_F1 + 4);
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character(0xF700), AXYNE_KEY_UP);
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character(0x7F), AXYNE_KEY_BACKSPACE);
    AXYNE_TEST_EQ_INT(axyne_key_from_mac_character(0x00E9), AXYNE_KEY_NONE);
    return 1;
}

static int test_windows_map(void)
{
    AxyneKeymap map;
    AxyneKeymapConflict conflicts[8];
    AxyneKeySequence sequence;
    AxyneKeyStroke altgr;
    char buffer[96];
    size_t conflict_count;

    AXYNE_TEST_STATUS(axyne_keymap_init(&map, AXYNE_PLATFORM_WINDOWS), AXYNE_STATUS_OK);
    conflict_count = axyne_keymap_conflicts(&map, conflicts, 8);
    for (size_t i = 0; i < conflict_count && i < 8; ++i)
        fprintf(stderr, "default conflict: %s / %s\n",
                axyne_command_name(conflicts[i].first),
                axyne_command_name(conflicts[i].second));
    AXYNE_TEST_EQ_INT(conflict_count, 0);

    /* F5 family (D1) and context-dependent keys. */
    AXYNE_TEST_CHECK(feed_is(&map, "F5", 0, 0, AXYNE_KEYMAP_COMMAND, AXYNE_COMMAND_DEBUG_START));
    AXYNE_TEST_CHECK(feed_is(&map, "F5", 0, AXYNE_KEYMAP_CONTEXT_DEBUGGING,
                             AXYNE_KEYMAP_COMMAND, AXYNE_COMMAND_DEBUG_CONTINUE));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+F5", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING));
    AXYNE_TEST_CHECK(feed_is(&map, "Shift+F5", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_DEBUG_STOP));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Shift+F5", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_DEBUG_RESTART));
    AXYNE_TEST_CHECK(feed_is(&map, "F11", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_VIEW_FULL_SCREEN));
    AXYNE_TEST_CHECK(feed_is(&map, "F11", 0, AXYNE_KEYMAP_CONTEXT_DEBUGGING,
                             AXYNE_KEYMAP_COMMAND, AXYNE_COMMAND_DEBUG_STEP_INTO));
    AXYNE_TEST_CHECK(feed_is(&map, "F10", 0, 0, AXYNE_KEYMAP_NONE, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(feed_is(&map, "F9", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_DEBUG_TOGGLE_BREAKPOINT));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Alt+B", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_BUILD_REBUILD));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+,", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_TOOLS_SETTINGS));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Break", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_BUILD_CANCEL));

    /* Native keys stay with the control but are known to lookups. */
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+C", 0, 0, AXYNE_KEYMAP_NONE, AXYNE_COMMAND_NONE));
    AXYNE_TEST_STATUS(axyne_key_sequence_parse("Ctrl+C", &sequence), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_keymap_lookup(&map, &sequence, 0), AXYNE_COMMAND_EDIT_COPY);
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Q", 0, 0, AXYNE_KEYMAP_NONE, AXYNE_COMMAND_NONE));

    /* Chords (D2): Ctrl+K Ctrl+O within the timeout. */
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 1000, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(axyne_keymap_pending(&map, 1100, NULL));
    (void)axyne_keymap_chord_hint(&map, 1100, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "(Ctrl+K) 두 번째 키를 기다리는 중…");
    /* A modifier alone neither completes nor drops the chord. */
    {
        AxyneKeyStroke ctrl_only = {AXYNE_KEY_NONE, AXYNE_KEYMOD_CTRL};
        AXYNE_TEST_EQ_INT(axyne_keymap_feed(&map, ctrl_only, 1200, 0, NULL), AXYNE_KEYMAP_NONE);
        AXYNE_TEST_CHECK(axyne_keymap_pending(&map, 1200, NULL));
    }
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+O", 1400, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_FILE_OPEN_FOLDER));
    AXYNE_TEST_CHECK(!axyne_keymap_pending(&map, 1400, NULL));
    AXYNE_TEST_CHECK(axyne_keymap_chord_hint(&map, 1400, buffer, sizeof(buffer)) == 0 &&
                     buffer[0] == '\0');
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 2000, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+R", 3500, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_HELP_KEYBOARD_SHORTCUTS)); /* exactly 1500 ms */
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 5000, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+S", 5100, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_TOOLS_KEYBINDINGS));
    /* Timeout: the second stroke is matched as a fresh stroke. */
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 6000, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(!axyne_keymap_pending(&map, 7501, NULL));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+O", 7501, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_FILE_OPEN));
    /* An unknown second stroke aborts the chord. */
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 8000, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(feed_is(&map, "X", 8100, 0, AXYNE_KEYMAP_ABORTED, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(feed_is(&map, "X", 8200, 0, AXYNE_KEYMAP_NONE, AXYNE_COMMAND_NONE));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 9000, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    axyne_keymap_cancel_pending(&map);
    AXYNE_TEST_CHECK(!axyne_keymap_pending(&map, 9001, NULL));
    /* The alias keeps working. */
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Shift+O", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_FILE_OPEN_FOLDER));

    /* AltGr exclusion: Ctrl+Alt+S from AltGr types text. */
    altgr = stroke_of("Ctrl+Alt+S");
    altgr.mods |= AXYNE_KEYMOD_ALTGR;
    AXYNE_TEST_EQ_INT(axyne_keymap_feed(&map, altgr, 0, 0, NULL), AXYNE_KEYMAP_NONE);
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Alt+S", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_FILE_SAVE_ALL));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 100, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    AXYNE_TEST_EQ_INT(axyne_keymap_feed(&map, altgr, 200, 0, NULL), AXYNE_KEYMAP_NONE);
    AXYNE_TEST_CHECK(!axyne_keymap_pending(&map, 200, NULL));
    axyne_keymap_set_altgr_exclusion(&map, 0);
    AXYNE_TEST_EQ_INT(axyne_keymap_feed(&map, altgr, 0, 0, NULL), AXYNE_KEYMAP_COMMAND);
    axyne_keymap_set_altgr_exclusion(&map, 1);

    /* Menu display text. */
    (void)axyne_keymap_display(&map, AXYNE_COMMAND_FILE_OPEN_FOLDER, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "Ctrl+K Ctrl+O");
    (void)axyne_keymap_display(&map, AXYNE_COMMAND_EDIT_MOVE_LINE_UP, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "Alt+\xe2\x86\x91");
    AXYNE_TEST_EQ_INT(axyne_keymap_display(&map, AXYNE_COMMAND_VIEW_OUTLINE, buffer,
                                           sizeof(buffer)), 0);
    AXYNE_TEST_STREQ(buffer, "");
    AXYNE_TEST_EQ_INT(axyne_keymap_bindings(&map, AXYNE_COMMAND_FILE_OPEN_FOLDER, NULL, 0), 2);
    /* macOS-only commands are absent. */
    AXYNE_TEST_EQ_INT(axyne_keymap_bindings(&map, AXYNE_COMMAND_HELP_CONTENTS, NULL, 0), 0);

    /* Overrides. */
    {
        const char *keys[] = {"Ctrl+Alt+W"};
        const char *bad[] = {"Ctrl+Nope"};
        const char *open_keys[] = {"Ctrl+O"};
        const char *chord_keys[] = {"Ctrl+K Ctrl+T", "Alt+F12"};
        AXYNE_TEST_STATUS(axyne_keymap_set_command_text(&map, "file.save", keys, 1),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+S", 0, 0, AXYNE_KEYMAP_NONE, AXYNE_COMMAND_NONE));
        AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Alt+W", 0, 0, AXYNE_KEYMAP_COMMAND,
                                 AXYNE_COMMAND_FILE_SAVE));
        AXYNE_TEST_STATUS(axyne_keymap_set_command_text(&map, "file.save", bad, 1),
                          AXYNE_STATUS_INVALID_ARGUMENT);
        AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+Alt+W", 0, 0, AXYNE_KEYMAP_COMMAND,
                                 AXYNE_COMMAND_FILE_SAVE));
        AXYNE_TEST_STATUS(axyne_keymap_set_command_text(&map, "file.nope", keys, 1),
                          AXYNE_STATUS_NOT_FOUND);
        /* Binding Save to Ctrl+O collides with Open. */
        AXYNE_TEST_STATUS(axyne_keymap_set_command_text(&map, "file.save", open_keys, 1),
                          AXYNE_STATUS_OK);
        conflict_count = axyne_keymap_conflicts(&map, conflicts, 8);
        AXYNE_TEST_EQ_INT(conflict_count, 1);
        AXYNE_TEST_CHECK(!conflicts[0].prefix &&
                         ((conflicts[0].first == AXYNE_COMMAND_FILE_OPEN &&
                           conflicts[0].second == AXYNE_COMMAND_FILE_SAVE) ||
                          (conflicts[0].first == AXYNE_COMMAND_FILE_SAVE &&
                           conflicts[0].second == AXYNE_COMMAND_FILE_OPEN)));
        /* Unbinding resolves it. */
        AXYNE_TEST_STATUS(axyne_keymap_set_command_text(&map, "file.save", NULL, 0),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_EQ_INT(axyne_keymap_conflicts(&map, NULL, 0), 0);
        AXYNE_TEST_EQ_INT(axyne_keymap_bindings(&map, AXYNE_COMMAND_FILE_SAVE, NULL, 0), 0);
        /* A single stroke that prefixes a chord is a prefix conflict. */
        AXYNE_TEST_STATUS(axyne_key_sequence_parse("Ctrl+K", &sequence), AXYNE_STATUS_OK);
        AXYNE_TEST_EQ_INT(axyne_keymap_find_conflict(&map, &sequence, AXYNE_COMMAND_NONE),
                          AXYNE_COMMAND_FILE_OPEN_FOLDER);
        AXYNE_TEST_STATUS(axyne_key_sequence_parse("Ctrl+O Ctrl+P", &sequence), AXYNE_STATUS_OK);
        AXYNE_TEST_EQ_INT(axyne_keymap_find_conflict(&map, &sequence, AXYNE_COMMAND_NONE),
                          AXYNE_COMMAND_FILE_OPEN);
        AXYNE_TEST_STATUS(axyne_key_sequence_parse("Ctrl+Alt+F12", &sequence), AXYNE_STATUS_OK);
        AXYNE_TEST_EQ_INT(axyne_keymap_find_conflict(&map, &sequence, AXYNE_COMMAND_NONE),
                          AXYNE_COMMAND_NONE);
        /* F10 is free outside the debug context class. */
        AXYNE_TEST_STATUS(axyne_key_sequence_parse("F10", &sequence), AXYNE_STATUS_OK);
        AXYNE_TEST_EQ_INT(axyne_keymap_find_conflict(&map, &sequence, AXYNE_COMMAND_NONE),
                          AXYNE_COMMAND_NONE);
        AXYNE_TEST_EQ_INT(axyne_keymap_find_conflict(&map, &sequence,
                                                     AXYNE_COMMAND_DEBUG_CONTINUE),
                          AXYNE_COMMAND_DEBUG_STEP_OVER);
        /* User chord plus alias. */
        AXYNE_TEST_STATUS(axyne_keymap_set_command_text(&map, "view.outline", chord_keys, 2),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+K", 0, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
        AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+T", 10, 0, AXYNE_KEYMAP_COMMAND,
                                 AXYNE_COMMAND_VIEW_OUTLINE));
        AXYNE_TEST_CHECK(feed_is(&map, "Alt+F12", 10, 0, AXYNE_KEYMAP_COMMAND,
                                 AXYNE_COMMAND_VIEW_OUTLINE));
        /* Commands unavailable on the platform cannot be bound. */
        AXYNE_TEST_STATUS(axyne_keymap_set_command(&map, AXYNE_COMMAND_TERMINAL_NEW_ZSH,
                                                   NULL, 0),
                          AXYNE_STATUS_INVALID_ARGUMENT);
    }
    axyne_keymap_destroy(&map);
    axyne_keymap_destroy(&map); /* safe twice */
    return 1;
}

static int test_macos_map(void)
{
    AxyneKeymap map;
    char buffer[96];
    AXYNE_TEST_STATUS(axyne_keymap_init(&map, AXYNE_PLATFORM_MACOS), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_keymap_conflicts(&map, NULL, 0), 0);
    AXYNE_TEST_CHECK(map.altgr_exclusion == 0);
    AXYNE_TEST_CHECK(feed_is(&map, "F5", 0, 0, AXYNE_KEYMAP_COMMAND, AXYNE_COMMAND_DEBUG_START));
    AXYNE_TEST_CHECK(feed_is(&map, "Ctrl+F5", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING));
    AXYNE_TEST_CHECK(feed_is(&map, "Cmd+R", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING));
    AXYNE_TEST_CHECK(feed_is(&map, "Shift+F5", 0, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_DEBUG_STOP));
    AXYNE_TEST_CHECK(feed_is(&map, "Cmd+K", 0, 0, AXYNE_KEYMAP_PENDING, AXYNE_COMMAND_NONE));
    (void)axyne_keymap_chord_hint(&map, 10, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "(\xe2\x8c\x98K) 두 번째 키를 기다리는 중…");
    AXYNE_TEST_CHECK(feed_is(&map, "Cmd+O", 10, 0, AXYNE_KEYMAP_COMMAND,
                             AXYNE_COMMAND_FILE_OPEN_FOLDER));
    (void)axyne_keymap_display(&map, AXYNE_COMMAND_FILE_SAVE_AS, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "\xe2\x87\xa7\xe2\x8c\x98S");
    (void)axyne_keymap_display(&map, AXYNE_COMMAND_FILE_OPEN_FOLDER, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "\xe2\x8c\x98K \xe2\x8c\x98O");
    /* No AltGr on macOS: Option combinations stay shortcuts. */
    {
        AxyneKeyStroke stroke = stroke_of("Cmd+Alt+S");
        stroke.mods |= AXYNE_KEYMOD_ALTGR;
        AXYNE_TEST_EQ_INT(axyne_keymap_feed(&map, stroke, 0, 0, NULL), AXYNE_KEYMAP_COMMAND);
    }
    AXYNE_TEST_EQ_INT(axyne_keymap_bindings(&map, AXYNE_COMMAND_DEBUG_EXTERNAL_WINDBG, NULL, 0), 0);
    axyne_keymap_destroy(&map);
    return 1;
}

int axyne_test_keymap(const char *root)
{
    (void)root;
    /* Fixture helpers from test_support.h are not needed here. */
    (void)axyne_test_path;
    (void)axyne_test_make_directory;
    AXYNE_TEST_CHECK(test_parse_format());
    AXYNE_TEST_CHECK(test_windows_map());
    AXYNE_TEST_CHECK(test_macos_map());
    return 1;
}
