#include "ui_windows_internal.h"

/* 보기 카테고리: 테마, 개요/도구 모음/상태 표시줄 토글, 동작 항상 표시 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->view_ui (struct AxyneWinViewUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_view_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_view_command_state(AxyneWindowState *state, AxyneCommandId id,
                                        AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_view = {
    .name = "view",
    .command = axyne_win_view_command,
    .command_state = axyne_win_view_command_state
};
