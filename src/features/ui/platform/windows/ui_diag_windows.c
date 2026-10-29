#include "ui_windows_internal.h"

/* 진단 카테고리: 인라인 진단, 탐색기 배지 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->diag_ui (struct AxyneWinDiagUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_diag_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_diag_command_state(AxyneWindowState *state, AxyneCommandId id,
                                        AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_diag = {
    .name = "diag",
    .command = axyne_win_diag_command,
    .command_state = axyne_win_diag_command_state
};
