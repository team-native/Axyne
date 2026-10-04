#include "axyne/palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/ui_design.h"

/* ---- small helpers ------------------------------------------------------ */

static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int is_continuation(unsigned char c) { return (c & 0xC0u) == 0x80u; }

static int is_separator(char c) { return c == '/' || c == '\\'; }

static int is_blank(char c) { return c == ' ' || c == '\t'; }

static char *dup_n(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    if (length != 0) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static AxyneStatus fail(AxyneError *error, AxyneStatus status, const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static AxyneStatus succeed(AxyneError *error)
{
    if (error != NULL) {
        error->code = AXYNE_STATUS_OK;
        error->message[0] = '\0';
    }
    return AXYNE_STATUS_OK;
}

/* ---- modes -------------------------------------------------------------- */

AxynePaletteMode axyne_palette_parse_mode(const char *input, const char **query)
{
    AxynePaletteMode mode = AXYNE_PALETTE_MODE_FILE;
    const char *rest;
    if (input == NULL) input = "";
    rest = input;
    switch (input[0]) {
    case '>': mode = AXYNE_PALETTE_MODE_COMMAND; break;
    case '@': mode = AXYNE_PALETTE_MODE_SYMBOL; break;
    case ':': mode = AXYNE_PALETTE_MODE_LINE; break;
    default: break;
    }
    if (mode != AXYNE_PALETTE_MODE_FILE) {
        rest = input + 1;
        while (is_blank(*rest)) ++rest;
    }
    if (query != NULL) *query = rest;
    return mode;
}

const char *axyne_palette_mode_prefix(AxynePaletteMode mode)
{
    switch (mode) {
    case AXYNE_PALETTE_MODE_COMMAND: return ">";
    case AXYNE_PALETTE_MODE_SYMBOL: return "@";
    case AXYNE_PALETTE_MODE_LINE: return ":";
    default: return "";
    }
}

/* ---- matching ----------------------------------------------------------- */

static int equal_at(const char *text, const char *query, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (fold((unsigned char)text[i]) != fold((unsigned char)query[i])) return 0;
    return 1;
}

static int is_word_boundary(const char *text, size_t at)
{
    char previous;
    if (at == 0) return 1;
    previous = text[at - 1];
    if (previous == '/' || previous == '\\' || previous == '.' ||
        previous == '_' || previous == '-' || previous == ' ')
        return 1;
    return previous >= 'a' && previous <= 'z' && text[at] >= 'A' && text[at] <= 'Z';
}

int axyne_palette_match(const char *text, const char *query,
                        AxynePaletteMatch *match)
{
    size_t text_length, query_length, penalty;
    int best = 0;
    size_t best_start = 0;

    if (text == NULL) text = "";
    if (query == NULL || query[0] == '\0') {
        if (match != NULL) {
            match->score = 0;
            match->start = 0;
            match->length = 0;
        }
        return 1;
    }
    text_length = strlen(text);
    query_length = strlen(query);
    if (query_length > text_length) return 0;
    penalty = text_length < 199 ? text_length : 199;
    for (size_t i = 0; i + query_length <= text_length; ++i) {
        int tier, score;
        if (is_continuation((unsigned char)text[i])) continue;
        if (i + query_length < text_length &&
            is_continuation((unsigned char)text[i + query_length]))
            continue;
        if (!equal_at(text + i, query, query_length)) continue;
        if (i == 0)
            tier = query_length == text_length ? AXYNE_PALETTE_SCORE_EXACT
                                               : AXYNE_PALETTE_SCORE_PREFIX;
        else if (is_word_boundary(text, i))
            tier = AXYNE_PALETTE_SCORE_BOUNDARY;
        else
            tier = AXYNE_PALETTE_SCORE_SUBSTRING;
        score = tier - (int)penalty;
        if (score > best) {
            best = score;
            best_start = i;
        }
        if (tier >= AXYNE_PALETTE_SCORE_BOUNDARY) break;
    }
    if (best == 0) return 0;
    if (match != NULL) {
        match->score = best;
        match->start = best_start;
        match->length = query_length;
    }
    return 1;
}

/* ---- result list -------------------------------------------------------- */

void axyne_palette_list_destroy(AxynePaletteList *list)
{
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].label);
        free(list->items[i].detail);
        free(list->items[i].badge);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

typedef struct Candidate {
    int score;
    size_t index; /* position in the input, the tie-breaker */
    size_t start, length;
    int by_keyword; /* command matched through its keywords */
} Candidate;

static int compare_candidates(const void *left, const void *right)
{
    const Candidate *a = (const Candidate *)left, *b = (const Candidate *)right;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    if (a->index != b->index) return a->index < b->index ? -1 : 1;
    return 0;
}

/* Sorts the candidates and returns how many are kept. */
static size_t select_top(Candidate *candidates, size_t count)
{
    if (count > 1) qsort(candidates, count, sizeof(*candidates), compare_candidates);
    return count < AXYNE_PALETTE_MAX_ITEMS ? count : AXYNE_PALETTE_MAX_ITEMS;
}

static int fill_item(AxynePaletteItem *item, AxynePaletteItemKind kind,
                     const char *label, size_t label_length,
                     const char *detail, size_t detail_length,
                     const char *badge, uint32_t badge_color,
                     const Candidate *candidate, int payload, int payload2)
{
    memset(item, 0, sizeof(*item));
    item->label = dup_n(label, label_length);
    item->detail = dup_n(detail, detail_length);
    item->badge = dup_n(badge, strlen(badge));
    if (item->label == NULL || item->detail == NULL || item->badge == NULL) {
        free(item->label);
        free(item->detail);
        free(item->badge);
        memset(item, 0, sizeof(*item));
        return 0;
    }
    item->badge_color = badge_color;
    item->kind = kind;
    item->match_start = candidate->length != 0 ? candidate->start : 0;
    item->match_len = candidate->length;
    item->payload = payload;
    item->payload2 = payload2;
    return 1;
}

static AxyneStatus allocate_list(AxynePaletteList *out, size_t count,
                                 AxyneError *error)
{
    out->items = NULL;
    out->count = 0;
    if (count == 0) return succeed(error);
    out->items = (AxynePaletteItem *)calloc(count, sizeof(*out->items));
    if (out->items == NULL)
        return fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                    "Unable to allocate palette results");
    return succeed(error);
}

static int has_query(const char *query) { return query != NULL && query[0] != '\0'; }

/* ---- commands ----------------------------------------------------------- */

#define PRIMARY AXYNE_PALETTE_MOD_PRIMARY
#define SHIFT AXYNE_PALETTE_MOD_SHIFT
#define ALT AXYNE_PALETTE_MOD_ALT

/* The shortcuts mirror the Windows accelerators and the macOS system menu. */
static const AxynePaletteCommand command_table[] = {
    { AXYNE_PALETTE_COMMAND_NEW_FILE, "새 파일", "new file create",
      { PRIMARY, "N", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_OPEN_FILE, "파일 열기", "open file",
      { PRIMARY, "O", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_OPEN_FOLDER, "폴더 열기", "open folder workspace",
      { PRIMARY | SHIFT, "O", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_SAVE, "저장", "save",
      { PRIMARY, "S", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_SAVE_AS, "다른 이름으로 저장", "save as",
      { PRIMARY | SHIFT, "S", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_CLOSE_TAB, "탭 닫기", "close tab",
      { PRIMARY, "W", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_FIND, "찾기", "find search",
      { PRIMARY, "F", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_REPLACE, "바꾸기", "replace",
      { PRIMARY, "H", PRIMARY | ALT, "F" } },
    { AXYNE_PALETTE_COMMAND_QUICK_FILE, "파일로 이동", "quick open go to file",
      { PRIMARY, "P", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_GO_TO_LINE, "줄로 이동", "go to line",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_GO_TO_SYMBOL, "심볼로 이동", "go to symbol outline",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_BUILD, "빌드", "build compile",
      { PRIMARY, "B", 0, NULL } },
    { AXYNE_PALETTE_COMMAND_RUN, "실행", "run",
      { 0, "F5", PRIMARY, "R" } },
    { AXYNE_PALETTE_COMMAND_START_DEBUGGING, "디버깅 시작", "start debugging debug",
      { 0, NULL, 0, "F5" } },
    { AXYNE_PALETTE_COMMAND_GIT_STATUS, "Git 상태", "git status",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_GIT_DIFF, "Git 변경 내용", "git diff changes",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_GIT_STAGE_ALL, "Git 모두 스테이지", "git stage all add",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_GIT_UNSTAGE_ALL, "Git 모두 스테이지 해제",
      "git unstage all reset", { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_PREFERENCES, "환경설정", "preferences settings options",
      { 0, NULL, PRIMARY, "," } },
    { AXYNE_PALETTE_COMMAND_WORKSPACE_SETTINGS, "작업 영역 설정",
      "workspace settings", { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_PANEL_OUTPUT, "출력 패널 선택", "output panel show",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_PANEL_PROBLEMS, "문제 패널 선택", "problems panel show errors",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_PANEL_TERMINAL, "터미널 패널 선택", "terminal panel show",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_CLEAR_OUTPUT, "출력 지우기", "clear output",
      { 0, NULL, 0, NULL } },
    { AXYNE_PALETTE_COMMAND_CONFIGURE_RUNNER, "실행 구성 (Runner 설정)",
      "runner configure settings build run command", { 0, NULL, 0, NULL } }
};

#undef PRIMARY
#undef SHIFT
#undef ALT

#define COMMAND_COUNT (sizeof(command_table) / sizeof(command_table[0]))

const AxynePaletteCommand *axyne_palette_commands(size_t *count)
{
    if (count != NULL) *count = COMMAND_COUNT;
    return command_table;
}

const AxynePaletteCommand *axyne_palette_command(AxynePaletteCommandId id)
{
    for (size_t i = 0; i < COMMAND_COUNT; ++i)
        if (command_table[i].id == id) return &command_table[i];
    return NULL;
}

size_t axyne_palette_format_shortcut(const AxynePaletteShortcut *shortcut,
                                     int mac_style, char *buffer,
                                     size_t capacity)
{
    char text[64];
    unsigned flags;
    const char *key;
    size_t length = 0;

    text[0] = '\0';
    if (shortcut != NULL) {
        flags = shortcut->flags;
        key = shortcut->key;
        if (mac_style && shortcut->mac_key != NULL) {
            flags = shortcut->mac_flags;
            key = shortcut->mac_key;
        }
        if (key != NULL) {
            if (mac_style) {
                if (flags & AXYNE_PALETTE_MOD_ALT)
                    length += (size_t)snprintf(text + length, sizeof(text) - length, "\xE2\x8C\xA5");
                if (flags & AXYNE_PALETTE_MOD_SHIFT)
                    length += (size_t)snprintf(text + length, sizeof(text) - length, "\xE2\x87\xA7");
                if (flags & AXYNE_PALETTE_MOD_PRIMARY)
                    length += (size_t)snprintf(text + length, sizeof(text) - length, "\xE2\x8C\x98");
                (void)snprintf(text + length, sizeof(text) - length, "%s", key);
            } else {
                if (flags & AXYNE_PALETTE_MOD_PRIMARY)
                    length += (size_t)snprintf(text + length, sizeof(text) - length, "Ctrl+");
                if (flags & AXYNE_PALETTE_MOD_SHIFT)
                    length += (size_t)snprintf(text + length, sizeof(text) - length, "Shift+");
                if (flags & AXYNE_PALETTE_MOD_ALT)
                    length += (size_t)snprintf(text + length, sizeof(text) - length, "Alt+");
                (void)snprintf(text + length, sizeof(text) - length, "%s", key);
            }
        }
    }
    length = strlen(text);
    if (buffer != NULL && capacity != 0) {
        size_t copy = length < capacity - 1 ? length : capacity - 1;
        memcpy(buffer, text, copy);
        buffer[copy] = '\0';
    }
    return length;
}

AxyneStatus axyne_palette_build_commands(const char *query, int mac_style,
                                         AxynePaletteList *out,
                                         AxyneError *error)
{
    Candidate candidates[COMMAND_COUNT];
    size_t count = 0, keep;
    AxyneStatus status;

    if (out == NULL)
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid palette arguments");
    out->items = NULL;
    out->count = 0;
    for (size_t i = 0; i < COMMAND_COUNT; ++i) {
        AxynePaletteMatch title, keywords;
        int title_hit = axyne_palette_match(command_table[i].title, query, &title);
        int keyword_hit = has_query(query) &&
            axyne_palette_match(command_table[i].keywords, query, &keywords);
        Candidate *candidate = &candidates[count];
        if (!title_hit && !keyword_hit) continue;
        memset(candidate, 0, sizeof(*candidate));
        candidate->index = i;
        if (title_hit && (!keyword_hit || title.score >= keywords.score / 2)) {
            candidate->score = title.score;
            candidate->start = title.start;
            candidate->length = title.length;
        } else {
            candidate->score = keywords.score / 2 > 0 ? keywords.score / 2 : 1;
            candidate->by_keyword = 1;
        }
        ++count;
    }
    keep = select_top(candidates, count);
    status = allocate_list(out, keep, error);
    if (status != AXYNE_STATUS_OK) return status;
    for (size_t i = 0; i < keep; ++i) {
        const AxynePaletteCommand *command = &command_table[candidates[i].index];
        char shortcut[64];
        size_t length = axyne_palette_format_shortcut(&command->shortcut,
                                                      mac_style, shortcut,
                                                      sizeof(shortcut));
        if (!fill_item(&out->items[i], AXYNE_PALETTE_ITEM_COMMAND,
                       command->title, strlen(command->title), shortcut,
                       length < sizeof(shortcut) ? length : sizeof(shortcut) - 1,
                       "", 0x8b919b, &candidates[i], (int)command->id, 0)) {
            axyne_palette_list_destroy(out);
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                        "Unable to allocate palette results");
        }
        ++out->count;
    }
    return succeed(error);
}

/* ---- files -------------------------------------------------------------- */

static int path_char_equal(char a, char b)
{
#ifdef _WIN32
    if (is_separator(a) && is_separator(b)) return 1;
    return fold((unsigned char)a) == fold((unsigned char)b);
#else
    return a == b;
#endif
}

/* Bytes of `path` taken up by `root` plus the separators after it, or 0 when
 * the path is not below the root. */
static size_t root_prefix(const char *path, const char *root)
{
    size_t root_length, i;
    if (root == NULL || root[0] == '\0') return 0;
    root_length = strlen(root);
    while (root_length > 0 && is_separator(root[root_length - 1])) --root_length;
    for (i = 0; i < root_length; ++i)
        if (path[i] == '\0' || !path_char_equal(path[i], root[i])) return 0;
    if (!is_separator(path[root_length])) return 0;
    i = root_length;
    while (is_separator(path[i])) ++i;
    return i;
}

AxyneStatus axyne_palette_build_files(const char *const *paths,
                                      size_t path_count, const char *root,
                                      const char *query,
                                      AxynePaletteList *out,
                                      AxyneError *error)
{
    Candidate *candidates = NULL;
    size_t count = 0, keep, limit;
    int filtering = has_query(query);
    AxyneStatus status;

    if (out == NULL || (paths == NULL && path_count != 0))
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid palette arguments");
    out->items = NULL;
    out->count = 0;
    limit = filtering ? path_count : (path_count < AXYNE_PALETTE_MAX_ITEMS
                                          ? path_count : AXYNE_PALETTE_MAX_ITEMS);
    if (limit != 0) {
        candidates = (Candidate *)malloc(limit * sizeof(*candidates));
        if (candidates == NULL)
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                        "Unable to allocate palette candidates");
    }
    for (size_t i = 0; i < path_count && count < limit; ++i) {
        const char *base;
        AxynePaletteMatch match;
        if (paths[i] == NULL) continue;
        base = paths[i];
        for (const char *p = paths[i]; *p != '\0'; ++p)
            if (is_separator(*p)) base = p + 1;
        if (base[0] == '\0') continue;
        if (!axyne_palette_match(base, query, &match)) continue;
        memset(&candidates[count], 0, sizeof(candidates[count]));
        candidates[count].score = match.score;
        candidates[count].index = i;
        candidates[count].start = match.start;
        candidates[count].length = match.length;
        ++count;
    }
    keep = select_top(candidates, count);
    status = allocate_list(out, keep, error);
    if (status != AXYNE_STATUS_OK) {
        free(candidates);
        return status;
    }
    for (size_t i = 0; i < keep; ++i) {
        const char *path = paths[candidates[i].index];
        const char *base = path, *rest, *last = NULL;
        size_t prefix = root_prefix(path, root), detail_length;
        AxyneFileBadge badge;
        for (const char *p = path; *p != '\0'; ++p)
            if (is_separator(*p)) base = p + 1;
        rest = path + prefix;
        for (const char *p = rest; *p != '\0'; ++p)
            if (is_separator(*p)) last = p;
        detail_length = last == NULL ? 0 : (size_t)(last - rest);
        if (last != NULL && last == rest && prefix == 0) detail_length = 1;
        badge = axyne_ui_file_badge(base);
        if (!fill_item(&out->items[i], AXYNE_PALETTE_ITEM_FILE, base, strlen(base),
                       rest, detail_length, badge.label, badge.color,
                       &candidates[i], (int)candidates[i].index, 0)) {
            axyne_palette_list_destroy(out);
            free(candidates);
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                        "Unable to allocate palette results");
        }
        ++out->count;
    }
    free(candidates);
    return succeed(error);
}

/* ---- symbols ------------------------------------------------------------ */

static const char *symbol_badge(AxyneSymbolKind kind, uint32_t *color)
{
    switch (kind) {
    case AXYNE_SYMBOL_FUNCTION: *color = 0x7db5e3; return "f";
    case AXYNE_SYMBOL_MACRO: *color = 0xd98e73; return "#";
    case AXYNE_SYMBOL_TYPE: *color = 0xc79ad9; return "T";
    default: *color = 0xd9b36c; return "v";
    }
}

AxyneStatus axyne_palette_build_symbols(const AxyneSymbolList *symbols,
                                        const char *query,
                                        AxynePaletteList *out,
                                        AxyneError *error)
{
    Candidate *candidates = NULL;
    size_t total, count = 0, keep, limit;
    int filtering = has_query(query);
    AxyneStatus status;

    if (out == NULL)
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid palette arguments");
    out->items = NULL;
    out->count = 0;
    total = symbols == NULL ? 0 : symbols->count;
    limit = filtering ? total : (total < AXYNE_PALETTE_MAX_ITEMS
                                     ? total : AXYNE_PALETTE_MAX_ITEMS);
    if (limit != 0) {
        candidates = (Candidate *)malloc(limit * sizeof(*candidates));
        if (candidates == NULL)
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                        "Unable to allocate palette candidates");
    }
    for (size_t i = 0; i < total && count < limit; ++i) {
        AxynePaletteMatch match;
        if (symbols->items[i].name == NULL ||
            !axyne_palette_match(symbols->items[i].name, query, &match))
            continue;
        memset(&candidates[count], 0, sizeof(candidates[count]));
        candidates[count].score = match.score;
        candidates[count].index = i;
        candidates[count].start = match.start;
        candidates[count].length = match.length;
        ++count;
    }
    keep = select_top(candidates, count);
    status = allocate_list(out, keep, error);
    if (status != AXYNE_STATUS_OK) {
        free(candidates);
        return status;
    }
    for (size_t i = 0; i < keep; ++i) {
        const AxyneSymbol *symbol = &symbols->items[candidates[i].index];
        char detail[48];
        uint32_t color;
        const char *badge = symbol_badge(symbol->kind, &color);
        int length = snprintf(detail, sizeof(detail), "줄 %lu",
                              (unsigned long)symbol->line);
        if (length < 0) length = 0;
        if ((size_t)length >= sizeof(detail)) length = (int)sizeof(detail) - 1;
        if (!fill_item(&out->items[i], AXYNE_PALETTE_ITEM_SYMBOL, symbol->name,
                       strlen(symbol->name), detail, (size_t)length, badge, color,
                       &candidates[i],
                       symbol->line > 2147483647u ? 2147483647 : (int)symbol->line,
                       symbol->column > 2147483647u ? 2147483647 : (int)symbol->column)) {
            axyne_palette_list_destroy(out);
            free(candidates);
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                        "Unable to allocate palette results");
        }
        ++out->count;
    }
    free(candidates);
    return succeed(error);
}

/* ---- line mode ---------------------------------------------------------- */

static int parse_number(const char **cursor, size_t *value)
{
    const char *p = *cursor;
    size_t number = 0;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        number = number * 10 + (size_t)(*p - '0');
        if (number > 2147483647u) number = 2147483647u;
        ++p;
    }
    *cursor = p;
    *value = number;
    return 1;
}

int axyne_palette_parse_line(const char *text, size_t *line, size_t *column)
{
    const char *p = text;
    size_t parsed_line = 0, parsed_column = 1;
    if (text == NULL) return 0;
    while (is_blank(*p)) ++p;
    if (*p == ':') ++p;
    while (is_blank(*p)) ++p;
    if (!parse_number(&p, &parsed_line) || parsed_line == 0) return 0;
    while (is_blank(*p)) ++p;
    if (*p == ':') {
        ++p;
        while (is_blank(*p)) ++p;
        if (*p != '\0') {
            if (!parse_number(&p, &parsed_column)) return 0;
            if (parsed_column == 0) parsed_column = 1;
            while (is_blank(*p)) ++p;
        }
    }
    if (*p != '\0') return 0;
    if (line != NULL) *line = parsed_line;
    if (column != NULL) *column = parsed_column;
    return 1;
}

size_t axyne_palette_clamp_line(size_t line, size_t line_count)
{
    if (line_count == 0) line_count = 1;
    if (line < 1) return 1;
    return line > line_count ? line_count : line;
}

size_t axyne_palette_clamp_column(size_t column, size_t line_length)
{
    size_t maximum = line_length == (size_t)-1 ? line_length : line_length + 1;
    if (column < 1) return 1;
    return column > maximum ? maximum : column;
}
