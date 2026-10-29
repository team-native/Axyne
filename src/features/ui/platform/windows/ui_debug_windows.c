#include "ui_windows_internal.h"

/* 디버그 카테고리: 디버거 세션, 디버그 콘솔, 외부 디버거, 실행 인수 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->debug_ui (struct AxyneWinDebugUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_debug_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_debug_command_state(AxyneWindowState *state, AxyneCommandId id,
                                         AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_debug = {
    .name = "debug",
    .command = axyne_win_debug_command,
    .command_state = axyne_win_debug_command_state
};
