#define _POSIX_C_SOURCE 200809L
#include "axyne/plugins.h"
#include "axyne/process.h"
#include "axyne/settings.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define AXYNE_PLUGIN_SEP '\\'
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#define AXYNE_PLUGIN_SEP '/'
#endif

/* ---- small helpers ------------------------------------------------------- */

static AxyneStatus plugin_error(AxyneError *error, AxyneStatus status, const char *format, ...)
{
    va_list arguments;
    if (error != NULL) {
        error->code = status;
        va_start(arguments, format);
        (void)vsnprintf(error->message, sizeof(error->message), format, arguments);
        va_end(arguments);
    }
    return status;
}

static void plugin_ok(AxyneError *error)
{
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
}

static char *copy_text(const char *value)
{
    size_t length = strlen(value != NULL ? value : "") + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) memcpy(copy, value != NULL ? value : "", length);
    return copy;
}

static char *join_path(const char *directory, const char *name)
{
    size_t a = strlen(directory), b = strlen(name);
    char *path = (char *)malloc(a + b + 2);
    if (path == NULL) return NULL;
    memcpy(path, directory, a);
    if (a > 0 && directory[a - 1] != '/' && directory[a - 1] != '\\') path[a++] = AXYNE_PLUGIN_SEP;
    memcpy(path + a, name, b + 1);
    return path;
}

static void free_strings(char **items, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(items[i]);
    free(items);
}

const char *axyne_plugin_platform(void)
{
#ifdef _WIN32
    return "windows";
#else
    return "macos";
#endif
}

static int valid_id(const char *id)
{
    size_t i, length = strlen(id);
    if (length < 2 || length > AXYNE_PLUGIN_ID_MAX) return 0;
    if (!(islower((unsigned char)id[0]) || isdigit((unsigned char)id[0]))) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!(islower(c) || isdigit(c) || c == '-' || c == '.')) return 0;
    }
    return strcmp(id, "..") != 0;
}

static int valid_version(const char *version)
{
    int parts = 0;
    const char *p = version;
    while (parts < 3) {
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) ++p;
        ++parts;
        if (parts < 3) { if (*p != '.') return 0; ++p; }
    }
    return *p == '\0' || *p == '-' || *p == '+';
}

int axyne_plugin_version_compare(const char *left, const char *right)
{
    int i;
    const char *a = left != NULL ? left : "", *b = right != NULL ? right : "";
    for (i = 0; i < 3; ++i) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') ++a;
        if (*b == '.') ++b;
    }
    return 0;
}

/* ---- file system ---------------------------------------------------------- */

#ifdef _WIN32
static wchar_t *wide_path(const char *utf8)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    wchar_t *wide;
    if (count <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide != NULL) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, count);
    return wide;
}

static char *utf8_path(const wchar_t *wide)
{
    int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *utf8;
    if (count <= 0) return NULL;
    utf8 = (char *)malloc((size_t)count);
    if (utf8 != NULL) WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, count, NULL, NULL);
    return utf8;
}
#endif

static int is_directory(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    DWORD attributes = wide != NULL ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int is_file(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    DWORD attributes = wide != NULL ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

static int make_directories(const char *path)
{
    char *copy = copy_text(path);
    size_t i;
    int ok = 1;
    if (copy == NULL) return 0;
    for (i = 1; copy[i] != '\0' && ok; ++i) {
        if (copy[i] == '/' || copy[i] == '\\') {
            char saved = copy[i];
            if (i == 2 && copy[1] == ':') continue; /* drive root */
            copy[i] = '\0';
            if (!is_directory(copy)) {
#ifdef _WIN32
                wchar_t *wide = wide_path(copy);
                ok = wide != NULL && (CreateDirectoryW(wide, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
                free(wide);
#else
                ok = mkdir(copy, 0755) == 0 || is_directory(copy);
#endif
            }
            copy[i] = saved;
        }
    }
    if (ok && !is_directory(copy)) {
#ifdef _WIN32
        wchar_t *wide = wide_path(copy);
        ok = wide != NULL && CreateDirectoryW(wide, NULL);
        free(wide);
#else
        ok = mkdir(copy, 0755) == 0;
#endif
    }
    free(copy);
    return ok;
}

/* Calls `visit` with each entry name of `directory` (not "." / ".."). */
static int list_directory(const char *directory, int (*visit)(const char *name, int is_dir, void *user),
                          void *user)
{
#ifdef _WIN32
    WIN32_FIND_DATAW entry;
    char *pattern = join_path(directory, "*");
    wchar_t *wide = pattern != NULL ? wide_path(pattern) : NULL;
    HANDLE find;
    int ok = 1;
    free(pattern);
    if (wide == NULL) return 0;
    find = FindFirstFileW(wide, &entry);
    free(wide);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        char *name;
        if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0) continue;
        name = utf8_path(entry.cFileName);
        if (name == NULL) continue;
        /* Never follow junctions/symlinks out of the plugin folder. */
        if (!visit(name, (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                         (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, user))
            ok = 0;
        free(name);
    } while (ok && FindNextFileW(find, &entry));
    FindClose(find);
    return ok;
#else
    DIR *dir = opendir(directory);
    struct dirent *entry;
    int ok = 1;
    if (dir == NULL) return 0;
    while (ok && (entry = readdir(dir)) != NULL) {
        char *path;
        struct stat info;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        path = join_path(directory, entry->d_name);
        if (path == NULL) { ok = 0; break; }
        if (!visit(entry->d_name, lstat(path, &info) == 0 && S_ISDIR(info.st_mode), user)) ok = 0;
        free(path);
    }
    closedir(dir);
    return ok;
#endif
}

typedef struct RemoveContext { const char *directory; int ok; } RemoveContext;
static int remove_tree(const char *path);

static int remove_entry(const char *name, int is_dir, void *user)
{
    RemoveContext *context = (RemoveContext *)user;
    char *path = join_path(context->directory, name);
    if (path == NULL) { context->ok = 0; return 0; }
    if (is_dir) {
        if (!remove_tree(path)) context->ok = 0;
    } else {
#ifdef _WIN32
        wchar_t *wide = wide_path(path);
        if (wide == NULL) context->ok = 0;
        else {
            SetFileAttributesW(wide, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileW(wide) && !RemoveDirectoryW(wide)) context->ok = 0; /* reparse dirs */
        }
        free(wide);
#else
        if (unlink(path) != 0) context->ok = 0;
#endif
    }
    free(path);
    return 1;
}

static int remove_tree(const char *path)
{
    RemoveContext context;
    context.directory = path;
    context.ok = 1;
    if (!is_directory(path)) return 1;
    if (!list_directory(path, remove_entry, &context)) return 0;
#ifdef _WIN32
    {
        wchar_t *wide = wide_path(path);
        if (wide == NULL || !RemoveDirectoryW(wide)) context.ok = 0;
        free(wide);
    }
#else
    if (rmdir(path) != 0) context.ok = 0;
#endif
    return context.ok;
}

static int rename_path(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t *a = wide_path(from), *b = wide_path(to);
    int ok = a != NULL && b != NULL && MoveFileExW(a, b, 0);
    free(a); free(b);
    return ok;
#else
    return rename(from, to) == 0;
#endif
}

static char *read_file(const char *path, size_t *length)
{
    FILE *file;
    char *buffer = NULL;
    long size;
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    file = wide != NULL ? _wfopen(wide, L"rb") : NULL;
    free(wide);
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) >= 0 && size < 1024 * 1024 &&
        fseek(file, 0, SEEK_SET) == 0) {
        buffer = (char *)malloc((size_t)size + 1);
        if (buffer != NULL && fread(buffer, 1, (size_t)size, file) == (size_t)size) {
            buffer[size] = '\0';
            *length = (size_t)size;
        } else {
            free(buffer);
            buffer = NULL;
        }
    }
    fclose(file);
    return buffer;
}

/* ---- manifest -------------------------------------------------------------- */

static char *json_string(const AxyneSettings *json, const char *pointer)
{
    char *value = NULL, *copy;
    if (axyne_settings_get_string(json, pointer, &value, NULL) != AXYNE_STATUS_OK) return copy_text("");
    copy = copy_text(value);
    axyne_settings_free_json(value);
    return copy;
}

/* String array at `pointer`; non-string items are skipped. */
static char **json_strings(const AxyneSettings *json, const char *pointer, size_t *count, int lower)
{
    size_t total = 0, i;
    char **items;
    *count = 0;
    if (axyne_settings_get_count(json, pointer, &total, NULL) != AXYNE_STATUS_OK || total == 0)
        return NULL;
    items = (char **)calloc(total, sizeof(char *));
    if (items == NULL) return NULL;
    for (i = 0; i < total; ++i) {
        char index[24], *item_pointer, *value = NULL;
        (void)snprintf(index, sizeof(index), "%zu", i);
        item_pointer = axyne_settings_pointer_join(pointer, index);
        if (item_pointer != NULL &&
            axyne_settings_get_string(json, item_pointer, &value, NULL) == AXYNE_STATUS_OK) {
            char *copy = copy_text(value);
            size_t k;
            if (copy != NULL && lower)
                for (k = 0; copy[k] != '\0'; ++k) copy[k] = (char)tolower((unsigned char)copy[k]);
            if (copy != NULL) items[(*count)++] = copy;
            axyne_settings_free_json(value);
        }
        axyne_settings_free_json(item_pointer);
    }
    return items;
}

static char *member_pointer(const char *array, size_t index, const char *member)
{
    char buffer[160];
    (void)snprintf(buffer, sizeof(buffer), "%s/%zu/%s", array, index, member);
    return copy_text(buffer);
}

void axyne_plugin_free(AxynePlugin *plugin)
{
    size_t i;
    if (plugin == NULL) return;
    free(plugin->id); free(plugin->name); free(plugin->version); free(plugin->description);
    free(plugin->author); free(plugin->category); free(plugin->license);
    free(plugin->min_ide_version); free(plugin->max_ide_version); free(plugin->directory);
    free_strings(plugin->platforms, plugin->platform_count);
    for (i = 0; i < plugin->command_count; ++i) {
        AxynePluginCommand *command = &plugin->commands[i];
        free(command->id); free(command->title); free(command->program); free(command->cwd);
        free_strings(command->args, command->arg_count);
        free_strings(command->extensions, command->extension_count);
    }
    free(plugin->commands);
    for (i = 0; i < plugin->requirement_count; ++i) {
        free(plugin->requirements[i].program);
        free(plugin->requirements[i].hint);
    }
    free(plugin->requirements);
    free(plugin);
}

static int plugin_compatible(const AxynePlugin *plugin, const char *ide_version)
{
    size_t i;
    int platform_ok = plugin->platform_count == 0;
    for (i = 0; i < plugin->platform_count; ++i)
        if (strcmp(plugin->platforms[i], axyne_plugin_platform()) == 0) platform_ok = 1;
    if (!platform_ok) return 0;
    if (ide_version != NULL && ide_version[0] != '\0') {
        if (plugin->min_ide_version[0] != '\0' &&
            axyne_plugin_version_compare(ide_version, plugin->min_ide_version) < 0) return 0;
        if (plugin->max_ide_version[0] != '\0' &&
            axyne_plugin_version_compare(ide_version, plugin->max_ide_version) > 0) return 0;
    }
    return 1;
}

AxynePlugin *axyne_plugin_parse(const char *json_text, size_t length, const char *directory,
                                const char *ide_version, AxyneError *error)
{
    AxyneSettings *json = NULL;
    AxynePlugin *plugin;
    char *text;
    size_t count = 0, i;
    if (json_text == NULL) { plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "manifest is empty"); return NULL; }
    text = (char *)malloc(length + 1);
    if (text == NULL) { plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); return NULL; }
    memcpy(text, json_text, length);
    text[length] = '\0';
    if (axyne_settings_create(&json, NULL) != AXYNE_STATUS_OK ||
        axyne_settings_set_json(json, "", text, NULL) != AXYNE_STATUS_OK) {
        free(text);
        axyne_settings_destroy(json);
        plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "manifest.json is not valid JSON");
        return NULL;
    }
    free(text);
    plugin = (AxynePlugin *)calloc(1, sizeof(*plugin));
    if (plugin == NULL) { axyne_settings_destroy(json); plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); return NULL; }
    plugin->id = json_string(json, "/id");
    plugin->name = json_string(json, "/name");
    plugin->version = json_string(json, "/version");
    plugin->description = json_string(json, "/description");
    plugin->author = json_string(json, "/author");
    plugin->category = json_string(json, "/category");
    plugin->license = json_string(json, "/license");
    plugin->min_ide_version = json_string(json, "/minIdeVersion");
    plugin->max_ide_version = json_string(json, "/maxIdeVersion");
    plugin->directory = copy_text(directory);
    plugin->platforms = json_strings(json, "/platforms", &plugin->platform_count, 1);
    plugin->enabled = 1;
    if (plugin->id == NULL || plugin->name == NULL || plugin->version == NULL ||
        plugin->directory == NULL) {
        axyne_plugin_free(plugin); axyne_settings_destroy(json);
        plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
        return NULL;
    }
    if (!valid_id(plugin->id) || plugin->name[0] == '\0' || !valid_version(plugin->version)) {
        axyne_plugin_free(plugin); axyne_settings_destroy(json);
        plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                     "manifest.json needs a lower-case id, a name and a MAJOR.MINOR.PATCH version");
        return NULL;
    }

    if (axyne_settings_get_count(json, "/requires", &count, NULL) == AXYNE_STATUS_OK && count > 0) {
        plugin->requirements = (AxynePluginRequirement *)calloc(count, sizeof(AxynePluginRequirement));
        for (i = 0; plugin->requirements != NULL && i < count; ++i) {
            char *program = member_pointer("/requires", i, "program");
            char *hint = member_pointer("/requires", i, "hint");
            AxynePluginRequirement *item = &plugin->requirements[plugin->requirement_count];
            item->program = program != NULL ? json_string(json, program) : NULL;
            item->hint = hint != NULL ? json_string(json, hint) : NULL;
            free(program); free(hint);
            if (item->program != NULL && item->program[0] != '\0' && item->hint != NULL)
                ++plugin->requirement_count;
            else { free(item->program); free(item->hint); item->program = item->hint = NULL; }
        }
    }
    count = 0;
    if (axyne_settings_get_count(json, "/commands", &count, NULL) == AXYNE_STATUS_OK && count > 0) {
        plugin->commands = (AxynePluginCommand *)calloc(count, sizeof(AxynePluginCommand));
        for (i = 0; plugin->commands != NULL && i < count; ++i) {
            AxynePluginCommand *command = &plugin->commands[plugin->command_count];
            char *id = member_pointer("/commands", i, "id");
            char *title = member_pointer("/commands", i, "title");
            char *program = member_pointer("/commands", i, "program");
            char *args = member_pointer("/commands", i, "args");
            char *cwd = member_pointer("/commands", i, "cwd");
            char *extensions = member_pointer("/commands", i, "extensions");
            if (id != NULL && title != NULL && program != NULL && args != NULL && cwd != NULL &&
                extensions != NULL) {
                command->id = json_string(json, id);
                command->title = json_string(json, title);
                command->program = json_string(json, program);
                if (command->program != NULL && command->program[0] == '\0') {
                    /* {"windows": "...", "macos": "..."}: per-platform program */
                    char *platform = axyne_settings_pointer_join(program, axyne_plugin_platform());
                    free(command->program);
                    command->program = platform != NULL ? json_string(json, platform) : NULL;
                    axyne_settings_free_json(platform);
                }
                command->cwd = json_string(json, cwd);
                command->args = json_strings(json, args, &command->arg_count, 0);
                command->extensions = json_strings(json, extensions, &command->extension_count, 1);
            }
            free(id); free(title); free(program); free(args); free(cwd); free(extensions);
            if (command->id != NULL && command->title != NULL && command->program != NULL &&
                command->cwd != NULL && command->title[0] != '\0' && command->program[0] != '\0') {
                ++plugin->command_count;
            } else {
                free(command->id); free(command->title); free(command->program); free(command->cwd);
                free_strings(command->args, command->arg_count);
                free_strings(command->extensions, command->extension_count);
                memset(command, 0, sizeof(*command));
            }
        }
    }
    axyne_settings_destroy(json);
    plugin->compatible = plugin_compatible(plugin, ide_version);
    plugin_ok(error);
    return plugin;
}

/* ---- set ------------------------------------------------------------------- */

typedef struct ScanContext {
    AxynePluginSet *set;
    const char *ide_version;
    size_t capacity;
} ScanContext;

static AxynePlugin *load_plugin_dir(const char *directory, const char *ide_version, AxyneError *error)
{
    char *manifest_path = join_path(directory, "manifest.json");
    size_t length = 0;
    char *text = manifest_path != NULL ? read_file(manifest_path, &length) : NULL;
    AxynePlugin *plugin;
    free(manifest_path);
    if (text == NULL) {
        plugin_error(error, AXYNE_STATUS_NOT_FOUND, "manifest.json is missing");
        return NULL;
    }
    plugin = axyne_plugin_parse(text, length, directory, ide_version, error);
    free(text);
    return plugin;
}

static int append_plugin(AxynePluginSet *set, size_t *capacity, AxynePlugin *plugin)
{
    if (set->count == *capacity) {
        size_t next = *capacity == 0 ? 8 : *capacity * 2;
        AxynePlugin *grown = (AxynePlugin *)realloc(set->items, next * sizeof(AxynePlugin));
        if (grown == NULL) return 0;
        set->items = grown;
        *capacity = next;
    }
    set->items[set->count++] = *plugin;
    free(plugin); /* contents moved into the array */
    return 1;
}

static int scan_entry(const char *name, int is_dir, void *user)
{
    ScanContext *context = (ScanContext *)user;
    char *directory;
    AxynePlugin *plugin;
    if (!is_dir || name[0] == '.') return 1; /* staging folders start with '.' */
    directory = join_path(context->set->plugins_dir, name);
    if (directory == NULL) return 0;
    plugin = load_plugin_dir(directory, context->ide_version, NULL);
    free(directory);
    /* The folder name must be the id: a renamed copy would shadow updates. */
    if (plugin != NULL && strcmp(plugin->id, name) == 0) {
        if (!append_plugin(context->set, &context->capacity, plugin)) axyne_plugin_free(plugin);
    } else {
        axyne_plugin_free(plugin);
    }
    return 1;
}

static void free_plugin_contents(AxynePlugin *plugin)
{
    AxynePlugin *heap = (AxynePlugin *)malloc(sizeof(*heap));
    if (heap == NULL) return;
    *heap = *plugin;
    axyne_plugin_free(heap);
}

void axyne_plugins_clear(AxynePluginSet *set)
{
    size_t i;
    if (set == NULL) return;
    for (i = 0; i < set->count; ++i) free_plugin_contents(&set->items[i]);
    free(set->items);
    free(set->plugins_dir);
    free(set->state_path);
    memset(set, 0, sizeof(*set));
}

static int compare_names(const void *a, const void *b)
{
    const AxynePlugin *x = (const AxynePlugin *)a, *y = (const AxynePlugin *)b;
    const char *p = x->name, *q = y->name;
    while (*p != '\0' && tolower((unsigned char)*p) == tolower((unsigned char)*q)) { ++p; ++q; }
    return tolower((unsigned char)*p) - tolower((unsigned char)*q);
}

AxynePlugin *axyne_plugins_find(const AxynePluginSet *set, const char *id)
{
    size_t i;
    if (set == NULL || id == NULL) return NULL;
    for (i = 0; i < set->count; ++i)
        if (strcmp(set->items[i].id, id) == 0) return &set->items[i];
    return NULL;
}

static void apply_disabled(AxynePluginSet *set)
{
    AxyneSettings *state = NULL;
    size_t count = 0, i;
    char **disabled;
    if (set->state_path == NULL || axyne_settings_load(set->state_path, &state, NULL) != AXYNE_STATUS_OK)
        return;
    disabled = json_strings(state, "/disabled", &count, 0);
    for (i = 0; i < count; ++i) {
        AxynePlugin *plugin = axyne_plugins_find(set, disabled[i]);
        if (plugin != NULL) plugin->enabled = 0;
    }
    free_strings(disabled, count);
    axyne_settings_destroy(state);
}

AxyneStatus axyne_plugins_load(AxynePluginSet *set, const char *plugins_dir, const char *state_path,
                               const char *ide_version, AxyneError *error)
{
    ScanContext context;
    if (set == NULL || plugins_dir == NULL)
        return plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "plugins folder is required");
    axyne_plugins_clear(set);
    set->plugins_dir = copy_text(plugins_dir);
    set->state_path = state_path != NULL ? copy_text(state_path) : NULL;
    if (set->plugins_dir == NULL) return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    context.set = set;
    context.ide_version = ide_version;
    context.capacity = 0;
    if (is_directory(plugins_dir)) (void)list_directory(plugins_dir, scan_entry, &context);
    if (set->count > 1) qsort(set->items, set->count, sizeof(AxynePlugin), compare_names);
    apply_disabled(set);
    plugin_ok(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_plugins_set_enabled(AxynePluginSet *set, const char *id, int enabled, AxyneError *error)
{
    AxynePlugin *plugin = axyne_plugins_find(set, id);
    AxyneSettings *state = NULL;
    AxyneStatus status;
    size_t i;
    if (plugin == NULL) return plugin_error(error, AXYNE_STATUS_NOT_FOUND, "plugin %s is not installed", id != NULL ? id : "");
    plugin->enabled = enabled != 0;
    if (set->state_path == NULL) { plugin_ok(error); return AXYNE_STATUS_OK; }
    if (axyne_settings_load(set->state_path, &state, NULL) != AXYNE_STATUS_OK &&
        axyne_settings_create(&state, error) != AXYNE_STATUS_OK)
        return error != NULL ? error->code : AXYNE_STATUS_OUT_OF_MEMORY;
    status = axyne_settings_set_json(state, "/disabled", "[]", error);
    for (i = 0; status == AXYNE_STATUS_OK && i < set->count; ++i) {
        if (!set->items[i].enabled) {
            /* ids are validated slugs: no JSON escaping needed */
            char item[AXYNE_PLUGIN_ID_MAX + 3];
            (void)snprintf(item, sizeof(item), "\"%s\"", set->items[i].id);
            status = axyne_settings_append_json(state, "/disabled", item, error);
        }
    }
    if (status == AXYNE_STATUS_OK) status = axyne_settings_save(state, set->state_path, error);
    axyne_settings_destroy(state);
    if (status == AXYNE_STATUS_OK) plugin_ok(error);
    return status;
}

AxyneStatus axyne_plugins_uninstall(AxynePluginSet *set, const char *id, AxyneError *error)
{
    AxynePlugin *plugin = axyne_plugins_find(set, id);
    size_t index;
    if (plugin == NULL) return plugin_error(error, AXYNE_STATUS_NOT_FOUND, "plugin %s is not installed", id != NULL ? id : "");
    if (!remove_tree(plugin->directory))
        return plugin_error(error, AXYNE_STATUS_IO_ERROR, "cannot remove %s (is a plugin process still running?)", plugin->directory);
    index = (size_t)(plugin - set->items);
    free_plugin_contents(plugin);
    memmove(&set->items[index], &set->items[index + 1], (set->count - index - 1) * sizeof(AxynePlugin));
    --set->count;
    plugin_ok(error);
    return AXYNE_STATUS_OK;
}

/* ---- install --------------------------------------------------------------- */

typedef struct ExtractWait {
#ifdef _WIN32
    HANDLE done;
#else
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int finished;
#endif
    int exit_code;
    char output[512];
    size_t length;
} ExtractWait;

static void extract_output(AxyneProcess *process, AxyneProcessStream stream, const char *bytes,
                           size_t length, void *user_data)
{
    ExtractWait *wait = (ExtractWait *)user_data;
    size_t room = sizeof(wait->output) - 1 - wait->length;
    (void)process; (void)stream;
    if (length > room) length = room;
    memcpy(wait->output + wait->length, bytes, length);
    wait->length += length;
    wait->output[wait->length] = '\0';
}

static void extract_exit(AxyneProcess *process, int exit_code, void *user_data)
{
    ExtractWait *wait = (ExtractWait *)user_data;
    (void)process;
    wait->exit_code = exit_code;
#ifdef _WIN32
    SetEvent(wait->done);
#else
    pthread_mutex_lock(&wait->lock);
    wait->finished = 1;
    pthread_cond_broadcast(&wait->changed);
    pthread_mutex_unlock(&wait->lock);
#endif
}

static AxyneStatus extract_zip(const char *zip_path, const char *destination, AxyneError *error)
{
    AxyneProcessSpec spec;
    AxyneProcess *process = NULL;
    ExtractWait wait;
    AxyneStatus status;
    char *program = NULL;
#ifdef _WIN32
    wchar_t system_dir[MAX_PATH];
    UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
    const char *arguments[4];
    if (length == 0 || length + 10 >= MAX_PATH) return plugin_error(error, AXYNE_STATUS_IO_ERROR, "cannot locate tar.exe");
    memcpy(system_dir + length, L"\\tar.exe", 9 * sizeof(wchar_t));
    program = utf8_path(system_dir);
    /* bsdtar reads zip archives and refuses absolute and ".." entries. */
    arguments[0] = "-xf"; arguments[1] = zip_path; arguments[2] = "-C"; arguments[3] = destination;
#else
    const char *arguments[4];
    program = copy_text("/usr/bin/ditto");
    arguments[0] = "-x"; arguments[1] = "-k"; arguments[2] = zip_path; arguments[3] = destination;
#endif
    if (program == NULL) return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    memset(&wait, 0, sizeof(wait));
    wait.exit_code = -1;
#ifdef _WIN32
    wait.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (wait.done == NULL) { free(program); return plugin_error(error, AXYNE_STATUS_IO_ERROR, "cannot wait for extraction"); }
#else
    pthread_mutex_init(&wait.lock, NULL);
    pthread_cond_init(&wait.changed, NULL);
#endif
    memset(&spec, 0, sizeof(spec));
    spec.executable = program;
    spec.arguments = arguments;
    spec.argument_count = 4;
    spec.working_directory = destination;
    spec.on_output = extract_output;
    spec.on_exit = extract_exit;
    spec.user_data = &wait;
    status = axyne_process_start(&spec, &process, error);
    free(program);
    if (status == AXYNE_STATUS_OK) {
#ifdef _WIN32
        WaitForSingleObject(wait.done, INFINITE);
#else
        pthread_mutex_lock(&wait.lock);
        while (!wait.finished) pthread_cond_wait(&wait.changed, &wait.lock);
        pthread_mutex_unlock(&wait.lock);
#endif
        axyne_process_release(process);
        if (wait.exit_code != 0)
            status = plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                  "the package could not be extracted: %s", wait.output);
    }
#ifdef _WIN32
    CloseHandle(wait.done);
#else
    pthread_cond_destroy(&wait.changed);
    pthread_mutex_destroy(&wait.lock);
#endif
    return status;
}

AxyneStatus axyne_plugins_install_zip(AxynePluginSet *set, const char *zip_path, const char *ide_version,
                                      char **installed_id, AxyneError *error)
{
    char staging_name[64];
    char *staging = NULL, *target = NULL, *backup = NULL;
    AxynePlugin *plugin = NULL;
    AxynePlugin *existing;
    AxyneStatus status;
    int was_enabled = 1;
    static unsigned counter;
    if (installed_id != NULL) *installed_id = NULL;
    if (set == NULL || set->plugins_dir == NULL || zip_path == NULL)
        return plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "a loaded plugin set and a package are required");
    if (!is_file(zip_path)) return plugin_error(error, AXYNE_STATUS_NOT_FOUND, "package %s was not found", zip_path);
    if (!make_directories(set->plugins_dir))
        return plugin_error(error, AXYNE_STATUS_IO_ERROR, "cannot create %s", set->plugins_dir);
    (void)snprintf(staging_name, sizeof(staging_name), ".install-%lu-%u",
#ifdef _WIN32
                   (unsigned long)GetCurrentProcessId(),
#else
                   (unsigned long)getpid(),
#endif
                   ++counter);
    staging = join_path(set->plugins_dir, staging_name);
    if (staging == NULL) return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    (void)remove_tree(staging);
    if (!make_directories(staging)) { free(staging); return plugin_error(error, AXYNE_STATUS_IO_ERROR, "cannot create a staging folder"); }

    status = extract_zip(zip_path, staging, error);
    if (status == AXYNE_STATUS_OK) {
        plugin = load_plugin_dir(staging, ide_version, error);
        status = plugin != NULL ? AXYNE_STATUS_OK : (error != NULL ? error->code : AXYNE_STATUS_INVALID_ARGUMENT);
    }
    if (status == AXYNE_STATUS_OK && !plugin->compatible)
        status = plugin_error(error, AXYNE_STATUS_UNSUPPORTED,
                              "%s %s does not support this platform or IDE version", plugin->name, plugin->version);
    if (status == AXYNE_STATUS_OK) {
        target = join_path(set->plugins_dir, plugin->id);
        backup = join_path(set->plugins_dir, ".previous");
        if (target == NULL || backup == NULL) status = plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    }
    if (status == AXYNE_STATUS_OK) {
        existing = axyne_plugins_find(set, plugin->id);
        if (existing != NULL) was_enabled = existing->enabled;
        (void)remove_tree(backup);
        /* Keep the old copy until the new one is in place. */
        if (is_directory(target) && !rename_path(target, backup))
            status = plugin_error(error, AXYNE_STATUS_IO_ERROR,
                                  "cannot replace %s (is a plugin process still running?)", target);
        else if (!rename_path(staging, target)) {
            (void)rename_path(backup, target);
            status = plugin_error(error, AXYNE_STATUS_IO_ERROR, "cannot move the plugin into %s", target);
        } else {
            (void)remove_tree(backup);
        }
    }
    (void)remove_tree(staging);
    if (status == AXYNE_STATUS_OK) {
        char *id = copy_text(plugin->id);
        char *plugins_dir = copy_text(set->plugins_dir);
        char *state_path = set->state_path != NULL ? copy_text(set->state_path) : NULL;
        if (id == NULL || plugins_dir == NULL) {
            status = plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
        } else {
            status = axyne_plugins_load(set, plugins_dir, state_path, ide_version, error);
            if (status == AXYNE_STATUS_OK && !was_enabled)
                status = axyne_plugins_set_enabled(set, id, 0, error);
        }
        free(plugins_dir);
        free(state_path);
        if (status == AXYNE_STATUS_OK && installed_id != NULL) *installed_id = id;
        else free(id);
    }
    axyne_plugin_free(plugin);
    free(staging); free(target); free(backup);
    if (status == AXYNE_STATUS_OK) plugin_ok(error);
    return status;
}

/* ---- commands -------------------------------------------------------------- */

int axyne_plugin_command_applies(const AxynePluginCommand *command, const char *path)
{
    const char *dot, *slash, *backslash;
    size_t i;
    if (command == NULL) return 0;
    if (command->extension_count == 0) return 1;
    if (path == NULL) return 0;
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    dot = strrchr(path, '.');
    if (dot == NULL || (slash != NULL && dot < slash)) return 0;
    for (i = 0; i < command->extension_count; ++i) {
        const char *a = dot + 1, *b = command->extensions[i];
        while (*a != '\0' && tolower((unsigned char)*a) == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return 1;
    }
    return 0;
}

static int is_path_like(const char *program)
{
    return strchr(program, '/') != NULL || strchr(program, '\\') != NULL;
}

static int is_absolute(const char *path)
{
#ifdef _WIN32
    return (isalpha((unsigned char)path[0]) && path[1] == ':') || (path[0] == '\\' && path[1] == '\\');
#else
    return path[0] == '/';
#endif
}

/* PATH lookup; on Windows .exe, then .cmd/.bat (*script = 1 for those). */
static char *find_program(const char *name, int *script)
{
    *script = 0;
#ifdef _WIN32
    {
        static const wchar_t *const extensions[] = { L".exe", L".cmd", L".bat" };
        wchar_t *wide = wide_path(name);
        wchar_t path[MAX_PATH];
        size_t i;
        if (wide == NULL) return NULL;
        for (i = 0; i < 3; ++i) {
            DWORD length = SearchPathW(NULL, wide, extensions[i], MAX_PATH, path, NULL);
            if (length != 0 && length < MAX_PATH) {
                size_t n = wcslen(path);
                free(wide);
                *script = n > 4 && (_wcsicmp(path + n - 4, L".cmd") == 0 || _wcsicmp(path + n - 4, L".bat") == 0);
                return utf8_path(path);
            }
        }
        free(wide);
        return NULL;
    }
#else
    {
        const char *paths = getenv("PATH"), *cursor;
        if (paths == NULL) paths = "/usr/bin:/bin";
        for (cursor = paths;;) {
            const char *end = strchr(cursor, ':');
            size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
            if (length != 0) {
                char *directory = (char *)malloc(length + 1), *candidate;
                if (directory == NULL) return NULL;
                memcpy(directory, cursor, length);
                directory[length] = '\0';
                candidate = join_path(directory, name);
                free(directory);
                if (candidate != NULL && is_file(candidate) && access(candidate, X_OK) == 0) return candidate;
                free(candidate);
            }
            if (end == NULL) break;
            cursor = end + 1;
        }
        return NULL;
    }
#endif
}

const AxynePluginRequirement *axyne_plugin_missing_requirement(const AxynePlugin *plugin)
{
    size_t i;
    if (plugin == NULL) return NULL;
    for (i = 0; i < plugin->requirement_count; ++i) {
        int script = 0;
        char *found = find_program(plugin->requirements[i].program, &script);
        if (found == NULL) return &plugin->requirements[i];
        free(found);
    }
    return NULL;
}

void axyne_plugin_invocation_clear(AxynePluginInvocation *invocation)
{
    if (invocation == NULL) return;
    free(invocation->executable);
    free_strings(invocation->arguments, invocation->argument_count);
    free(invocation->working_directory);
    memset(invocation, 0, sizeof(*invocation));
}

typedef struct Buffer { char *data; size_t length, capacity; } Buffer;

static int buffer_append(Buffer *buffer, const char *text, size_t length)
{
    if (buffer->length + length + 1 > buffer->capacity) {
        size_t next = (buffer->length + length + 1) * 2;
        char *grown = (char *)realloc(buffer->data, next);
        if (grown == NULL) return 0;
        buffer->data = grown;
        buffer->capacity = next;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

/* Returns the expanded text, or NULL with *missing naming the unavailable
 * variable (NULL when out of memory). */
static char *expand(const char *text, const AxynePlugin *plugin, const AxynePluginContext *context,
                    const char **missing)
{
    Buffer out = {0};
    const char *p = text;
    const char *file = context != NULL ? context->file : NULL;
    const char *workspace = context != NULL ? context->workspace_folder : NULL;
    const char *base = NULL, *slash, *backslash, *dot;
    *missing = NULL;
    if (file != NULL) {
        slash = strrchr(file, '/');
        backslash = strrchr(file, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
        base = slash != NULL ? slash + 1 : file;
    }
    if (!buffer_append(&out, "", 0)) return NULL;
    while (*p != '\0') {
        const char *end;
        size_t name_length;
        if (p[0] != '$' || p[1] != '{' || (end = strchr(p + 2, '}')) == NULL) {
            if (!buffer_append(&out, p, 1)) { free(out.data); return NULL; }
            ++p;
            continue;
        }
        name_length = (size_t)(end - (p + 2));
#define VAR_IS(name) (name_length == strlen(name) && strncmp(p + 2, name, name_length) == 0)
        if (VAR_IS("pluginDir")) {
            if (!buffer_append(&out, plugin->directory, strlen(plugin->directory))) goto oom;
        } else if (VAR_IS("workspaceFolder")) {
            if (workspace == NULL) { *missing = "${workspaceFolder}"; free(out.data); return NULL; }
            if (!buffer_append(&out, workspace, strlen(workspace))) goto oom;
        } else if (VAR_IS("file") || VAR_IS("fileDirname") || VAR_IS("fileBasename") ||
                   VAR_IS("fileBasenameNoExtension") || VAR_IS("fileExtname")) {
            if (file == NULL) { *missing = "${file}"; free(out.data); return NULL; }
            dot = strrchr(base, '.');
            if (VAR_IS("file")) { if (!buffer_append(&out, file, strlen(file))) goto oom; }
            else if (VAR_IS("fileDirname")) {
                size_t length = base > file ? (size_t)(base - file - 1) : 0;
                if (!buffer_append(&out, base > file ? file : ".", base > file ? length : 1)) goto oom;
            } else if (VAR_IS("fileBasename")) { if (!buffer_append(&out, base, strlen(base))) goto oom; }
            else if (VAR_IS("fileBasenameNoExtension")) {
                if (!buffer_append(&out, base, dot != NULL ? (size_t)(dot - base) : strlen(base))) goto oom;
            } else if (dot != NULL && !buffer_append(&out, dot, strlen(dot))) goto oom;
        } else {
            if (!buffer_append(&out, p, (size_t)(end - p) + 1)) goto oom; /* unknown: keep */
        }
#undef VAR_IS
        p = end + 1;
    }
    return out.data;
oom:
    free(out.data);
    return NULL;
}

AxyneStatus axyne_plugin_command_prepare(const AxynePlugin *plugin, const AxynePluginCommand *command,
                                         const AxynePluginContext *context,
                                         AxynePluginInvocation *invocation, AxyneError *error)
{
    const char *missing = NULL;
    char *program = NULL;
    int script = 0;
    size_t i;
    if (invocation == NULL || plugin == NULL || command == NULL)
        return plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "plugin command is required");
    memset(invocation, 0, sizeof(*invocation));

    if (is_path_like(command->program)) {
        char *expanded = expand(command->program, plugin, context, &missing);
        if (expanded == NULL) goto failed;
        program = is_absolute(expanded) ? copy_text(expanded) : join_path(plugin->directory, expanded);
        free(expanded);
        if (program != NULL && !is_file(program)) {
            AxyneStatus status = plugin_error(error, AXYNE_STATUS_NOT_FOUND, "%s is missing from the plugin", program);
            free(program);
            return status;
        }
        if (program != NULL) {
#ifdef _WIN32
            size_t n = strlen(program);
            script = n > 4 && (_stricmp(program + n - 4, ".cmd") == 0 ||
                               _stricmp(program + n - 4, ".bat") == 0);
#else
            /* zip tools on Windows drop the executable bit of bundled scripts */
            if (access(program, X_OK) != 0) (void)chmod(program, 0755);
#endif
        }
    } else {
        program = find_program(command->program, &script);
        if (program == NULL) {
            size_t k;
            for (k = 0; k < plugin->requirement_count; ++k)
                if (strcmp(plugin->requirements[k].program, command->program) == 0 &&
                    plugin->requirements[k].hint[0] != '\0')
                    return plugin_error(error, AXYNE_STATUS_NOT_FOUND, "%s was not found on PATH. %s",
                                        command->program, plugin->requirements[k].hint);
            return plugin_error(error, AXYNE_STATUS_NOT_FOUND, "%s was not found on PATH", command->program);
        }
    }
    if (program == NULL) return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");

    invocation->arguments = (char **)calloc(command->arg_count + 4, sizeof(char *));
    if (invocation->arguments == NULL) { free(program); return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); }
    for (i = 0; i < command->arg_count; ++i) {
        invocation->arguments[i] = expand(command->args[i], plugin, context, &missing);
        if (invocation->arguments[i] == NULL) { invocation->argument_count = i; free(program); goto failed; }
    }
    invocation->argument_count = command->arg_count;
    if (command->cwd[0] != '\0') {
        invocation->working_directory = expand(command->cwd, plugin, context, &missing);
        if (invocation->working_directory == NULL) { free(program); goto failed; }
    } else if (context != NULL && context->workspace_folder != NULL) {
        invocation->working_directory = copy_text(context->workspace_folder);
    } else {
        invocation->working_directory = copy_text(plugin->directory);
    }

#ifdef _WIN32
    if (script) {
        /* cmd.exe /d /s /c ""script" "arg"" - as the runner wraps scripts. */
        Buffer line = {0};
        wchar_t system_dir[MAX_PATH];
        UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
        int ok = strpbrk(program, "\"%\r\n") == NULL;
        for (i = 0; ok && i < invocation->argument_count; ++i)
            if (strpbrk(invocation->arguments[i], "\"%\r\n") != NULL) ok = 0;
        if (!ok) {
            free(program);
            axyne_plugin_invocation_clear(invocation);
            return plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                "a script command cannot take arguments containing quotes or %%");
        }
        ok = buffer_append(&line, "\"\"", 2) && buffer_append(&line, program, strlen(program)) &&
             buffer_append(&line, "\"", 1);
        for (i = 0; ok && i < invocation->argument_count; ++i)
            ok = buffer_append(&line, " \"", 2) &&
                 buffer_append(&line, invocation->arguments[i], strlen(invocation->arguments[i])) &&
                 buffer_append(&line, "\"", 1);
        ok = ok && buffer_append(&line, "\"", 1) && length != 0 && length + 10 < MAX_PATH;
        free(program);
        program = NULL;
        if (ok) {
            memcpy(system_dir + length, L"\\cmd.exe", 9 * sizeof(wchar_t));
            program = utf8_path(system_dir);
        }
        for (i = 0; i < invocation->argument_count; ++i) free(invocation->arguments[i]);
        invocation->argument_count = 0;
        if (program == NULL) {
            free(line.data);
            axyne_plugin_invocation_clear(invocation);
            return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "cannot prepare the script command");
        }
        invocation->arguments[0] = copy_text("/d");
        invocation->arguments[1] = copy_text("/s");
        invocation->arguments[2] = copy_text("/c");
        invocation->arguments[3] = line.data;
        invocation->argument_count = 4;
    }
#endif
    invocation->executable = program;
    if (invocation->working_directory == NULL || invocation->arguments == NULL) {
        axyne_plugin_invocation_clear(invocation);
        return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    }
    plugin_ok(error);
    return AXYNE_STATUS_OK;

failed:
    axyne_plugin_invocation_clear(invocation);
    if (missing != NULL)
        return plugin_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                            "%s needs %s (open a %s first)", command->title, missing,
                            strcmp(missing, "${file}") == 0 ? "saved file" : "workspace folder");
    return plugin_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
}
