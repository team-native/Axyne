#include "ui_windows_internal.h"

/* 파일 카테고리: 새 프로젝트, 모두 저장, 자동 저장, 창 닫기, 최근 폴더/지우기 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->file_ui (struct AxyneWinFileUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_file_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_file_command_state(AxyneWindowState *state, AxyneCommandId id,
                                        AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_file = {
    .name = "file",
    .command = axyne_win_file_command,
    .command_state = axyne_win_file_command_state
};
