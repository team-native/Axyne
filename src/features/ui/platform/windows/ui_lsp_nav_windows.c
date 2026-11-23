#include "ui_windows_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/completion.h"
#include "axyne/syntax.h"

enum {
    AXYNE_WM_LSP_COMPLETION = AXYNE_WM_FEATURE(AXYNE_WIN_FEATURE_LSP_NAV, 0),
    AXYNE_WM_LSP_NAVIGATION = AXYNE_WM_FEATURE(AXYNE_WIN_FEATURE_LSP_NAV, 1),
    AXYNE_WM_LSP_RESULT = AXYNE_WM_FEATURE(AXYNE_WIN_FEATURE_LSP_NAV, 2)
};

/* One completion item as the UI keeps it: everything needed to rank it, show
 * it and apply it, owned copies of what the reader thread received. */
typedef struct AxyneWinLspItem {
    char *label;
    char *insert_text;
    char *detail;
    char *label_detail;
    char *documentation;
    char *sort_text;
    char *filter_text;
    char *raw_json;
    int kind;
    int deprecated;
    int preselect;
    int has_edit;
    AxyneLspTextEdit edit;
    AxyneLspTextEdit *extra;
    size_t extra_count;
    int resolve_sent;
    uint64_t resolve_id;
} AxyneWinLspItem;

struct AxyneWinLspNavUi {
    uint64_t completion_request;
    LRESULT completion_caret;
    char *completion_path;
    uint64_t definition_request;
    char *definition_path;
    uint64_t references_request;
    char *references_path;
    /* The LSP list on screen: all items of the last response, and the
     * indexes of those that match the typed prefix in display order. */
    AxyneWinLspItem *items;
    size_t item_count;
    size_t *order;
    size_t order_count;
    size_t prefix_bytes;
    int list_active;
    int icons_registered;
    char detail_text[1024];
};

typedef struct AxyneWinLspCompletionMessage {
    AxyneLspClient *client;
    uint64_t request_id;
    AxyneWinLspItem *items;
    size_t count;
} AxyneWinLspCompletionMessage;

static char *axyne_win_lsp_dup(const char *text)
{
    return text != NULL ? _strdup(text) : NULL;
}

static void axyne_win_lsp_item_free(AxyneWinLspItem *item)
{
    free(item->label); free(item->insert_text); free(item->detail);
    free(item->label_detail); free(item->documentation); free(item->sort_text);
    free(item->filter_text); free(item->raw_json); free(item->edit.new_text);
    for (size_t i = 0; i < item->extra_count; ++i) free(item->extra[i].new_text);
    free(item->extra);
    memset(item, 0, sizeof(*item));
}

static void axyne_win_lsp_items_free(AxyneWinLspItem *items, size_t count)
{
    for (size_t i = 0; i < count; ++i) axyne_win_lsp_item_free(&items[i]);
    free(items);
}

static int axyne_win_lsp_item_from(const AxyneLspCompletionItem *source,
                                   AxyneWinLspItem *item)
{
    memset(item, 0, sizeof(*item));
    if (source->label == NULL) return 0;
    item->label = axyne_win_lsp_dup(source->label);
    item->insert_text = axyne_win_lsp_dup(source->insert_text);
    item->detail = axyne_win_lsp_dup(source->detail);
    item->label_detail = axyne_win_lsp_dup(source->label_detail);
    item->documentation = axyne_win_lsp_dup(source->documentation);
    item->sort_text = axyne_win_lsp_dup(source->sort_text);
    item->filter_text = axyne_win_lsp_dup(source->filter_text);
    item->raw_json = axyne_win_lsp_dup(source->raw_json);
    item->kind = source->kind;
    item->deprecated = source->deprecated;
    item->preselect = source->preselect;
    if (source->has_text_edit && source->text_edit.new_text != NULL) {
        item->has_edit = 1;
        item->edit.range = source->text_edit.range;
        item->edit.new_text = axyne_win_lsp_dup(source->text_edit.new_text);
    }
    if (source->additional_edit_count > 0) {
        item->extra = (AxyneLspTextEdit *)calloc(source->additional_edit_count,
                                                  sizeof(*item->extra));
        if (item->extra != NULL) {
            for (size_t i = 0; i < source->additional_edit_count; ++i) {
                item->extra[i].range = source->additional_edits[i].range;
                item->extra[i].new_text = axyne_win_lsp_dup(source->additional_edits[i].new_text);
            }
            item->extra_count = source->additional_edit_count;
        }
    }
    if (item->label == NULL) { axyne_win_lsp_item_free(item); return 0; }
    return 1;
}

static void axyne_win_lsp_free_completion(AxyneWinLspCompletionMessage *message)
{
    if (message == NULL) return;
    axyne_win_lsp_items_free(message->items, message->count);
    free(message);
}

typedef struct AxyneWinLspNavigationMessage {
    AxyneLspClient *client;
    uint64_t request_id;
    size_t count;
    char *uri;
    AxyneLspPosition start;
} AxyneWinLspNavigationMessage;

static void axyne_win_lsp_free_navigation(AxyneWinLspNavigationMessage *message)
{
    if (message == NULL) return;
    free(message->uri);
    free(message);
}

void axyne_win_lsp_nav_drain(HWND window)
{
    MSG pending;
    if (window == NULL) return;
    while (PeekMessageW(&pending, window, AXYNE_WM_LSP_COMPLETION,
                        AXYNE_WM_LSP_COMPLETION, PM_REMOVE))
        axyne_win_lsp_free_completion(
            (AxyneWinLspCompletionMessage *)pending.lParam);
    while (PeekMessageW(&pending, window, AXYNE_WM_LSP_NAVIGATION,
                        AXYNE_WM_LSP_NAVIGATION, PM_REMOVE))
        axyne_win_lsp_free_navigation(
            (AxyneWinLspNavigationMessage *)pending.lParam);
    while (PeekMessageW(&pending, window, AXYNE_WM_LSP_RESULT,
                        AXYNE_WM_LSP_RESULT, PM_REMOVE))
        axyne_lsp_result_free((AxyneLspResult *)pending.lParam);
}

static int axyne_win_lsp_document_supported(const AxyneDocument *document)
{
    return document != NULL && !document->is_untitled && document->path != NULL &&
        document->path[0] != '\0' &&
        axyne_syntax_completes(axyne_syntax_for_path(document->path));
}

static void axyne_win_lsp_clear_request_paths(struct AxyneWinLspNavUi *ui)
{
    if (ui == NULL) return;
    ui->completion_request = 0;
    ui->definition_request = 0;
    ui->references_request = 0;
    free(ui->completion_path);
    free(ui->definition_path);
    free(ui->references_path);
    ui->completion_path = NULL;
    ui->definition_path = NULL;
    ui->references_path = NULL;
}

static void axyne_win_lsp_set_status(AxyneWindowState *state, const char *text)
{
    if (state == NULL) return;
    (void)snprintf(state->lsp_status, sizeof(state->lsp_status), "LSP: %s",
                   text != NULL ? text : "");
    InvalidateRect(state->main_window, NULL, FALSE);
}

static void axyne_win_lsp_forget_shown(struct AxyneWinLspNavUi *ui)
{
    axyne_win_lsp_items_free(ui->items, ui->item_count);
    free(ui->order);
    ui->items = NULL;
    ui->order = NULL;
    ui->item_count = 0;
    ui->order_count = 0;
    ui->list_active = 0;
}

/* ---- ranking ------------------------------------------------------------ */

/* Case-insensitive subsequence match with bonuses for a prefix, consecutive
 * runs and word starts (snake_case, camelCase). -1 when it does not match. */
static int axyne_win_lsp_fuzzy_score(const char *pattern, size_t pattern_length,
                                     const char *text)
{
    size_t text_length = strlen(text), ti = 0, pi;
    int score = 0, run = 0;
    long last = -2;
    if (pattern_length == 0) return 0;
    for (pi = 0; pi < pattern_length; ++pi) {
        int wanted = tolower((unsigned char)pattern[pi]), found = 0;
        for (; ti < text_length; ++ti)
            if (tolower((unsigned char)text[ti]) == wanted) { found = 1; break; }
        if (!found) return -1;
        if ((long)ti == last + 1) { ++run; score += 6 + run; } else run = 0;
        if (ti == 0) score += 12;
        else if (text[ti - 1] == '_' || text[ti - 1] == '-' || text[ti - 1] == '.' ||
                 (isupper((unsigned char)text[ti]) && islower((unsigned char)text[ti - 1])))
            score += 8;
        if (text[ti] == pattern[pi]) score += 1;
        last = (long)ti;
        ++ti;
    }
    if (_strnicmp(text, pattern, pattern_length) == 0)
        score += 100 + (text_length == pattern_length ? 50 : 0);
    score -= (int)((text_length - pattern_length) / 4);
    return score;
}

typedef struct AxyneWinLspRanked {
    size_t index;
    int score;
} AxyneWinLspRanked;

static const AxyneWinLspItem *axyne_win_lsp_rank_items;

static int axyne_win_lsp_rank_compare(const void *left, const void *right)
{
    const AxyneWinLspRanked *a = (const AxyneWinLspRanked *)left;
    const AxyneWinLspRanked *b = (const AxyneWinLspRanked *)right;
    const AxyneWinLspItem *x = &axyne_win_lsp_rank_items[a->index];
    const AxyneWinLspItem *y = &axyne_win_lsp_rank_items[b->index];
    int compare;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    if (x->preselect != y->preselect) return x->preselect ? -1 : 1;
    if (x->deprecated != y->deprecated) return x->deprecated ? 1 : -1;
    compare = strcmp(x->sort_text != NULL ? x->sort_text : x->label,
                     y->sort_text != NULL ? y->sort_text : y->label);
    if (compare != 0) return compare;
    compare = strcmp(x->label, y->label);
    if (compare != 0) return compare;
    return a->index < b->index ? -1 : 1;
}

/* ---- list icons ---------------------------------------------------------- */

enum { AXYNE_LSP_ICON_SIZE = 16, AXYNE_LSP_ICON_GROUPS = 8 };

typedef struct AxyneLspIconStyle {
    uint32_t color;
    char letter;
} AxyneLspIconStyle;

static const AxyneLspIconStyle axyne_lsp_icon_styles[AXYNE_LSP_ICON_GROUPS] = {
    { 0xd18f4d, 'f' }, /* 1: method, function, constructor */
    { 0xa0a63f, 'v' }, /* 2: variable, field, property, value */
    { 0x3f80d9, 'C' }, /* 3: class, interface, struct, enum */
    { 0xd17f9a, 'M' }, /* 4: module, file, folder, reference */
    { 0xd16bb3, 'k' }, /* 5: keyword, operator */
    { 0x3fb4d1, 'c' }, /* 6: constant, enum member */
    { 0x3f80d9, 'T' }, /* 7: type parameter */
    { 0x90857d, '.' }  /* 8: text, snippet, anything else */
};

static int axyne_win_lsp_icon_group(int kind)
{
    switch (kind) {
    case 2: case 3: case 4: return 1;
    case 5: case 6: case 10: case 12: return 2;
    case 7: case 8: case 13: case 22: case 23: return 3;
    case 9: case 17: case 18: case 19: return 4;
    case 14: case 24: return 5;
    case 20: case 21: return 6;
    case 25: return 7;
    default: return 8;
    }
}

/* 5x7 glyphs, one row per string. */
static const char *axyne_win_lsp_glyph(char letter)
{
    switch (letter) {
    case 'f': return "..##." ".#..." "####." ".#..." ".#..." ".#..." ".#...";
    case 'v': return "....." "#...#" "#...#" "#...#" ".#.#." ".#.#." "..#..";
    case 'C': return ".###." "#...#" "#...." "#...." "#...." "#...#" ".###.";
    case 'M': return "#...#" "##.##" "#.#.#" "#...#" "#...#" "#...#" "#...#";
    case 'k': return "#...." "#...." "#..#." "#.#.." "###.." "#..#." "#...#";
    case 'c': return "....." ".###." "#...#" "#...." "#...." "#...#" ".###.";
    case 'T': return "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#..";
    default:  return "....." "....." "....." "..#.." "....." "....." ".....";
    }
}

static void axyne_win_lsp_register_icons(AxyneWindowState *state, struct AxyneWinLspNavUi *ui)
{
    unsigned char pixels[AXYNE_LSP_ICON_SIZE * AXYNE_LSP_ICON_SIZE * 4];
    if (ui->icons_registered || state->editor == NULL) return;
    for (int group = 1; group <= AXYNE_LSP_ICON_GROUPS; ++group) {
        const AxyneLspIconStyle *style = &axyne_lsp_icon_styles[group - 1];
        const char *glyph = axyne_win_lsp_glyph(style->letter);
        memset(pixels, 0, sizeof(pixels));
        for (int y = 1; y < AXYNE_LSP_ICON_SIZE - 1; ++y) {
            for (int x = 1; x < AXYNE_LSP_ICON_SIZE - 1; ++x) {
                int corner = (x == 1 || x == AXYNE_LSP_ICON_SIZE - 2) &&
                             (y == 1 || y == AXYNE_LSP_ICON_SIZE - 2);
                unsigned char *pixel = pixels + (y * AXYNE_LSP_ICON_SIZE + x) * 4;
                if (corner) continue;
                pixel[0] = (unsigned char)(style->color & 0xff);
                pixel[1] = (unsigned char)((style->color >> 8) & 0xff);
                pixel[2] = (unsigned char)((style->color >> 16) & 0xff);
                pixel[3] = 255;
            }
        }
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                unsigned char *pixel;
                if (glyph[row * 5 + column] != '#') continue;
                pixel = pixels + ((4 + row) * AXYNE_LSP_ICON_SIZE + 5 + column) * 4;
                pixel[0] = pixel[1] = pixel[2] = 255;
                pixel[3] = 255;
            }
        }
        SendMessageA(state->editor, SCI_RGBAIMAGESETWIDTH, AXYNE_LSP_ICON_SIZE, 0);
        SendMessageA(state->editor, SCI_RGBAIMAGESETHEIGHT, AXYNE_LSP_ICON_SIZE, 0);
        SendMessageA(state->editor, SCI_REGISTERRGBAIMAGE, (WPARAM)group, (LPARAM)pixels);
    }
    ui->icons_registered = 1;
}

/* ---- detail panel -------------------------------------------------------- */

/* Index into ui->items of the entry highlighted in the list, or -1. */
static long axyne_win_lsp_current_item(AxyneWindowState *state)
{
    struct AxyneWinLspNavUi *ui = state->lsp_nav_ui;
    LRESULT current;
    if (ui == NULL || state->editor == NULL || !ui->list_active) return -1;
    /* A word-completion list (separator ' ') may have replaced ours. */
    if (SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) == 0 ||
        SendMessageA(state->editor, SCI_AUTOCGETSEPARATOR, 0, 0) != '\n')
        return -1;
    current = SendMessageA(state->editor, SCI_AUTOCGETCURRENT, 0, 0);
    if (current < 0 || (size_t)current >= ui->order_count) return -1;
    return (long)ui->order[current];
}

static void axyne_win_lsp_request_resolve(AxyneWindowState *state, size_t index)
{
    struct AxyneWinLspNavUi *ui = state->lsp_nav_ui;
    AxyneWinLspItem *item = &ui->items[index];
    AxyneError error;
    uint64_t request_id = 0;
    if (item->resolve_sent || item->raw_json == NULL || state->lsp == NULL) return;
    item->resolve_sent = 1;
    if (axyne_lsp_resolve_completion(state->lsp, item->raw_json, &request_id, &error) ==
        AXYNE_STATUS_OK)
        item->resolve_id = request_id;
}

/* "kind · detail" plus the documentation, for the panel beside the list. The
 * text before " · " is the accent-coloured head, the rest is muted. */
const char *axyne_win_lsp_completion_detail(AxyneWindowState *state, const char *word)
{
    struct AxyneWinLspNavUi *ui;
    const AxyneWinLspItem *item;
    const char *kind, *signature;
    char *plain = NULL;
    size_t used = 0;
    long index;
    (void)word;
    if (state == NULL || state->lsp_nav_ui == NULL) return NULL;
    ui = state->lsp_nav_ui;
    index = axyne_win_lsp_current_item(state);
    if (index < 0) return NULL;
    item = &ui->items[index];
    if (item->documentation == NULL) axyne_win_lsp_request_resolve(state, (size_t)index);
    kind = axyne_completion_kind_name(item->kind);
    signature = item->detail != NULL && item->detail[0] != '\0' ? item->detail : item->label_detail;
    while (signature != NULL && (*signature == ' ' || *signature == '\t')) ++signature;
    if (signature != NULL && signature[0] == '\0') signature = NULL;
    if (kind == NULL && signature == NULL && item->documentation == NULL && !item->deprecated)
        return NULL;
    used += (size_t)snprintf(ui->detail_text, sizeof(ui->detail_text), "%s%s%s",
                             kind != NULL ? kind : "item",
                             signature != NULL ? " \xc2\xb7 " : "",
                             signature != NULL ? signature : "");
    if (used >= sizeof(ui->detail_text)) used = sizeof(ui->detail_text) - 1;
    if (item->documentation != NULL) {
        plain = axyne_lsp_markdown_to_plain(item->documentation);
        if (plain != NULL && plain[0] != '\0') {
            size_t room = sizeof(ui->detail_text) - used - 1, length = strlen(plain);
            const char *separator = signature != NULL ? "\n\n" : " \xc2\xb7 ";
            if (room > strlen(separator) + 4) {
                room -= strlen(separator);
                if (length > room) {
                    length = room;
                    while (length > 0 && ((unsigned char)plain[length] & 0xc0u) == 0x80u) --length;
                }
                memcpy(ui->detail_text + used, separator, strlen(separator));
                used += strlen(separator);
                memcpy(ui->detail_text + used, plain, length);
                used += length;
                ui->detail_text[used] = '\0';
            }
        }
    }
    free(plain);
    if (item->deprecated && used + 16 < sizeof(ui->detail_text))
        (void)snprintf(ui->detail_text + used, sizeof(ui->detail_text) - used,
                       "%sDeprecated", signature != NULL || item->documentation != NULL ? "\n\n" : " \xc2\xb7 ");
    return ui->detail_text;
}

/* ---- showing the list ---------------------------------------------------- */

static void axyne_win_lsp_present(AxyneWindowState *state)
{
    struct AxyneWinLspNavUi *ui = state->lsp_nav_ui;
    AxyneWinLspRanked *ranked;
    LRESULT caret, word_start;
    char prefix[256];
    size_t prefix_length, matches = 0, length = 0, shown;
    char *list;
    ui->list_active = 0;
    free(ui->order);
    ui->order = NULL;
    ui->order_count = 0;
    caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    word_start = SendMessageA(state->editor, SCI_WORDSTARTPOSITION, (WPARAM)caret, TRUE);
    prefix_length = (size_t)(caret - word_start);
    if (prefix_length >= sizeof(prefix) || ui->item_count == 0) {
        if (SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
            SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
        return;
    }
    prefix[0] = '\0';
    if (prefix_length > 0) {
        struct Sci_TextRange range;
        range.chrg.cpMin = (Sci_PositionCR)word_start;
        range.chrg.cpMax = (Sci_PositionCR)caret;
        range.lpstrText = prefix;
        SendMessageA(state->editor, SCI_GETTEXTRANGE, 0, (LPARAM)&range);
    }
    prefix[prefix_length] = '\0';
    ranked = (AxyneWinLspRanked *)malloc(ui->item_count * sizeof(*ranked));
    if (ranked == NULL) return;
    for (size_t i = 0; i < ui->item_count; ++i) {
        const AxyneWinLspItem *item = &ui->items[i];
        const char *key = item->filter_text != NULL ? item->filter_text : item->label;
        int score = axyne_win_lsp_fuzzy_score(prefix, prefix_length, key);
        /* An item that already equals the typed word has nothing to add. */
        if (score < 0 || (prefix_length > 0 && strlen(item->label) == prefix_length &&
                          _stricmp(item->label, prefix) == 0 && !item->has_edit))
            continue;
        ranked[matches].index = i;
        ranked[matches].score = score;
        ++matches;
    }
    if (matches == 0) {
        free(ranked);
        if (SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
            SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
        return;
    }
    axyne_win_lsp_rank_items = ui->items;
    qsort(ranked, matches, sizeof(*ranked), axyne_win_lsp_rank_compare);
    shown = matches > 200 ? 200 : matches; /* the cap applies after filtering */
    ui->order = (size_t *)malloc(shown * sizeof(*ui->order));
    list = (char *)malloc(shown * 160 + 1);
    if (ui->order == NULL || list == NULL) {
        free(ranked); free(list); free(ui->order); ui->order = NULL;
        if (SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
            SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
        return;
    }
    axyne_win_lsp_register_icons(state, ui);
    for (size_t i = 0; i < shown; ++i) {
        const AxyneWinLspItem *item = &ui->items[ranked[i].index];
        size_t before = length;
        ui->order[i] = ranked[i].index;
        if (i > 0) list[length++] = '\n';
        length += (size_t)snprintf(list + length, 140, "%s%s%s", item->label,
                                   item->label_detail != NULL ? " " : "",
                                   item->label_detail != NULL ? item->label_detail : "");
        if (length > before + 139) {
            length = before + 139;
            /* back up over an incomplete UTF-8 sequence at the cut */
            {
                size_t cut = length;
                while (cut > before && ((unsigned char)list[cut - 1] & 0xc0u) == 0x80u) --cut;
                if (cut > before) {
                    unsigned char lead = (unsigned char)list[cut - 1];
                    size_t need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
                    if (length - (cut - 1) < need) length = cut - 1;
                }
            }
        }
        for (size_t k = before + (i > 0 ? 1 : 0); k < length; ++k)
            if (list[k] == '\n' || list[k] == '\r' || list[k] == 0x1f) list[k] = ' ';
        list[length++] = 0x1f;
        length += (size_t)snprintf(list + length, 8, "%d",
                                   axyne_win_lsp_icon_group(item->kind));
    }
    list[length] = '\0';
    ui->order_count = shown;
    ui->prefix_bytes = prefix_length;
    ui->list_active = 1;
    SendMessageA(state->editor, SCI_AUTOCSETTYPESEPARATOR, 0x1f, 0);
    axyne_win_completion_show_list(state, prefix_length, list, '\n');
    free(list);
    free(ranked);
}

static void axyne_win_lsp_show_completion(
    AxyneWindowState *state, AxyneWinLspCompletionMessage *message)
{
    struct AxyneWinLspNavUi *ui;
    AxyneDocument *document;
    LRESULT caret;
    if (state == NULL || message == NULL || state->editor == NULL ||
        state->lsp_nav_ui == NULL)
        return;
    ui = state->lsp_nav_ui;
    if (state->lsp != message->client ||
        !axyne_win_lsp_document_supported(
            document = axyne_win_active_document(state)) ||
        message->request_id != state->lsp_nav_ui->completion_request ||
        ui->completion_path == NULL || document->path == NULL ||
        strcmp(ui->completion_path, document->path) != 0)
        return;
    caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    if (caret != ui->completion_caret) return;
    axyne_win_lsp_forget_shown(ui);
    ui->items = message->items;
    ui->item_count = message->count;
    message->items = NULL;
    message->count = 0;
    axyne_win_lsp_present(state);
}

/* ---- applying a chosen item ---------------------------------------------- */

typedef struct AxyneWinLspPlannedEdit {
    size_t start, end;
    const char *text;
    size_t order; /* index in the server's list: breaks ties between equal starts */
} AxyneWinLspPlannedEdit;

static int axyne_win_lsp_planned_compare(const void *left, const void *right)
{
    const AxyneWinLspPlannedEdit *a = (const AxyneWinLspPlannedEdit *)left;
    const AxyneWinLspPlannedEdit *b = (const AxyneWinLspPlannedEdit *)right;
    if (a->start != b->start) return a->start > b->start ? -1 : 1; /* last edit first */
    if (a->order != b->order) return a->order > b->order ? -1 : 1;  /* later insert first */
    return 0;
}

/* Replaces the typed prefix (or the item's textEdit range) with the item and
 * applies its additional edits, e.g. an auto-import at the top of the file. */
static void axyne_win_lsp_apply_item(AxyneWindowState *state, const AxyneWinLspItem *item)
{
    AxyneWinLspPlannedEdit *plan;
    const char *text;
    LRESULT length, caret;
    size_t count = 1 + item->extra_count, main_start, main_end, shift = 0, i;
    const char *main_text;
    size_t main_length;
    length = SendMessageA(state->editor, SCI_GETLENGTH, 0, 0);
    caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    text = (const char *)SendMessageA(state->editor, SCI_GETCHARACTERPOINTER, 0, 0);
    if (text == NULL || length < 0 || caret < 0) return;
    plan = (AxyneWinLspPlannedEdit *)malloc(count * sizeof(*plan));
    if (plan == NULL) return;
    if (item->has_edit) {
        main_start = axyne_lsp_offset_for_position(text, (size_t)length, item->edit.range.start);
        main_end = axyne_lsp_offset_for_position(text, (size_t)length, item->edit.range.end);
        main_text = item->edit.new_text;
        if (main_end < main_start) main_end = main_start;
        /* characters typed since the request belong to the replaced word */
        if ((size_t)caret > main_end && (size_t)caret >= main_start) main_end = (size_t)caret;
    } else {
        main_start = (size_t)SendMessageA(state->editor, SCI_WORDSTARTPOSITION, (WPARAM)caret, TRUE);
        if (main_start > (size_t)caret) main_start = (size_t)caret;
        main_end = (size_t)caret;
        main_text = item->insert_text != NULL && item->insert_text[0] != '\0'
            ? item->insert_text : item->label;
    }
    main_length = strlen(main_text);
    plan[0].start = main_start; plan[0].end = main_end; plan[0].text = main_text;
    plan[0].order = 0;
    for (i = 0; i < item->extra_count; ++i) {
        plan[1 + i].start = axyne_lsp_offset_for_position(text, (size_t)length, item->extra[i].range.start);
        plan[1 + i].end = axyne_lsp_offset_for_position(text, (size_t)length, item->extra[i].range.end);
        if (plan[1 + i].end < plan[1 + i].start) plan[1 + i].end = plan[1 + i].start;
        plan[1 + i].text = item->extra[i].new_text != NULL ? item->extra[i].new_text : "";
        plan[1 + i].order = 1 + i;
        if (plan[1 + i].end <= main_start)
            shift += strlen(plan[1 + i].text) - (plan[1 + i].end - plan[1 + i].start);
    }
    qsort(plan, count, sizeof(*plan), axyne_win_lsp_planned_compare);
    SendMessageA(state->editor, SCI_BEGINUNDOACTION, 0, 0);
    for (i = 0; i < count; ++i) {
        SendMessageA(state->editor, SCI_SETTARGETRANGE, (WPARAM)plan[i].start, (LPARAM)plan[i].end);
        SendMessageA(state->editor, SCI_REPLACETARGET, (WPARAM)strlen(plan[i].text),
                     (LPARAM)plan[i].text);
    }
    SendMessageA(state->editor, SCI_ENDUNDOACTION, 0, 0);
    SendMessageA(state->editor, SCI_GOTOPOS, (WPARAM)(main_start + main_length + shift), 0);
    free(plan);
}

static void axyne_win_lsp_apply_selection(AxyneWindowState *state)
{
    struct AxyneWinLspNavUi *ui = state->lsp_nav_ui;
    long index = axyne_win_lsp_current_item(state);
    if (index < 0) return;
    SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0); /* we insert it ourselves */
    ui->list_active = 0;
    axyne_win_lsp_apply_item(state, &ui->items[index]);
}

void axyne_win_lsp_completion(AxyneLspClient *client, uint64_t request_id,
                              const AxyneLspCompletionItem *items,
                              size_t count, void *user_data)
{
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    AxyneWinLspCompletionMessage *message;
    if (state == NULL || state->main_window == NULL) return;
    if (items == NULL) count = 0;
    if (count > 5000) count = 5000;
    message = (AxyneWinLspCompletionMessage *)calloc(1, sizeof(*message));
    if (message == NULL) return;
    message->client = client;
    message->request_id = request_id;
    if (count > 0) {
        message->items = (AxyneWinLspItem *)calloc(count, sizeof(*message->items));
        if (message->items == NULL) {
            free(message);
            return;
        }
    }
    for (size_t i = 0; i < count; ++i)
        if (axyne_win_lsp_item_from(&items[i], &message->items[message->count]))
            ++message->count;
    if (!PostMessageW(state->main_window, AXYNE_WM_LSP_COMPLETION, 0,
                      (LPARAM)message))
        axyne_win_lsp_free_completion(message);
}

/* Results of the requests beyond completion; runs on the reader thread. */
void axyne_win_lsp_result(AxyneLspClient *client, AxyneLspResult *result, void *user_data)
{
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    (void)client;
    if (state == NULL || state->main_window == NULL || result == NULL) return;
    result->detached = 1;
    if (!PostMessageW(state->main_window, AXYNE_WM_LSP_RESULT, 0, (LPARAM)result))
        axyne_lsp_result_free(result);
}

static void axyne_win_lsp_handle_result(AxyneWindowState *state, AxyneLspResult *result)
{
    struct AxyneWinLspNavUi *ui = state->lsp_nav_ui;
    if (ui == NULL) return;
    if (result->kind == AXYNE_LSP_RESULT_RESOLVED_COMPLETION && !result->failed &&
        result->has_completion_item) {
        AxyneWinLspItem *item = NULL;
        AxyneLspCompletionItem *resolved = &result->completion_item;
        for (size_t i = 0; i < ui->item_count; ++i)
            if (ui->items[i].resolve_id == result->request_id) { item = &ui->items[i]; break; }
        if (item == NULL) return;
        if (resolved->documentation != NULL) {
            free(item->documentation);
            item->documentation = axyne_win_lsp_dup(resolved->documentation);
        }
        if (resolved->detail != NULL && resolved->detail[0] != '\0') {
            free(item->detail);
            item->detail = axyne_win_lsp_dup(resolved->detail);
        }
        if (resolved->additional_edit_count > 0) {
            for (size_t i = 0; i < item->extra_count; ++i) free(item->extra[i].new_text);
            free(item->extra);
            item->extra = (AxyneLspTextEdit *)calloc(resolved->additional_edit_count,
                                                      sizeof(*item->extra));
            item->extra_count = 0;
            if (item->extra != NULL) {
                for (size_t i = 0; i < resolved->additional_edit_count; ++i) {
                    item->extra[i].range = resolved->additional_edits[i].range;
                    item->extra[i].new_text = axyne_win_lsp_dup(resolved->additional_edits[i].new_text);
                }
                item->extra_count = resolved->additional_edit_count;
            }
        }
        item->resolve_id = 0;
        axyne_win_completion_refresh_detail(state);
    }
}

void axyne_win_lsp_navigation(AxyneLspClient *client, uint64_t request_id,
                              const AxyneLspLocation *locations,
                              size_t count, void *user_data)
{
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    AxyneWinLspNavigationMessage *message;
    if (state == NULL || state->main_window == NULL)
        return;
    message = (AxyneWinLspNavigationMessage *)calloc(1, sizeof(*message));
    if (message == NULL) return;
    message->client = client;
    message->request_id = request_id;
    message->count = count;
    if (count > 0 && locations != NULL && locations[0].uri != NULL) {
        message->uri = _strdup(locations[0].uri);
        message->start = locations[0].range.start;
        if (message->uri == NULL) {
            axyne_win_lsp_free_navigation(message);
            return;
        }
    }
    if (!PostMessageW(state->main_window, AXYNE_WM_LSP_NAVIGATION, 0,
                      (LPARAM)message))
        axyne_win_lsp_free_navigation(message);
}

static char *axyne_win_lsp_uri_path(const char *uri)
{
    const char *source;
    char *path, *out;
    size_t length;
    if (uri == NULL || _strnicmp(uri, "file://", 7) != 0) return NULL;
    source = uri + 7;
    if (_strnicmp(source, "localhost/", 10) == 0) source += 9;
    if (source[0] == '/' && isalpha((unsigned char)source[1]) &&
        source[2] == ':')
        ++source;
    length = strlen(source);
    path = (char *)malloc(length + 3);
    if (path == NULL) return NULL;
    out = path;
    if (source[0] != '/' &&
        !(isalpha((unsigned char)source[0]) && source[1] == ':')) {
        *out++ = '\\';
        *out++ = '\\';
    }
    while (*source != '\0') {
        if (*source == '%') {
            int high, low;
            if (source[1] == '\0' || source[2] == '\0' ||
                !isxdigit((unsigned char)source[1]) ||
                !isxdigit((unsigned char)source[2])) {
                free(path);
                return NULL;
            }
            high = isdigit((unsigned char)source[1]) ? source[1] - '0' :
                tolower((unsigned char)source[1]) - 'a' + 10;
            low = isdigit((unsigned char)source[2]) ? source[2] - '0' :
                tolower((unsigned char)source[2]) - 'a' + 10;
            if ((high << 4 | low) == 0) {
                free(path);
                return NULL;
            }
            *out++ = (char)((high << 4) | low);
            source += 3;
        } else {
            {
                char value = *source++;
                *out++ = value == '/' ? '\\' : value;
            }
        }
    }
    *out = '\0';
    return path;
}

static void axyne_win_lsp_handle_navigation(
    AxyneWindowState *state, HWND window,
    AxyneWinLspNavigationMessage *message)
{
    struct AxyneWinLspNavUi *ui;
    AxyneDocument *document;
    if (state == NULL || message == NULL || state->lsp_nav_ui == NULL ||
        state->lsp != message->client)
        return;
    ui = state->lsp_nav_ui;
    if (message->request_id == ui->definition_request &&
        ui->definition_path != NULL &&
        (document = axyne_win_active_document(state)) != NULL &&
        document->path != NULL &&
        strcmp(ui->definition_path, document->path) == 0) {
        char *path = axyne_win_lsp_uri_path(message->uri);
        ui->definition_request = 0;
        free(ui->definition_path);
        ui->definition_path = NULL;
        if (path != NULL && message->count > 0 &&
            axyne_win_open_document(state, path, 0, 0)) {
            axyne_win_goto(state, message->start.line + 1,
                           message->start.character + 1, TRUE);
        } else {
            axyne_win_lsp_set_status(state, "정의를 찾지 못했습니다");
        }
        free(path);
        InvalidateRect(window, NULL, FALSE);
    } else if (message->request_id == ui->references_request &&
               ui->references_path != NULL &&
               (document = axyne_win_active_document(state)) != NULL &&
               document->path != NULL &&
               strcmp(ui->references_path, document->path) == 0) {
        char text[96];
        ui->references_request = 0;
        free(ui->references_path);
        ui->references_path = NULL;
        (void)snprintf(text, sizeof(text), "참조 %zu개", message->count);
        axyne_win_lsp_set_status(state, text);
    }
}

static int axyne_win_lsp_navigate(AxyneWindowState *state, int references)
{
    struct AxyneWinLspNavUi *ui;
    AxyneDocument *document;
    AxyneLspPosition position;
    AxyneError error;
    AxyneStatus status;
    uint64_t request_id = 0;
    LRESULT caret, line_start;
    char **request_path;
    uint64_t *request_slot;

    if (state == NULL || state->lsp_nav_ui == NULL) return 0;
    ui = state->lsp_nav_ui;
    request_path = references ? &ui->references_path : &ui->definition_path;
    request_slot = references ? &ui->references_request : &ui->definition_request;
    free(*request_path);
    *request_path = NULL;
    *request_slot = 0;
    if (state->editor == NULL || !axyne_win_capture_editor(state) ||
        !axyne_win_lsp_open_active(state))
        return 0;
    document = axyne_win_active_document(state);
    if (!axyne_win_lsp_document_supported(document)) return 0;
    if (state->lsp == NULL) return 0;
    caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    position.line = (size_t)SendMessageA(state->editor,
        SCI_LINEFROMPOSITION, (WPARAM)caret, 0);
    line_start = SendMessageA(state->editor, SCI_POSITIONFROMLINE,
        (WPARAM)position.line, 0);
    position.character = axyne_lsp_utf16_character(
        (document->contents != NULL ? document->contents : "") +
            (size_t)line_start,
        (size_t)(caret - line_start), (size_t)(caret - line_start));
    *request_path = _strdup(document->path);
    if (*request_path == NULL) return 0;
    status = references
        ? axyne_lsp_references(state->lsp, document, position,
                               &request_id, &error)
        : axyne_lsp_definition(state->lsp, document, position,
                               &request_id, &error);
    if (status != AXYNE_STATUS_OK) {
        axyne_win_lsp_set_status(state, error.message);
        free(*request_path);
        *request_path = NULL;
        return 0;
    }
    *request_slot = request_id;
    {
        char text[96];
        (void)snprintf(text, sizeof(text), "요청 %llu 전송됨",
                       (unsigned long long)request_id);
        axyne_win_lsp_set_status(state, text);
    }
    return 1;
}

/* '>' and ':' only trigger as the second half of "->" and "::"; a lone
 * comparison or label colon must not open the list. */
static int axyne_win_lsp_trigger_needs_pair(AxyneWindowState *state, int ch)
{
    LRESULT caret;
    int previous;
    if (ch != '>' && ch != ':') return 0;
    caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    if (caret < 2) return 1;
    previous = (int)SendMessageA(state->editor, SCI_GETCHARAT, (WPARAM)(caret - 2), 0);
    return ch == '>' ? previous != '-' : previous != ':';
}

static void axyne_win_lsp_send_completion(AxyneWindowState *state);

static void axyne_win_lsp_request_completion(AxyneWindowState *state,
                                              const SCNotification *notification)
{
    AxyneDocument *document;
    if (state == NULL || state->lsp_nav_ui == NULL || state->editor == NULL ||
        state->lsp == NULL || notification == NULL)
        return;
    document = axyne_win_active_document(state);
    if (!axyne_win_lsp_document_supported(document)) {
        if (SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
            SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
        return;
    }
    if (!isalnum((unsigned char)notification->ch) && notification->ch != '_' &&
        notification->ch != '.' && notification->ch != '>' &&
        notification->ch != ':') {
        if (SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
            SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
        return;
    }
    if (axyne_win_lsp_trigger_needs_pair(state, notification->ch) ||
        axyne_win_completion_blocked(state)) {
        if (SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
            SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
        return;
    }
    axyne_win_lsp_send_completion(state);
}

/* Requests completion at the caret: after typing, or on demand (Ctrl+Space). */
static void axyne_win_lsp_send_completion(AxyneWindowState *state)
{
    struct AxyneWinLspNavUi *ui;
    AxyneDocument *document;
    AxyneLspPosition position;
    AxyneError error;
    uint64_t request_id = 0;
    LRESULT caret, line_start;
    if (!axyne_win_capture_editor(state)) return;
    document = axyne_win_active_document(state);
    if (!axyne_win_lsp_document_supported(document)) return;
    axyne_win_lsp_sync_active(state);
    ui = state->lsp_nav_ui;
    caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    position.line = (size_t)SendMessageA(state->editor,
        SCI_LINEFROMPOSITION, (WPARAM)caret, 0);
    line_start = SendMessageA(state->editor, SCI_POSITIONFROMLINE,
        (WPARAM)position.line, 0);
    position.character = axyne_lsp_utf16_character(
        (document->contents != NULL ? document->contents : "") +
            (size_t)line_start,
        (size_t)(caret - line_start), (size_t)(caret - line_start));
    if (axyne_lsp_completion(state->lsp, document, position,
                             &request_id, &error) != AXYNE_STATUS_OK)
        return;
    free(ui->completion_path);
    ui->completion_path = _strdup(document->path);
    if (ui->completion_path == NULL) {
        ui->completion_request = 0;
        return;
    }
    ui->completion_request = request_id;
    ui->completion_caret = caret;
}

static void axyne_win_lsp_nav_create(AxyneWindowState *state)
{
    if (state == NULL || state->lsp_nav_ui != NULL) return;
    state->lsp_nav_ui = (struct AxyneWinLspNavUi *)calloc(
        1, sizeof(*state->lsp_nav_ui));
}

static void axyne_win_lsp_nav_destroy(AxyneWindowState *state)
{
    if (state == NULL || state->lsp_nav_ui == NULL) return;
    axyne_win_lsp_clear_request_paths(state->lsp_nav_ui);
    axyne_win_lsp_forget_shown(state->lsp_nav_ui);
    free(state->lsp_nav_ui);
    state->lsp_nav_ui = NULL;
}

static int axyne_win_lsp_nav_message(AxyneWindowState *state, HWND window,
                                     UINT message, WPARAM w_param,
                                     LPARAM l_param, LRESULT *result)
{
    (void)w_param;
    if (message == AXYNE_WM_LSP_COMPLETION) {
        AxyneWinLspCompletionMessage *completion =
            (AxyneWinLspCompletionMessage *)l_param;
        if (completion != NULL) {
            if (state != NULL) axyne_win_lsp_show_completion(state, completion);
            axyne_win_lsp_free_completion(completion);
        }
        if (result != NULL) *result = 0;
        return 1;
    }
    if (message == AXYNE_WM_LSP_RESULT) {
        AxyneLspResult *lsp_result = (AxyneLspResult *)l_param;
        if (lsp_result != NULL) {
            if (state != NULL) axyne_win_lsp_handle_result(state, lsp_result);
            axyne_lsp_result_free(lsp_result);
        }
        if (result != NULL) *result = 0;
        return 1;
    }
    if (message == AXYNE_WM_LSP_NAVIGATION) {
        AxyneWinLspNavigationMessage *navigation =
            (AxyneWinLspNavigationMessage *)l_param;
        if (navigation != NULL) {
            axyne_win_lsp_handle_navigation(state, window, navigation);
            axyne_win_lsp_free_navigation(navigation);
        }
        if (result != NULL) *result = 0;
        return 1;
    }
    return 0;
}

static int axyne_win_lsp_nav_command_state(AxyneWindowState *state,
                                            AxyneCommandId id,
                                            AxyneWinCommandState *out)
{
    AxyneDocument *document;
    if (id != AXYNE_COMMAND_EDIT_GO_TO_DEFINITION &&
        id != AXYNE_COMMAND_EDIT_FIND_REFERENCES &&
        id != AXYNE_COMMAND_EDIT_TRIGGER_SUGGEST)
        return 0;
    document = axyne_win_active_document(state);
    out->enabled = axyne_win_lsp_document_supported(document);
    out->checked = 0;
    return 1;
}

/* Ctrl+Space: opens the LSP completion list at the caret without typing. */
static int axyne_win_lsp_trigger_suggest(AxyneWindowState *state)
{
    if (state == NULL || state->lsp_nav_ui == NULL || state->editor == NULL) return 0;
    if (!axyne_win_capture_editor(state) || !axyne_win_lsp_open_active(state)) return 0;
    if (state->lsp == NULL || axyne_win_completion_blocked(state)) return 0;
    axyne_win_lsp_send_completion(state);
    return 1;
}

static int axyne_win_lsp_nav_command(AxyneWindowState *state, AxyneCommandId id)
{
    if (id == AXYNE_COMMAND_EDIT_TRIGGER_SUGGEST)
        return axyne_win_lsp_trigger_suggest(state);
    if (id == AXYNE_COMMAND_EDIT_GO_TO_DEFINITION)
        return axyne_win_lsp_navigate(state, 0);
    if (id == AXYNE_COMMAND_EDIT_FIND_REFERENCES)
        return axyne_win_lsp_navigate(state, 1);
    return 0;
}

static void axyne_win_lsp_document_activated(AxyneWindowState *state,
                                             AxyneDocument *document)
{
    if (state == NULL || state->lsp_nav_ui == NULL) return;
    if (state->editor != NULL && SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
        SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
    axyne_win_lsp_forget_shown(state->lsp_nav_ui);
    axyne_win_lsp_clear_request_paths(state->lsp_nav_ui);
    if (axyne_win_lsp_document_supported(document) &&
        axyne_win_capture_editor(state))
        (void)axyne_win_lsp_open_active(state);
    else if (state->editor != NULL &&
             SendMessageA(state->editor, SCI_AUTOCACTIVE, 0, 0) != 0)
        SendMessageA(state->editor, SCI_AUTOCCANCEL, 0, 0);
}

static void axyne_win_lsp_document_closed(AxyneWindowState *state,
                                          const AxyneDocument *document)
{
    AxyneError error;
    if (state == NULL || document == NULL || state->lsp == NULL ||
        document->path == NULL || document->is_untitled)
        return;
    if (axyne_language_for_path(document->path) == state->lsp_language)
        (void)axyne_lsp_did_close(state->lsp, document, &error);
    if (state->lsp_nav_ui != NULL &&
        ((state->lsp_nav_ui->completion_path != NULL &&
          strcmp(state->lsp_nav_ui->completion_path, document->path) == 0) ||
         (state->lsp_nav_ui->definition_path != NULL &&
          strcmp(state->lsp_nav_ui->definition_path, document->path) == 0) ||
         (state->lsp_nav_ui->references_path != NULL &&
          strcmp(state->lsp_nav_ui->references_path, document->path) == 0)))
        axyne_win_lsp_clear_request_paths(state->lsp_nav_ui);
}

static void axyne_win_lsp_document_saved(AxyneWindowState *state,
                                         AxyneDocument *document)
{
    if (document != NULL && document == axyne_win_active_document(state) &&
        axyne_win_lsp_document_supported(document))
        (void)axyne_win_lsp_open_active(state);
}

static void axyne_win_lsp_editor_notify(AxyneWindowState *state,
                                         const SCNotification *notification)
{
    if (notification == NULL || state == NULL || state->lsp_nav_ui == NULL) return;
    if (notification->nmhdr.code == SCN_CHARADDED)
        axyne_win_lsp_request_completion(state, notification);
    else if (notification->nmhdr.code == SCN_AUTOCSELECTION && state->lsp_nav_ui->list_active)
        axyne_win_lsp_apply_selection(state);
    else if (notification->nmhdr.code == SCN_AUTOCCANCELLED ||
             notification->nmhdr.code == SCN_AUTOCCOMPLETED)
        state->lsp_nav_ui->list_active = 0;
}

const AxyneWinFeature axyne_win_feature_lsp_nav = {
    .name = "lsp-navigation",
    .create = axyne_win_lsp_nav_create,
    .destroy = axyne_win_lsp_nav_destroy,
    .message = axyne_win_lsp_nav_message,
    .command = axyne_win_lsp_nav_command,
    .command_state = axyne_win_lsp_nav_command_state,
    .editor_notify = axyne_win_lsp_editor_notify,
    .document_activated = axyne_win_lsp_document_activated,
    .document_closed = axyne_win_lsp_document_closed,
    .document_saved = axyne_win_lsp_document_saved
};

int axyne_win_lsp_nav_dispatch(AxyneWindowState *state, int references)
{
    return axyne_win_lsp_navigate(state, references);
}
