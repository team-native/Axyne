#include "ui_windows_internal.h"

/* 릴리스 카테고리: 릴리스 노트, 업데이트 확인, 첫 실행 배너 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->release_ui (struct AxyneWinReleaseUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_release_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_release_command_state(AxyneWindowState *state, AxyneCommandId id,
                                           AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_release = {
    .name = "release",
    .command = axyne_win_release_command,
    .command_state = axyne_win_release_command_state
};
