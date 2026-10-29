#include "ui_windows_internal.h"

/* 편집 카테고리: 블록 주석 등 새 편집 명령 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->edit_ui (struct AxyneWinEditUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_edit_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_edit_command_state(AxyneWindowState *state, AxyneCommandId id,
                                        AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_edit = {
    .name = "edit",
    .command = axyne_win_edit_command,
    .command_state = axyne_win_edit_command_state
};
