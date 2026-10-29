#include "ui_windows_internal.h"

/* 상태 표시줄 카테고리: 세그먼트, 메모리 게이지 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->status_ui (struct AxyneWinStatusUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_status_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_status_command_state(AxyneWindowState *state, AxyneCommandId id,
                                          AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_status = {
    .name = "status",
    .command = axyne_win_status_command,
    .command_state = axyne_win_status_command_state
};
