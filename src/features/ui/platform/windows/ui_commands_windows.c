#include "ui_windows_internal.h"

/* Base registry/feature glue of the Windows UI: the feature table and the
 * hook fan-out that ui_windows.c calls at fixed points. */

static const AxyneWinFeature *const axyne_win_features[AXYNE_WIN_FEATURE_COUNT] = {
    &axyne_win_feature_file,
    &axyne_win_feature_edit,
    &axyne_win_feature_view,
    &axyne_win_feature_build,
    &axyne_win_feature_debug,
    &axyne_win_feature_terminal,
    &axyne_win_feature_status,
    &axyne_win_feature_tools,
    &axyne_win_feature_help,
    &axyne_win_feature_lsp_nav,
    &axyne_win_feature_diag,
    &axyne_win_feature_release
};

const AxyneWinFeature *axyne_win_feature(AxyneWinFeatureId id)
{
    if ((int)id < 0 || id >= AXYNE_WIN_FEATURE_COUNT) return NULL;
    return axyne_win_features[id];
}

void axyne_win_hooks_create(AxyneWindowState *state)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->create != NULL) f->create(state);
    }
}

void axyne_win_hooks_destroy(AxyneWindowState *state)
{
    /* Reverse order: later features may depend on earlier ones. */
    size_t i = AXYNE_WIN_FEATURE_COUNT;
    while (i-- > 0)
        if (axyne_win_features[i]->destroy != NULL) axyne_win_features[i]->destroy(state);
}

int axyne_win_hooks_message(AxyneWindowState *state, HWND window, UINT message,
                            WPARAM w_param, LPARAM l_param, LRESULT *result)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->message != NULL && f->message(state, window, message, w_param, l_param, result))
            return 1;
    }
    return 0;
}

int axyne_win_hooks_command(AxyneWindowState *state, AxyneCommandId id)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->command != NULL && f->command(state, id)) return 1;
    }
    return 0;
}

int axyne_win_hooks_command_state(AxyneWindowState *state, AxyneCommandId id,
                                  AxyneWinCommandState *out)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->command_state == NULL) continue;
        out->enabled = 0;
        out->checked = 0;
        if (f->command_state(state, id, out)) return 1;
    }
    out->enabled = 0;
    out->checked = 0;
    return 0;
}

void axyne_win_hooks_menu_populate(AxyneWindowState *state, AxyneWinMenuSlot slot,
                                   AxyneWinMenu *menu)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->menu_populate != NULL) f->menu_populate(state, slot, menu);
    }
}

int axyne_win_hooks_timer(AxyneWindowState *state, UINT_PTR timer_id)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->timer != NULL && f->timer(state, timer_id)) return 1;
    }
    return 0;
}

void axyne_win_hooks_editor_notify(AxyneWindowState *state,
                                   const SCNotification *notification)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->editor_notify != NULL) f->editor_notify(state, notification);
    }
}

void axyne_win_hooks_document_activated(AxyneWindowState *state, AxyneDocument *document)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->document_activated != NULL) f->document_activated(state, document);
    }
}

void axyne_win_hooks_document_closed(AxyneWindowState *state, const AxyneDocument *document)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->document_closed != NULL) f->document_closed(state, document);
    }
}

void axyne_win_hooks_document_saved(AxyneWindowState *state, AxyneDocument *document)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->document_saved != NULL) f->document_saved(state, document);
    }
}

int axyne_win_hooks_status_paint(AxyneWindowState *state, HDC dc, RECT area)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->status_paint != NULL && f->status_paint(state, dc, area)) return 1;
    }
    return 0;
}

int axyne_win_hooks_panel_paint(AxyneWindowState *state, HDC dc, AxyneWinPanelTab tab,
                                RECT body)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->panel_paint != NULL && f->panel_paint(state, dc, tab, body)) return 1;
    }
    return 0;
}

void axyne_win_hooks_panel_layout(AxyneWindowState *state, AxyneWinPanelTab tab,
                                  RECT body, int visible)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->panel_layout != NULL) f->panel_layout(state, tab, body, visible);
    }
}

void axyne_win_hooks_layout(AxyneWindowState *state, int width, int height)
{
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->layout != NULL) f->layout(state, width, height);
    }
}
