#include "axyne/palette_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/filesystem.h"

#ifdef _WIN32
#include <ctype.h>
#endif

#define AXYNE_PALETTE_NO_ROW ((size_t)-1)

/* ---- path helpers ------------------------------------------------------- */

static char *copy_text(const char *text)
{
    size_t length = text == NULL ? 0 : strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    if (length != 0) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

#ifdef _WIN32
static int path_char_equal(unsigned char a, unsigned char b)
{
    if (a == '\\') a = '/';
    if (b == '\\') b = '/';
    return a == b || (a < 0x80 && b < 0x80 && tolower(a) == tolower(b));
}
#else
static int path_char_equal(unsigned char a, unsigned char b) { return a == b; }
#endif

static int same_path(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (!path_char_equal((unsigned char)*a, (unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int add_path(AxynePaletteController *c, const char *path)
{
    if (c->path_count == c->path_capacity) {
        size_t capacity = c->path_capacity == 0 ? 256 : c->path_capacity * 2;
        char **grown = (char **)realloc(c->paths, capacity * sizeof(*grown));
        if (grown == NULL) return 0;
        c->paths = grown;
        c->path_capacity = capacity;
    }
    c->paths[c->path_count] = copy_text(path);
    if (c->paths[c->path_count] == NULL) return 0;
    ++c->path_count;
    return 1;
}

/* ---- iterative workspace walk ------------------------------------------- */

typedef struct WalkFrame {
    AxyneDirectoryList list;
    size_t next;
    size_t depth;
} WalkFrame;

typedef struct Walk {
    WalkFrame *frames;
    size_t count;
    size_t capacity;
} Walk;

static int walk_push(Walk *walk, const char *directory, size_t depth)
{
    AxyneDirectoryList list = {0};
    if (axyne_fs_list_directory(directory, &list, NULL) != AXYNE_STATUS_OK)
        return 1; /* an unreadable directory is simply skipped */
    if (walk->count == walk->capacity) {
        size_t capacity = walk->capacity == 0 ? 16 : walk->capacity * 2;
        WalkFrame *grown = (WalkFrame *)realloc(walk->frames,
                                                capacity * sizeof(*grown));
        if (grown == NULL) {
            axyne_fs_free_directory_list(&list);
            return 0;
        }
        walk->frames = grown;
        walk->capacity = capacity;
    }
    walk->frames[walk->count].list = list;
    walk->frames[walk->count].next = 0;
    walk->frames[walk->count].depth = depth;
    ++walk->count;
    return 1;
}

static void walk_clear(Walk *walk)
{
    while (walk->count > 0) {
        --walk->count;
        axyne_fs_free_directory_list(&walk->frames[walk->count].list);
    }
}

static void walk_destroy(Walk *walk)
{
    if (walk == NULL) return;
    walk_clear(walk);
    free(walk->frames);
    free(walk);
}

static void walk_finish(AxynePaletteController *c)
{
    walk_destroy((Walk *)c->walk);
    c->walk = NULL;
}

/* ---- list building ------------------------------------------------------ */

static void clear_view(AxynePaletteController *c)
{
    axyne_palette_list_destroy(&c->list);
    c->message[0] = '\0';
    c->message_actionable = 0;
    c->line_valid = 0;
}

static void set_message(AxynePaletteController *c, const char *text, int actionable)
{
    (void)snprintf(c->message, sizeof(c->message), "%s", text);
    c->message_actionable = actionable;
}

static void load_document(AxynePaletteController *c)
{
    char *path = NULL, *text = NULL;
    size_t length = 0, lines = 1;
    if (c->symbols_loaded) return;
    c->symbols_loaded = 1;
    c->symbols_supported = 0;
    c->document_available = 0;
    if (c->document_fn == NULL ||
        !c->document_fn(c->document_user, &path, &text, &length, &lines)) {
        free(path);
        free(text);
        return;
    }
    c->document_available = 1;
    if (lines > 0) c->line_count = lines;
    if (path != NULL && axyne_symbols_supports_file(path) && text != NULL) {
        AxyneSymbolList symbols = {0};
        if (axyne_symbols_scan(text, length, &symbols, NULL) == AXYNE_STATUS_OK) {
            axyne_symbols_destroy(&c->symbols);
            c->symbols = symbols;
            c->symbols_supported = 1;
        }
    }
    free(path);
    free(text);
}

static AxyneStatus rebuild(AxynePaletteController *c, int keep_selection)
{
    const char *query = c->input == NULL ? "" : c->input + c->query_offset;
    AxyneStatus status = AXYNE_STATUS_OK;
    size_t previous = c->selection;

    clear_view(c);
    switch (c->mode) {
    case AXYNE_PALETTE_MODE_FILE:
        status = axyne_palette_build_files((const char *const *)c->paths,
                                           c->path_count, c->root, query,
                                           &c->list, NULL);
        if (status == AXYNE_STATUS_OK && c->list.count == 0)
            set_message(c, c->walk != NULL ? "파일 검색 중..." : "일치하는 파일 없음", 0);
        break;
    case AXYNE_PALETTE_MODE_COMMAND:
        status = axyne_palette_build_commands(query, c->mac_style, &c->list, NULL);
        if (status == AXYNE_STATUS_OK && c->list.count == 0)
            set_message(c, "일치하는 명령 없음", 0);
        break;
    case AXYNE_PALETTE_MODE_SYMBOL:
        load_document(c);
        if (!c->document_available) {
            set_message(c, "열린 문서 없음", 0);
        } else if (!c->symbols_supported) {
            set_message(c, "지원되지 않는 파일 형식", 0);
        } else {
            status = axyne_palette_build_symbols(&c->symbols, query, &c->list, NULL);
            if (status == AXYNE_STATUS_OK && c->list.count == 0)
                set_message(c, "일치하는 기호 없음", 0);
        }
        break;
    case AXYNE_PALETTE_MODE_LINE: {
        size_t line = 0, column = 0;
        load_document(c);
        if (c->line_count == 0) c->line_count = 1;
        if (axyne_palette_parse_line(query, &line, &column)) {
            char hint[96];
            line = axyne_palette_clamp_line(line, c->line_count);
            c->target_line = line;
            c->target_column = column;
            c->line_valid = 1;
            if (strchr(query, ':') != NULL && column > 1)
                (void)snprintf(hint, sizeof(hint), "%zu줄 %zu열로 이동", line, column);
            else
                (void)snprintf(hint, sizeof(hint), "%zu줄로 이동", line);
            set_message(c, hint, 1);
        } else {
            char hint[96];
            (void)snprintf(hint, sizeof(hint), "이동할 줄 번호를 입력하세요 (1-%zu)",
                           c->line_count);
            set_message(c, hint, 0);
        }
        break;
    }
    }
    if (status != AXYNE_STATUS_OK) {
        clear_view(c);
        return status;
    }
    {
        size_t rows = axyne_palette_ctl_row_count(c);
        c->selection = keep_selection && previous < rows ? previous : 0;
        if (c->scroll > c->selection) c->scroll = c->selection;
        if (!keep_selection) c->scroll = 0;
        if (rows <= AXYNE_PALETTE_VISIBLE_ROWS) c->scroll = 0;
        else if (c->scroll > rows - AXYNE_PALETTE_VISIBLE_ROWS)
            c->scroll = rows - AXYNE_PALETTE_VISIBLE_ROWS;
    }
    return AXYNE_STATUS_OK;
}

/* ---- public API --------------------------------------------------------- */

void axyne_palette_ctl_init(AxynePaletteController *c, int mac_style,
                            AxynePaletteDocumentFn document_fn,
                            void *document_user)
{
    if (c == NULL) return;
    memset(c, 0, sizeof(*c));
    c->mac_style = mac_style;
    c->document_fn = document_fn;
    c->document_user = document_user;
}

void axyne_palette_ctl_close(AxynePaletteController *c)
{
    size_t i;
    if (c == NULL) return;
    walk_finish(c);
    for (i = 0; i < c->path_count; ++i) free(c->paths[i]);
    free(c->paths);
    c->paths = NULL;
    c->path_count = c->path_capacity = c->open_count = 0;
    free(c->root);
    c->root = NULL;
    free(c->input);
    c->input = NULL;
    axyne_palette_list_destroy(&c->list);
    axyne_symbols_destroy(&c->symbols);
    memset(&c->symbols, 0, sizeof(c->symbols));
    c->symbols_loaded = c->symbols_supported = c->document_available = 0;
    c->message[0] = '\0';
    c->message_actionable = 0;
    c->selection = c->scroll = 0;
    c->mode = AXYNE_PALETTE_MODE_FILE;
    c->query_offset = 0;
    c->line_valid = 0;
    c->walk_truncated = 0;
    c->active = 0;
}

void axyne_palette_ctl_destroy(AxynePaletteController *c)
{
    axyne_palette_ctl_close(c);
}

AxyneStatus axyne_palette_ctl_open(AxynePaletteController *c, const char *root,
                                   const char *const *open_paths,
                                   size_t open_count, const char *initial_input)
{
    size_t i;
    AxyneStatus status;
    if (c == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    axyne_palette_ctl_close(c);
    c->line_count = 1;
    for (i = 0; i < open_count && open_paths != NULL; ++i) {
        if (open_paths[i] == NULL || open_paths[i][0] == '\0') continue;
        if (!add_path(c, open_paths[i])) {
            axyne_palette_ctl_close(c);
            return AXYNE_STATUS_OUT_OF_MEMORY;
        }
    }
    c->open_count = c->path_count;
    if (root != NULL && root[0] != '\0') {
        Walk *walk;
        c->root = copy_text(root);
        walk = (Walk *)calloc(1, sizeof(*walk));
        if (c->root == NULL || walk == NULL) {
            free(walk);
            axyne_palette_ctl_close(c);
            return AXYNE_STATUS_OUT_OF_MEMORY;
        }
        c->walk = walk;
        if (!walk_push(walk, root, 0) || walk->count == 0) walk_finish(c);
    }
    c->active = 1;
    status = axyne_palette_ctl_set_input(c, initial_input == NULL ? "" : initial_input);
    if (status != AXYNE_STATUS_OK) axyne_palette_ctl_close(c);
    return status;
}

AxyneStatus axyne_palette_ctl_set_input(AxynePaletteController *c, const char *input)
{
    const char *query = NULL;
    char *copy;
    if (c == NULL || !c->active) return AXYNE_STATUS_INVALID_ARGUMENT;
    copy = copy_text(input);
    if (copy == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    free(c->input);
    c->input = copy;
    c->mode = axyne_palette_parse_mode(c->input, &query);
    c->query_offset = (size_t)(query - c->input);
    return rebuild(c, 0);
}

int axyne_palette_ctl_walk_running(const AxynePaletteController *c)
{
    return c != NULL && c->walk != NULL;
}

int axyne_palette_ctl_walk_step(AxynePaletteController *c, size_t budget, int *changed)
{
    Walk *walk;
    size_t added = 0;
    if (changed != NULL) *changed = 0;
    if (c == NULL || !c->active || c->walk == NULL) return 0;
    walk = (Walk *)c->walk;
    while (budget > 0 && walk->count > 0) {
        WalkFrame *frame = &walk->frames[walk->count - 1];
        AxyneFileEntry *entry;
        if (frame->next >= frame->list.count) {
            axyne_fs_free_directory_list(&frame->list);
            --walk->count;
            continue;
        }
        entry = &frame->list.entries[frame->next++];
        --budget;
        if (entry->kind == AXYNE_FILE_KIND_DIRECTORY) {
            if (frame->depth < AXYNE_PALETTE_WALK_MAX_DEPTH) {
                size_t depth = frame->depth + 1;
                /* `frame` may move when the stack grows; do not reuse it. */
                if (!walk_push(walk, entry->path, depth)) walk_clear(walk);
                else if (walk->count > 0) {
                    size_t listed = walk->frames[walk->count - 1].list.count;
                    budget = listed >= budget ? 0 : budget - listed;
                }
            }
        } else if (entry->path != NULL) {
            size_t i;
            int duplicate = 0;
            for (i = 0; i < c->open_count && !duplicate; ++i)
                duplicate = same_path(c->paths[i], entry->path);
            if (duplicate) continue;
            if (c->path_count >= AXYNE_PALETTE_WALK_MAX_PATHS) {
                c->walk_truncated = 1;
                walk_clear(walk);
                break;
            }
            if (!add_path(c, entry->path)) {
                walk_clear(walk);
                break;
            }
            ++added;
        }
    }
    if (walk->count == 0) walk_finish(c);
    if ((added > 0 || c->walk == NULL) && c->mode == AXYNE_PALETTE_MODE_FILE) {
        if (rebuild(c, 1) == AXYNE_STATUS_OK && changed != NULL) *changed = 1;
    }
    return c->walk != NULL;
}

size_t axyne_palette_ctl_row_count(const AxynePaletteController *c)
{
    if (c == NULL) return 0;
    if (c->list.count > 0) return c->list.count;
    return c->message[0] != '\0' ? 1 : 0;
}

size_t axyne_palette_ctl_visible_rows(const AxynePaletteController *c)
{
    size_t rows = axyne_palette_ctl_row_count(c);
    if (rows == 0) return 1;
    return rows < AXYNE_PALETTE_VISIBLE_ROWS ? rows : AXYNE_PALETTE_VISIBLE_ROWS;
}

static void keep_visible(AxynePaletteController *c)
{
    size_t rows = axyne_palette_ctl_row_count(c);
    if (c->selection < c->scroll) c->scroll = c->selection;
    else if (c->selection >= c->scroll + AXYNE_PALETTE_VISIBLE_ROWS)
        c->scroll = c->selection + 1 - AXYNE_PALETTE_VISIBLE_ROWS;
    if (rows <= AXYNE_PALETTE_VISIBLE_ROWS) c->scroll = 0;
    else if (c->scroll > rows - AXYNE_PALETTE_VISIBLE_ROWS)
        c->scroll = rows - AXYNE_PALETTE_VISIBLE_ROWS;
}

void axyne_palette_ctl_move(AxynePaletteController *c, int delta)
{
    size_t rows;
    if (c == NULL) return;
    rows = axyne_palette_ctl_row_count(c);
    if (rows == 0) return;
    if (delta < 0) {
        size_t step = (size_t)(-(long long)delta) % rows;
        c->selection = (c->selection + rows - step) % rows;
    } else {
        c->selection = (c->selection + (size_t)delta % rows) % rows;
    }
    keep_visible(c);
}

void axyne_palette_ctl_select(AxynePaletteController *c, size_t index)
{
    size_t rows;
    if (c == NULL) return;
    rows = axyne_palette_ctl_row_count(c);
    if (rows == 0) return;
    c->selection = index < rows ? index : rows - 1;
    keep_visible(c);
}

void axyne_palette_ctl_scroll(AxynePaletteController *c, int rows_delta)
{
    size_t rows;
    if (c == NULL) return;
    rows = axyne_palette_ctl_row_count(c);
    if (rows <= AXYNE_PALETTE_VISIBLE_ROWS) { c->scroll = 0; return; }
    if (rows_delta < 0) {
        size_t step = (size_t)(-(long long)rows_delta);
        c->scroll = step > c->scroll ? 0 : c->scroll - step;
    } else {
        size_t maximum = rows - AXYNE_PALETTE_VISIBLE_ROWS;
        size_t step = (size_t)rows_delta;
        c->scroll = step > maximum - c->scroll ? maximum : c->scroll + step;
    }
}

const char *axyne_palette_ctl_title(AxynePaletteMode mode)
{
    switch (mode) {
    case AXYNE_PALETTE_MODE_COMMAND: return "명령 실행";
    case AXYNE_PALETTE_MODE_SYMBOL: return "기호 이동";
    case AXYNE_PALETTE_MODE_LINE: return "줄 이동";
    case AXYNE_PALETTE_MODE_FILE: break;
    }
    return "파일 이동";
}

void axyne_palette_action_destroy(AxynePaletteAction *action)
{
    if (action == NULL) return;
    free(action->path);
    memset(action, 0, sizeof(*action));
}

int axyne_palette_ctl_activate(AxynePaletteController *c, size_t row,
                               AxynePaletteAction *action)
{
    const AxynePaletteItem *item;
    if (action == NULL) return 0;
    memset(action, 0, sizeof(*action));
    if (c == NULL || !c->active) return 0;
    if (row == AXYNE_PALETTE_NO_ROW) row = c->selection;
    if (c->mode == AXYNE_PALETTE_MODE_LINE) {
        if (!c->line_valid) return 0;
        action->kind = AXYNE_PALETTE_ACTION_GOTO;
        action->line = c->target_line;
        action->column = c->target_column;
        return 1;
    }
    if (row >= c->list.count) return 0;
    item = &c->list.items[row];
    switch (item->kind) {
    case AXYNE_PALETTE_ITEM_FILE:
        if (item->payload < 0 || (size_t)item->payload >= c->path_count) return 0;
        action->path = copy_text(c->paths[item->payload]);
        if (action->path == NULL) return 0;
        action->kind = AXYNE_PALETTE_ACTION_OPEN_FILE;
        return 1;
    case AXYNE_PALETTE_ITEM_COMMAND:
        switch ((AxynePaletteCommandId)item->payload) {
        case AXYNE_PALETTE_COMMAND_QUICK_FILE:
            action->kind = AXYNE_PALETTE_ACTION_SET_INPUT;
            action->text = "";
            return 1;
        case AXYNE_PALETTE_COMMAND_GO_TO_LINE:
            action->kind = AXYNE_PALETTE_ACTION_SET_INPUT;
            action->text = ":";
            return 1;
        case AXYNE_PALETTE_COMMAND_GO_TO_SYMBOL:
            action->kind = AXYNE_PALETTE_ACTION_SET_INPUT;
            action->text = "@";
            return 1;
        default:
            action->kind = AXYNE_PALETTE_ACTION_COMMAND;
            action->command = (AxynePaletteCommandId)item->payload;
            return 1;
        }
    case AXYNE_PALETTE_ITEM_SYMBOL:
        action->kind = AXYNE_PALETTE_ACTION_GOTO;
        action->line = axyne_palette_clamp_line((size_t)item->payload,
                                                c->line_count > 0 ? c->line_count : 1);
        action->column = item->payload2 > 0 ? (size_t)item->payload2 : 1;
        return 1;
    }
    return 0;
}
