#include "ui_macos_internal.h"

#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

@interface AxyneMacLspNavState : NSObject {
@public
    uint64_t completionRequest;
    NSInteger completionCaret;
    char *completionPath;
    uint64_t definitionRequest;
    char *definitionPath;
    uint64_t referencesRequest;
    char *referencesPath;
}
@end

@implementation AxyneMacLspNavState
@end

typedef struct AxyneMacLspCompletion {
    AxyneLspClient *client;
    uint64_t request;
    char **words;
    size_t count;
} AxyneMacLspCompletion;

typedef struct AxyneMacLspNavigation {
    AxyneLspClient *client;
    uint64_t request;
    size_t count;
    char *uri;
    AxyneLspPosition start;
} AxyneMacLspNavigation;

static char *axyne_macos_lsp_completion_word(const AxyneLspCompletionItem *item)
{
    const char *word = item->insert_text != NULL && item->insert_text[0] != '\0'
        ? item->insert_text : item->label;
    size_t length = 0;
    if (word == NULL) return NULL;
    while (*word == ' ' || *word == '\t') ++word;
    if (strlen(word) >= 3 && (unsigned char)word[0] == 0xe2 &&
        (unsigned char)word[1] == 0x80 && (unsigned char)word[2] == 0xa2)
        word += 3;
    while (*word == ' ') ++word;
    while (word[length] != '\0' && word[length] != '\n' && word[length] != '(' &&
           word[length] != ' ' && word[length] != '$') ++length;
    if (length == 0) return NULL;
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, word, length);
    copy[length] = '\0';
    return copy;
}

void axyne_macos_lsp_completion(AxyneLspClient *client, uint64_t request_id,
                                const AxyneLspCompletionItem *items,
                                size_t count, void *user_data)
{
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    AxyneMacLspCompletion *message;
    if (view == nil || (count > 0 && items == NULL)) return;
    if (count > 200) count = 200;
    message = (AxyneMacLspCompletion *)calloc(1, sizeof(*message));
    if (message == NULL) return;
    message->client = client;
    message->request = request_id;
    message->words = count > 0 ? (char **)calloc(count, sizeof(char *)) : NULL;
    for (size_t i = 0; i < count && message->words != NULL; ++i) {
        char *word = axyne_macos_lsp_completion_word(&items[i]);
        if (word != NULL) message->words[message->count++] = word;
    }
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [view lspNavApplyCompletion:message->words count:message->count
                             client:message->client request:message->request];
        for (size_t i = 0; i < message->count; ++i) free(message->words[i]);
        free(message->words);
        free(message);
        [view release];
    });
}

void axyne_macos_lsp_navigation(AxyneLspClient *client, uint64_t request_id,
                                const AxyneLspLocation *locations,
                                size_t count, void *user_data)
{
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    AxyneMacLspNavigation *message;
    if (view == nil || (count > 0 && locations == NULL)) return;
    message = (AxyneMacLspNavigation *)calloc(1, sizeof(*message));
    if (message == NULL) return;
    message->client = client;
    message->request = request_id;
    message->count = count;
    if (count > 0 && locations[0].uri != NULL) {
        message->uri = strdup(locations[0].uri);
        message->start = locations[0].range.start;
    }
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [view lspNavApplyNavigation:message->uri line:message->start.line
                          character:message->start.character count:message->count
                            client:message->client request:message->request];
        free(message->uri);
        free(message);
        [view release];
    });
}

static BOOL axyne_macos_lsp_document_supported(const AxyneDocument *document)
{
    return document != NULL && document->path != NULL && !document->is_untitled &&
        !document->is_image && !document->is_virtual &&
        axyne_language_for_path(document->path) != AXYNE_LANGUAGE_NONE;
}

static BOOL axyne_macos_lsp_trigger(unichar character)
{
    return (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_' ||
        character == '.' || character == '>' || character == ':';
}

static int axyne_macos_lsp_word_compare(const void *left, const void *right)
{
    return strcasecmp(*(const char *const *)left, *(const char *const *)right);
}

@implementation AxyneWorkspaceView (AxyneLspNavFeature)

- (AxyneMacLspNavState *)lspNavState
{
    if (_lspNavUi == nil) _lspNavUi = [[AxyneMacLspNavState alloc] init];
    return (AxyneMacLspNavState *)_lspNavUi;
}

- (NSInteger)lspNavValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)item;
    if (command != AXYNE_COMMAND_EDIT_GO_TO_DEFINITION && command != AXYNE_COMMAND_EDIT_FIND_REFERENCES)
        return AXYNE_MAC_COMMAND_UNHANDLED;
    return axyne_macos_lsp_document_supported([self activeDocument]) &&
        ![self imagePreviewShown] ? AXYNE_MAC_COMMAND_ENABLED : AXYNE_MAC_COMMAND_DISABLED;
}

- (BOOL)lspNavPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    AxyneDocument *document;
    AxyneLspPosition position;
    AxyneError error;
    AxyneStatus status;
    AxyneMacLspNavState *ui;
    uint64_t request = 0;
    NSInteger caret, line, line_start;
    BOOL references;
    (void)sender;
    if (command != AXYNE_COMMAND_EDIT_GO_TO_DEFINITION && command != AXYNE_COMMAND_EDIT_FIND_REFERENCES)
        return NO;
    references = command == AXYNE_COMMAND_EDIT_FIND_REFERENCES;
    ui = [self lspNavState];
    if (ui == nil) return YES;
    if (references) {
        ui->referencesRequest = 0;
        free(ui->referencesPath);
        ui->referencesPath = NULL;
    } else {
        ui->definitionRequest = 0;
        free(ui->definitionPath);
        ui->definitionPath = NULL;
    }
    if (![self captureEditor] || ![self openLspForActive]) return YES;
    document = [self activeDocument];
    [self syncLspActive];
    caret = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    line = [self sendEditorMessage:SCI_LINEFROMPOSITION wParam:(uintptr_t)caret lParam:0];
    line_start = [self sendEditorMessage:SCI_POSITIONFROMLINE wParam:(uintptr_t)line lParam:0];
    position.line = (size_t)line;
    position.character = axyne_lsp_utf16_character(
        (document->contents != NULL ? document->contents : "") + (size_t)line_start,
        (size_t)(caret - line_start), (size_t)(caret - line_start));
    status = references ? axyne_lsp_references(self->_lsp, document, position, &request, &error) :
        axyne_lsp_definition(self->_lsp, document, position, &request, &error);
    if (status != AXYNE_STATUS_OK) {
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
        return YES;
    }
    if (references) {
        ui->referencesPath = strdup(document->path);
        ui->referencesRequest = ui->referencesPath != NULL ? request : 0;
    } else {
        ui->definitionPath = strdup(document->path);
        ui->definitionRequest = ui->definitionPath != NULL ? request : 0;
    }
    [self setLspStatus:[NSString stringWithFormat:@"LSP: request %llu sent",
        (unsigned long long)request]];
    return YES;
}

- (void)lspNavApplyCompletion:(char **)words count:(size_t)count
                       client:(AxyneLspClient *)client request:(uint64_t)request
{
    AxyneMacLspNavState *ui = (AxyneMacLspNavState *)_lspNavUi;
    AxyneDocument *document = [self activeDocument];
    NSInteger caret, word_start;
    char prefix[256];
    size_t prefix_length, shown = 0, length = 0;
    char **matches;
    char *list;
    if (ui == nil || _editorView == nil || client != _lsp ||
        request != ui->completionRequest || count == 0 || document == NULL ||
        document->path == NULL || ui->completionPath == NULL ||
        strcmp(document->path, ui->completionPath) != 0) return;
    caret = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    if (caret != ui->completionCaret) return;
    word_start = [self sendEditorMessage:SCI_WORDSTARTPOSITION wParam:(uintptr_t)caret lParam:1];
    prefix_length = (size_t)(caret - word_start);
    if (prefix_length >= sizeof(prefix)) return;
    if (prefix_length > 0) {
        const char *text = (const char *)(intptr_t)[self sendEditorMessage:SCI_GETRANGEPOINTER
            wParam:(uintptr_t)word_start lParam:(intptr_t)prefix_length];
        if (text == NULL) return;
        memcpy(prefix, text, prefix_length);
    }
    prefix[prefix_length] = '\0';
    matches = (char **)calloc(count, sizeof(char *));
    if (matches == NULL) return;
    for (size_t i = 0; i < count; ++i) {
        if (words[i] == NULL || strncasecmp(words[i], prefix, prefix_length) != 0) continue;
        if (prefix_length > 0 && strlen(words[i]) == prefix_length) continue;
        matches[shown++] = words[i];
    }
    if (shown == 0) { free(matches); return; }
    qsort(matches, shown, sizeof(char *), axyne_macos_lsp_word_compare);
    for (size_t i = 0; i < shown; ++i) length += strlen(matches[i]) + 1;
    list = (char *)malloc(length + 1);
    if (list == NULL) { free(matches); return; }
    length = 0;
    for (size_t i = 0; i < shown; ++i) {
        size_t n = strlen(matches[i]);
        if (i > 0 && strcasecmp(matches[i], matches[i - 1]) == 0) continue;
        if (length > 0) list[length++] = '\n';
        memcpy(list + length, matches[i], n);
        length += n;
    }
    list[length] = '\0';
    (void)[self sendEditorMessage:SCI_AUTOCSETIGNORECASE wParam:1 lParam:0];
    (void)[self sendEditorMessage:SCI_AUTOCSETAUTOHIDE wParam:1 lParam:0];
    (void)[self sendEditorMessage:SCI_AUTOCSETCHOOSESINGLE wParam:0 lParam:0];
    (void)[self sendEditorMessage:SCI_AUTOCSETCANCELATSTART wParam:0 lParam:0];
    (void)[self sendEditorMessage:SCI_AUTOCSETMAXHEIGHT wParam:8 lParam:0];
    (void)[self sendEditorMessage:SCI_AUTOCSETSEPARATOR wParam:'\n' lParam:0];
    (void)[self sendEditorMessage:SCI_AUTOCSHOW wParam:(uintptr_t)prefix_length lParam:(intptr_t)list];
    free(list);
    free(matches);
}

- (void)lspNavApplyNavigation:(char *)uri line:(size_t)line character:(size_t)character
                        count:(size_t)count client:(AxyneLspClient *)client
                      request:(uint64_t)request
{
    AxyneMacLspNavState *ui = (AxyneMacLspNavState *)_lspNavUi;
    AxyneDocument *document = [self activeDocument];
    if (ui == nil || client != _lsp || document == NULL || document->path == NULL) return;
    if (request == ui->referencesRequest) {
        if (ui->referencesPath == NULL || strcmp(document->path, ui->referencesPath) != 0) return;
        ui->referencesRequest = 0;
        free(ui->referencesPath);
        ui->referencesPath = NULL;
        [self setLspStatus:[NSString stringWithFormat:@"LSP: 참조 %zu개", count]];
        return;
    }
    if (request != ui->definitionRequest || ui->definitionPath == NULL ||
        strcmp(document->path, ui->definitionPath) != 0) return;
    ui->definitionRequest = 0;
    free(ui->definitionPath);
    ui->definitionPath = NULL;
    NSString *uriString = uri != NULL ? [NSString stringWithUTF8String:uri] : nil;
    NSURL *url = uriString != nil ? [NSURL URLWithString:uriString] : nil;
    if (count == 0 || url == nil || ![url isFileURL] || [url path] == nil) {
        [self setLspStatus:@"LSP: 정의를 찾지 못했습니다"];
        return;
    }
    [self showProblemAtPath:[url path] line:line + 1 column:character + 1 utf16:YES];
}

- (void)lspNavDocumentActivated:(AxyneDocument *)document
{
    AxyneMacLspNavState *ui = [self lspNavState];
    ui->completionRequest = 0;
    ui->definitionRequest = 0;
    ui->referencesRequest = 0;
    free(ui->completionPath); ui->completionPath = NULL;
    free(ui->definitionPath); ui->definitionPath = NULL;
    free(ui->referencesPath); ui->referencesPath = NULL;
    if (axyne_macos_lsp_document_supported(document) && ![self imagePreviewShown])
        (void)[self openLspForActive];
}

- (void)lspNavDocumentWillClose:(AxyneDocument *)document
{
    AxyneMacLspNavState *ui = (AxyneMacLspNavState *)_lspNavUi;
    if (ui != nil && document != NULL && document->path != NULL) {
        if (ui->completionPath != NULL && strcmp(ui->completionPath, document->path) == 0) {
            ui->completionRequest = 0; free(ui->completionPath); ui->completionPath = NULL;
        }
        if (ui->definitionPath != NULL && strcmp(ui->definitionPath, document->path) == 0) {
            ui->definitionRequest = 0; free(ui->definitionPath); ui->definitionPath = NULL;
        }
        if (ui->referencesPath != NULL && strcmp(ui->referencesPath, document->path) == 0) {
            ui->referencesRequest = 0; free(ui->referencesPath); ui->referencesPath = NULL;
        }
    }
}

- (void)lspNavDocumentSaved:(AxyneDocument *)document
{
    if (axyne_macos_lsp_document_supported(document)) {
        AxyneMacLspNavState *ui = [self lspNavState];
        ui->completionRequest = 0;
        ui->definitionRequest = 0;
        ui->referencesRequest = 0;
        free(ui->completionPath); ui->completionPath = NULL;
        free(ui->definitionPath); ui->definitionPath = NULL;
        free(ui->referencesPath); ui->referencesPath = NULL;
        (void)[self openLspForActive];
    }
}

- (void)lspNavEditorNotification:(SCNotification *)notification
{
    AxyneDocument *document = [self activeDocument];
    if (notification == NULL || notification->nmhdr.code != SCN_CHARADDED) return;
    if (!axyne_macos_lsp_document_supported(document) || !axyne_syntax_completes(
            axyne_syntax_for_path(document->path)) ||
        [self completionBlocked]) {
        if ([self sendEditorMessage:SCI_AUTOCACTIVE wParam:0 lParam:0] != 0)
            (void)[self sendEditorMessage:SCI_AUTOCCANCEL wParam:0 lParam:0];
        return;
    }
    if (!axyne_macos_lsp_trigger((unichar)notification->ch)) return;
    [self lspNavRequestCompletion];
}

- (void)lspNavRequestCompletion
{
    AxyneDocument *document = [self activeDocument];
    AxyneMacLspNavState *ui = [self lspNavState];
    AxyneLspPosition position;
    AxyneError error;
    uint64_t request = 0;
    NSInteger caret, line, line_start;
    if (!axyne_macos_lsp_document_supported(document) || _lsp == NULL ||
        _lspLanguage != axyne_language_for_path(document->path) ||
        [self imagePreviewShown] || ![self captureEditor]) return;
    document = [self activeDocument];
    [self syncLspActive];
    caret = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    line = [self sendEditorMessage:SCI_LINEFROMPOSITION wParam:(uintptr_t)caret lParam:0];
    line_start = [self sendEditorMessage:SCI_POSITIONFROMLINE wParam:(uintptr_t)line lParam:0];
    position.line = (size_t)line;
    position.character = axyne_lsp_utf16_character(
        (document->contents != NULL ? document->contents : "") + (size_t)line_start,
        (size_t)(caret - line_start), (size_t)(caret - line_start));
    if (axyne_lsp_completion(_lsp, document, position, &request, &error) == AXYNE_STATUS_OK) {
        ui->completionRequest = request;
        ui->completionCaret = caret;
        free(ui->completionPath);
        ui->completionPath = strdup(document->path);
        if (ui->completionPath == NULL) ui->completionRequest = 0;
    }
}

- (BOOL)lspNavDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)lspNavDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)lspNavLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)lspNavLayout {}
- (void)lspNavApplyPreferences {}

- (void)lspNavTeardown
{
    AxyneMacLspNavState *ui = (AxyneMacLspNavState *)_lspNavUi;
    if (ui != nil) {
        free(ui->completionPath);
        free(ui->definitionPath);
        free(ui->referencesPath);
    }
    [_lspNavUi release];
    _lspNavUi = nil;
}

@end
