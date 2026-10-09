#include "test_support.h"

#include <string.h>

#include "axyne/commands.h"
#include "axyne/palette.h"
#include "axyne/preferences.h"

static int axyne_test_keys_duplicate(AxynePlatform platform)
{
    size_t count = axyne_command_count();
    for (size_t i = 0; i < count; ++i) {
        const AxyneCommandInfo *a = axyne_command_at(i);
        for (size_t ka = 0; ka < AXYNE_COMMAND_DEFAULT_KEYS; ++ka) {
            const char *keys = axyne_command_default_keys(a->id, platform, ka);
            if (keys == NULL) continue;
            for (size_t j = i; j < count; ++j) {
                const AxyneCommandInfo *b = axyne_command_at(j);
                for (size_t kb = 0; kb < AXYNE_COMMAND_DEFAULT_KEYS; ++kb) {
                    const char *other = axyne_command_default_keys(b->id, platform, kb);
                    if (other == NULL || (i == j && kb <= ka)) continue;
                    /* Debug-context bindings may share keys with others. */
                    if (((a->flags ^ b->flags) & AXYNE_COMMAND_FLAG_DEBUG_CONTEXT) != 0)
                        continue;
                    if (strcmp(keys, other) == 0) {
                        fprintf(stderr, "duplicate default %s: %s and %s\n", keys,
                                a->name, b->name);
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

int axyne_test_commands(const char *root)
{
    size_t count = axyne_command_count();
    size_t palette_count = 0;
    const AxynePaletteCommand *palette = axyne_palette_commands(&palette_count);
    (void)root;

    AXYNE_TEST_EQ_INT(count, AXYNE_COMMAND_COUNT - 1);
    AXYNE_TEST_CHECK(axyne_command_info(AXYNE_COMMAND_NONE) == NULL);
    AXYNE_TEST_CHECK(axyne_command_info(AXYNE_COMMAND_COUNT) == NULL);
    AXYNE_TEST_CHECK(axyne_command_at(count) == NULL);
    for (size_t i = 0; i < count; ++i) {
        const AxyneCommandInfo *info = axyne_command_at(i);
        AXYNE_TEST_CHECK(info != NULL);
        AXYNE_TEST_EQ_INT(info->id, i + 1);
        AXYNE_TEST_CHECK(axyne_command_info(info->id) == info);
        AXYNE_TEST_CHECK(info->name != NULL && info->name[0] != '\0');
        AXYNE_TEST_CHECK(info->title != NULL && info->title[0] != '\0');
        AXYNE_TEST_CHECK(info->keywords != NULL && info->keywords[0] != '\0');
        AXYNE_TEST_CHECK(info->group < AXYNE_COMMAND_GROUP_COUNT);
        AXYNE_TEST_CHECK(info->platforms != 0 &&
                         (info->platforms & ~(unsigned)AXYNE_PLATFORM_BOTH) == 0);
        AXYNE_TEST_CHECK(axyne_command_find(info->name) == info);
        AXYNE_TEST_EQ_INT(axyne_command_id(info->name), info->id);
        /* Ids are unique: the lookup above finds this entry, not another. */
        for (size_t j = i + 1; j < count; ++j) {
            const AxyneCommandInfo *other = axyne_command_at(j);
            if (strcmp(info->name, other->name) == 0) {
                fprintf(stderr, "duplicate command id %s\n", info->name);
                return 0;
            }
            AXYNE_TEST_CHECK(info->palette_id == 0 ||
                             info->palette_id != other->palette_id);
            AXYNE_TEST_CHECK(info->legacy_action < 0 ||
                             info->legacy_action != other->legacy_action);
        }
        AXYNE_TEST_CHECK(axyne_command_title(info->id, AXYNE_PLATFORM_WINDOWS) ==
                         info->title);
        if (info->mac_title != NULL)
            AXYNE_TEST_CHECK(axyne_command_title(info->id, AXYNE_PLATFORM_MACOS) ==
                             info->mac_title);
    }
    AXYNE_TEST_CHECK(axyne_command_find("file.unknown") == NULL);
    AXYNE_TEST_CHECK(axyne_command_find(NULL) == NULL);
    AXYNE_TEST_STREQ(axyne_command_name(AXYNE_COMMAND_FILE_SAVE_ALL), "file.saveAll");
    AXYNE_TEST_STREQ(axyne_command_title(AXYNE_COMMAND_FILE_SAVE_ALL,
                                         AXYNE_PLATFORM_WINDOWS), "모두 저장");
    AXYNE_TEST_STREQ(axyne_command_title(AXYNE_COMMAND_FILE_EXIT,
                                         AXYNE_PLATFORM_MACOS), "Axyne 종료");
    AXYNE_TEST_STREQ(axyne_command_name(AXYNE_COMMAND_COUNT), "");

    /* Every palette command maps to exactly one registry command. */
    AXYNE_TEST_CHECK(palette != NULL && palette_count > 0);
    for (size_t i = 0; i < palette_count; ++i) {
        AxyneCommandId id = axyne_command_from_palette((int)palette[i].id);
        if (id == AXYNE_COMMAND_NONE) {
            fprintf(stderr, "palette command %d (%s) has no registry entry\n",
                    (int)palette[i].id, palette[i].title);
            return 0;
        }
        AXYNE_TEST_CHECK((axyne_command_info(id)->flags &
                          AXYNE_COMMAND_FLAG_PALETTE) != 0);
    }
    AXYNE_TEST_EQ_INT(axyne_command_from_palette(0), AXYNE_COMMAND_NONE);

    /* Every legacy preference action maps to a registry command. */
    for (int action = 0; action < AXYNE_ACTION_COUNT; ++action)
        AXYNE_TEST_CHECK(axyne_command_from_legacy_action(action) != AXYNE_COMMAND_NONE);
    AXYNE_TEST_EQ_INT(axyne_command_from_legacy_action(AXYNE_ACTION_RUN),
                      AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING);
    AXYNE_TEST_EQ_INT(axyne_command_from_legacy_action(-1), AXYNE_COMMAND_NONE);

    /* Defaults from the gap report (D1/D2) and Figma. */
    AXYNE_TEST_STREQ(axyne_command_default_keys(AXYNE_COMMAND_DEBUG_START,
                         AXYNE_PLATFORM_WINDOWS, 0), "F5");
    AXYNE_TEST_STREQ(axyne_command_default_keys(AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING,
                         AXYNE_PLATFORM_WINDOWS, 0), "Ctrl+F5");
    AXYNE_TEST_STREQ(axyne_command_default_keys(AXYNE_COMMAND_DEBUG_STOP,
                         AXYNE_PLATFORM_MACOS, 0), "Shift+F5");
    AXYNE_TEST_STREQ(axyne_command_default_keys(AXYNE_COMMAND_FILE_OPEN_FOLDER,
                         AXYNE_PLATFORM_WINDOWS, 0), "Ctrl+K Ctrl+O");
    AXYNE_TEST_STREQ(axyne_command_default_keys(AXYNE_COMMAND_FILE_OPEN_FOLDER,
                         AXYNE_PLATFORM_WINDOWS, 1), "Ctrl+Shift+O");
    AXYNE_TEST_STREQ(axyne_command_default_keys(AXYNE_COMMAND_FILE_OPEN_FOLDER,
                         AXYNE_PLATFORM_MACOS, 0), "Cmd+K Cmd+O");
    AXYNE_TEST_STREQ(axyne_command_default_keys(AXYNE_COMMAND_HELP_KEYBOARD_SHORTCUTS,
                         AXYNE_PLATFORM_WINDOWS, 0), "Ctrl+K Ctrl+R");
    AXYNE_TEST_CHECK(axyne_command_default_keys(AXYNE_COMMAND_FILE_SAVE,
                         AXYNE_PLATFORM_WINDOWS, 2) == NULL);

    /* Platform availability. */
    AXYNE_TEST_CHECK(axyne_command_available(AXYNE_COMMAND_DEBUG_EXTERNAL_WINDBG,
                                             AXYNE_PLATFORM_WINDOWS));
    AXYNE_TEST_CHECK(!axyne_command_available(AXYNE_COMMAND_DEBUG_EXTERNAL_WINDBG,
                                              AXYNE_PLATFORM_MACOS));
    AXYNE_TEST_CHECK(axyne_command_available(AXYNE_COMMAND_TERMINAL_NEW_ZSH,
                                             AXYNE_PLATFORM_MACOS));
    AXYNE_TEST_CHECK(axyne_command_default_keys(AXYNE_COMMAND_HELP_CONTENTS,
                         AXYNE_PLATFORM_WINDOWS, 0) == NULL);

    /* No two commands share a default key text per platform (debug-context
     * bindings excepted). The keymap test repeats this on parsed keys. */
    AXYNE_TEST_CHECK(!axyne_test_keys_duplicate(AXYNE_PLATFORM_WINDOWS));
    AXYNE_TEST_CHECK(!axyne_test_keys_duplicate(AXYNE_PLATFORM_MACOS));

    AXYNE_TEST_STREQ(axyne_command_group_title(AXYNE_COMMAND_GROUP_BUILD), "빌드");
    AXYNE_TEST_STREQ(axyne_command_group_title(AXYNE_COMMAND_GROUP_COUNT), "");
#ifdef __APPLE__
    AXYNE_TEST_EQ_INT(axyne_platform_current(), AXYNE_PLATFORM_MACOS);
#else
    AXYNE_TEST_EQ_INT(axyne_platform_current(), AXYNE_PLATFORM_WINDOWS);
#endif
    return 1;
}
