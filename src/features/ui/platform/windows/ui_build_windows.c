#include "ui_windows_internal.h"

/* 빌드 카테고리: CMake, 빌드 구성, 다시 빌드/정리, tasks.json (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->build_ui (struct AxyneWinBuildUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_build_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_build_command_state(AxyneWindowState *state, AxyneCommandId id,
                                         AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_build = {
    .name = "build",
    .command = axyne_win_build_command,
    .command_state = axyne_win_build_command_state
};
