#include "ui_windows_internal.h"

/* 도움말 카테고리: 시작하기, 로그 폴더, 라이선스 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->help_ui (struct AxyneWinHelpUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_help_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_help_command_state(AxyneWindowState *state, AxyneCommandId id,
                                        AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_help = {
    .name = "help",
    .command = axyne_win_help_command,
    .command_state = axyne_win_help_command_state
};
