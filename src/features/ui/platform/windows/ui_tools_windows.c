#include "ui_windows_internal.h"

/* 도구 카테고리: 외부 도구, 키바인딩, 메모리 사용량, 파일 연결, theme.json (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->tools_ui (struct AxyneWinToolsUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_tools_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_tools_command_state(AxyneWindowState *state, AxyneCommandId id,
                                         AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_tools = {
    .name = "tools",
    .command = axyne_win_tools_command,
    .command_state = axyne_win_tools_command_state
};
