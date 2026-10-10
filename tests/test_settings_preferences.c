#include "test_support.h"

#include <string.h>

#include "axyne/keymap.h"
#include "axyne/log.h"
#include "axyne/preferences.h"
#include "axyne/settings.h"

static int axyne_test_json_has(const char *path, const char *pointer)
{
    AxyneSettings *document = NULL;
    AxyneSettingsType type;
    int found;
    if (axyne_settings_load(path, &document, NULL) != AXYNE_STATUS_OK) return -1;
    found = axyne_settings_get_type(document, pointer, &type, NULL) == AXYNE_STATUS_OK;
    axyne_settings_destroy(document);
    return found;
}

static int axyne_test_json_string_is(const char *path, const char *pointer,
                                     const char *expected)
{
    AxyneSettings *document = NULL;
    char *value = NULL;
    int same;
    if (axyne_settings_load(path, &document, NULL) != AXYNE_STATUS_OK) return 0;
    same = axyne_settings_get_string(document, pointer, &value, NULL) == AXYNE_STATUS_OK &&
           strcmp(value, expected) == 0;
    if (!same) fprintf(stderr, "%s%s is \"%s\", expected \"%s\"\n", path, pointer,
                       value != NULL ? value : "(missing)", expected);
    axyne_settings_free_json(value);
    axyne_settings_destroy(document);
    return same;
}

static AxyneKeyStroke axyne_test_stroke(const char *text)
{
    AxyneKeyStroke stroke = {0, 0};
    (void)axyne_key_stroke_parse(text, &stroke);
    return stroke;
}

/* Settings v2: new fields, command-keyed bindings, migration, file naming. */
static int axyne_test_settings_v2(const char *root)
{
    char directory[512], path[512], legacy_path[512], other[512];
    AxynePreferences *original = (AxynePreferences *)calloc(1, sizeof(AxynePreferences));
    AxynePreferences *loaded = (AxynePreferences *)calloc(1, sizeof(AxynePreferences));
    AxynePreferences *workspace = (AxynePreferences *)calloc(1, sizeof(AxynePreferences));
    AxyneSettings *settings = NULL;
    AxyneError error = {0};
    AxyneKeymap map;
    AxyneCommandId command = AXYNE_COMMAND_NONE;
    const AxyneKeyBinding *binding;
    const AxyneCommandBinding *entry;
    char *resolved;
    int ok = 0;

#define V2_CHECK(condition) do { if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); goto done; } } while (0)
    V2_CHECK(original != NULL && loaded != NULL && workspace != NULL);
    V2_CHECK(axyne_test_path(directory, sizeof(directory), root, "v2"));
    V2_CHECK(axyne_test_make_directory(directory));
    V2_CHECK(axyne_test_path(path, sizeof(path), directory, "settings.json"));
    V2_CHECK(axyne_test_path(legacy_path, sizeof(legacy_path), directory, "preferences.json"));
    V2_CHECK(axyne_test_path(other, sizeof(other), directory, "other.json"));
    {
        char logs[512];
        V2_CHECK(axyne_test_path(logs, sizeof(logs), root, "logs"));
        axyne_log_set_directory(logs);
    }

    /* Defaults. */
    axyne_preferences_defaults(original);
    V2_CHECK(original->files.auto_save == 0 && original->files.auto_save_delay_ms == 1000);
    V2_CHECK(original->editor.undo_limit_mb == 64 && original->editor.inline_diagnostics == 1 &&
             original->editor.show_line_endings == 0);
    V2_CHECK(strcmp(original->build.build_directory, "build/${config}") == 0);
    V2_CHECK(original->version == AXYNE_PREFERENCES_VERSION);
    binding = axyne_preferences_find_binding(original, AXYNE_ACTION_RUN);
    V2_CHECK(binding != NULL && strcmp(binding->key, "F5") == 0 && binding->modifiers == 0);
    V2_CHECK(axyne_preferences_check_undo_limit(0) != AXYNE_PREFERENCE_CHECK_OK &&
             axyne_preferences_check_undo_limit(1024) == AXYNE_PREFERENCE_CHECK_OK &&
             axyne_preferences_check_undo_limit(1025) != AXYNE_PREFERENCE_CHECK_OK);
    V2_CHECK(axyne_preferences_check_auto_save_delay(99) != AXYNE_PREFERENCE_CHECK_OK &&
             axyne_preferences_check_auto_save_delay(1000) == AXYNE_PREFERENCE_CHECK_OK);

    /* Packed arguments. */
    {
        const char *args[] = {"-G", "Ninja Multi-Config", ""};
        V2_CHECK(axyne_preference_args_set(&original->build.configure_args, args, 3) == AXYNE_STATUS_OK);
        V2_CHECK(original->build.configure_args.count == 3);
        V2_CHECK(strcmp(axyne_preference_args_get(&original->build.configure_args, 1),
                        "Ninja Multi-Config") == 0);
        V2_CHECK(strcmp(axyne_preference_args_get(&original->build.configure_args, 2), "") == 0);
        V2_CHECK(axyne_preference_args_get(&original->build.configure_args, 3) == NULL);
    }

    /* Every new field survives a global save/load round trip. */
    original->files.auto_save = 1;
    original->files.auto_save_delay_ms = 2500;
    original->editor.undo_limit_mb = 128;
    original->editor.show_line_endings = 1;
    original->editor.inline_diagnostics = 0;
    axyne_preferences_select_theme(&original->theme, AXYNE_THEME_HIGH_CONTRAST);
    V2_CHECK(original->theme.background == 0x000000 && original->theme.accent == 0xc9a0ff);
    strcpy(original->terminal.default_profile, "PowerShell 7");
    strcpy(original->build.cmake_path, "C:\\Program Files\\CMake\\bin\\cmake.exe");
    strcpy(original->build.generator, "Ninja");
    strcpy(original->build.build_directory, "out/${config}");
    original->debugger.backend = AXYNE_DEBUGGER_BACKEND_CUSTOM;
    strcpy(original->debugger.path, "/opt/gdb/bin/gdb");
    strcpy(original->debugger.visual_studio, "devenv.exe");
    strcpy(original->debugger.windbg, "windbgx.exe");
    strcpy(original->debugger.x64dbg, "x64dbg.exe");
    original->diff.ignore_whitespace = 1;
    strcpy(original->diff.external_tool, "meld");
    {
        const char *diff_args[] = {"$LOCAL", "$REMOTE"};
        const char *tool_args[] = {"--file", "${file}"};
        V2_CHECK(axyne_preference_args_set(&original->diff.external_tool_args, diff_args, 2) == AXYNE_STATUS_OK);
        original->external_tool_count = 2;
        strcpy(original->external_tools[0].name, "clang-format");
        strcpy(original->external_tools[0].command, "clang-format");
        V2_CHECK(axyne_preference_args_set(&original->external_tools[0].args, tool_args, 2) == AXYNE_STATUS_OK);
        strcpy(original->external_tools[0].cwd, "${workspaceFolder}");
        strcpy(original->external_tools[1].name, "\xed\x95\x9c\xea\xb8\x80 \"tool\"");
        strcpy(original->external_tools[1].command, "echo");
    }
    {
        const char *chord[] = {"ctrl+k ctrl+t", "Alt+F12"};
        const char *save_keys[] = {"Ctrl+Q"};
        const char *open_chord[] = {"Ctrl+K Ctrl+P"};
        const char *bad[] = {"Ctrl+Nope"};
        V2_CHECK(axyne_preferences_set_command_binding(original, "view.outline", chord, 2) == AXYNE_STATUS_OK);
        V2_CHECK(axyne_preferences_set_command_binding(original, "view.toolbar", NULL, 0) == AXYNE_STATUS_OK);
        V2_CHECK(axyne_preferences_set_command_binding(original, "future.command", save_keys, 1) == AXYNE_STATUS_OK);
        /* Legacy action, single stroke: goes to the legacy view. */
        V2_CHECK(axyne_preferences_set_command_binding(original, "file.save", save_keys, 1) == AXYNE_STATUS_OK);
        V2_CHECK(axyne_preferences_find_command_binding(original, "file.save") == NULL);
        binding = axyne_preferences_find_binding(original, AXYNE_ACTION_SAVE);
        V2_CHECK(binding != NULL && strcmp(binding->key, "Q") == 0 && binding->enabled);
        /* Legacy action, chord: stays command-keyed. */
        V2_CHECK(axyne_preferences_set_command_binding(original, "file.open", open_chord, 1) == AXYNE_STATUS_OK);
        V2_CHECK(axyne_preferences_find_command_binding(original, "file.open") != NULL);
        V2_CHECK(axyne_preferences_set_command_binding(original, "view.panel", bad, 1) == AXYNE_STATUS_INVALID_ARGUMENT);
        V2_CHECK(axyne_preferences_set_command_binding(original, "bad id!", save_keys, 1) == AXYNE_STATUS_INVALID_ARGUMENT);
        entry = axyne_preferences_find_command_binding(original, "view.outline");
        V2_CHECK(entry != NULL && entry->count == 2 && strcmp(entry->keys[0], "Ctrl+K Ctrl+T") == 0);
    }
    V2_CHECK(axyne_preferences_save_global(original, path, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_preferences_load_global(path, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_preferences_changed_fields(original, loaded, NULL) == 0);
    V2_CHECK(!axyne_preferences_command_bindings_changed(original, loaded));
    V2_CHECK(loaded->version == AXYNE_PREFERENCES_VERSION && !loaded->migrated);
    V2_CHECK(loaded->theme.preset == AXYNE_THEME_HIGH_CONTRAST);
    V2_CHECK(loaded->external_tool_count == 2 &&
             strcmp(loaded->external_tools[1].name, "\xed\x95\x9c\xea\xb8\x80 \"tool\"") == 0 &&
             strcmp(axyne_preference_args_get(&loaded->external_tools[0].args, 1), "${file}") == 0);
    V2_CHECK(strcmp(loaded->build.cmake_path, original->build.cmake_path) == 0);
    V2_CHECK(loaded->build.configure_args.count == 3);
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_SAVE);
    V2_CHECK(binding != NULL && strcmp(binding->key, "Q") == 0);
    entry = axyne_preferences_find_command_binding(loaded, "view.toolbar");
    V2_CHECK(entry != NULL && entry->count == 0);
    V2_CHECK(axyne_preferences_find_command_binding(loaded, "future.command") != NULL);
    V2_CHECK(axyne_test_json_string_is(path, "/keybindings/file.save", "Ctrl+Q"));
    V2_CHECK(axyne_test_json_string_is(path, "/keybindings/view.outline/1", "Alt+F12"));
    V2_CHECK(axyne_test_json_string_is(path, "/keybindings/view.toolbar", ""));
    /* Unchanged legacy defaults are not pinned (aliases stay intact). */
    V2_CHECK(axyne_test_json_has(path, "/keybindings/file.newFile") == 0);
    V2_CHECK(axyne_test_json_string_is(path, "/theme/preset", "highContrast"));
    V2_CHECK(axyne_test_json_string_is(path, "/debugger/backend", "custom"));

    /* The key map honours both binding views. */
    V2_CHECK(axyne_keymap_build(&map, AXYNE_PLATFORM_WINDOWS, loaded) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_keymap_feed(&map, axyne_test_stroke("Ctrl+Q"), 0, 0, &command) ==
                 AXYNE_KEYMAP_COMMAND && command == AXYNE_COMMAND_FILE_SAVE);
    V2_CHECK(axyne_keymap_feed(&map, axyne_test_stroke("Ctrl+S"), 0, 0, &command) == AXYNE_KEYMAP_NONE);
    V2_CHECK(axyne_keymap_feed(&map, axyne_test_stroke("Ctrl+O"), 0, 0, &command) == AXYNE_KEYMAP_NONE);
    V2_CHECK(axyne_keymap_feed(&map, axyne_test_stroke("Ctrl+K"), 0, 0, &command) == AXYNE_KEYMAP_PENDING);
    V2_CHECK(axyne_keymap_feed(&map, axyne_test_stroke("Ctrl+P"), 5, 0, &command) ==
                 AXYNE_KEYMAP_COMMAND && command == AXYNE_COMMAND_FILE_OPEN);
    V2_CHECK(axyne_keymap_feed(&map, axyne_test_stroke("Alt+F12"), 5, 0, &command) ==
                 AXYNE_KEYMAP_COMMAND && command == AXYNE_COMMAND_VIEW_OUTLINE);
    V2_CHECK(axyne_keymap_feed(&map, axyne_test_stroke("Ctrl+F5"), 5, 0, &command) ==
                 AXYNE_KEYMAP_COMMAND && command == AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING);
    axyne_keymap_destroy(&map);

    /* Removing a legacy override restores its default. */
    axyne_preferences_remove_command_binding(loaded, "file.save");
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_SAVE);
    V2_CHECK(binding != NULL && strcmp(binding->key, "S") == 0);
    axyne_preferences_remove_command_binding(loaded, "view.outline");
    V2_CHECK(axyne_preferences_find_command_binding(loaded, "view.outline") == NULL);

    /* Unknown members survive a save; known ones are rewritten. */
    V2_CHECK(axyne_settings_create(&settings, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_settings_set_json(settings, "/lsp", "{\"clangd\":{\"path\":\"x\"}}", &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_settings_set_json(settings, "/editor", "{\"futureKey\":true,\"tabWidth\":3}", &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_settings_save(settings, other, &error) == AXYNE_STATUS_OK);
    axyne_settings_destroy(settings);
    settings = NULL;
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->editor.tab_width == 3);
    loaded->editor.tab_width = 5;
    V2_CHECK(axyne_preferences_save(loaded, other, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_test_json_has(other, "/lsp/clangd/path") == 1);
    V2_CHECK(axyne_test_json_has(other, "/editor/futureKey") == 1);
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->editor.tab_width == 5);

    /* Workspace saves write present fields only and prune empty sections. */
    axyne_preferences_defaults(workspace);
    workspace->present_fields = AXYNE_PREFERENCE_FILES_AUTO_SAVE;
    memset(workspace->binding_present, 0, sizeof(workspace->binding_present));
    workspace->files.auto_save = 1;
    V2_CHECK(axyne_preferences_save_workspace(workspace, other, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_test_json_has(other, "/files/autoSave") == 1);
    V2_CHECK(axyne_test_json_has(other, "/editor/tabWidth") == 0);
    V2_CHECK(axyne_test_json_has(other, "/build") == 0);
    V2_CHECK(axyne_test_json_has(other, "/keybindings") == 0);
    V2_CHECK(axyne_test_json_has(other, "/editor/futureKey") == 1);
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->present_fields == AXYNE_PREFERENCE_FILES_AUTO_SAVE);
    axyne_preferences_defaults(original);
    axyne_preferences_apply_workspace(original, loaded);
    V2_CHECK(original->files.auto_save == 1 && original->editor.tab_width == 4);
    /* Workspace command bindings override global ones by id. */
    {
        const char *global_keys[] = {"Alt+1"};
        AxynePreferences *base = (AxynePreferences *)calloc(1, sizeof(AxynePreferences));
        AxynePreferences *edited = (AxynePreferences *)calloc(1, sizeof(AxynePreferences));
        AxynePreferences *out = (AxynePreferences *)calloc(1, sizeof(AxynePreferences));
        V2_CHECK(base != NULL && edited != NULL && out != NULL);
        axyne_preferences_defaults(original);
        (void)axyne_preferences_set_command_binding(original, "view.outline", global_keys, 1);
        (void)axyne_preferences_set_command_binding(original, "view.toolbar", global_keys, 1);
        V2_CHECK(axyne_test_write(other, "{\"keybindings\":{\"view.outline\":\"Alt+2\"}}"));
        V2_CHECK(axyne_preferences_load(other, workspace, &error) == AXYNE_STATUS_OK);
        axyne_preferences_apply_workspace(original, workspace);
        entry = axyne_preferences_find_command_binding(original, "view.outline");
        V2_CHECK(entry != NULL && strcmp(entry->keys[0], "Alt+2") == 0);
        V2_CHECK(axyne_preferences_find_command_binding(original, "view.toolbar") != NULL);
        /* Editing base: only the workspace's own entries are present. */
        axyne_preferences_workspace_base(base, original, workspace);
        V2_CHECK(axyne_preferences_find_command_binding(base, "view.outline")->present == 1);
        V2_CHECK(axyne_preferences_find_command_binding(base, "view.toolbar")->present == 0);
        *edited = *base;
        edited->editor.font_size = 14;
        axyne_preferences_prepare_save(out, base, edited, 1);
        V2_CHECK(axyne_preferences_save_workspace(out, other, &error) == AXYNE_STATUS_OK);
        V2_CHECK(axyne_test_json_string_is(other, "/keybindings/view.outline", "Alt+2"));
        V2_CHECK(axyne_test_json_has(other, "/keybindings/view.toolbar") == 0);
        V2_CHECK(axyne_test_json_has(other, "/editor/fontSize") == 1);

        /* Review case: the current UIs build the workspace editing base from
         * the effective profile (global command bindings included) without
         * axyne_preferences_workspace_base. Saving a font-size change must not
         * copy the global keybindings into the workspace file. */
        V2_CHECK(axyne_test_write(path, "{\"keybindings\":{\"view.outline\":\"Ctrl+Alt+O\"}}"));
        V2_CHECK(axyne_preferences_load_global(path, original, &error) == AXYNE_STATUS_OK);
        axyne_test_remove_file(other);
        *base = *original;                 /* initial = state->preferences */
        base->present_fields = 0;          /* no workspace file yet */
        memset(base->binding_present, 0, sizeof(base->binding_present));
        *edited = *base;
        edited->editor.font_size = 15;
        axyne_preferences_prepare_save(out, base, edited, 1);
        V2_CHECK(axyne_preferences_save_workspace(out, other, &error) == AXYNE_STATUS_OK);
        V2_CHECK(axyne_test_json_has(other, "/keybindings") == 0);
        V2_CHECK(axyne_test_json_has(other, "/editor/fontSize") == 1);
        /* The UIs also create an empty workspace file from the effective
         * profile with nothing marked present. */
        axyne_test_remove_file(other);
        *out = *original;
        out->present_fields = 0;
        memset(out->binding_present, 0, sizeof(out->binding_present));
        V2_CHECK(axyne_preferences_save_workspace(out, other, &error) == AXYNE_STATUS_OK);
        V2_CHECK(axyne_test_json_has(other, "/keybindings") == 0);
        /* Changing a command binding in the workspace editor writes it. */
        {
            const char *keys[] = {"Alt+9"};
            *edited = *base;
            V2_CHECK(axyne_preferences_set_command_binding(edited, "view.outline", keys, 1) == AXYNE_STATUS_OK);
            axyne_preferences_prepare_save(out, base, edited, 1);
            V2_CHECK(axyne_preferences_save_workspace(out, other, &error) == AXYNE_STATUS_OK);
            V2_CHECK(axyne_test_json_string_is(other, "/keybindings/view.outline", "Alt+9"));
        }
        free(base);
        free(edited);
        free(out);
    }

    /* A workspace binding for an action without a default slot applies. */
    {
        const char *keys[] = {"Ctrl+,"};
        axyne_preferences_defaults(original);
        axyne_preferences_defaults(workspace);
        workspace->present_fields = 0;
        memset(workspace->binding_present, 0, sizeof(workspace->binding_present));
        V2_CHECK(axyne_preferences_find_binding(original, AXYNE_ACTION_PREFERENCES) == NULL);
        V2_CHECK(axyne_preferences_set_command_binding(workspace, "tools.settings", keys, 1) == AXYNE_STATUS_OK);
        axyne_preferences_apply_workspace(original, workspace);
        binding = axyne_preferences_find_binding(original, AXYNE_ACTION_PREFERENCES);
        V2_CHECK(binding != NULL && binding->enabled && strcmp(binding->key, ",") == 0);
    }

    /* Legacy key text no stroke can express is kept verbatim. */
    {
        AxyneKeyBinding *save;
        axyne_preferences_defaults(original);
        save = (AxyneKeyBinding *)axyne_preferences_find_binding(original, AXYNE_ACTION_SAVE);
        strcpy(save->key, "Weird");
        V2_CHECK(axyne_preferences_save_global(original, path, &error) == AXYNE_STATUS_OK);
#ifdef __APPLE__
        V2_CHECK(axyne_test_json_string_is(path, "/keybindings/file.save", "Cmd+Weird"));
#else
        V2_CHECK(axyne_test_json_string_is(path, "/keybindings/file.save", "Ctrl+Weird"));
#endif
        V2_CHECK(axyne_preferences_load_global(path, loaded, &error) == AXYNE_STATUS_OK);
        binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_SAVE);
        V2_CHECK(binding != NULL && strcmp(binding->key, "Weird") == 0 &&
                 binding->modifiers == AXYNE_KEY_MODIFIER_COMMAND && binding->enabled);
    }

    /* D1: loading never migrates; the explicit hook does, once. */
    V2_CHECK(axyne_test_write(legacy_path,
        "{\"editor\":{\"tabWidth\":2},\"keybindings\":["
        "{\"action\":9,\"modifiers\":0,\"key\":\"F5\",\"enabled\":true},"
        "{\"action\":2,\"modifiers\":1,\"key\":\"Q\",\"enabled\":true}]}"));
    V2_CHECK(axyne_preferences_load(legacy_path, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->version == 1 && loaded->migrated == 0);
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_RUN);
    V2_CHECK(binding != NULL && strcmp(binding->key, "F5") == 0 && binding->modifiers == 0);
    /* Saving without the hook keeps the file at version 1 and F5. */
    V2_CHECK(axyne_preferences_save(loaded, path, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_test_json_has(path, "/version") == 0);
    V2_CHECK(axyne_test_json_has(path, "/keybindings/debug.runWithoutDebugging") == 0);
    V2_CHECK(axyne_preferences_migrate_run_binding(loaded) == 1);
    V2_CHECK(loaded->migrated == 1 && loaded->version == AXYNE_PREFERENCES_VERSION_RUN_MIGRATED);
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_RUN);
    {
        AxyneKeyStroke run;
        V2_CHECK(binding != NULL && axyne_keymap_stroke_from_legacy(binding->modifiers, binding->key,
                                                                    axyne_platform_current(), &run));
        V2_CHECK(axyne_key_stroke_equal(run, axyne_test_stroke("Ctrl+F5")));
    }
    V2_CHECK(axyne_preferences_migrate_run_binding(loaded) == 0);
    V2_CHECK(axyne_preferences_save(loaded, path, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_preferences_load(path, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->version == 2 && loaded->migrated == 0);
    V2_CHECK(axyne_preferences_migrate_run_binding(loaded) == 0);
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_SAVE);
    V2_CHECK(binding != NULL && strcmp(binding->key, "Q") == 0);
    V2_CHECK(axyne_test_json_string_is(path, "/keybindings/debug.runWithoutDebugging", "Ctrl+F5"));
    /* A migrated (version 2) file that binds Run to F5 on purpose keeps it. */
    V2_CHECK(axyne_test_write(other,
        "{\"version\":2,\"keybindings\":{\"debug.runWithoutDebugging\":\"F5\"}}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(axyne_preferences_migrate_run_binding(loaded) == 0);
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_RUN);
    V2_CHECK(binding != NULL && binding->modifiers == 0 && !loaded->migrated);

    /* Lenient loading: invalid keybindings / tools are skipped. */
    V2_CHECK(axyne_test_write(other,
        "{\"editor\":{\"tabWidth\":3},\"keybindings\":{\"file.save\":\"Ctrl+\","
        "\"view.panel\":\"Ctrl+Nope\",\"view.outline\":7,\"bad id!\":\"Alt+1\","
        "\"view.toolbar\":\"Alt+T\"}}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->editor.tab_width == 3);
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_SAVE);
    V2_CHECK(binding != NULL && binding->enabled && strcmp(binding->key, "S") == 0);
    V2_CHECK(axyne_preferences_find_command_binding(loaded, "view.panel") == NULL);
    V2_CHECK(axyne_preferences_find_command_binding(loaded, "view.outline") == NULL);
    V2_CHECK(axyne_preferences_find_command_binding(loaded, "view.toolbar") != NULL);
    V2_CHECK(axyne_test_write(other, "{\"editor\":{\"tabWidth\":6},\"keybindings\":null}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->editor.tab_width == 6);
    V2_CHECK(axyne_test_write(other, "{\"keybindings\":\"oops\",\"externalTools\":{}}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->external_tool_count == 0);
    V2_CHECK(axyne_test_write(other,
        "{\"externalTools\":[{\"name\":\"a\",\"command\":\"a\"},{\"name\":\"no command\"},"
        "{\"name\":\"b\",\"command\":\"b\",\"args\":[1]},7,"
        "{\"name\":\"c\",\"command\":\"c\"},{\"name\":\"d\",\"command\":\"d\"},"
        "{\"name\":\"e\",\"command\":\"e\"},{\"name\":\"f\",\"command\":\"f\"},"
        "{\"name\":\"g\",\"command\":\"g\"},{\"name\":\"h\",\"command\":\"h\"},"
        "{\"name\":\"i\",\"command\":\"i\"},{\"name\":\"j\",\"command\":\"j\"}]}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->external_tool_count == AXYNE_PREFERENCE_EXTERNAL_TOOL_MAX);
    V2_CHECK(strcmp(loaded->external_tools[0].name, "a") == 0 &&
             strcmp(loaded->external_tools[1].name, "c") == 0 &&
             strcmp(loaded->external_tools[7].name, "i") == 0);
    /* Scalar type errors still fail as before. */
    V2_CHECK(axyne_test_write(other, "{\"keybindings\":{\"file.save\":null}}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    binding = axyne_preferences_find_binding(loaded, AXYNE_ACTION_SAVE);
    V2_CHECK(binding != NULL && !binding->enabled);
    V2_CHECK(axyne_test_write(other, "{\"theme\":{\"preset\":\"neon\"}}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_INVALID_ARGUMENT);
    /* Out-of-range numbers are clamped. */
    V2_CHECK(axyne_test_write(other, "{\"editor\":{\"undoLimitMb\":0},\"files\":{\"autoSaveDelayMs\":5}}"));
    V2_CHECK(axyne_preferences_load(other, loaded, &error) == AXYNE_STATUS_OK);
    V2_CHECK(loaded->editor.undo_limit_mb == 1 && loaded->files.auto_save_delay_ms == 100);

    /* D3 file naming. */
    {
        char empty[512];
        V2_CHECK(axyne_test_path(empty, sizeof(empty), directory, "empty"));
        V2_CHECK(axyne_test_make_directory(empty));
        resolved = axyne_preferences_read_path(empty);
        V2_CHECK(resolved != NULL && strstr(resolved, "settings.json") != NULL);
        axyne_preferences_free_path(resolved);
        /* directory holds both: settings.json wins. */
        resolved = axyne_preferences_read_path(directory);
        V2_CHECK(resolved != NULL && strcmp(resolved, path) == 0);
        axyne_preferences_free_path(resolved);
        axyne_test_remove_file(path);
        resolved = axyne_preferences_read_path(directory);
        V2_CHECK(resolved != NULL && strcmp(resolved, legacy_path) == 0);
        axyne_preferences_free_path(resolved);
        resolved = axyne_preferences_write_path(directory);
        V2_CHECK(resolved != NULL && strcmp(resolved, path) == 0);
        axyne_preferences_free_path(resolved);
        resolved = axyne_preferences_workspace_directory(directory);
        V2_CHECK(resolved != NULL && strstr(resolved, ".axyne") != NULL &&
                 strncmp(resolved, directory, strlen(directory)) == 0);
        axyne_preferences_free_path(resolved);
        V2_CHECK(axyne_preferences_read_path(NULL) == NULL);
    }
    ok = 1;
done:
#undef V2_CHECK
    axyne_log_shutdown();
    axyne_settings_destroy(settings);
    free(original);
    free(loaded);
    free(workspace);
    return ok;
}

int axyne_test_settings_preferences(const char *root)
{
    char settings_path[512], preferences_directory[512], preferences_path[512];
    AxyneSettings *settings = NULL;
    AxyneSettings *loaded = NULL;
    AxynePreferences defaults, loaded_preferences, effective, workspace;
    const AxyneKeyBinding *binding;
    char *value = NULL;
    AxyneError error = {0};

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(settings_path, sizeof(settings_path), root,
                                     "settings.json"));
    AXYNE_TEST_CHECK(axyne_test_path(preferences_directory,
                                     sizeof(preferences_directory), root,
                                     "preferences"));
    AXYNE_TEST_CHECK(axyne_test_path(preferences_path, sizeof(preferences_path),
                                     preferences_directory, "global.json"));

    AXYNE_TEST_STATUS(axyne_settings_create(&settings, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor", "{}",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor/tabWidth", "4",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/a~1b", "\"value\"",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/items", "[1,2,3]",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_get_json(settings, "/a~1b", &value,
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(value, "\"value\"") == 0);
    axyne_settings_free_json(value);
    value = NULL;
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/items/1", "7",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_remove(settings, "/items/0", &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_save(settings, settings_path, &error),
                      AXYNE_STATUS_OK);
    axyne_settings_destroy(settings);
    settings = NULL;

    AXYNE_TEST_STATUS(axyne_settings_load(settings_path, &loaded, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_get_json(loaded, "/items/0", &value,
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(value, "7") == 0);
    axyne_settings_free_json(value);
    value = NULL;
    AXYNE_TEST_STATUS(axyne_settings_get_json(loaded, "/missing", &value,
                                               &error), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(value == NULL);
    axyne_settings_destroy(loaded);
    loaded = NULL;

    axyne_preferences_defaults(&defaults);
    AXYNE_TEST_CHECK(defaults.editor.tab_width == 4 &&
                     defaults.editor.insert_spaces == 1);
    binding = axyne_preferences_find_binding(&defaults, AXYNE_ACTION_SAVE);
    AXYNE_TEST_CHECK(binding != NULL && strcmp(binding->key, "S") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_preferences_action_name(AXYNE_ACTION_RUN),
                            "Run") == 0);
    AXYNE_TEST_STATUS(axyne_preferences_save(&defaults, preferences_path,
                                              &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_preferences_load(preferences_path,
                                              &loaded_preferences, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(loaded_preferences.editor.tab_width ==
                         defaults.editor.tab_width &&
                     loaded_preferences.theme.accent == defaults.theme.accent);
    binding = axyne_preferences_find_binding(&loaded_preferences, AXYNE_ACTION_SAVE);
    AXYNE_TEST_CHECK(binding != NULL && strcmp(binding->key, "S") == 0);

    effective = defaults;
    memset(&workspace, 0, sizeof(workspace));
    workspace.editor.tab_width = 8;
    workspace.present_fields = AXYNE_PREFERENCE_EDITOR_TAB_WIDTH;
    axyne_preferences_apply_workspace(&effective, &workspace);
    AXYNE_TEST_CHECK(effective.editor.tab_width == 8 &&
                     effective.editor.font_size == defaults.editor.font_size);

    /* New editor settings: defaults, round trip, workspace overrides. */
    AXYNE_TEST_CHECK(defaults.editor.line_numbers == 1 &&
                     defaults.editor.highlight_current_line == 1 &&
                     defaults.editor.auto_indent == 1 &&
                     defaults.editor.rendering == AXYNE_RENDERING_DIRECTWRITE);
    AXYNE_TEST_CHECK(loaded_preferences.editor.line_numbers == 1 &&
                     loaded_preferences.editor.rendering ==
                         AXYNE_RENDERING_DIRECTWRITE);
    {
        AxynePreferences changed = defaults;
        char legacy_path[512], bad_path[512];
        changed.editor.line_numbers = 0;
        changed.editor.highlight_current_line = 0;
        changed.editor.auto_indent = 0;
        changed.editor.rendering = AXYNE_RENDERING_GDI;
        AXYNE_TEST_STATUS(axyne_preferences_save_global(&changed,
                              preferences_path, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load_global(preferences_path,
                              &loaded_preferences, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(loaded_preferences.editor.line_numbers == 0 &&
                         loaded_preferences.editor.highlight_current_line == 0 &&
                         loaded_preferences.editor.auto_indent == 0 &&
                         loaded_preferences.editor.rendering ==
                             AXYNE_RENDERING_GDI);
        AXYNE_TEST_CHECK((loaded_preferences.present_fields &
                          AXYNE_PREFERENCE_EDITOR_RENDERING) != 0);

        /* Workspace saves only present fields. */
        memset(&workspace, 0, sizeof(workspace));
        workspace.editor.line_numbers = 0;
        workspace.present_fields = AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS;
        AXYNE_TEST_STATUS(axyne_preferences_save_workspace(&workspace,
                              preferences_path, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load(preferences_path,
                              &loaded_preferences, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(loaded_preferences.present_fields ==
                         AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS);
        effective = defaults;
        axyne_preferences_apply_workspace(&effective, &loaded_preferences);
        AXYNE_TEST_CHECK(effective.editor.line_numbers == 0 &&
                         effective.editor.auto_indent == 1 &&
                         effective.editor.rendering ==
                             AXYNE_RENDERING_DIRECTWRITE);

        /* Legacy files without the new keys keep the defaults. */
        AXYNE_TEST_CHECK(axyne_test_path(legacy_path, sizeof(legacy_path),
                                         preferences_directory, "legacy.json"));
        AXYNE_TEST_STATUS(axyne_settings_create(&settings, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor",
                              "{\"tabWidth\":2}", &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_save(settings, legacy_path, &error),
                          AXYNE_STATUS_OK);
        axyne_settings_destroy(settings);
        settings = NULL;
        AXYNE_TEST_STATUS(axyne_preferences_load(legacy_path,
                              &loaded_preferences, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(loaded_preferences.editor.tab_width == 2 &&
                         loaded_preferences.editor.line_numbers == 1 &&
                         loaded_preferences.editor.auto_indent == 1 &&
                         loaded_preferences.present_fields ==
                             AXYNE_PREFERENCE_EDITOR_TAB_WIDTH);

        /* Invalid values are rejected. */
        AXYNE_TEST_CHECK(axyne_test_path(bad_path, sizeof(bad_path),
                                         preferences_directory, "bad.json"));
        AXYNE_TEST_STATUS(axyne_settings_create(&settings, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor",
                              "{\"rendering\":\"opengl\"}", &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_save(settings, bad_path, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load(bad_path,
                              &loaded_preferences, &error),
                          AXYNE_STATUS_INVALID_ARGUMENT);
        AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor",
                              "{\"lineNumbers\":1}", &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_save(settings, bad_path, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load(bad_path,
                              &loaded_preferences, &error),
                          AXYNE_STATUS_INVALID_ARGUMENT);
        axyne_settings_destroy(settings);
        settings = NULL;
    }

    AXYNE_TEST_CHECK(axyne_preferences_check_font_size(5) != AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_font_size(6) == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_font_size(72) == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_font_size(73) != AXYNE_PREFERENCE_CHECK_OK);
    AXYNE_TEST_CHECK(axyne_preferences_check_tab_width(0) != AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_tab_width(16) == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_tab_width(17) != AXYNE_PREFERENCE_CHECK_OK);
    AXYNE_TEST_CHECK(axyne_preferences_check_key("") != AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_key("F5") == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_key("0123456789abcdef") != AXYNE_PREFERENCE_CHECK_OK);
    AXYNE_TEST_CHECK(axyne_preferences_check_font_family("") == AXYNE_PREFERENCE_CHECK_OK);
    {
        AxynePreferences edited = defaults;
        AxyneKeyBinding *save = (AxyneKeyBinding *)axyne_preferences_find_binding(&edited, AXYNE_ACTION_SAVE);
        strcpy(save->key, "Q"); save->enabled = 0; save->modifiers = 0;
        axyne_preferences_restore_binding(&edited, AXYNE_ACTION_SAVE);
        save = (AxyneKeyBinding *)axyne_preferences_find_binding(&edited, AXYNE_ACTION_SAVE);
        AXYNE_TEST_CHECK(strcmp(save->key, "S") == 0 && save->enabled == 1 &&
                         save->modifiers == AXYNE_KEY_MODIFIER_COMMAND);
        axyne_preferences_select_theme(&edited.theme, AXYNE_THEME_LIGHT);
        AXYNE_TEST_CHECK(edited.theme.preset == AXYNE_THEME_LIGHT &&
                         edited.theme.editor_background == 0xffffff);
    }

    {
        AxynePreferences base, edited, out;
        unsigned char bindings[AXYNE_ACTION_COUNT];
        AxyneKeyBinding *save;
        uint64_t fields;
        axyne_preferences_defaults(&base);
        edited = base;
        AXYNE_TEST_CHECK(axyne_preferences_changed_fields(&base, &edited, bindings) == 0);
        edited.editor.line_numbers = 0;
        edited.editor.rendering = AXYNE_RENDERING_GDI;
        strcpy(edited.editor.font_family, "Menlo");
        axyne_preferences_select_theme(&edited.theme, AXYNE_THEME_LIGHT);
        save = (AxyneKeyBinding *)axyne_preferences_find_binding(&edited, AXYNE_ACTION_SAVE);
        save->enabled = 0;
        fields = axyne_preferences_changed_fields(&base, &edited, bindings);
        AXYNE_TEST_CHECK(fields == (AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS |
                                    AXYNE_PREFERENCE_EDITOR_RENDERING |
                                    AXYNE_PREFERENCE_EDITOR_FONT_FAMILY |
                                    AXYNE_PREFERENCE_THEME_PRESET));
        AXYNE_TEST_CHECK(bindings[AXYNE_ACTION_SAVE] == 1 && bindings[AXYNE_ACTION_NEW] == 0);
        /* Workspace: keep existing overrides, add only what changed. */
        base.present_fields = AXYNE_PREFERENCE_EDITOR_TAB_WIDTH;
        memset(base.binding_present, 0, sizeof(base.binding_present));
        base.binding_present[AXYNE_ACTION_OPEN] = 1;
        axyne_preferences_prepare_save(&out, &base, &edited, 1);
        AXYNE_TEST_CHECK(out.present_fields == (fields | AXYNE_PREFERENCE_EDITOR_TAB_WIDTH));
        AXYNE_TEST_CHECK(out.binding_present[AXYNE_ACTION_SAVE] == 1 &&
                         out.binding_present[AXYNE_ACTION_OPEN] == 1 &&
                         out.binding_present[AXYNE_ACTION_NEW] == 0);
        /* Global: everything is written. */
        axyne_preferences_prepare_save(&out, &base, &edited, 0);
        AXYNE_TEST_CHECK(out.present_fields == AXYNE_PREFERENCE_ALL_FIELDS &&
                         out.binding_present[AXYNE_ACTION_NEW] == 1);
    }

    AXYNE_TEST_CHECK(axyne_test_settings_v2(root));
    axyne_test_remove_tree(root);
    return 1;
}
