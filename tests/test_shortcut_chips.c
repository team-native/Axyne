#include "test_support.h"

#include <string.h>

#include "axyne/shortcut_chips.h"

static int chips_are(const char *text, size_t count, const char *a,
                     const char *b, const char *c, const char *d)
{
    const char *want[4] = { a, b, c, d };
    AxyneShortcutChips chips;
    if (axyne_shortcut_chips(text, &chips) != count || chips.count != count) return 0;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(chips.chips[i], want[i]) != 0) return 0;
    return 1;
}

/* Like chips_are for the first four chips of a longer shortcut. */
static int chips_prefix(const char *text, size_t count, const char *a,
                        const char *b, const char *c, const char *d)
{
    const char *want[4] = { a, b, c, d };
    AxyneShortcutChips chips;
    if (axyne_shortcut_chips(text, &chips) != count) return 0;
    for (size_t i = 0; i < 4; ++i)
        if (strcmp(chips.chips[i], want[i]) != 0) return 0;
    return 1;
}

int axyne_test_shortcut_chips(const char *root)
{
    AxyneShortcutChips chips;
    char many[64];
    (void)root;
    (void)axyne_test_path;
    (void)axyne_test_make_directory;

    AXYNE_TEST_CHECK(chips_are("\xe2\x87\xa7\xe2\x8c\x98S", 3, "\xe2\x87\xa7", "\xe2\x8c\x98", "S", NULL));
    AXYNE_TEST_CHECK(chips_prefix("\xe2\x8c\x83\xe2\x8c\xa5\xe2\x87\xa7\xe2\x8c\x98" "F5", 5,
                               "\xe2\x8c\x83", "\xe2\x8c\xa5", "\xe2\x87\xa7", "\xe2\x8c\x98"));
    (void)axyne_shortcut_chips("\xe2\x8c\x83\xe2\x8c\xa5\xe2\x87\xa7\xe2\x8c\x98" "F5", &chips);
    AXYNE_TEST_CHECK(strcmp(chips.chips[4], "F5") == 0);
    AXYNE_TEST_CHECK(chips_are("\xe2\x8c\x98\xe2\x86\x91", 2, "\xe2\x8c\x98", "\xe2\x86\x91", NULL, NULL));
    AXYNE_TEST_CHECK(chips_are("Ctrl+Shift+S", 3, "Ctrl", "Shift", "S", NULL));
    AXYNE_TEST_CHECK(chips_are("Ctrl + Alt + F5", 3, "Ctrl", "Alt", "F5", NULL));
    AXYNE_TEST_CHECK(chips_are("F5", 1, "F5", NULL, NULL, NULL));
    AXYNE_TEST_CHECK(chips_are("Ctrl++", 2, "Ctrl", "+", NULL, NULL));
    AXYNE_TEST_CHECK(chips_are("+", 1, "+", NULL, NULL, NULL));
    AXYNE_TEST_CHECK(chips_are("", 0, NULL, NULL, NULL, NULL));
    AXYNE_TEST_CHECK(axyne_shortcut_chips(NULL, &chips) == 0 && chips.count == 0);

    /* Overflow keeps the first AXYNE_SHORTCUT_CHIP_MAX chips. */
    memcpy(many, "A+B+C+D+E+F+G+H", 16);
    AXYNE_TEST_CHECK(axyne_shortcut_chips(many, &chips) == AXYNE_SHORTCUT_CHIP_MAX &&
                     strcmp(chips.chips[AXYNE_SHORTCUT_CHIP_MAX - 1], "F") == 0);

    /* Over-long tokens are cut on a UTF-8 boundary and stay terminated. */
    AXYNE_TEST_CHECK(axyne_shortcut_chips(
        "\xea\xb0\x80\xea\xb0\x80\xea\xb0\x80\xea\xb0\x80\xea\xb0\x80\xea\xb0\x80", &chips) == 1 &&
        strlen(chips.chips[0]) == 15 && strcmp(chips.chips[0] + 12, "\xea\xb0\x80") == 0);
    return 1;
}
