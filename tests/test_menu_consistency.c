/* Static consistency checks between the menu definitions and their handlers.
 * The native adapters cannot be built or run on every platform, so this reads
 * both adapter sources as text and verifies, for each menu entry:
 *   macOS   - every action selector installed in axyne_install_menu and every
 *             base handler of the registry commands (ui_commands_macos.m) has
 *             an implementation in AxyneWorkspaceView, in ui_macos.m or a
 *             category file (or is a system selector); the menu labels come
 *             from the command registry and the macOS menu layout;
 *   Windows - the menus (ui_commands_windows.c menu table) list every
 *             registry command available on Windows; the base and every
 *             feature file (ui_<feature>_windows.c) report a state only for
 *             commands they handle and vice versa (no enabled entry that does
 *             nothing); legacy ids stay unique and are handled;
 *   both    - the shared menu entries exist on both platforms (Windows:
 *             registry titles of menu commands; macOS: registry titles placed
 *             by the macOS menu layout).
 * The repository root is the first argument. Portable C17 only (no POSIX
 * string helpers) so it runs on the MSVC, clang and gcc CI builds. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/document.h"

static const char *source_root;
static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)
#define FAIL(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); \
    ++failures; } while (0)

static char *copy_text(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

/* Reads a repository file with CR removed, so the markers below match a
 * checkout with CRLF line endings (Windows runners) as well. */
static char *read_file(const char *relative)
{
    char path[1024];
    FILE *file;
    long size;
    char *text;
    char *in, *out;
    (void)snprintf(path, sizeof(path), "%s/%s", source_root, relative);
    file = fopen(path, "rb");
    if (file == NULL) { FAIL("cannot open %s", path); return NULL; }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); FAIL("cannot size %s", path); return NULL;
    }
    text = (char *)malloc((size_t)size + 1);
    if (text == NULL || fread(text, 1, (size_t)size, file) != (size_t)size) {
        fclose(file); free(text); FAIL("cannot read %s", path); return NULL;
    }
    text[size] = '\0';
    fclose(file);
    for (in = out = text; *in != '\0'; ++in)
        if (*in != '\r') *out++ = *in;
    *out = '\0';
    return text;
}

/* Copy of text[from_marker, to_marker); the last occurrence of from_marker is
 * used so forward declarations are skipped. */
static char *region(const char *text, const char *from_marker, const char *to_marker)
{
    const char *begin = NULL;
    const char *cursor = text;
    const char *end;
    while ((cursor = strstr(cursor, from_marker)) != NULL) { begin = cursor; ++cursor; }
    if (begin == NULL) { FAIL("missing marker: %s", from_marker); return NULL; }
    end = strstr(begin, to_marker);
    if (end == NULL) { FAIL("missing marker: %s", to_marker); return NULL; }
    return copy_text(begin, (size_t)(end - begin));
}

static int is_identifier_char(int c) { return isalnum(c) || c == '_'; }

/* True when `word` occurs in text as a whole identifier. */
static int has_word(const char *text, const char *word)
{
    size_t length = strlen(word);
    const char *cursor = text;
    while ((cursor = strstr(cursor, word)) != NULL) {
        int before = cursor == text || !is_identifier_char((unsigned char)cursor[-1]);
        int after = !is_identifier_char((unsigned char)cursor[length]);
        if (before && after) return 1;
        cursor += length;
    }
    return 0;
}

/* ---- macOS: selectors -------------------------------------------------- */

static int selector_is_system(const char *name)
{
    return strcmp(name, "orderFrontStandardAboutPanel:") == 0 ||
           strcmp(name, "terminate:") == 0;
}

/* macOS sources that implement AxyneWorkspaceView methods besides the main
 * @implementation in ui_macos.m: the command routing and the feature
 * category files (one per feature category). */
static const char *const macos_category_files[] = {
    "src/features/ui/platform/macos/ui_commands_macos.m",
    "src/features/ui/platform/macos/ui_file_macos.m",
    "src/features/ui/platform/macos/ui_edit_macos.m",
    "src/features/ui/platform/macos/ui_view_macos.m",
    "src/features/ui/platform/macos/ui_build_macos.m",
    "src/features/ui/platform/macos/ui_debug_macos.m",
    "src/features/ui/platform/macos/ui_terminal_macos.m",
    "src/features/ui/platform/macos/ui_status_macos.m",
    "src/features/ui/platform/macos/ui_tools_macos.m",
    "src/features/ui/platform/macos/ui_help_macos.m",
    "src/features/ui/platform/macos/ui_lsp_nav_macos.m",
    "src/features/ui/platform/macos/ui_diag_macos.m",
    "src/features/ui/platform/macos/ui_release_macos.m"
};
#define MACOS_CATEGORY_FILE_COUNT \
    (sizeof(macos_category_files) / sizeof(macos_category_files[0]))

/* True when `signature` ("\n- (void)name:(") is implemented by the view:
 * in the main @implementation of ui_macos.m or in a category file. */
static int macos_view_implements(const char *implementation,
                                 char *const *categories, const char *signature)
{
    size_t i;
    if (implementation != NULL && strstr(implementation, signature) != NULL) return 1;
    for (i = 0; i < MACOS_CATEGORY_FILE_COUNT; ++i)
        if (categories[i] != NULL && strstr(categories[i], signature) != NULL) return 1;
    return 0;
}

/* Every @selector(...) in `text` must be a system action or an action
 * method (one sender argument) implemented by the workspace view. */
static size_t check_macos_selector_text(const char *text, const char *where,
                                        const char *implementation,
                                        char *const *categories)
{
    const char *cursor = text;
    size_t count = 0;
    while ((cursor = strstr(cursor, "@selector(")) != NULL) {
        char name[96];
        char signature[128];
        const char *close;
        size_t length;
        cursor += strlen("@selector(");
        close = strchr(cursor, ')');
        if (close == NULL) break;
        length = (size_t)(close - cursor);
        if (length == 0 || length >= sizeof(name)) { FAIL("bad selector text"); break; }
        memcpy(name, cursor, length);
        name[length] = '\0';
        ++count;
        if (selector_is_system(name)) continue;
        if (name[length - 1] != ':') { FAIL("menu selector %s takes no sender", name); continue; }
        (void)snprintf(signature, sizeof(signature), "\n- (void)%s(", name);
        if (!macos_view_implements(implementation, categories, signature))
            FAIL("macOS %s selector %s has no implementation", where, name);
    }
    return count;
}

static void check_macos_selectors(void)
{
    char *source = read_file("src/features/ui/platform/macos/ui_macos.m");
    char *categories[MACOS_CATEGORY_FILE_COUNT];
    char *install = NULL, *implementation = NULL, *base = NULL;
    size_t i, count = 0;
    for (i = 0; i < MACOS_CATEGORY_FILE_COUNT; ++i)
        categories[i] = read_file(macos_category_files[i]);
    if (source == NULL || categories[0] == NULL) goto done;
    /* The application menu installed by hand. */
    install = region(source, "static void axyne_install_menu(NSApplication *application,\n"
                             "                               AxyneWorkspaceView *workspace)\n{",
                     "int axyne_ui_run");
    /* The newline skips the categories (AxyneWorkspaceView (AxynePalette))
     * that the region already contains. */
    implementation = region(source, "@implementation AxyneWorkspaceView\n",
                            "@interface AxyneApplicationDelegate");
    /* Registry menu items run -performCommand:, whose base handlers are the
     * selectors returned by axyne_macos_base_action. */
    base = region(categories[0], "static SEL axyne_macos_base_action(",
                  "/* ---- key map");
    if (install != NULL && implementation != NULL && base != NULL) {
        count += check_macos_selector_text(install, "menu", implementation, categories);
        count += check_macos_selector_text(base, "command", implementation, categories);
        CHECK(macos_view_implements(implementation, categories, "\n- (void)performCommand:("));
        CHECK(count >= 40);
    }
done:
    free(install); free(implementation); free(base); free(source);
    for (i = 0; i < MACOS_CATEGORY_FILE_COUNT; ++i) free(categories[i]);
}

/* ---- macOS: menu labels from the command registry --------------------- */

/* Copies the C string literal at *cursor (blanks skipped) and advances past
 * it. Returns 0 when there is none. */
static int read_literal(const char **cursor, char *out, size_t capacity)
{
    const char *p = *cursor;
    const char *end;
    size_t length;
    while (isspace((unsigned char)*p)) ++p;
    if (*p != '"') return 0;
    end = strchr(p + 1, '"');
    if (end == NULL) return 0;
    length = (size_t)(end - p - 1);
    if (length >= capacity) length = capacity - 1;
    memcpy(out, p + 1, length);
    out[length] = '\0';
    *cursor = end + 1;
    return 1;
}

static int label_matches(const char *title, const char *label)
{
    size_t length = strlen(label);
    if (strcmp(title, label) == 0) return 1;
    if (strncmp(title, label, length) != 0) return 0;
    return strcmp(title + length, "\xe2\x80\xa6") == 0 || strcmp(title + length, "...") == 0;
}

/* True when the macOS menus show `label`: a command of the registry table
 * (src/features/commands/commands.c; its macOS title when it has one) whose
 * id the macOS menu layout (ui_commands_macos.m, ITEM(...)) lists. */
static int macos_menu_has_label(const char *registry, const char *layout,
                                const char *label)
{
    const char *cursor = registry;
    while ((cursor = strstr(cursor, "\n    E(")) != NULL) {
        char id[80], name[80], title[160], mac[160], item[96];
        const char *p = cursor + strlen("\n    E(");
        size_t n = 0;
        cursor = p;
        while (is_identifier_char((unsigned char)*p) && n + 1 < sizeof(id)) id[n++] = *p++;
        id[n] = '\0';
        if (*p != ',') continue;
        ++p;
        if (!read_literal(&p, name, sizeof(name))) continue;
        while (isspace((unsigned char)*p) || *p == ',') ++p;
        if (!read_literal(&p, title, sizeof(title))) continue;
        while (isspace((unsigned char)*p) || *p == ',') ++p;
        mac[0] = '\0';
        if (*p == '"') (void)read_literal(&p, mac, sizeof(mac));
        if (!label_matches(mac[0] != '\0' ? mac : title, label)) continue;
        (void)snprintf(item, sizeof(item), "ITEM(%s)", id);
        if (strstr(layout, item) != NULL) return 1;
    }
    return 0;
}

/* Shared labels whose macOS menu entry now shows the registry title. */
static const char *macos_label_alias(const char *label)
{
    if (strcmp(label, "preferences.json 열기") == 0) return "settings.json 열기";
    if (strcmp(label, "실행") == 0) return "디버깅하지 않고 실행";
    return label;
}

static char *macos_registry, *macos_layout;

static void load_macos_labels(void)
{
    char *commands = read_file("src/features/commands/commands.c");
    char *routing = read_file("src/features/ui/platform/macos/ui_commands_macos.m");
    if (commands != NULL)
        macos_registry = region(commands, "static const AxyneCommandInfo command_table[] = {",
                                "\n};");
    if (routing != NULL)
        macos_layout = region(routing, "static const AxyneMacMenuEntry axyne_macos_file_menu[]",
                              "/* ---- base handlers");
    free(commands); free(routing);
}

static void free_macos_labels(void)
{
    free(macos_registry); free(macos_layout);
    macos_registry = macos_layout = NULL;
}

static int macos_has_label(const char *label)
{
    static int loaded;
    if (!loaded) {
        loaded = 1;
        load_macos_labels();
        (void)atexit(free_macos_labels);
    }
    if (macos_registry == NULL || macos_layout == NULL) return 0;
    return macos_menu_has_label(macos_registry, macos_layout, macos_label_alias(label));
}

/* ---- Windows: command ids ---------------------------------------------- */

typedef struct { char name[64]; long value; } EnumEntry;
static EnumEntry entries[256];
static size_t entry_count;

static void strip_comments(char *text)
{
    char *in = text, *out = text;
    while (*in != '\0') {
        if (in[0] == '/' && in[1] == '*') {
            in += 2;
            while (*in != '\0' && !(in[0] == '*' && in[1] == '/')) ++in;
            if (*in != '\0') in += 2;
        } else if (in[0] == '/' && in[1] == '/') {
            while (*in != '\0' && *in != '\n') ++in;
        } else *out++ = *in++;
    }
    *out = '\0';
}

/* Parses `enum { A = 1, B, ... };` blocks made only of plain names and numbers
 * (the command id enums); other enums are skipped. */
static void parse_enums(const char *source)
{
    const char *cursor = source;
    while ((cursor = strstr(cursor, "enum {")) != NULL) {
        const char *end = strstr(cursor, "};");
        char *block;
        char *item, *comma = NULL;
        long next = 0;
        int plain = 1;
        EnumEntry local[128];
        size_t local_count = 0;
        size_t i;
        if (end == NULL) break;
        block = copy_text(cursor + strlen("enum {"),
                          (size_t)(end - cursor) - strlen("enum {"));
        if (block == NULL) return;
        strip_comments(block);
        for (item = block; item != NULL && plain; item = comma != NULL ? comma + 1 : NULL) {
            char name[64];
            long value;
            const char *p = item;
            size_t n = 0;
            comma = strchr(item, ',');
            if (comma != NULL) *comma = '\0';
            while (isspace((unsigned char)*p)) ++p;
            if (*p == '\0') continue;
            while (is_identifier_char((unsigned char)*p) && n + 1 < sizeof(name)) name[n++] = *p++;
            name[n] = '\0';
            while (isspace((unsigned char)*p)) ++p;
            if (*p == '=') {
                char *stop;
                ++p;
                value = strtol(p, &stop, 10);
                while (isspace((unsigned char)*stop)) ++stop;
                if (stop == p || *stop != '\0') { plain = 0; break; }
                next = value;
            } else if (*p != '\0') { plain = 0; break; }
            if (local_count < 128) {
                memcpy(local[local_count].name, name, sizeof(name));
                local[local_count].value = next++;
                ++local_count;
            }
        }
        if (plain && local_count > 0 &&
            (strncmp(local[0].name, "AXYNE_CMD_", 10) == 0 ||
             strncmp(local[0].name, "AXYNE_TERMINAL_", 15) == 0)) {
            for (i = 0; i < local_count && entry_count < 256; ++i)
                entries[entry_count++] = local[i];
        }
        free(block);
        cursor = end;
    }
}

static const EnumEntry *find_entry(const char *name)
{
    size_t i;
    for (i = 0; i < entry_count; ++i)
        if (strcmp(entries[i].name, name) == 0) return &entries[i];
    return NULL;
}

/* Body of the function whose definition starts with `signature` (up to the
 * first line that is a lone "}"); NULL when missing. */
static char *function_body(const char *text, const char *signature)
{
    const char *begin = strstr(text, signature);
    const char *end;
    if (begin == NULL) { FAIL("missing function: %s", signature); return NULL; }
    end = strstr(begin, "\n}\n");
    if (end == NULL) { FAIL("unterminated function: %s", signature); return NULL; }
    return copy_text(begin, (size_t)(end - begin) + 2);
}

/* ---- Windows: registry commands ---------------------------------------- */

#define MAX_COMMANDS 256
typedef struct {
    char name[96];   /* AXYNE_COMMAND_... */
    char title[160]; /* Windows title (UTF-8) */
    int windows;     /* available on Windows */
    int context;     /* explorer context menu group */
} RegistryCommand;
static RegistryCommand registry[MAX_COMMANDS];
static size_t registry_count;

static RegistryCommand *find_command(const char *name)
{
    size_t i;
    for (i = 0; i < registry_count; ++i)
        if (strcmp(registry[i].name, name) == 0) return &registry[i];
    return NULL;
}

/* Splits the argument list starting after "E(" into at most `max` trimmed
 * arguments; commas inside string literals and parentheses do not split.
 * Returns the count, 0 on malformed text. */
static size_t split_arguments(const char *p, char args[][192], size_t max)
{
    size_t count = 0, n = 0;
    int depth = 0, quoted = 0;
    for (; *p != '\0'; ++p) {
        char c = *p;
        if (quoted) {
            if (c == '\\' && p[1] != '\0') { if (n + 2 < 192) { args[count][n++] = c; args[count][n++] = p[1]; } ++p; continue; }
            if (c == '"') quoted = 0;
        } else if (c == '"') quoted = 1;
        else if (c == '(') ++depth;
        else if (c == ')') {
            if (depth == 0) break;
            --depth;
        } else if (c == ',' && depth == 0) {
            args[count][n] = '\0';
            if (++count >= max) return 0;
            n = 0;
            continue;
        }
        if (n + 1 < 192 && !(n == 0 && isspace((unsigned char)c))) args[count][n++] = c;
    }
    if (*p != ')') return 0;
    args[count][n] = '\0';
    ++count;
    for (n = 0; n < count; ++n) {
        size_t length = strlen(args[n]);
        while (length > 0 && isspace((unsigned char)args[n][length - 1])) args[n][--length] = '\0';
    }
    return count;
}

/* Reads the AxyneCommandId enum and the registry table (titles, platforms,
 * groups) from the shared sources. */
static void load_registry(void)
{
    char *header = read_file("include/axyne/commands.h");
    char *table = read_file("src/features/commands/commands.c");
    char *ids = header != NULL ? region(header, "typedef enum AxyneCommandId {", "} AxyneCommandId;") : NULL;
    const char *cursor;
    if (ids != NULL) {
        strip_comments(ids);
        cursor = ids;
        while ((cursor = strstr(cursor, "AXYNE_COMMAND_")) != NULL) {
            size_t n = 0;
            char name[96];
            while (is_identifier_char((unsigned char)cursor[n]) && n + 1 < sizeof(name)) {
                name[n] = cursor[n]; ++n;
            }
            name[n] = '\0';
            cursor += n;
            if (strcmp(name, "AXYNE_COMMAND_NONE") == 0 || strcmp(name, "AXYNE_COMMAND_COUNT") == 0 ||
                registry_count >= MAX_COMMANDS)
                continue;
            memcpy(registry[registry_count].name, name, n + 1);
            ++registry_count;
        }
    }
    CHECK(registry_count > 100);
    if (table != NULL) {
        char *rows = region(table, "static const AxyneCommandInfo command_table[] = {", "\n};");
        size_t parsed = 0;
        cursor = rows;
        while (cursor != NULL && (cursor = strstr(cursor, "    E(")) != NULL) {
            char args[14][192];
            char name[224];
            RegistryCommand *command;
            cursor += strlen("    E(");
            if (split_arguments(cursor, args, 14) != 14) { FAIL("cannot parse registry row"); continue; }
            (void)snprintf(name, sizeof(name), "AXYNE_COMMAND_%s", args[0]);
            command = find_command(name);
            if (command == NULL) { FAIL("registry row %s is not in AxyneCommandId", name); continue; }
            ++parsed;
            if (args[2][0] == '"') {
                size_t length = strlen(args[2]);
                if (length >= 2 && length - 2 < sizeof(command->title)) {
                    memcpy(command->title, args[2] + 1, length - 2);
                    command->title[length - 2] = '\0';
                }
            }
            command->windows = strcmp(args[7], "BOTH") == 0 || strcmp(args[7], "WIN") == 0;
            command->context = strcmp(args[5], "G(CONTEXT)") == 0;
        }
        CHECK(parsed == registry_count);
        free(rows);
    }
    free(ids); free(header); free(table);
}

/* Collects the `case AXYNE_COMMAND_X:` labels of a function body. */
static size_t collect_cases(const char *body, char names[][96], size_t max)
{
    size_t count = 0;
    const char *cursor = body;
    while (cursor != NULL && (cursor = strstr(cursor, "case AXYNE_COMMAND_")) != NULL) {
        size_t n = 0;
        cursor += strlen("case ");
        while (is_identifier_char((unsigned char)cursor[n]) && n + 1 < 96) {
            if (count < max) names[count][n] = cursor[n];
            ++n;
        }
        if (count < max) { names[count][n] = '\0'; ++count; }
        cursor += n;
    }
    return count;
}

static int has_name(char names[][96], size_t count, const char *name)
{
    size_t i;
    for (i = 0; i < count; ++i) if (strcmp(names[i], name) == 0) return 1;
    return 0;
}

/* A command whose state is reported (enabled = shown as implemented) must
 * have a handler and vice versa: no enabled entry that does nothing. */
static void check_state_matches_handler(const char *file, const char *source,
                                        const char *state_signature,
                                        const char *command_signature)
{
    static char states[MAX_COMMANDS][96], handlers[MAX_COMMANDS][96];
    char *state_body = function_body(source, state_signature);
    char *command_body = function_body(source, command_signature);
    size_t state_count, handler_count, i;
    if (state_body != NULL && command_body != NULL) {
        state_count = collect_cases(state_body, states, MAX_COMMANDS);
        handler_count = collect_cases(command_body, handlers, MAX_COMMANDS);
        for (i = 0; i < state_count; ++i) {
            if (find_command(states[i]) == NULL) FAIL("%s: unknown command %s", file, states[i]);
            if (!has_name(handlers, handler_count, states[i]))
                FAIL("%s: %s has a state but no handler", file, states[i]);
        }
        for (i = 0; i < handler_count; ++i)
            if (!has_name(states, state_count, handlers[i]))
                FAIL("%s: %s has a handler but no state (it would stay disabled)", file, handlers[i]);
    }
    free(state_body); free(command_body);
}

static const char *const windows_features[] = {
    "file", "edit", "view", "build", "debug", "terminal", "status", "tools", "help",
    "lsp_nav", "diag", "release"
};

static void check_windows_commands(void)
{
    char *source = read_file("src/features/ui/platform/windows/ui_windows.c");
    char *glue = read_file("src/features/ui/platform/windows/ui_commands_windows.c");
    char *cmake = read_file("src/features/ui/CMakeLists.txt");
    char *menus = NULL, *legacy = NULL, *actions = NULL, *base = NULL;
    const char *cursor;
    size_t i, j, menu_ids = 0;
    if (source == NULL || glue == NULL || cmake == NULL) goto done;
    {
        char *copy = copy_text(source, strlen(source));
        if (copy != NULL) { strip_comments(copy); parse_enums(copy); free(copy); }
    }
    CHECK(entry_count > 60);
    /* Legacy ids (toolbar, explorer context menu, recent files, panel
     * buttons) stay unique, out of the Open Recent range and out of the
     * registry menu id range [3000, 3000 + commands). */
    {
        const EnumEntry *recent = find_entry("AXYNE_CMD_RECENT_BASE");
        CHECK(recent != NULL);
        CHECK(strstr(source, "#include \"ui_windows_internal.h\"") != NULL);
        for (i = 0; i < entry_count; ++i) {
            for (j = i + 1; j < entry_count; ++j)
                if (entries[i].value == entries[j].value)
                    FAIL("duplicate command id %ld: %s and %s", entries[i].value,
                         entries[i].name, entries[j].name);
            if (recent != NULL && strcmp(entries[i].name, "AXYNE_CMD_RECENT_BASE") != 0 &&
                entries[i].value >= recent->value &&
                entries[i].value < recent->value + AXYNE_RECENT_FILES_LIMIT)
                FAIL("%s collides with the Open Recent id range", entries[i].name);
            if (entries[i].value >= 3000 && entries[i].value < 3000 + (long)MAX_COMMANDS)
                FAIL("%s collides with the registry menu id range", entries[i].name);
        }
    }
    /* Every registry id used by the Windows menus exists, and every Windows
     * command (except the explorer context menu) is in the menus. */
    menus = region(glue, "/* ---- menu table", "/* ---- end of menu table");
    if (menus == NULL) goto done;
    {
        char *code = copy_text(menus, strlen(menus));
        if (code != NULL) strip_comments(code);
        cursor = code;
        while (cursor != NULL && (cursor = strstr(cursor, "AXYNE_COMMAND_")) != NULL) {
            char name[96];
            size_t n = 0;
            while (is_identifier_char((unsigned char)cursor[n]) && n + 1 < sizeof(name)) {
                name[n] = cursor[n]; ++n;
            }
            name[n] = '\0';
            cursor += n;
            if (strcmp(name, "AXYNE_COMMAND_NONE") == 0 || strncmp(name, "AXYNE_COMMAND_FLAG", 18) == 0)
                continue;
            ++menu_ids;
            if (find_command(name) == NULL) FAIL("Windows menu id %s is not a registry command", name);
        }
        for (i = 0; i < registry_count; ++i)
            if (registry[i].windows && !registry[i].context && code != NULL &&
                !has_word(code, registry[i].name))
                FAIL("Windows menus lack registry command %s", registry[i].name);
        free(code);
    }
    CHECK(menu_ids >= 100);
    /* The base and every feature report state only for commands they run. */
    check_state_matches_handler("ui_windows.c", source,
        "int axyne_win_base_command_state(AxyneWindowState *state, AxyneCommandId id,",
        "int axyne_win_base_command(AxyneWindowState *state, AxyneCommandId id)\n{");
    for (i = 0; i < sizeof(windows_features) / sizeof(windows_features[0]); ++i) {
        char relative[160], file[96], state_signature[160], command_signature[160], table[96];
        char *feature;
        (void)snprintf(file, sizeof(file), "ui_%s_windows.c", windows_features[i]);
        (void)snprintf(relative, sizeof(relative), "src/features/ui/platform/windows/%s", file);
        feature = read_file(relative);
        if (feature == NULL) continue;
        if (strstr(cmake, file) == NULL) FAIL("%s is not in src/features/ui/CMakeLists.txt", file);
        (void)snprintf(table, sizeof(table), "&axyne_win_feature_%s", windows_features[i]);
        if (strstr(glue, table) == NULL) FAIL("feature table lacks %s", table);
        (void)snprintf(state_signature, sizeof(state_signature),
                       "static int axyne_win_%s_command_state(AxyneWindowState *state",
                       windows_features[i]);
        (void)snprintf(command_signature, sizeof(command_signature),
                       "static int axyne_win_%s_command(AxyneWindowState *state, AxyneCommandId id)\n{",
                       windows_features[i]);
        if (strstr(feature, state_signature) != NULL || strstr(feature, command_signature) != NULL)
            check_state_matches_handler(file, feature, state_signature, command_signature);
        free(feature);
    }
    /* Every legacy id the base maps a registry command onto is handled by the
     * legacy dispatcher or the action switch. */
    base = function_body(source, "int axyne_win_base_command(AxyneWindowState *state, AxyneCommandId id)\n{");
    legacy = function_body(source, "static void axyne_legacy_command(HWND window, AxyneWindowState *state, UINT command)\n{");
    actions = function_body(source, "static int axyne_action_command(HWND window, AxyneWindowState *state, UINT command)\n{");
    if (base == NULL || legacy == NULL || actions == NULL) goto done;
    for (i = 0; i < entry_count; ++i) {
        if (!has_word(base, entries[i].name)) continue;
        if (!has_word(legacy, entries[i].name) && !has_word(actions, entries[i].name) &&
            strncmp(entries[i].name, "AXYNE_CMD_EXPLORER_", 19) != 0 &&
            strcmp(entries[i].name, "AXYNE_CMD_WORKSPACE") != 0)
            FAIL("legacy command %s used by the base is never handled", entries[i].name);
    }
    /* Explorer and folder commands go through axyne_workspace_operation. */
    CHECK(strstr(legacy, "axyne_workspace_operation(window, state, command)") != NULL);
    /* Every id handled by the action switch must be inside the id range that
     * axyne_action_command accepts, and every id in that enum needs a case. */
    {
        const EnumEntry *first = find_entry("AXYNE_CMD_GOTO_LINE");
        const EnumEntry *last = find_entry("AXYNE_CMD_REPORT_ISSUE");
        CHECK(first != NULL && last != NULL);
        if (first != NULL && last != NULL) {
            for (i = 0; i < entry_count; ++i) {
                char label[96];
                if (entries[i].value < first->value || entries[i].value > last->value) continue;
                (void)snprintf(label, sizeof(label), "case %s:", entries[i].name);
                if (strstr(actions, label) == NULL)
                    FAIL("%s is in the action id range but has no case", entries[i].name);
            }
        }
    }
done:
    free(menus); free(legacy); free(actions); free(base);
    free(source); free(glue); free(cmake);
}

/* ---- both platforms ---------------------------------------------------- */

static int source_has_label(const char *source, char prefix, const char *label)
{
    char needle[160];
    char alternate[160];
    (void)snprintf(needle, sizeof(needle), "%c\"%s\"", prefix, label);
    if (strstr(source, needle) != NULL) return 1;
    (void)snprintf(needle, sizeof(needle), "%c\"%s\xe2\x80\xa6\"", prefix, label);
    (void)snprintf(alternate, sizeof(alternate), "%c\"%s...\"", prefix, label);
    return strstr(source, needle) != NULL || strstr(source, alternate) != NULL;
}

/* Menu entries both platforms offer. Windows builds its menus from the
 * registry, so a Windows label must be the registry title (with or without
 * "..."/"…") of a command in the Windows menu table. */
static const char *const shared_labels[] = {
    "새 파일", "열기", "폴더 열기", "저장", "다른 이름으로 저장", "닫기",
    "실행 취소", "다시 실행", "잘라내기", "복사", "붙여넣기", "모두 선택",
    "찾기", "바꾸기", "파일에서 찾기", "파일 이동", "줄로 이동", "줄 선택",
    "줄 주석 토글", "줄 복제", "줄 위로 이동", "줄 아래로 이동", "들여쓰기",
    "내어쓰기", "정의로 이동", "참조 찾기", "탐색기", "하단 패널", "출력",
    "문제", "터미널", "확대", "축소", "기본 크기", "자동 줄 바꿈", "빌드",
    "디버깅하지 않고 실행", "Runner 설정", "빌드 취소", "디버깅 시작", "중지",
    "일시 중지", "계속", "프로시저 단위 실행", "한 단계씩 코드 실행",
    "프로시저 나가기", "중단점 토글", "모든 중단점 삭제", "새 터미널",
    "작업 영역 설정", "settings.json 열기", "Git 상태", "Git 변경 사항",
    "모두 스테이지", "모두 스테이지 해제", "키보드 단축키 참조", "문제 보고",
    "Axyne 정보"
};

static int title_matches(const char *title, const char *label)
{
    size_t length = strlen(label);
    if (strncmp(title, label, length) != 0) return 0;
    return title[length] == '\0' || strcmp(title + length, "...") == 0 ||
           strcmp(title + length, "\xe2\x80\xa6") == 0;
}

static void check_windows_labels(void)
{
    char *glue = read_file("src/features/ui/platform/windows/ui_commands_windows.c");
    char *menus = glue != NULL ? region(glue, "/* ---- menu table", "/* ---- end of menu table") : NULL;
    size_t i, k;
    if (menus != NULL) {
        for (i = 0; i < sizeof(shared_labels) / sizeof(shared_labels[0]); ++i) {
            int found = 0;
            for (k = 0; k < registry_count && !found; ++k)
                found = registry[k].windows && title_matches(registry[k].title, shared_labels[i]) &&
                        has_word(menus, registry[k].name);
            if (!found) FAIL("Windows menu lacks shared entry: %s", shared_labels[i]);
        }
    }
    free(menus); free(glue);
}

/* macOS menu labels come from the registry titles placed by the macOS layout
 * table (ui_commands_macos.m); macos_has_label maps renamed entries. */
static void check_macos_labels(void)
{
    static const char *const labels[] = {
        "새 파일", "열기", "폴더 열기", "저장", "다른 이름으로 저장", "닫기",
        "실행 취소", "다시 실행", "잘라내기", "복사", "붙여넣기", "모두 선택",
        "찾기", "바꾸기", "파일에서 찾기", "파일 이동", "줄로 이동", "줄 선택",
        "줄 주석 토글", "줄 복제", "줄 위로 이동", "줄 아래로 이동", "들여쓰기",
        "내어쓰기", "정의로 이동", "참조 찾기", "탐색기", "하단 패널", "출력",
        "문제", "터미널", "확대", "축소", "기본 크기", "자동 줄 바꿈", "빌드",
        "실행", "Runner 설정", "빌드 취소", "디버깅 시작", "중지", "일시 중지",
        "계속", "프로시저 단위 실행", "한 단계씩 코드 실행", "프로시저 나가기",
        "중단점 토글", "모든 중단점 삭제", "새 터미널", "작업 영역 설정",
        "preferences.json 열기", "Git 상태", "Git 변경 사항", "모두 스테이지",
        "모두 스테이지 해제", "키보드 단축키 참조", "문제 보고", "Axyne 정보"
    };
    char *mac = read_file("src/features/ui/platform/macos/ui_macos.m");
    size_t i;
    if (mac != NULL) {
        for (i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
            if (!macos_has_label(labels[i]))
                FAIL("macOS menu lacks shared entry: %s", labels[i]);
    }
    free(mac);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <repository root>\n", argv[0]);
        return 2;
    }
    source_root = argv[1];
    load_registry();
    check_macos_selectors();
    check_macos_labels();
    check_windows_commands();
    check_windows_labels();
    if (failures == 0) puts("menu consistency ok");
    return failures == 0 ? 0 : 1;
}
