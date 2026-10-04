#include "test_support.h"

#include <string.h>

#include "axyne/popup_menu_layout.h"

static int accelerator_is(uint32_t key, unsigned modifiers, const char *want)
{
    char text[32];
    size_t length = axyne_popup_accelerator(text, sizeof(text), key, modifiers);
    return strcmp(text, want) == 0 && length == strlen(want);
}

static int check_accelerators(void)
{
    char tiny[8];
    AXYNE_TEST_CHECK(accelerator_is('n', AXYNE_POPUP_MOD_COMMAND, "\xe2\x8c\x98N"));
    /* An upper-case equivalent implies Shift; an explicit Shift is not doubled. */
    AXYNE_TEST_CHECK(accelerator_is('S', AXYNE_POPUP_MOD_COMMAND, "\xe2\x87\xa7\xe2\x8c\x98S"));
    AXYNE_TEST_CHECK(accelerator_is('Z', AXYNE_POPUP_MOD_COMMAND | AXYNE_POPUP_MOD_SHIFT,
                                    "\xe2\x87\xa7\xe2\x8c\x98Z"));
    AXYNE_TEST_CHECK(accelerator_is('d', AXYNE_POPUP_MOD_COMMAND | AXYNE_POPUP_MOD_SHIFT,
                                    "\xe2\x87\xa7\xe2\x8c\x98" "D"));
    AXYNE_TEST_CHECK(accelerator_is(0xF700, AXYNE_POPUP_MOD_OPTION, "\xe2\x8c\xa5\xe2\x86\x91"));
    AXYNE_TEST_CHECK(accelerator_is(0xF701, AXYNE_POPUP_MOD_OPTION, "\xe2\x8c\xa5\xe2\x86\x93"));
    AXYNE_TEST_CHECK(accelerator_is(0xF708, 0, "F5"));
    AXYNE_TEST_CHECK(accelerator_is(0xF70F, AXYNE_POPUP_MOD_SHIFT, "\xe2\x87\xa7" "F12"));
    AXYNE_TEST_CHECK(accelerator_is(',', AXYNE_POPUP_MOD_COMMAND, "\xe2\x8c\x98,"));
    AXYNE_TEST_CHECK(accelerator_is('/', AXYNE_POPUP_MOD_COMMAND, "\xe2\x8c\x98/"));
    AXYNE_TEST_CHECK(accelerator_is('b', AXYNE_POPUP_MOD_CONTROL | AXYNE_POPUP_MOD_OPTION |
                                    AXYNE_POPUP_MOD_SHIFT | AXYNE_POPUP_MOD_COMMAND,
                                    "\xe2\x8c\x83\xe2\x8c\xa5\xe2\x87\xa7\xe2\x8c\x98" "B"));
    AXYNE_TEST_CHECK(accelerator_is(0x0D, AXYNE_POPUP_MOD_COMMAND, "\xe2\x8c\x98\xe2\x86\xa9"));
    AXYNE_TEST_CHECK(accelerator_is(' ', 0, "Space"));
    AXYNE_TEST_CHECK(accelerator_is(0, AXYNE_POPUP_MOD_COMMAND, ""));
    AXYNE_TEST_CHECK(axyne_popup_accelerator(tiny, sizeof(tiny), 'a', AXYNE_POPUP_MOD_COMMAND) == 0);
    AXYNE_TEST_CHECK(tiny[0] == '\0');
    AXYNE_TEST_CHECK(axyne_popup_accelerator(tiny, 0, 'a', 0) == 0);
    return 1;
}

static int check_layout(void)
{
    const uint8_t kinds[4] = { AXYNE_POPUP_KIND_ITEM, AXYNE_POPUP_KIND_SEPARATOR,
                               AXYNE_POPUP_KIND_ITEM, AXYNE_POPUP_KIND_ITEM };
    int tops[4], heights[4];
    int total = axyne_popup_layout(kinds, 4, tops, heights);
    AXYNE_TEST_CHECK(tops[0] == 5 && heights[0] == 27);
    AXYNE_TEST_CHECK(tops[1] == 32 && heights[1] == 9);
    AXYNE_TEST_CHECK(tops[2] == 41 && tops[3] == 68);
    AXYNE_TEST_CHECK(total == 68 + 27 + 5);
    AXYNE_TEST_CHECK(axyne_popup_layout(kinds, 0, NULL, NULL) == 10);
    AXYNE_TEST_CHECK(axyne_popup_hit(tops, heights, 4, 4) == -1);
    AXYNE_TEST_CHECK(axyne_popup_hit(tops, heights, 4, 5) == 0);
    AXYNE_TEST_CHECK(axyne_popup_hit(tops, heights, 4, 31) == 0);
    AXYNE_TEST_CHECK(axyne_popup_hit(tops, heights, 4, 32) == 1);
    AXYNE_TEST_CHECK(axyne_popup_hit(tops, heights, 4, 94) == 3);
    AXYNE_TEST_CHECK(axyne_popup_hit(tops, heights, 4, 95) == -1);
    return 1;
}

static int check_width(void)
{
    AXYNE_TEST_CHECK(axyne_popup_width(0, 0) == AXYNE_POPUP_MIN_WIDTH);
    AXYNE_TEST_CHECK(axyne_popup_width(60, 0) == AXYNE_POPUP_MIN_WIDTH);
    AXYNE_TEST_CHECK(axyne_popup_width(100, 30) == 10 + 20 + 18 + 8 + 100 + 24 + 30);
    AXYNE_TEST_CHECK(axyne_popup_width(5000, 0) == AXYNE_POPUP_MAX_WIDTH);
    return 1;
}

static int check_navigation(void)
{
    /* header, item, item, separator, item, disabled item */
    const uint8_t sel[6] = { 0, 1, 1, 0, 1, 0 };
    const uint8_t none[3] = { 0, 0, 0 };
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, -1, AXYNE_POPUP_NAV_NEXT) == 1);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, 2, AXYNE_POPUP_NAV_NEXT) == 4);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, 4, AXYNE_POPUP_NAV_NEXT) == 1);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, 1, AXYNE_POPUP_NAV_PREVIOUS) == 4);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, 4, AXYNE_POPUP_NAV_PREVIOUS) == 2);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, -1, AXYNE_POPUP_NAV_PREVIOUS) == 4);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, 99, AXYNE_POPUP_NAV_NEXT) == 1);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, 3, AXYNE_POPUP_NAV_FIRST) == 1);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 6, 1, AXYNE_POPUP_NAV_LAST) == 4);
    AXYNE_TEST_CHECK(axyne_popup_nav(none, 3, -1, AXYNE_POPUP_NAV_NEXT) == -1);
    AXYNE_TEST_CHECK(axyne_popup_nav(none, 3, 1, AXYNE_POPUP_NAV_PREVIOUS) == -1);
    AXYNE_TEST_CHECK(axyne_popup_nav(none, 3, -1, AXYNE_POPUP_NAV_LAST) == -1);
    AXYNE_TEST_CHECK(axyne_popup_nav(sel, 0, -1, AXYNE_POPUP_NAV_NEXT) == -1);
    {
        const uint8_t one[1] = { 1 };
        AXYNE_TEST_CHECK(axyne_popup_nav(one, 1, 0, AXYNE_POPUP_NAV_NEXT) == 0);
        AXYNE_TEST_CHECK(axyne_popup_nav(one, 1, 0, AXYNE_POPUP_NAV_PREVIOUS) == 0);
    }
    return 1;
}

static int check_placement(void)
{
    AxynePopupRect screen = { 0, 0, 1000, 800 };
    AxynePopupRect anchor = { 100, 700, 50, 26 };
    AxynePopupRect out = axyne_popup_place_below(anchor, 200, 300, screen, 2);
    AXYNE_TEST_CHECK(out.x == 100 && out.y == 700 - 2 - 300 && out.w == 200 && out.h == 300);
    /* Right edge clamp. */
    anchor.x = 950;
    out = axyne_popup_place_below(anchor, 200, 300, screen, 2);
    AXYNE_TEST_CHECK(out.x == 800);
    /* Not enough room below and enough above: opens above. */
    anchor.x = 10; anchor.y = 100;
    out = axyne_popup_place_below(anchor, 200, 300, screen, 2);
    AXYNE_TEST_CHECK(out.y == 100 + 26 + 2);
    /* Room neither way: top edge stays visible. */
    anchor.y = 400;
    out = axyne_popup_place_below(anchor, 200, 900, screen, 2);
    AXYNE_TEST_CHECK(out.y + out.h == 800 && out.y < 0);
    /* Offset visible frame (menu bar / dock inset). */
    {
        AxynePopupRect visible = { 0, 70, 1000, 700 };
        AxynePopupRect a = { 20, 150, 40, 26 };
        out = axyne_popup_place_below(a, 100, 200, visible, 2);
        AXYNE_TEST_CHECK(out.y == 150 + 26 + 2);
    }
    /* Submenu: right of the parent, first row aligned, flips when clipped. */
    {
        AxynePopupRect parent = { 100, 400, 250, 200 };
        AxynePopupRect side = axyne_popup_place_side(parent, 560, 300, 120, screen, 3);
        AXYNE_TEST_CHECK(side.x == 347);
        AXYNE_TEST_CHECK(side.y + side.h == 560 + 5);
        parent.x = 700;
        side = axyne_popup_place_side(parent, 560, 300, 120, screen, 3);
        AXYNE_TEST_CHECK(side.x == 700 - 300 + 3);
    }
    return 1;
}

int axyne_test_popup_menu(const char *root)
{
    (void)root;
    (void)axyne_test_path;
    (void)axyne_test_make_directory;
    AXYNE_TEST_CHECK(check_accelerators());
    AXYNE_TEST_CHECK(check_layout());
    AXYNE_TEST_CHECK(check_width());
    AXYNE_TEST_CHECK(check_navigation());
    AXYNE_TEST_CHECK(check_placement());
    return 1;
}
