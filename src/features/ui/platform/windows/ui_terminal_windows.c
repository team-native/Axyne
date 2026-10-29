#include "ui_windows_internal.h"

/* 터미널 카테고리: 프로필, 여러 세션, 분할 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->terminal_ui (struct AxyneWinTerminalUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_terminal_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_terminal_command_state(AxyneWindowState *state, AxyneCommandId id,
                                            AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_terminal = {
    .name = "terminal",
    .command = axyne_win_terminal_command,
    .command_state = axyne_win_terminal_command_state
};
