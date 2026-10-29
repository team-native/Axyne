#include "ui_windows_internal.h"

/* LSP 탐색 카테고리: 정의/참조 이동, 작업 영역 기호 (Windows).
 * Stub registered by the UI foundation: the feature category fills in these
 * hooks and keeps its data in state->lsp_nav_ui (struct AxyneWinLspNavUi).
 * Until then it implements no command, so its menu items stay disabled. */

static int axyne_win_lsp_nav_command(AxyneWindowState *state, AxyneCommandId id)
{
    (void)state;
    (void)id;
    return 0;
}

static int axyne_win_lsp_nav_command_state(AxyneWindowState *state, AxyneCommandId id,
                                           AxyneWinCommandState *out)
{
    (void)state;
    (void)id;
    (void)out;
    return 0;
}

const AxyneWinFeature axyne_win_feature_lsp_nav = {
    .name = "lsp_nav",
    .command = axyne_win_lsp_nav_command,
    .command_state = axyne_win_lsp_nav_command_state
};
