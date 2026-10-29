#include "ui_windows_internal.h"
#include "axyne/release_ui.h"

/* 릴리스 카테고리: 릴리스 노트, 업데이트 확인, 첫 실행 배너 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->release_ui (struct AxyneWinReleaseUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_release_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    if (id == AXYNE_COMMAND_HELP_CHECK_FOR_UPDATES ||
        id == AXYNE_COMMAND_HELP_RELEASE_NOTES) {
        axyne_release_ui_open(state != NULL ? state->main_window : NULL,
            id == AXYNE_COMMAND_HELP_CHECK_FOR_UPDATES);
        return 1;
    }
    return 0;
}

static int axyne_win_release_command_state(AxyneWindowState *state, AxyneCommandId id,
                                           AxyneWinCommandState *out)
{
    (void)state;
    if (id == AXYNE_COMMAND_HELP_CHECK_FOR_UPDATES ||
        id == AXYNE_COMMAND_HELP_RELEASE_NOTES) {
        if (out != NULL) {
            out->enabled = 1;
            out->checked = 0;
        }
        return 1;
    }
    return 0;
}

static int axyne_win_release_message(AxyneWindowState *state, HWND window,
                                     UINT message, WPARAM w_param,
                                     LPARAM l_param, LRESULT *result)
{
    (void)state;
    (void)w_param;
    (void)l_param;
    (void)result;
    if (message == WM_DESTROY)
        axyne_release_ui_close_owner(window);
    return 0;
}

const AxyneWinFeature axyne_win_feature_release = {
    .name = "release",
    .message = axyne_win_release_message,
    .command = axyne_win_release_command,
    .command_state = axyne_win_release_command_state
};
