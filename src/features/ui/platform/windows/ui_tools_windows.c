/* Tools feature: installed plugins and their external commands. */
#include "ui_windows_internal.h"

#include "axyne/app_paths.h"
#include "axyne/build_config.h"
#include "../../extensions_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    AXYNE_TOOLS_EXTENSIONS_ID = 60000,
    AXYNE_TOOLS_PLUGIN_ID_BASE = 60001,
    AXYNE_TOOLS_PLUGIN_ID_LIMIT = 65000,
    AXYNE_TOOLS_PLUGIN_ID_CAPACITY =
        AXYNE_TOOLS_PLUGIN_ID_LIMIT - AXYNE_TOOLS_PLUGIN_ID_BASE
};

static void axyne_win_tools_ensure_plugins(AxyneWindowState *state)
{
    char *config, *plugins_dir, *state_path;
    if (state == NULL || state->plugins_loaded) return;
    state->plugins_loaded = 1;
    config = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR);
    if (config == NULL) return;
    plugins_dir = axyne_app_path_join(config, "plugins");
    state_path = axyne_app_path_join(config, "plugins.json");
    if (plugins_dir != NULL)
        (void)axyne_plugins_load(&state->plugins, plugins_dir, state_path,
                                 AXYNE_CONFIG_VERSION, NULL);
    axyne_app_path_free(plugins_dir);
    axyne_app_path_free(state_path);
    axyne_app_path_free(config);
}

static void axyne_win_tools_menu_populate(AxyneWindowState *state,
                                          AxyneWinMenuSlot slot,
                                          AxyneWinMenu *menu)
{
    AxyneWinMenu *plugins_menu;
    AxyneDocument *document;
    const char *path;
    size_t i, j;
    unsigned target = 0;
    char label[512];
    int truncated = 0;
    if (state == NULL || menu == NULL || slot != AXYNE_WIN_MENU_SLOT_PLUGINS) return;
    axyne_win_tools_ensure_plugins(state);
    plugins_menu = axyne_win_menu_submenu(menu, "플러그인", 0);
    if (plugins_menu == NULL) return;
    document = axyne_win_active_document(state);
    path = document != NULL && !document->is_untitled ? document->path : NULL;
    for (i = 0; i < state->plugins.count; ++i) {
        const AxynePlugin *plugin = &state->plugins.items[i];
        int requirements_met;
        if (!plugin->enabled || !plugin->compatible) continue;
        requirements_met = axyne_plugin_missing_requirement(plugin) == NULL;
        for (j = 0; j < plugin->command_count; ++j) {
            unsigned flags;
            if (target >= AXYNE_TOOLS_PLUGIN_ID_CAPACITY) {
                truncated = 1;
                break;
            }
            flags = requirements_met &&
                    axyne_plugin_command_applies(&plugin->commands[j], path)
                ? 0 : AXYNE_WIN_MENU_DISABLED;
            (void)snprintf(label, sizeof(label), "%s: %s", plugin->name,
                           plugin->commands[j].title);
            axyne_win_menu_add(plugins_menu,
                AXYNE_TOOLS_PLUGIN_ID_BASE + target, label, NULL, flags);
            ++target;
        }
        if (truncated) break;
    }
    if (target == 0)
        axyne_win_menu_add(plugins_menu, 0, "설치된 플러그인 명령 없음", NULL,
                           AXYNE_WIN_MENU_DISABLED);
    if (truncated)
        axyne_win_menu_add(plugins_menu, 0, "명령 목록이 너무 깁니다 (일부 숨김)", NULL,
                           AXYNE_WIN_MENU_DISABLED);
    axyne_win_menu_add(menu, AXYNE_TOOLS_EXTENSIONS_ID, "확장...", NULL, 0);
}

static void axyne_win_tools_run_plugin(AxyneWindowState *state, unsigned target)
{
    AxynePlugin *plugin = NULL;
    AxynePluginCommand *command = NULL;
    AxynePluginInvocation invocation;
    AxynePluginContext context;
    AxyneProcessSpec spec;
    AxyneDocument *document;
    AxyneError error;
    size_t i, j;
    unsigned index = 0;
    char header[512];

    axyne_win_tools_ensure_plugins(state);
    for (i = 0; i < state->plugins.count && command == NULL; ++i) {
        AxynePlugin *candidate = &state->plugins.items[i];
        if (!candidate->enabled || !candidate->compatible) continue;
        for (j = 0; j < candidate->command_count; ++j, ++index) {
            if (index == target) {
                plugin = candidate;
                command = &candidate->commands[j];
                break;
            }
        }
    }
    if (plugin == NULL || command == NULL) return;
    axyne_win_select_panel_tab(state, AXYNE_WIN_PANEL_OUTPUT, 0);
    if (axyne_win_process_busy(state)) {
        axyne_win_output_append(state, "[plugin] Another action is running. Stop it first.\n",
                                AXYNE_PROCESS_STDERR);
        return;
    }
    document = axyne_win_active_document(state);
    if (document != NULL && document->is_dirty && !document->is_untitled &&
        !axyne_win_save_active(state)) {
        axyne_win_output_append(state, "[plugin] Save cancelled; command was not started.\n",
                                AXYNE_PROCESS_STDERR);
        return;
    }
    memset(&context, 0, sizeof(context));
    context.file = document != NULL && !document->is_untitled ? document->path : NULL;
    context.workspace_folder = state->explorer.root;
    axyne_win_output_clear(state);
    memset(&invocation, 0, sizeof(invocation));
    if (axyne_plugin_command_prepare(plugin, command, &context, &invocation,
                                     &error) != AXYNE_STATUS_OK) {
        (void)snprintf(header, sizeof(header), "[plugin] %s: %s\n",
                       plugin->name, error.message);
        axyne_win_output_append(state, header, AXYNE_PROCESS_STDERR);
        axyne_plugin_invocation_clear(&invocation);
        return;
    }
    memset(&spec, 0, sizeof(spec));
    spec.executable = invocation.executable;
    spec.arguments = (const char *const *)invocation.arguments;
    spec.argument_count = invocation.argument_count;
    spec.working_directory = invocation.working_directory;
    spec.on_output = axyne_terminal_output;
    spec.on_exit = axyne_terminal_exit;
    spec.user_data = state;
    if (axyne_process_start(&spec, &state->action_process, &error) != AXYNE_STATUS_OK) {
        (void)snprintf(header, sizeof(header), "[plugin] %s: %s\n",
                       plugin->name, error.message);
        axyne_win_output_append(state, header, AXYNE_PROCESS_STDERR);
    } else {
        (void)snprintf(header, sizeof(header), "[plugin] %s: %s\n",
                       plugin->name, command->title);
        axyne_win_output_append(state, header, AXYNE_PROCESS_STDOUT);
        state->active_action = 3;
        state->last_exit_failed = 0;
        axyne_refresh_action_controls(state);
        axyne_win_invalidate(state, NULL);
    }
    axyne_plugin_invocation_clear(&invocation);
}

static int axyne_win_tools_message(AxyneWindowState *state, HWND window,
                                   UINT message, WPARAM w_param,
                                   LPARAM l_param, LRESULT *result)
{
    UINT id;
    if (state == NULL || message != WM_COMMAND || l_param != 0 ||
        HIWORD(w_param) != 0)
        return 0;
    id = LOWORD(w_param);
    if (id == AXYNE_TOOLS_EXTENSIONS_ID) {
        axyne_win_tools_ensure_plugins(state);
        axyne_extensions_window_show(window, &state->plugins,
                                     AXYNE_CONFIG_VERSION, AXYNE_EXTENSIONS_INSTALLED);
    } else if (id >= AXYNE_TOOLS_PLUGIN_ID_BASE && id < AXYNE_TOOLS_PLUGIN_ID_LIMIT) {
        axyne_win_tools_run_plugin(state, id - AXYNE_TOOLS_PLUGIN_ID_BASE);
    } else {
        return 0;
    }
    if (result != NULL) *result = 0;
    return 1;
}

static void axyne_win_tools_destroy(AxyneWindowState *state)
{
    if (state != NULL) axyne_plugins_clear(&state->plugins);
}

const AxyneWinFeature axyne_win_feature_tools = {
    .name = "tools",
    .destroy = axyne_win_tools_destroy,
    .message = axyne_win_tools_message,
    .menu_populate = axyne_win_tools_menu_populate
};
