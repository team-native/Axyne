#include "ui_windows_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/syntax.h"

enum {
    AXYNE_WM_LSP_COMPLETION = AXYNE_WM_FEATURE(AXYNE_WIN_FEATURE_LSP_NAV, 0),
    AXYNE_WM_LSP_NAVIGATION = AXYNE_WM_FEATURE(AXYNE_WIN_FEATURE_LSP_NAV, 1)
};

struct AxyneWinLspNavUi {
    uint64_t completion_request;
    LRESULT completion_caret;
    char *completion_path;
    uint64_t definition_request;
    char *definition_path;
    uint64_t references_request;
    char *references_path;
};

typedef struct AxyneWinLspCompletionMessage {
    AxyneLspClient *client;
    uint64_t request_id;
    char **words;
    size_t count;
} AxyneWinLspCompletionMessage;

typedef struct AxyneWinLspNavigationMessage {
    AxyneLspClient *client;
    uint64_t request_id;
    size_t count;
    char *uri;
    AxyneLspPosition start;
} AxyneWinLspNavigationMessage;

static void axyne_win_lsp_free_completion(AxyneWinLspCompletionMessage *message)
{
    if (message == NULL) return;
    for (size_t i = 0; i < message->count; ++i) free(message->words[i]);
    free(message->words);
    free(message);
}

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

static void axyne_win_lsp_show_completion(
    AxyneWindowState *state, AxyneWinLspCompletionMessage *message)
{
    struct AxyneWinLspNavUi *ui;
    AxyneDocument *document;
    LRESULT caret, word_start;
    char prefix[256];
    char **matches;
    size_t count = 0, length = 0, prefix_length;
    char *list;

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
    word_start = SendMessageA(state->editor, SCI_WORDSTARTPOSITION,
                              (WPARAM)caret, TRUE);
    prefix_length = (size_t)(caret - word_start);
    if (prefix_length >= sizeof(prefix)) return;
    if (prefix_length > 0) {
        struct Sci_TextRange range;
        range.chrg.cpMin = (Sci_PositionCR)word_start;
        range.chrg.cpMax = (Sci_PositionCR)caret;
        range.lpstrText = prefix;
        SendMessageA(state->editor, SCI_GETTEXTRANGE, 0, (LPARAM)&range);
    }
    prefix[prefix_length] = '\0';
    matches = message->count > 0
        ? (char **)calloc(message->count, sizeof(*matches)) : NULL;
    if (message->count > 0 && matches == NULL) return;
    for (size_t i = 0; i < message->count; ++i) {
        const char *word = message->words[i];
        size_t word_length;
        if (word == NULL || _strnicmp(word, prefix, prefix_length) != 0) continue;
        word_length = strlen(word);
        if (word_length == prefix_length) continue;
        matches[count++] = (char *)word;
    }
    if (count == 0) {
        free(matches);
        return;
    }
    for (size_t i = 1; i < count; ++i) {
        char *value = matches[i];
        size_t j = i;
        while (j > 0 && _stricmp(matches[j - 1], value) > 0) {
            matches[j] = matches[j - 1];
            --j;
        }
        matches[j] = value;
    }
    for (size_t i = 0; i < count; ++i) {
        if (i > 0 && _stricmp(matches[i], matches[i - 1]) == 0) continue;
        length += strlen(matches[i]) + 1;
    }
    list = (char *)malloc(length + 1);
    if (list == NULL) {
        free(matches);
        return;
    }
    length = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t word_length;
        if (i > 0 && _stricmp(matches[i], matches[i - 1]) == 0) continue;
        word_length = strlen(matches[i]);
        if (length > 0) list[length++] = '\n';
        memcpy(list + length, matches[i], word_length);
        length += word_length;
    }
    list[length] = '\0';
    SendMessageA(state->editor, SCI_AUTOCSETIGNORECASE, TRUE, 0);
    SendMessageA(state->editor, SCI_AUTOCSETAUTOHIDE, TRUE, 0);
    SendMessageA(state->editor, SCI_AUTOCSETCHOOSESINGLE, FALSE, 0);
    SendMessageA(state->editor, SCI_AUTOCSETCANCELATSTART, FALSE, 0);
    SendMessageA(state->editor, SCI_AUTOCSETMAXHEIGHT, 8, 0);
    SendMessageA(state->editor, SCI_AUTOCSETSEPARATOR, '\n', 0);
    SendMessageA(state->editor, SCI_AUTOCSHOW, (WPARAM)prefix_length,
                 (LPARAM)list);
    free(list);
    free(matches);
}

void axyne_win_lsp_completion(AxyneLspClient *client, uint64_t request_id,
                              const AxyneLspCompletionItem *items,
                              size_t count, void *user_data)
{
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    AxyneWinLspCompletionMessage *message;
    if (state == NULL || state->main_window == NULL || items == NULL ||
        count == 0)
        return;
    if (count > 200) count = 200;
    message = (AxyneWinLspCompletionMessage *)calloc(1, sizeof(*message));
    if (message == NULL) return;
    message->client = client;
    message->request_id = request_id;
    message->words = (char **)calloc(count, sizeof(*message->words));
    if (message->words == NULL) {
        axyne_win_lsp_free_completion(message);
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *word = items[i].insert_text != NULL &&
            items[i].insert_text[0] != '\0' ? items[i].insert_text : items[i].label;
        size_t length = 0;
        if (word == NULL) continue;
        while (*word == ' ' || *word == '\t') ++word;
        if (strlen(word) >= 3 && (unsigned char)word[0] == 0xe2 &&
            (unsigned char)word[1] == 0x80 &&
            (unsigned char)word[2] == 0xa2)
            word += 3;
        while (*word == ' ') ++word;
        while (word[length] != '\0' && word[length] != '\n' &&
               word[length] != '(' && word[length] != ' ' &&
               word[length] != '$')
            ++length;
        if (length == 0) continue;
        message->words[message->count] = (char *)malloc(length + 1);
        if (message->words[message->count] == NULL) break;
        memcpy(message->words[message->count], word, length);
        message->words[message->count][length] = '\0';
        ++message->count;
    }
    if (!PostMessageW(state->main_window, AXYNE_WM_LSP_COMPLETION, 0,
                      (LPARAM)message))
        axyne_win_lsp_free_completion(message);
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

static void axyne_win_lsp_request_completion(AxyneWindowState *state,
                                              const SCNotification *notification)
{
    struct AxyneWinLspNavUi *ui;
    AxyneDocument *document;
    AxyneLspPosition position;
    AxyneError error;
    uint64_t request_id = 0;
    LRESULT caret, line_start;
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
        id != AXYNE_COMMAND_EDIT_FIND_REFERENCES)
        return 0;
    document = axyne_win_active_document(state);
    out->enabled = axyne_win_lsp_document_supported(document);
    out->checked = 0;
    return 1;
}

static int axyne_win_lsp_nav_command(AxyneWindowState *state, AxyneCommandId id)
{
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
    if (notification != NULL && notification->nmhdr.code == SCN_CHARADDED)
        axyne_win_lsp_request_completion(state, notification);
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
