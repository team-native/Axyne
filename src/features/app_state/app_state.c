#include "axyne/app_state.h"

#include "axyne/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AxyneStatus state_error(AxyneError *error, AxyneStatus status, const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static char *dup_text(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) text = "";
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static void free_list(char **items, size_t count)
{
    for (size_t i = 0; i < count; ++i) free(items[i]);
}

/* ---- run configuration -------------------------------------------------- */

void axyne_run_configuration_clear(AxyneRunConfiguration *run)
{
    if (run == NULL) return;
    free(run->runner);
    free_list(run->args, run->arg_count);
    free(run->args);
    free(run->cwd);
    for (size_t i = 0; i < run->env_count; ++i) {
        free(run->env[i].name);
        free(run->env[i].value);
    }
    free(run->env);
    free_list(run->program_args, run->program_arg_count);
    free(run->program_args);
    memset(run, 0, sizeof(*run));
}

static AxyneStatus copy_list(char ***destination, size_t *destination_count,
                             char *const *source, size_t count)
{
    *destination = NULL;
    *destination_count = 0;
    if (count == 0) return AXYNE_STATUS_OK;
    *destination = (char **)calloc(count, sizeof(char *));
    if (*destination == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    for (size_t i = 0; i < count; ++i) {
        (*destination)[i] = dup_text(source[i]);
        if ((*destination)[i] == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        *destination_count = i + 1;
    }
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_run_configuration_copy(AxyneRunConfiguration *destination,
                                         const AxyneRunConfiguration *source)
{
    AxyneRunConfiguration copy;
    AxyneStatus status;
    if (destination == NULL || source == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(&copy, 0, sizeof(copy));
    copy.runner = dup_text(source->runner);
    copy.cwd = dup_text(source->cwd);
    if (copy.runner == NULL || copy.cwd == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto fail; }
    status = copy_list(&copy.args, &copy.arg_count, source->args, source->arg_count);
    if (status != AXYNE_STATUS_OK) goto fail;
    status = copy_list(&copy.program_args, &copy.program_arg_count, source->program_args,
                       source->program_arg_count);
    if (status != AXYNE_STATUS_OK) goto fail;
    if (source->env_count != 0) {
        copy.env = (AxyneEnvironmentVariable *)calloc(source->env_count, sizeof(*copy.env));
        if (copy.env == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto fail; }
        for (size_t i = 0; i < source->env_count; ++i) {
            copy.env[i].name = dup_text(source->env[i].name);
            copy.env[i].value = dup_text(source->env[i].value);
            copy.env_count = i + 1;
            if (copy.env[i].name == NULL || copy.env[i].value == NULL) {
                status = AXYNE_STATUS_OUT_OF_MEMORY;
                goto fail;
            }
        }
    }
    *destination = copy;
    return AXYNE_STATUS_OK;
fail:
    axyne_run_configuration_clear(&copy);
    return status;
}

static AxyneStatus read_string_member(const AxyneSettings *document, const char *base,
                                      const char *name, char **out, AxyneError *error)
{
    char *pointer = axyne_settings_pointer_join(base, name);
    AxyneSettingsType type;
    AxyneStatus status;
    if (pointer == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    if (axyne_settings_get_type(document, pointer, &type, NULL) != AXYNE_STATUS_OK) {
        axyne_settings_free_json(pointer);
        *out = dup_text("");
        return *out != NULL ? AXYNE_STATUS_OK : AXYNE_STATUS_OUT_OF_MEMORY;
    }
    status = axyne_settings_get_string(document, pointer, out, error);
    axyne_settings_free_json(pointer);
    if (status == AXYNE_STATUS_OK) {
        /* Re-own with malloc/free semantics of this module. */
        char *owned = dup_text(*out);
        axyne_settings_free_json(*out);
        *out = owned;
        if (owned == NULL) status = AXYNE_STATUS_OUT_OF_MEMORY;
    } else {
        *out = NULL;
    }
    return status;
}

static AxyneStatus read_string_array(const AxyneSettings *document, const char *pointer,
                                     char ***items, size_t *count, size_t limit,
                                     AxyneError *error)
{
    AxyneSettingsType type;
    size_t total = 0;
    AxyneStatus status;
    *items = NULL;
    *count = 0;
    if (axyne_settings_get_type(document, pointer, &type, NULL) != AXYNE_STATUS_OK)
        return AXYNE_STATUS_OK;
    if (type != AXYNE_SETTINGS_TYPE_ARRAY)
        return state_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "expected an array of strings");
    status = axyne_settings_get_count(document, pointer, &total, error);
    if (status != AXYNE_STATUS_OK || total == 0) return status;
    if (limit != 0 && total > limit) total = limit;
    *items = (char **)calloc(total, sizeof(char *));
    if (*items == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    for (size_t i = 0; i < total; ++i) {
        char index[24];
        char *item_pointer, *text = NULL;
        (void)snprintf(index, sizeof(index), "%u", (unsigned)i);
        item_pointer = axyne_settings_pointer_join(pointer, index);
        if (item_pointer == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        status = axyne_settings_get_string(document, item_pointer, &text, error);
        axyne_settings_free_json(item_pointer);
        if (status != AXYNE_STATUS_OK)
            return state_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "expected an array of strings");
        (*items)[i] = dup_text(text);
        axyne_settings_free_json(text);
        if ((*items)[i] == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        *count = i + 1;
    }
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_run_configuration_read(const AxyneSettings *document,
                                         const char *json_pointer,
                                         AxyneRunConfiguration *run, AxyneError *error)
{
    AxyneSettingsType type;
    AxyneStatus status;
    char *pointer = NULL;
    if (document == NULL || json_pointer == NULL || run == NULL)
        return AXYNE_STATUS_INVALID_ARGUMENT;
    axyne_run_configuration_clear(run);
    status = axyne_settings_get_type(document, json_pointer, &type, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (type != AXYNE_SETTINGS_TYPE_OBJECT)
        return state_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "run configuration must be an object");
    status = read_string_member(document, json_pointer, "runner", &run->runner, error);
    if (status == AXYNE_STATUS_OK)
        status = read_string_member(document, json_pointer, "cwd", &run->cwd, error);
    if (status == AXYNE_STATUS_OK) {
        pointer = axyne_settings_pointer_join(json_pointer, "args");
        status = pointer != NULL ? read_string_array(document, pointer, &run->args, &run->arg_count, 0, error)
                                 : AXYNE_STATUS_OUT_OF_MEMORY;
        axyne_settings_free_json(pointer);
    }
    if (status == AXYNE_STATUS_OK) {
        pointer = axyne_settings_pointer_join(json_pointer, "programArgs");
        status = pointer != NULL ? read_string_array(document, pointer, &run->program_args,
                                                     &run->program_arg_count, 0, error)
                                 : AXYNE_STATUS_OUT_OF_MEMORY;
        axyne_settings_free_json(pointer);
    }
    if (status == AXYNE_STATUS_OK) {
        size_t members = 0;
        pointer = axyne_settings_pointer_join(json_pointer, "env");
        if (pointer == NULL) status = AXYNE_STATUS_OUT_OF_MEMORY;
        else if (axyne_settings_get_type(document, pointer, &type, NULL) == AXYNE_STATUS_OK) {
            if (type != AXYNE_SETTINGS_TYPE_OBJECT)
                status = state_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "env must be an object");
            else
                status = axyne_settings_get_count(document, pointer, &members, error);
            if (status == AXYNE_STATUS_OK && members != 0) {
                run->env = (AxyneEnvironmentVariable *)calloc(members, sizeof(*run->env));
                if (run->env == NULL) status = AXYNE_STATUS_OUT_OF_MEMORY;
            }
            for (size_t i = 0; status == AXYNE_STATUS_OK && i < members; ++i) {
                char *name = NULL;
                status = axyne_settings_get_key(document, pointer, i, &name, error);
                if (status != AXYNE_STATUS_OK) break;
                run->env[i].name = dup_text(name);
                run->env_count = i + 1;
                status = run->env[i].name != NULL
                    ? read_string_member(document, pointer, name, &run->env[i].value, error)
                    : AXYNE_STATUS_OUT_OF_MEMORY;
                axyne_settings_free_json(name);
                if (status == AXYNE_STATUS_OK && run->env[i].value == NULL)
                    status = AXYNE_STATUS_OUT_OF_MEMORY;
            }
        }
        axyne_settings_free_json(pointer);
    }
    if (status != AXYNE_STATUS_OK) axyne_run_configuration_clear(run);
    return status;
}

static AxyneStatus write_string_array(AxyneSettings *document, const char *pointer,
                                      char *const *items, size_t count, AxyneError *error)
{
    AxyneStatus status = axyne_settings_set_json(document, pointer, "[]", error);
    for (size_t i = 0; status == AXYNE_STATUS_OK && i < count; ++i) {
        char index[24];
        char *item;
        (void)snprintf(index, sizeof(index), "%u", (unsigned)i);
        status = axyne_settings_append_json(document, pointer, "\"\"", error);
        if (status != AXYNE_STATUS_OK) break;
        item = axyne_settings_pointer_join(pointer, index);
        if (item == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        status = axyne_settings_set_string(document, item, items[i] != NULL ? items[i] : "", error);
        axyne_settings_free_json(item);
    }
    return status;
}

AxyneStatus axyne_run_configuration_write(AxyneSettings *document, const char *json_pointer,
                                          const AxyneRunConfiguration *run, AxyneError *error)
{
    static const char *const names[] = {"runner", "cwd", "args", "programArgs", "env"};
    char *pointers[5] = {NULL, NULL, NULL, NULL, NULL};
    AxyneStatus status;
    if (document == NULL || json_pointer == NULL || run == NULL)
        return AXYNE_STATUS_INVALID_ARGUMENT;
    status = axyne_settings_set_json(document, json_pointer, "{}", error);
    for (size_t i = 0; status == AXYNE_STATUS_OK && i < 5; ++i) {
        pointers[i] = axyne_settings_pointer_join(json_pointer, names[i]);
        if (pointers[i] == NULL) status = AXYNE_STATUS_OUT_OF_MEMORY;
    }
    if (status == AXYNE_STATUS_OK)
        status = axyne_settings_set_string(document, pointers[0], run->runner != NULL ? run->runner : "", error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_settings_set_string(document, pointers[1], run->cwd != NULL ? run->cwd : "", error);
    if (status == AXYNE_STATUS_OK)
        status = write_string_array(document, pointers[2], run->args, run->arg_count, error);
    if (status == AXYNE_STATUS_OK)
        status = write_string_array(document, pointers[3], run->program_args,
                                    run->program_arg_count, error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_settings_set_json(document, pointers[4], "{}", error);
    for (size_t i = 0; status == AXYNE_STATUS_OK && i < run->env_count; ++i) {
        char *member;
        if (run->env[i].name == NULL || run->env[i].name[0] == '\0') continue;
        member = axyne_settings_pointer_join(pointers[4], run->env[i].name);
        if (member == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; break; }
        status = axyne_settings_set_string(document, member,
                                           run->env[i].value != NULL ? run->env[i].value : "", error);
        axyne_settings_free_json(member);
    }
    for (size_t i = 0; i < 5; ++i) axyne_settings_free_json(pointers[i]);
    return status;
}

/* ---- application state -------------------------------------------------- */

void axyne_app_state_init(AxyneAppState *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

void axyne_app_state_clear(AxyneAppState *state)
{
    if (state == NULL) return;
    free_list(state->recent_files, state->recent_file_count);
    free_list(state->recent_folders, state->recent_folder_count);
    free(state->last_seen_version);
    axyne_run_configuration_clear(&state->run);
    memset(state, 0, sizeof(*state));
}

static int same_path(const char *a, const char *b)
{
#ifdef _WIN32
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x == '/') x = '\\';
        if (y == '/') y = '\\';
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
        if (x == '\0') return 1;
    }
#else
    return strcmp(a, b) == 0;
#endif
}

static int remove_from(char **items, size_t *count, const char *path)
{
    for (size_t i = 0; i < *count; ++i) {
        if (!same_path(items[i], path)) continue;
        free(items[i]);
        memmove(&items[i], &items[i + 1], (*count - i - 1) * sizeof(items[0]));
        --*count;
        items[*count] = NULL;
        return 1;
    }
    return 0;
}

static AxyneStatus push_front(char **items, size_t *count, const char *path)
{
    char *copy;
    if (path == NULL || path[0] == '\0') return AXYNE_STATUS_INVALID_ARGUMENT;
    copy = dup_text(path);
    if (copy == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    (void)remove_from(items, count, path);
    if (*count == AXYNE_APP_STATE_RECENT_MAX) {
        free(items[*count - 1]);
        --*count;
    }
    memmove(&items[1], &items[0], *count * sizeof(items[0]));
    items[0] = copy;
    ++*count;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_app_state_add_recent_file(AxyneAppState *state, const char *utf8_path)
{
    if (state == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    return push_front(state->recent_files, &state->recent_file_count, utf8_path);
}

AxyneStatus axyne_app_state_add_recent_folder(AxyneAppState *state, const char *utf8_path)
{
    if (state == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    return push_front(state->recent_folders, &state->recent_folder_count, utf8_path);
}

int axyne_app_state_remove_recent_file(AxyneAppState *state, const char *utf8_path)
{
    return state != NULL && utf8_path != NULL &&
           remove_from(state->recent_files, &state->recent_file_count, utf8_path);
}

int axyne_app_state_remove_recent_folder(AxyneAppState *state, const char *utf8_path)
{
    return state != NULL && utf8_path != NULL &&
           remove_from(state->recent_folders, &state->recent_folder_count, utf8_path);
}

void axyne_app_state_clear_recent(AxyneAppState *state)
{
    if (state == NULL) return;
    free_list(state->recent_files, state->recent_file_count);
    free_list(state->recent_folders, state->recent_folder_count);
    memset(state->recent_files, 0, sizeof(state->recent_files));
    memset(state->recent_folders, 0, sizeof(state->recent_folders));
    state->recent_file_count = 0;
    state->recent_folder_count = 0;
}

AxyneStatus axyne_app_state_set_last_seen_version(AxyneAppState *state, const char *version)
{
    char *copy;
    if (state == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    copy = dup_text(version);
    if (copy == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    free(state->last_seen_version);
    state->last_seen_version = copy;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_app_state_set_run_configuration(AxyneAppState *state,
                                                  const AxyneRunConfiguration *run)
{
    AxyneRunConfiguration copy;
    AxyneStatus status;
    if (state == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    if (run == NULL) {
        axyne_run_configuration_clear(&state->run);
        state->has_run_configuration = 0;
        return AXYNE_STATUS_OK;
    }
    status = axyne_run_configuration_copy(&copy, run);
    if (status != AXYNE_STATUS_OK) return status;
    axyne_run_configuration_clear(&state->run);
    state->run = copy;
    state->has_run_configuration = 1;
    return AXYNE_STATUS_OK;
}

static AxyneStatus load_recent(const AxyneSettings *document, const char *pointer,
                               char **items, size_t *count, AxyneError *error)
{
    char **loaded = NULL;
    size_t loaded_count = 0;
    AxyneStatus status = read_string_array(document, pointer, &loaded, &loaded_count, 0, error);
    /* Oldest first so that push_front leaves the file order intact. */
    for (size_t i = loaded_count; status == AXYNE_STATUS_OK && i > 0; --i)
        if (loaded[i - 1][0] != '\0') status = push_front(items, count, loaded[i - 1]);
    free_list(loaded, loaded_count);
    free(loaded);
    return status;
}

AxyneStatus axyne_app_state_load(const char *utf8_path, AxyneAppState *state, AxyneError *error)
{
    AxyneSettings *document = NULL;
    AxyneSettingsType type;
    AxyneStatus status;
    if (utf8_path == NULL || state == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    axyne_app_state_clear(state);
    status = axyne_settings_load(utf8_path, &document, error);
    if (status == AXYNE_STATUS_NOT_FOUND) {
        if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
        return AXYNE_STATUS_OK;
    }
    if (status != AXYNE_STATUS_OK) return status;
    if (axyne_settings_get_type(document, "", &type, NULL) != AXYNE_STATUS_OK ||
        type != AXYNE_SETTINGS_TYPE_OBJECT)
        status = state_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "state.json must hold an object");
    if (status == AXYNE_STATUS_OK)
        status = load_recent(document, "/recentFiles", state->recent_files,
                             &state->recent_file_count, error);
    if (status == AXYNE_STATUS_OK)
        status = load_recent(document, "/recentFolders", state->recent_folders,
                             &state->recent_folder_count, error);
    if (status == AXYNE_STATUS_OK)
        status = read_string_member(document, "", "lastSeenVersion", &state->last_seen_version, error);
    if (status == AXYNE_STATUS_OK &&
        axyne_settings_get_type(document, "/releaseBannerSuppressed", &type, NULL) == AXYNE_STATUS_OK) {
        char *json = NULL;
        if (type != AXYNE_SETTINGS_TYPE_BOOL) {
            status = state_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "releaseBannerSuppressed must be a boolean");
        } else if (axyne_settings_get_json(document, "/releaseBannerSuppressed", &json, error) == AXYNE_STATUS_OK) {
            state->release_banner_suppressed = strcmp(json, "true") == 0;
            axyne_settings_free_json(json);
        }
    }
    if (status == AXYNE_STATUS_OK &&
        axyne_settings_get_type(document, "/runConfiguration", &type, NULL) == AXYNE_STATUS_OK) {
        status = axyne_run_configuration_read(document, "/runConfiguration", &state->run, error);
        state->has_run_configuration = status == AXYNE_STATUS_OK;
    }
    axyne_settings_destroy(document);
    if (status != AXYNE_STATUS_OK) axyne_app_state_clear(state);
    return status;
}

static AxyneStatus ensure_parent(const char *utf8_path, AxyneError *error)
{
    const char *slash = strrchr(utf8_path, '/');
    const char *backslash = strrchr(utf8_path, '\\');
    AxyneDirectoryList list = {0};
    AxyneStatus status;
    size_t length;
    char *parent;
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash == NULL || slash == utf8_path) return AXYNE_STATUS_OK;
    length = (size_t)(slash - utf8_path);
    parent = (char *)malloc(length + 1);
    if (parent == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    memcpy(parent, utf8_path, length);
    parent[length] = '\0';
    status = axyne_fs_list_directory(parent, &list, NULL);
    if (status == AXYNE_STATUS_OK) axyne_fs_free_directory_list(&list);
    else status = axyne_fs_create_directory(parent, error);
    free(parent);
    return status;
}

AxyneStatus axyne_app_state_save(const AxyneAppState *state, const char *utf8_path,
                                 AxyneError *error)
{
    AxyneSettings *document = NULL;
    AxyneSettingsType type;
    AxyneStatus status;
    if (state == NULL || utf8_path == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    if (axyne_settings_load(utf8_path, &document, NULL) != AXYNE_STATUS_OK ||
        axyne_settings_get_type(document, "", &type, NULL) != AXYNE_STATUS_OK ||
        type != AXYNE_SETTINGS_TYPE_OBJECT) {
        axyne_settings_destroy(document);
        document = NULL;
        status = axyne_settings_create(&document, error);
        if (status != AXYNE_STATUS_OK) return status;
    }
    status = axyne_settings_set_json(document, "/version", "1", error);
    if (status == AXYNE_STATUS_OK)
        status = write_string_array(document, "/recentFiles", state->recent_files,
                                    state->recent_file_count, error);
    if (status == AXYNE_STATUS_OK)
        status = write_string_array(document, "/recentFolders", state->recent_folders,
                                    state->recent_folder_count, error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_settings_set_string(document, "/lastSeenVersion",
                                           state->last_seen_version != NULL ? state->last_seen_version : "",
                                           error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_settings_set_json(document, "/releaseBannerSuppressed",
                                         state->release_banner_suppressed ? "true" : "false", error);
    if (status == AXYNE_STATUS_OK) {
        if (state->has_run_configuration) {
            status = axyne_run_configuration_write(document, "/runConfiguration", &state->run, error);
        } else {
            AxyneStatus removed = axyne_settings_remove(document, "/runConfiguration", NULL);
            if (removed != AXYNE_STATUS_OK && removed != AXYNE_STATUS_NOT_FOUND) status = removed;
        }
    }
    if (status == AXYNE_STATUS_OK) status = ensure_parent(utf8_path, error);
    if (status == AXYNE_STATUS_OK) status = axyne_settings_save(document, utf8_path, error);
    axyne_settings_destroy(document);
    return status;
}
