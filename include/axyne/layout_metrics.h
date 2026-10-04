#ifndef AXYNE_LAYOUT_METRICS_H
#define AXYNE_LAYOUT_METRICS_H

#include "axyne/ui_design.h"

/* Resizable explorer column and bottom panel. The AXYNE_UI_SIDEBAR and
 * AXYNE_UI_PANEL constants are the defaults; the user drags the borders to
 * change them within these limits. Pure integer math shared by both native
 * adapters. Sizes are logical pixels; the sidebar width includes its 1px
 * right border. */
enum {
    AXYNE_LAYOUT_SIDEBAR_MIN = 160,
    AXYNE_LAYOUT_SIDEBAR_MAX = 600,
    AXYNE_LAYOUT_PANEL_MIN = 80,
    AXYNE_LAYOUT_EDITOR_MIN_HEIGHT = 120,
    /* Grab zones, in pixels around the 1px border line. */
    AXYNE_LAYOUT_GRAB_BEFORE_SIDEBAR = 4,
    AXYNE_LAYOUT_GRAB_AFTER_SIDEBAR = 2,
    AXYNE_LAYOUT_GRAB_BEFORE_PANEL = 1,
    AXYNE_LAYOUT_GRAB_AFTER_PANEL = 5
};

/* Largest allowed sidebar width: min(600, half the window), never below the
 * minimum so a tiny window still gets a usable explorer. */
static inline int axyne_layout_sidebar_max(int window_width)
{
    int limit = window_width / 2;
    if (limit > AXYNE_LAYOUT_SIDEBAR_MAX) limit = AXYNE_LAYOUT_SIDEBAR_MAX;
    return limit < AXYNE_LAYOUT_SIDEBAR_MIN ? AXYNE_LAYOUT_SIDEBAR_MIN : limit;
}

static inline int axyne_layout_clamp_sidebar(int width, int window_width)
{
    int max = axyne_layout_sidebar_max(window_width);
    if (width < AXYNE_LAYOUT_SIDEBAR_MIN) return AXYNE_LAYOUT_SIDEBAR_MIN;
    return width > max ? max : width;
}

/* `window_height` is the full client height; the editor keeps at least
 * AXYNE_LAYOUT_EDITOR_MIN_HEIGHT below the menu/toolbar/tab chrome, above the
 * status bar. `top_chrome` is the height of everything above the editor
 * content (in-window menu, toolbar, tab row; whatever the adapter draws). */
static inline int axyne_layout_panel_max(int window_height, int top_chrome)
{
    int limit = window_height - top_chrome - AXYNE_UI_STATUS -
                AXYNE_LAYOUT_EDITOR_MIN_HEIGHT;
    return limit < AXYNE_LAYOUT_PANEL_MIN ? AXYNE_LAYOUT_PANEL_MIN : limit;
}

static inline int axyne_layout_clamp_panel(int height, int window_height, int top_chrome)
{
    int max = axyne_layout_panel_max(window_height, top_chrome);
    if (height < AXYNE_LAYOUT_PANEL_MIN) return AXYNE_LAYOUT_PANEL_MIN;
    return height > max ? max : height;
}

/* Vertical border at x = sidebar_width - 1. A zero width means hidden. */
static inline int axyne_layout_on_sidebar_splitter(int x, int sidebar_width)
{
    int border = sidebar_width - 1;
    return sidebar_width > 0 && x >= border - AXYNE_LAYOUT_GRAB_BEFORE_SIDEBAR &&
           x < border + AXYNE_LAYOUT_GRAB_AFTER_SIDEBAR;
}

/* Horizontal border at y = panel_top. A zero height means hidden. */
static inline int axyne_layout_on_panel_splitter(int y, int panel_top, int panel_height)
{
    return panel_height > 0 && y >= panel_top - AXYNE_LAYOUT_GRAB_BEFORE_PANEL &&
           y < panel_top + AXYNE_LAYOUT_GRAB_AFTER_PANEL;
}

/* Drag results: the sidebar grows with the pointer moving right; the panel
 * grows with the pointer moving up. */
static inline int axyne_layout_drag_sidebar(int start_width, int start_x, int x,
                                            int window_width)
{
    return axyne_layout_clamp_sidebar(start_width + (x - start_x), window_width);
}

static inline int axyne_layout_drag_panel(int start_height, int start_y, int y,
                                          int window_height, int top_chrome)
{
    return axyne_layout_clamp_panel(start_height - (y - start_y), window_height,
                                    top_chrome);
}

#endif
