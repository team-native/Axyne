#include "test_support.h"

#include "axyne/layout_metrics.h"

#define CHECK(cond) \
    do { if (!(cond)) { fprintf(stderr, "layout-metrics failed: %s (line %d)\n", #cond, __LINE__); return 0; } } while (0)

int axyne_test_layout_metrics(const char *root)
{
    const int top = AXYNE_UI_MENU + AXYNE_UI_TOOLBAR + AXYNE_UI_TABS;
    (void)root;
    (void)axyne_test_path;
    (void)axyne_test_make_directory;

    /* Defaults sit inside the limits of a normal window. */
    CHECK(axyne_layout_clamp_sidebar(AXYNE_UI_SIDEBAR, 1280) == AXYNE_UI_SIDEBAR);
    CHECK(axyne_layout_clamp_panel(AXYNE_UI_PANEL, 800, top) == AXYNE_UI_PANEL);

    /* Sidebar limits: min 160, max min(600, half the window). */
    CHECK(axyne_layout_clamp_sidebar(0, 1280) == 160);
    CHECK(axyne_layout_clamp_sidebar(159, 1280) == 160);
    CHECK(axyne_layout_clamp_sidebar(160, 1280) == 160);
    CHECK(axyne_layout_clamp_sidebar(600, 1600) == 600);
    CHECK(axyne_layout_clamp_sidebar(900, 1600) == 600);
    CHECK(axyne_layout_clamp_sidebar(900, 1000) == 500);
    CHECK(axyne_layout_sidebar_max(1000) == 500);
    CHECK(axyne_layout_sidebar_max(3000) == 600);
    /* A window narrower than twice the minimum still yields the minimum. */
    CHECK(axyne_layout_clamp_sidebar(400, 200) == 160);

    /* Panel limits: min 80, editor keeps 120px. */
    CHECK(axyne_layout_clamp_panel(10, 800, top) == 80);
    CHECK(axyne_layout_clamp_panel(80, 800, top) == 80);
    CHECK(axyne_layout_panel_max(800, top) == 800 - top - AXYNE_UI_STATUS - 120);
    CHECK(axyne_layout_clamp_panel(5000, 800, top) == 800 - top - AXYNE_UI_STATUS - 120);
    CHECK(axyne_layout_clamp_panel(200, 200, top) == 80);

    /* Window resize: a stored size is re-clamped against the new window. */
    CHECK(axyne_layout_clamp_sidebar(500, 800) == 400);
    CHECK(axyne_layout_clamp_sidebar(500, 1400) == 500);
    CHECK(axyne_layout_clamp_panel(500, 500, top) == 500 - top - AXYNE_UI_STATUS - 120);
    CHECK(axyne_layout_clamp_panel(500, 900, top) == 500);

    /* Dragging: right grows the sidebar, up grows the panel. */
    CHECK(axyne_layout_drag_sidebar(248, 247, 347, 1280) == 348);
    CHECK(axyne_layout_drag_sidebar(248, 247, 0, 1280) == 160);
    CHECK(axyne_layout_drag_sidebar(248, 247, 2000, 1280) == 600);
    CHECK(axyne_layout_drag_panel(230, 500, 400, 900, top) == 330);
    CHECK(axyne_layout_drag_panel(230, 500, 900, 900, top) == 80);
    CHECK(axyne_layout_drag_panel(230, 500, -100, 900, top) ==
          900 - top - AXYNE_UI_STATUS - 120);

    /* Grab zones: 6px around the border, none while hidden. */
    CHECK(axyne_layout_on_sidebar_splitter(247, 248));
    CHECK(axyne_layout_on_sidebar_splitter(243, 248));
    CHECK(axyne_layout_on_sidebar_splitter(248, 248));
    CHECK(!axyne_layout_on_sidebar_splitter(242, 248));
    CHECK(!axyne_layout_on_sidebar_splitter(249, 248));
    CHECK(!axyne_layout_on_sidebar_splitter(-1, 0));
    CHECK(!axyne_layout_on_sidebar_splitter(0, 0));
    CHECK(axyne_layout_on_panel_splitter(500, 500, 230));
    CHECK(axyne_layout_on_panel_splitter(499, 500, 230));
    CHECK(axyne_layout_on_panel_splitter(504, 500, 230));
    CHECK(!axyne_layout_on_panel_splitter(498, 500, 230));
    CHECK(!axyne_layout_on_panel_splitter(505, 500, 230));
    CHECK(!axyne_layout_on_panel_splitter(500, 500, 0));

    /* Default reset values stay the Figma numbers. */
    CHECK(AXYNE_UI_SIDEBAR == 248 && AXYNE_UI_PANEL == 230);
    return 1;
}
