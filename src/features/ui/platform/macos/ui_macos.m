#import <AppKit/AppKit.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <dispatch/dispatch.h>

#include "axyne/document.h"
#include "axyne/search.h"
#include "axyne/explorer.h"
#include "axyne/watcher.h"
#include "axyne/process.h"
#include "axyne/runner.h"
#include "axyne/preferences.h"
#include "Scintilla.h"

enum { SCI_GETTEXT = 2182, SCI_GETTEXTLENGTH = 2183, SCI_SETTEXT = 2181,
       SCI_GETMODIFY = 2159, SCI_SETSAVEPOINT = 2014,
       SCI_CLEARALL = 2004, SCI_ADDTEXT = 2001, SCI_GETDOCPOINTER = 2357,
       SCI_SETDOCPOINTER = 2358, SCI_CREATEDOCUMENT = 2375,
       SCI_RELEASEDOCUMENT = 2377, SCI_GETCURRENTPOS = 2008,
       SCI_SETSEL = 2160, SCI_REPLACESEL = 2170,
       SCI_POSITIONFROMLINE = 2167, SCI_GOTOPOS = 2025,
       SCI_BEGINUNDOACTION = 2078, SCI_ENDUNDOACTION = 2079,
       SCI_STYLESETFORE = 2051, SCI_STYLESETBACK = 2052,
       SCI_STYLESETSIZE = 2055, SCI_STYLESETFONT = 2056,
       SCI_SETINDENT = 2122, SCI_SETUSETABS = 2124,
       SCI_SETWRAPMODE = 2268, SCI_SETVIEWWS = 2021 };

@interface NSObject (AxyneScintillaMessages)
- (NSInteger)message:(unsigned int)message wParam:(uintptr_t)wParam
               lParam:(intptr_t)lParam;
@end

static const CGFloat AXYNE_SIDEBAR = 248.0;
static const CGFloat AXYNE_TOOLBAR = 40.0;
static const CGFloat AXYNE_TABS = 36.0;
static const CGFloat AXYNE_STATUS = 26.0;
static const CGFloat AXYNE_BOTTOM = 158.0;

static NSColor *axyne_color(CGFloat red, CGFloat green, CGFloat blue)
{
    return [NSColor colorWithCalibratedRed:red / 255.0
                                     green:green / 255.0
                                      blue:blue / 255.0
                                     alpha:1.0];
}

@interface AxyneWorkspaceView : NSView {
    NSView *_editorView;
    NSBundle *_scintillaBundle;
    AxyneDocumentSet _documents;
    AxyneExplorer _explorer;
    AxyneWatcher *_watcher;
    NSInteger _explorerSelection;
    BOOL _hasExplorerSelection;
    NSMenu *_recentMenu;
    BOOL _loadingEditor;
    BOOL _editorDocumentInitialized;
    AxyneRunnerConfig _terminalRunner;
    AxyneRunnerConfig _actionRunner;
    AxyneProcess *_terminalProcess;
    NSTextView *_terminalOutput;
    NSTextField *_terminalInput;
    NSButton *_terminalStart;
    NSButton *_terminalStop;
    NSButton *_terminalSend;
    int _activeAction;
    int _lastExitCode;
    BOOL _lastExitFailed;
    BOOL _hasExitStatus;
    AxynePreferences _globalPreferences;
    AxynePreferences _preferences;
    char *_globalPreferencesPath;
    char *_workspacePreferencesPath;
    unsigned char _workspaceBindingPresent[AXYNE_ACTION_COUNT];
}
@end

static NSColor *axyne_preference_color(uint32_t value)
{
    return axyne_color((CGFloat)((value >> 16) & 0xff),
                       (CGFloat)((value >> 8) & 0xff),
                       (CGFloat)(value & 0xff));
}

static void axyne_macos_select_theme(AxyneThemePreferences *theme,
                                     AxyneThemePreset preset)
{
    theme->preset = preset;
    if (preset == AXYNE_THEME_LIGHT) {
        theme->background = 0xf5f6f8; theme->panel = 0xffffff;
        theme->toolbar = 0xe9ebef; theme->border = 0xd3d7de;
        theme->text = 0x24272d; theme->muted = 0x68707d;
        theme->accent = 0x7650b5; theme->editor_background = 0xffffff;
        theme->editor_text = 0x24272d;
    } else {
        theme->background = 0x16171a; theme->panel = 0x1f2126;
        theme->toolbar = 0x1c1e22; theme->border = 0x292c32;
        theme->text = 0xc7c9ce; theme->muted = 0x737780;
        theme->accent = 0xb67af6; theme->editor_background = 0x1a1c20;
        theme->editor_text = 0xcbced6;
    }
}

static BOOL axyne_macos_prefers_dark(NSView *view)
{
    NSAppearance *appearance = [view effectiveAppearance];
    NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:
        @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    return [match isEqualToString:NSAppearanceNameDarkAqua];
}

static char *axyne_macos_global_preferences_path(void)
{
    NSArray *directories = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString *base = [directories count] != 0 ? [directories objectAtIndex:0] : nil;
    NSString *directory;
    NSString *path;
    if (base == nil) return NULL;
    directory = [base stringByAppendingPathComponent:@"Axyne"];
    path = [directory stringByAppendingPathComponent:@"preferences.json"];
    return strdup([path UTF8String]);
}

static char *axyne_macos_workspace_preferences_path(const char *root)
{
    NSString *rootPath; NSString *directory; NSString *path;
    if (root == NULL) return NULL;
    rootPath = [NSString stringWithUTF8String:root];
    if (rootPath == nil) return NULL;
    directory = [rootPath stringByAppendingPathComponent:@".axyne"];
    path = [directory stringByAppendingPathComponent:@"preferences.json"];
    return strdup([path UTF8String]);
}

static BOOL axyne_macos_binding_matches(const AxynePreferences *preferences,
                                        AxynePreferenceAction action,
                                        NSString *key, NSEvent *event)
{
    const AxyneKeyBinding *binding = axyne_preferences_find_binding(
        preferences, action);
    unsigned int modifiers = 0;
    NSString *expected;
    if (binding == NULL || !binding->enabled) return NO;
    if (([event modifierFlags] & NSEventModifierFlagCommand) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_COMMAND;
    if (([event modifierFlags] & NSEventModifierFlagControl) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_CONTROL;
    if (([event modifierFlags] & NSEventModifierFlagShift) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_SHIFT;
    if (([event modifierFlags] & NSEventModifierFlagOption) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_ALT;
    if (binding->modifiers != modifiers) return NO;
    expected = [[NSString stringWithUTF8String:binding->key] lowercaseString];
    if ([expected isEqualToString:@"f5"])
        return [event keyCode] == 96;
    return [expected isEqualToString:[key lowercaseString]];
}

@interface AxyneWorkspaceView (AxyneActions)
- (void)newDocument:(id)sender;
- (void)openDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)saveDocumentAs:(id)sender;
- (void)closeDocument:(id)sender;
- (void)openRecent:(id)sender;
- (BOOL)confirmCloseAll;
- (void)notification:(SCNotification *)notification;
- (void)setRecentMenu:(NSMenu *)menu;
- (void)refreshRecentMenu;
- (BOOL)captureEditor;
- (BOOL)loadActiveDocument;
- (BOOL)confirmCloseDocumentAtIndex:(size_t)index;
- (void)findOrReplace:(BOOL)replace;
- (void)searchFolder:(BOOL)quickFile;
- (void)openWorkspace:(id)sender;
- (void)newExplorerFile:(id)sender;
- (void)newExplorerFolder:(id)sender;
- (void)renameExplorerItem:(id)sender;
- (void)removeExplorerItem:(id)sender;
- (void)workspaceEvent;
- (BOOL)refreshExplorer;
- (void)showWorkspaceError:(NSString *)prefix error:(AxyneError *)error;
- (void)showWorkspaceMessage:(NSString *)message;
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSInteger)explorerNodeAtPoint:(NSPoint)point;
- (void)performExplorerOperation:(AxyneFileKind)kind;
- (NSString *)askForText:(NSString *)title label:(NSString *)label;
- (void)startTerminal:(id)sender;
- (void)stopTerminal:(id)sender;
- (void)sendTerminal:(id)sender;
- (void)terminalAppend:(const char *)bytes length:(size_t)length
                stream:(AxyneProcessStream)stream;
- (void)terminalExited:(int)exitCode;
- (BOOL)configureRunner;
- (void)buildDocument:(id)sender;
- (void)runDocument:(id)sender;
- (void)showGlobalPreferences:(id)sender;
- (void)showWorkspacePreferences:(id)sender;
- (void)applyPreferences;
- (void)applySystemAppearance;
- (BOOL)showPreferences:(BOOL)workspace;
@end

static void axyne_install_menu(NSApplication *application,
                               AxyneWorkspaceView *workspace);

typedef struct AxyneMacWorkspaceEvent {
    AxyneWatchEventKind kind;
    char *path;
} AxyneMacWorkspaceEvent;

typedef struct AxyneMacTerminalMessage {
    char *bytes;
    size_t length;
    AxyneProcessStream stream;
} AxyneMacTerminalMessage;

static void axyne_macos_terminal_output(AxyneProcess *process,
                                         AxyneProcessStream stream,
                                         const char *bytes, size_t length,
                                         void *user_data)
{
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    AxyneMacTerminalMessage *message;
    (void)process;
    if (view == nil || bytes == NULL || length == 0 ||
        length > SIZE_MAX - sizeof(*message) - 1) return;
    message = (AxyneMacTerminalMessage *)malloc(sizeof(*message) + length + 1);
    if (message == NULL) return;
    message->bytes = (char *)(message + 1);
    memcpy(message->bytes, bytes, length);
    message->bytes[length] = '\0';
    message->length = length;
    message->stream = stream;
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [view terminalAppend:message->bytes length:message->length
                       stream:message->stream];
        [view release];
        free(message);
    });
}

static void axyne_macos_terminal_exit(AxyneProcess *process, int exit_code,
                                      void *user_data)
{
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    (void)process;
    if (view == nil) return;
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [view terminalExited:exit_code];
        [view release];
    });
}

static void axyne_macos_watch_callback(const AxyneWatchEvent *event,
                                       void *user_data)
{
    AxyneMacWorkspaceEvent *copy;
    size_t length;
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    if (event == NULL || event->path == NULL || view == nil) return;
    copy = (AxyneMacWorkspaceEvent *)calloc(1, sizeof(*copy));
    if (copy == NULL) return;
    length = strlen(event->path);
    copy->path = (char *)malloc(length + 1);
    if (copy->path == NULL) { free(copy); return; }
    memcpy(copy->path, event->path, length + 1);
    copy->kind = event->kind;
    dispatch_async(dispatch_get_main_queue(), ^{
        [view workspaceEvent];
        free(copy->path);
        free(copy);
    });
}

static void axyne_macos_runner_values_free(char **values, size_t count)
{
    size_t i;
    if (values == NULL) return;
    for (i = 0; i < count; ++i) free(values[i]);
    free(values);
}

static int axyne_macos_runner_split_lines(char *text, char ***values,
                                          size_t *count)
{
    char **items = NULL;
    size_t item_count = 0;
    char *cursor = text;
    if (values == NULL || count == NULL) return 0;
    *values = NULL;
    *count = 0;
    if (text == NULL) return 1;
    while (*cursor != '\0') {
        char *start = cursor;
        char *copy;
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') ++cursor;
        if (cursor != start) {
            copy = (char *)malloc((size_t)(cursor - start) + 1);
            if (copy == NULL) {
                axyne_macos_runner_values_free(items, item_count);
                return 0;
            }
            memcpy(copy, start, (size_t)(cursor - start));
            copy[cursor - start] = '\0';
            {
                char **grown = (char **)realloc(items,
                    (item_count + 1) * sizeof(*grown));
                if (grown == NULL) {
                    free(copy);
                    axyne_macos_runner_values_free(items, item_count);
                    return 0;
                }
                items = grown;
            }
            items[item_count++] = copy;
        }
        while (*cursor == '\r' || *cursor == '\n') ++cursor;
    }
    *values = items;
    *count = item_count;
    return 1;
}

static NSString *axyne_macos_runner_lines(char **values, size_t count)
{
    NSMutableString *result = [NSMutableString string];
    for (size_t i = 0; i < count; ++i) {
        if (i != 0) [result appendString:@"\n"];
        [result appendString:[NSString stringWithUTF8String:values[i]] ?: @""];
    }
    return result;
}

static NSTextField *axyne_macos_label(NSString *text, CGFloat y)
{
    NSTextField *field = [NSTextField labelWithString:text];
    [field setFrame:NSMakeRect(0, y, 460, 20)];
    return field;
}

@implementation AxyneWorkspaceView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) {
        if (axyne_explorer_initialize(&_explorer, NULL) != AXYNE_STATUS_OK ||
            axyne_documents_initialize(&_documents, NULL) != AXYNE_STATUS_OK) {
            axyne_explorer_destroy(&_explorer);
            [self release];
            return nil;
        }
        {
            AxyneRunnerSpec spec = {0};
            spec.executable = "/bin/sh";
            if (axyne_runner_initialize(&_terminalRunner, NULL) != AXYNE_STATUS_OK ||
                axyne_runner_configure(&_terminalRunner, &spec, NULL) != AXYNE_STATUS_OK ||
                axyne_runner_initialize(&_actionRunner, NULL) != AXYNE_STATUS_OK) {
                axyne_documents_destroy(&_documents);
                axyne_explorer_destroy(&_explorer);
                [self release];
                return nil;
            }
        }
        {
            AxyneError preferenceError;
            AxyneStatus status;
            axyne_preferences_defaults(&_globalPreferences);
            _globalPreferencesPath = axyne_macos_global_preferences_path();
            status = _globalPreferencesPath == NULL ? AXYNE_STATUS_NOT_FOUND :
                axyne_preferences_load_global(_globalPreferencesPath,
                                               &_globalPreferences,
                                               &preferenceError);
            _preferences = _globalPreferences;
            if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_NOT_FOUND)
                [self showWorkspaceError:@"Unable to load preferences" error:&preferenceError];
        }
        _terminalOutput = [[NSTextView alloc] initWithFrame:NSZeroRect];
        [_terminalOutput setEditable:NO];
        [_terminalOutput setSelectable:YES];
        [_terminalOutput setFont:[NSFont fontWithName:@"Menlo" size:11]];
        [_terminalOutput setTextColor:axyne_preference_color(_preferences.theme.text)];
        [_terminalOutput setBackgroundColor:axyne_preference_color(_preferences.theme.background)];
        [self addSubview:_terminalOutput];
        _terminalInput = [[NSTextField alloc] initWithFrame:NSZeroRect];
        [_terminalInput setPlaceholderString:@"Terminal input"];
        [self addSubview:_terminalInput];
        _terminalStart = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_terminalStart setTitle:@"Start Terminal"];
        [_terminalStart setTarget:self]; [_terminalStart setAction:@selector(startTerminal:)];
        [self addSubview:_terminalStart];
        _terminalStop = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_terminalStop setTitle:@"Stop"]; [_terminalStop setTarget:self];
        [_terminalStop setAction:@selector(stopTerminal:)]; [_terminalStop setEnabled:NO];
        [self addSubview:_terminalStop];
        _terminalSend = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_terminalSend setTitle:@"Send"]; [_terminalSend setTarget:self];
        [_terminalSend setAction:@selector(sendTerminal:)];
        [self addSubview:_terminalSend];
        [self applyPreferences];
    }
    return self;
}

- (AxyneDocument *)activeDocument
{
    if (_documents.count == 0 || _documents.active_index >= _documents.count)
        return NULL;
    return &_documents.documents[_documents.active_index];
}

- (NSInteger)sendEditorMessage:(unsigned int)message wParam:(uintptr_t)wParam
                         lParam:(intptr_t)lParam
{
    if (_editorView == nil) return 0;
    return [_editorView message:message wParam:wParam lParam:lParam];
}

- (void)applyPreferences
{
    [self applySystemAppearance];
    NSString *fontName = _preferences.editor.font_family[0] != '\0'
        ? [NSString stringWithUTF8String:_preferences.editor.font_family]
        : @"Menlo";
    const char *fontUTF8 = [fontName UTF8String];
    unsigned int fontSize = _preferences.editor.font_size;
    if (fontSize < 6 || fontSize > 72) fontSize = 11;
    [_terminalOutput setFont:[NSFont fontWithName:fontName size:fontSize]
                         ?: [NSFont userFixedPitchFontOfSize:fontSize]];
    [_terminalOutput setTextColor:axyne_preference_color(_preferences.theme.text)];
    [_terminalOutput setBackgroundColor:axyne_preference_color(_preferences.theme.background)];
    if (_editorView != nil) {
        [self sendEditorMessage:SCI_STYLESETFORE wParam:32
                              lParam:(intptr_t)_preferences.theme.editor_text];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:32
                              lParam:(intptr_t)_preferences.theme.editor_background];
        [self sendEditorMessage:SCI_STYLESETSIZE wParam:32 lParam:(intptr_t)fontSize];
        [self sendEditorMessage:SCI_STYLESETFONT wParam:32 lParam:(intptr_t)fontUTF8];
        [self sendEditorMessage:SCI_SETINDENT wParam:_preferences.editor.tab_width lParam:0];
        [self sendEditorMessage:SCI_SETUSETABS wParam:_preferences.editor.insert_spaces ? 0 : 1 lParam:0];
        [self sendEditorMessage:SCI_SETWRAPMODE wParam:_preferences.editor.word_wrap ? 1 : 0 lParam:0];
        [self sendEditorMessage:SCI_SETVIEWWS wParam:_preferences.editor.show_whitespace ? 1 : 0 lParam:0];
    }
    [self setNeedsDisplay:YES];
}

- (void)applySystemAppearance
{
    if (_preferences.theme.preset == AXYNE_THEME_SYSTEM) {
        axyne_macos_select_theme(&_preferences.theme,
            axyne_macos_prefers_dark(self) ? AXYNE_THEME_DARK : AXYNE_THEME_LIGHT);
        _preferences.theme.preset = AXYNE_THEME_SYSTEM;
    }
}

- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    if (_preferences.theme.preset == AXYNE_THEME_SYSTEM)
        [self applyPreferences];
}

- (BOOL)captureEditorSnapshot
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || _editorView == nil) return YES;
    NSInteger length = [self sendEditorMessage:SCI_GETTEXTLENGTH wParam:0 lParam:0];
    if (length < 0 || (uint64_t)length >= SIZE_MAX) return NO;
    char *text = malloc((size_t)length + 1);
    if (text == NULL) return NO;
    (void)[self sendEditorMessage:SCI_GETTEXT wParam:(uintptr_t)length + 1
                            lParam:(intptr_t)text];
    AxyneStatus status = axyne_documents_set_contents(&_documents,
        _documents.active_index, text, (size_t)length, NULL);
    free(text);
    return status == AXYNE_STATUS_OK;
}

- (BOOL)captureEditor
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || ([self sendEditorMessage:SCI_GETMODIFY wParam:0 lParam:0] == 0 &&
                        !doc->is_dirty)) return YES;
    if ([self captureEditorSnapshot]) return YES;
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:@"Could not capture editor contents"];
    [alert setInformativeText:@"The operation was cancelled. Your edits remain open in the editor."];
    [alert runModal];
    return NO;
}

- (BOOL)loadActiveDocument
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || _editorView == nil) return NO;
    size_t previousIndex = _documents.active_index;
    _loadingEditor = YES;
    if (!_editorDocumentInitialized) {
        doc->native_editor_document = (void *)(uintptr_t)[self
            sendEditorMessage:SCI_GETDOCPOINTER wParam:0 lParam:0];
        _editorDocumentInitialized = YES;
    } else if (doc->native_editor_document == NULL) {
        NSInteger created = [self sendEditorMessage:SCI_CREATEDOCUMENT
            wParam:doc->length lParam:0];
        if (created == 0) {
            _loadingEditor = NO;
            (void)axyne_documents_set_active(&_documents, previousIndex, NULL);
            return NO;
        }
        doc->native_editor_document = (void *)(uintptr_t)created;
        doc->owns_native_editor_document = 1;
        (void)[self sendEditorMessage:SCI_SETDOCPOINTER wParam:0
            lParam:(intptr_t)doc->native_editor_document];
        (void)[self sendEditorMessage:SCI_ADDTEXT wParam:doc->length
            lParam:(intptr_t)doc->contents];
        if (!doc->is_dirty)
            (void)[self sendEditorMessage:SCI_SETSAVEPOINT wParam:0 lParam:0];
    } else {
        (void)[self sendEditorMessage:SCI_SETDOCPOINTER wParam:0
            lParam:(intptr_t)doc->native_editor_document];
    }
    _loadingEditor = NO;
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    return YES;
}

- (void)updateWindowTitle
{
    AxyneDocument *doc = [self activeDocument];
    NSString *name = doc != NULL && doc->title != NULL
        ? [NSString stringWithUTF8String:doc->title] : @"Untitled";
    [[self window] setTitle:[NSString stringWithFormat:@"%@%@ - Axyne",
        doc != NULL && doc->is_dirty ? @"● " : @"", name ?: @"Untitled"]];
}

- (void)notification:(SCNotification *)notification
{
    AxyneDocument *doc = [self activeDocument];
    if (_loadingEditor || notification == NULL || doc == NULL) return;
    if (notification->nmhdr.code == SCN_MODIFIED &&
        [self sendEditorMessage:SCI_GETMODIFY wParam:0 lParam:0] != 0) {
        if (!doc->is_dirty) {
            (void)axyne_documents_mark_dirty(&_documents, _documents.active_index, NULL);
            [self setNeedsDisplay:YES];
            [self updateWindowTitle];
        }
    } else if (notification->nmhdr.code == SCN_SAVEPOINTREACHED) {
        (void)axyne_documents_mark_clean(&_documents,
            _documents.active_index, NULL);
        [self setNeedsDisplay:YES];
        [self updateWindowTitle];
    } else if (notification->nmhdr.code == SCN_SAVEPOINTLEFT) {
        (void)axyne_documents_mark_dirty(&_documents,
            _documents.active_index, NULL);
        [self setNeedsDisplay:YES];
        [self updateWindowTitle];
    }
}

- (BOOL)saveActiveToPath:(NSString *)path
{
    if (![self captureEditor]) return NO;
    const char *utf8Path = [path UTF8String];
    AxyneError error;
    AxyneStatus status = axyne_documents_save_as(&_documents,
        _documents.active_index, utf8Path, &error);
    if (status != AXYNE_STATUS_OK) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not save file"];
        [alert setInformativeText:[NSString stringWithUTF8String:error.message] ?: @""];
        [alert runModal];
        return NO;
    }
    (void)[self sendEditorMessage:SCI_SETSAVEPOINT wParam:0 lParam:0];
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    [self refreshRecentMenu];
    return YES;
}

- (BOOL)saveActive
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL) return NO;
    if (doc->is_untitled) {
        NSSavePanel *panel = [NSSavePanel savePanel];
        if ([panel runModal] != NSModalResponseOK) return NO;
        return [self saveActiveToPath:[[panel URL] path]];
    }
    if (![self captureEditor]) return NO;
    AxyneError error;
    AxyneStatus status = axyne_documents_save(&_documents,
        _documents.active_index, &error);
    if (status != AXYNE_STATUS_OK) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not save file"];
        [alert setInformativeText:[NSString stringWithUTF8String:error.message] ?: @""];
        [alert runModal];
        return NO;
    }
    (void)[self sendEditorMessage:SCI_SETSAVEPOINT wParam:0 lParam:0];
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    [self refreshRecentMenu];
    return YES;
}

- (void)newDocument:(id)sender
{
    (void)sender;
    if (![self captureEditor]) return;
    size_t previousIndex = _documents.active_index;
    size_t index;
    if (axyne_documents_new(&_documents, &index, NULL) == AXYNE_STATUS_OK) {
        (void)axyne_documents_set_active(&_documents, index, NULL);
        if (![self loadActiveDocument]) {
            (void)axyne_documents_close(&_documents, index, NULL);
            (void)axyne_documents_set_active(&_documents, previousIndex, NULL);
        }
    }
}

- (void)openPath:(NSString *)path
{
    if (path == nil) return;
    if (![self captureEditor]) return;
    size_t previousCount = _documents.count;
    size_t previousIndex = _documents.active_index;
    size_t index = 0;
    AxyneError error;
    AxyneStatus status = axyne_documents_open(&_documents,
        [path UTF8String], &index, &error);
    if (status != AXYNE_STATUS_OK) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not open file"];
        [alert setInformativeText:[NSString stringWithUTF8String:error.message] ?: @""];
        [alert runModal];
        return;
    }
    (void)axyne_documents_set_active(&_documents, index, NULL);
    if (![self loadActiveDocument]) {
        if (_documents.count > previousCount)
            (void)axyne_documents_close(&_documents, index, NULL);
        (void)axyne_documents_set_active(&_documents, previousIndex, NULL);
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not open file"];
        [alert setInformativeText:@"Scintilla could not create the document."];
        [alert runModal];
        return;
    }
    [self refreshRecentMenu];
}

- (void)openDocument:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] == NSModalResponseOK)
        [self openPath:[[panel URL] path]];
}

- (void)showWorkspaceError:(NSString *)prefix error:(AxyneError *)error
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:prefix ?: @"Workspace operation failed"];
    [alert setInformativeText:error != NULL
        ? ([NSString stringWithUTF8String:error->message] ?: @"") : @""];
    [alert runModal];
}

- (void)showWorkspaceMessage:(NSString *)message
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:message ?: @"Workspace operation failed"];
    [alert runModal];
}

- (BOOL)selectWorkspaceURL:(NSURL *)url
{
    const char *path;
    AxyneWatcher *watcher = NULL;
    AxyneError error;
    AxyneStatus status;
    if (url == nil) return NO;
    path = [[url path] UTF8String];
    if (path == NULL) return NO;
    status = axyne_watcher_start(path, axyne_macos_watch_callback, self,
                                 &watcher, &error);
    if (status != AXYNE_STATUS_OK) {
        [self showWorkspaceError:@"Unable to watch workspace" error:&error];
        return NO;
    }
    status = axyne_explorer_set_root(&_explorer, path, &error);
    if (status != AXYNE_STATUS_OK) {
        axyne_watcher_stop(watcher); axyne_watcher_release(watcher);
        [self showWorkspaceError:@"Unable to open workspace" error:&error];
        return NO;
    }
    if (_watcher != NULL) {
        axyne_watcher_stop(_watcher);
        axyne_watcher_release(_watcher);
    }
    _watcher = watcher;
    _hasExplorerSelection = NO;
    _preferences = _globalPreferences;
    memset(_workspaceBindingPresent, 0, sizeof(_workspaceBindingPresent));
    free(_workspacePreferencesPath);
    _workspacePreferencesPath = axyne_macos_workspace_preferences_path(path);
    if (_workspacePreferencesPath != NULL) {
        AxynePreferences workspacePreferences;
        status = axyne_preferences_load(_workspacePreferencesPath,
                                        &workspacePreferences, &error);
        if (status == AXYNE_STATUS_OK) {
            /* Keep the file's binding mask separate from the normalized
             * snapshot used to calculate effective workspace preferences. */
            memcpy(_workspaceBindingPresent, workspacePreferences.binding_present,
                   sizeof(_workspaceBindingPresent));
            axyne_preferences_mark_all(&workspacePreferences);
            axyne_preferences_apply_workspace(&_preferences,
                                               &workspacePreferences);
            [self applyPreferences];
        } else if (status != AXYNE_STATUS_NOT_FOUND) {
            [self showWorkspaceError:@"Unable to load workspace preferences" error:&error];
            [self applyPreferences];
        } else [self applyPreferences];
    }
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)openWorkspace:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:YES];
    [panel setCanChooseFiles:NO];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] == NSModalResponseOK)
        (void)[self selectWorkspaceURL:[panel URL]];
}

- (void)workspaceEvent
{
    [self refreshExplorer];
}

- (BOOL)refreshExplorer
{
    AxyneError error;
    if (_explorer.root == NULL) return NO;
    if (axyne_explorer_reload(&_explorer, &error) != AXYNE_STATUS_OK) {
        [self showWorkspaceError:@"Unable to refresh workspace" error:&error];
        [self setNeedsDisplay:YES];
        return NO;
    } else if (_hasExplorerSelection &&
               (size_t)_explorerSelection >= _explorer.count) {
        _hasExplorerSelection = NO;
    }
    [self setNeedsDisplay:YES];
    return YES;
}

- (NSInteger)explorerNodeAtPoint:(NSPoint)point
{
    CGFloat top = AXYNE_TOOLBAR + AXYNE_TABS + 31.0;
    NSInteger row;
    if (_explorer.root == NULL || point.y < top) return NSNotFound;
    row = (NSInteger)((point.y - top) / 22.0);
    return row >= 0 && (size_t)row < _explorer.count ? row : NSNotFound;
}

- (void)newExplorerFile:(id)sender
{
    (void)sender;
    [self performExplorerOperation:AXYNE_FILE_KIND_FILE];
}

- (void)newExplorerFolder:(id)sender
{
    (void)sender;
    [self performExplorerOperation:AXYNE_FILE_KIND_DIRECTORY];
}

- (void)renameExplorerItem:(id)sender
{
    (void)sender;
    if (!_hasExplorerSelection) return;
    AxyneExplorerNode *node = &_explorer.nodes[_explorerSelection];
    if (_explorer.root != NULL && strcmp(node->path, _explorer.root) == 0) {
        [self showWorkspaceMessage:@"The workspace root cannot be renamed or deleted."];
        return;
    }
    NSString *name = [self askForText:@"Rename" label:@"New name"];
    if ([name length] == 0) return;
    if (!axyne_explorer_is_safe_child_name([name UTF8String])) {
        [self showWorkspaceMessage:@"Use one valid file or folder name without separators, . or .."];
        return;
    }
    NSString *nodePath = [NSString stringWithUTF8String:node->path];
    NSString *parent = [nodePath stringByDeletingLastPathComponent];
    AxyneError error;
    AxyneStatus status = axyne_fs_rename_at([parent UTF8String], node->name,
                                            [name UTF8String], &error);
    if (status != AXYNE_STATUS_OK)
        [self showWorkspaceError:@"Rename failed" error:&error];
    else if ([self refreshExplorer]) _hasExplorerSelection = NO;
}

- (void)removeExplorerItem:(id)sender
{
    (void)sender;
    if (!_hasExplorerSelection) return;
    AxyneExplorerNode *node = &_explorer.nodes[_explorerSelection];
    if (_explorer.root != NULL && strcmp(node->path, _explorer.root) == 0) {
        [self showWorkspaceMessage:@"The workspace root cannot be renamed or deleted."];
        return;
    }
    NSString *nodePath = [NSString stringWithUTF8String:node->path];
    NSString *parent = [nodePath stringByDeletingLastPathComponent];
    AxyneError error;
    AxyneStatus status = axyne_fs_remove_at([parent UTF8String], node->name,
                                            &error);
    if (status != AXYNE_STATUS_OK)
        [self showWorkspaceError:@"Delete failed" error:&error];
    else if ([self refreshExplorer]) _hasExplorerSelection = NO;
}

- (void)performExplorerOperation:(AxyneFileKind)kind
{
    NSString *name = [self askForText:kind == AXYNE_FILE_KIND_FILE
        ? @"New File" : @"New Folder" label:@"Name"];
    NSString *parent = _explorer.root != NULL
        ? [NSString stringWithUTF8String:_explorer.root] : nil;
    if (_hasExplorerSelection && (size_t)_explorerSelection < _explorer.count) {
        AxyneExplorerNode *node = &_explorer.nodes[_explorerSelection];
        NSString *nodePath = [NSString stringWithUTF8String:node->path];
        parent = node->kind == AXYNE_FILE_KIND_DIRECTORY ? nodePath :
            [nodePath stringByDeletingLastPathComponent];
    }
    if ([name length] == 0 || [parent length] == 0) return;
    if (!axyne_explorer_is_safe_child_name([name UTF8String])) {
        [self showWorkspaceMessage:@"Use one valid file or folder name without separators, . or .."];
        return;
    }
    AxyneError error;
    AxyneStatus status = kind == AXYNE_FILE_KIND_FILE
        ? axyne_fs_create_file_at([parent UTF8String], [name UTF8String], &error)
        : axyne_fs_create_directory_at([parent UTF8String], [name UTF8String], &error);
    if (status != AXYNE_STATUS_OK)
        [self showWorkspaceError:@"Create failed" error:&error];
    else [self refreshExplorer];
}

- (void)saveDocument:(id)sender
{
    (void)sender;
    (void)[self saveActive];
}

- (void)saveDocumentAs:(id)sender
{
    (void)sender;
    NSSavePanel *panel = [NSSavePanel savePanel];
    if ([panel runModal] == NSModalResponseOK)
        (void)[self saveActiveToPath:[[panel URL] path]];
}

- (void)openRecent:(id)sender
{
    NSString *path = [sender representedObject];
    [self openPath:path];
}

- (void)setRecentMenu:(NSMenu *)menu
{
    [_recentMenu release];
    _recentMenu = [menu retain];
    [self refreshRecentMenu];
}

- (void)refreshRecentMenu
{
    if (_recentMenu == nil) return;
    [_recentMenu removeAllItems];
    if (_documents.recent_count == 0) {
        NSMenuItem *empty = [[NSMenuItem alloc] initWithTitle:@"No Recent Files"
            action:nil keyEquivalent:@""];
        [empty setEnabled:NO];
        [_recentMenu addItem:empty];
        [empty release];
        return;
    }
    for (size_t i = 0; i < _documents.recent_count; ++i) {
        NSString *path = [NSString stringWithUTF8String:_documents.recent_paths[i]];
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:path ?: @"(Invalid path)"
            action:@selector(openRecent:) keyEquivalent:@""];
        [item setTarget:self];
        [item setRepresentedObject:path];
        [_recentMenu addItem:item];
        [item release];
    }
}

- (BOOL)confirmCloseDocumentAtIndex:(size_t)index
{
    AxyneDocument *doc = &_documents.documents[index];
    if (!doc->is_dirty) return YES;
    NSString *name = [NSString stringWithUTF8String:doc->title ?: "Untitled"];
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:[NSString stringWithFormat:@"Save changes to %@?", name]];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Discard"];
    [alert addButtonWithTitle:@"Cancel"];
    NSInteger result = [alert runModal];
    if (result == NSAlertFirstButtonReturn) {
        if (![self captureEditor]) return NO;
        (void)axyne_documents_set_active(&_documents, index, NULL);
        [self loadActiveDocument];
        return [self saveActive];
    }
    return result == NSAlertSecondButtonReturn;
}

- (void)closeDocument:(id)sender
{
    (void)sender;
    size_t index = _documents.active_index;
    if (![self captureEditor]) return;
    if (![self confirmCloseDocumentAtIndex:index]) return;
    AxyneDocument *doc = &_documents.documents[index];
    if (doc->owns_native_editor_document)
        (void)[self sendEditorMessage:SCI_RELEASEDOCUMENT wParam:0
            lParam:(intptr_t)doc->native_editor_document];
    (void)axyne_documents_close(&_documents, index, NULL);
    (void)axyne_documents_set_active(&_documents, _documents.active_index, NULL);
    [self loadActiveDocument];
}

- (BOOL)confirmCloseAll
{
    if (![self captureEditor]) return NO;
    for (size_t i = 0; i < _documents.count; ++i)
        if (![self confirmCloseDocumentAtIndex:i]) return NO;
    return YES;
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)loadScintillaView
{
    if (_editorView != nil) {
        return;
    }
    NSString *frameworkPath = [[NSBundle mainBundle] pathForResource:@"Scintilla"
                                                               ofType:@"framework"
                                                          inDirectory:@"Frameworks"];
    if (frameworkPath != nil) {
        _scintillaBundle = [[NSBundle bundleWithPath:frameworkPath] retain];
        [_scintillaBundle load];
    }

    Class scintillaClass = NSClassFromString(@"ScintillaView");
    if (scintillaClass != Nil) {
        _editorView = [[scintillaClass alloc] initWithFrame:NSZeroRect];
        [(id)_editorView setDelegate:self];
        [_editorView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [self addSubview:_editorView];
        [self setNeedsLayout:YES];
    }
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self loadScintillaView];
    [self applyPreferences];
    [self loadActiveDocument];
}

- (void)mouseDown:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (point.y >= AXYNE_TOOLBAR && point.y < AXYNE_TOOLBAR + AXYNE_TABS &&
        point.x >= AXYNE_SIDEBAR + 12) {
        CGFloat offset = point.x - AXYNE_SIDEBAR - 12;
        size_t index = (size_t)(offset / 184);
        if (index < _documents.count) {
            if (![self captureEditor]) return;
            if (fmod(offset, 184) >= 160) {
                if ([self confirmCloseDocumentAtIndex:index]) {
                    AxyneDocument *doc = &_documents.documents[index];
                    if (doc->owns_native_editor_document)
                        (void)[self sendEditorMessage:SCI_RELEASEDOCUMENT wParam:0
                            lParam:(intptr_t)doc->native_editor_document];
                    (void)axyne_documents_close(&_documents, index, NULL);
                    [self loadActiveDocument];
                }
            } else {
                (void)axyne_documents_set_active(&_documents, index, NULL);
                [self loadActiveDocument];
            }
            return;
        }
    }
    if (point.x < AXYNE_SIDEBAR && point.y >= AXYNE_TOOLBAR + AXYNE_TABS) {
        NSInteger row = [self explorerNodeAtPoint:point];
        if (row != NSNotFound) {
            _explorerSelection = row;
            _hasExplorerSelection = YES;
            AxyneExplorerNode *node = &_explorer.nodes[(size_t)row];
            if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
                if (axyne_explorer_toggle(&_explorer, (size_t)row, NULL) != AXYNE_STATUS_OK)
                    [self showWorkspaceError:@"Unable to read workspace folder" error:NULL];
                [self setNeedsDisplay:YES];
            } else {
                [self openPath:[NSString stringWithUTF8String:node->path]];
            }
        } else if (_explorer.root == NULL) {
            [self openWorkspace:nil];
        }
        return;
    }
    [super mouseDown:event];
}

- (void)rightMouseDown:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    NSInteger row = [self explorerNodeAtPoint:point];
    if (point.x >= AXYNE_SIDEBAR || point.y < AXYNE_TOOLBAR + AXYNE_TABS) {
        [super rightMouseDown:event];
        return;
    }
    if (row != NSNotFound) {
        _explorerSelection = row;
        _hasExplorerSelection = YES;
    }
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@"Workspace"] autorelease];
    NSMenuItem *workspace = [menu addItemWithTitle:@"Open Workspace Folder..."
        action:@selector(openWorkspace:) keyEquivalent:@""];
    [workspace setTarget:self];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *file = [menu addItemWithTitle:@"New File"
        action:@selector(newExplorerFile:) keyEquivalent:@""];
    NSMenuItem *folder = [menu addItemWithTitle:@"New Folder"
        action:@selector(newExplorerFolder:) keyEquivalent:@""];
    [file setTarget:self]; [folder setTarget:self];
    if (row != NSNotFound) {
        NSMenuItem *rename = [menu addItemWithTitle:@"Rename"
            action:@selector(renameExplorerItem:) keyEquivalent:@""];
        NSMenuItem *remove = [menu addItemWithTitle:@"Delete"
            action:@selector(removeExplorerItem:) keyEquivalent:@""];
        [rename setTarget:self]; [remove setTarget:self];
    }
    [menu popUpMenuPositioningItem:nil atLocation:point inView:self];
    [self setNeedsDisplay:YES];
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    NSString *key = [event charactersIgnoringModifiers];
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_SEARCH_WORKSPACE, key, event)) { [self searchFolder:NO]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_FIND, key, event)) { [self findOrReplace:NO]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_REPLACE, key, event)) { [self findOrReplace:YES]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_QUICK_FILE, key, event)) { [self searchFolder:YES]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_NEW, key, event)) { [self newDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_OPEN, key, event)) { [self openDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_SAVE, key, event)) { [self saveDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_CLOSE, key, event)) { [self closeDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_BUILD, key, event)) { [self buildDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_RUN, key, event)) { [self runDocument:nil]; return YES; }
    return [super performKeyEquivalent:event];
}

- (NSString *)askForText:(NSString *)title label:(NSString *)label
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:title];
    NSTextField *field = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)] autorelease];
    [field setPlaceholderString:label];
    [alert setAccessoryView:field];
    [alert addButtonWithTitle:@"Continue"]; [alert addButtonWithTitle:@"Cancel"];
    return [alert runModal] == NSAlertFirstButtonReturn ? [field stringValue] : nil;
}

- (BOOL)showPreferences:(BOOL)workspace
{
    AxynePreferences next = workspace ? _preferences : _globalPreferences;
    NSString *theme = [[self askForText:workspace ? @"Workspace Settings" : @"Preferences"
                                   label:@"Theme: dark, light, or system"] lowercaseString];
    NSString *fontSize;
    NSString *tabWidth;
    NSString *spaces;
    NSString *wrap;
    AxyneError error;
    AxyneStatus status;
    const char *path = workspace ? _workspacePreferencesPath : _globalPreferencesPath;
    if (workspace)
        memcpy(next.binding_present, _workspaceBindingPresent,
               sizeof(next.binding_present));
    if ([theme length] == 0 || (![theme isEqualToString:@"dark"] &&
        ![theme isEqualToString:@"light"] && ![theme isEqualToString:@"system"])) {
        if (theme != nil) [self showWorkspaceMessage:@"Theme must be dark, light, or system."];
        return NO;
    }
    axyne_macos_select_theme(&next.theme,
        [theme isEqualToString:@"light"] ? AXYNE_THEME_LIGHT :
        [theme isEqualToString:@"system"] ? AXYNE_THEME_SYSTEM : AXYNE_THEME_DARK);
    if (workspace) {
        next.present_fields = 0;
        next.present_fields |= AXYNE_PREFERENCE_THEME_PRESET;
    }
    fontSize = [self askForText:@"Editor Preferences" label:@"Font size: 6-72"];
    if (fontSize == nil) return NO;
    if ([fontSize integerValue] < 6 || [fontSize integerValue] > 72) { [self showWorkspaceMessage:@"Font size must be between 6 and 72."]; return NO; }
    next.editor.font_size = (unsigned int)[fontSize integerValue];
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_FONT_SIZE;
    tabWidth = [self askForText:@"Editor Preferences" label:@"Tab width: 1-16"];
    if (tabWidth == nil) return NO;
    if ([tabWidth integerValue] < 1 || [tabWidth integerValue] > 16) { [self showWorkspaceMessage:@"Tab width must be between 1 and 16."]; return NO; }
    next.editor.tab_width = (unsigned int)[tabWidth integerValue];
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_TAB_WIDTH;
    spaces = [[self askForText:@"Editor Preferences" label:@"Insert spaces: yes or no"] lowercaseString];
    if (spaces == nil || (![spaces isEqualToString:@"yes"] && ![spaces isEqualToString:@"no"])) { if (spaces != nil) [self showWorkspaceMessage:@"Enter yes or no."]; return NO; }
    next.editor.insert_spaces = [spaces isEqualToString:@"yes"];
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_INSERT_SPACES;
    wrap = [[self askForText:@"Editor Preferences" label:@"Word wrap: yes or no"] lowercaseString];
    if (wrap == nil || (![wrap isEqualToString:@"yes"] && ![wrap isEqualToString:@"no"])) { if (wrap != nil) [self showWorkspaceMessage:@"Enter yes or no."]; return NO; }
    next.editor.word_wrap = [wrap isEqualToString:@"yes"];
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_WORD_WRAP;
    NSString *fontFamily = [self askForText:@"Editor Preferences" label:@"Font family: blank for native default"];
    if (fontFamily == nil) return NO;
    if ([[fontFamily dataUsingEncoding:NSUTF8StringEncoding] length] >= AXYNE_PREFERENCE_TEXT_MAX) { [self showWorkspaceMessage:@"The font family is invalid."]; return NO; }
    (void)snprintf(next.editor.font_family, sizeof(next.editor.font_family), "%s", [fontFamily UTF8String] ?: "");
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_FONT_FAMILY;
    NSString *showWhitespace = [[self askForText:@"Editor Preferences" label:@"Show whitespace: yes or no"] lowercaseString];
    if (showWhitespace == nil || (![showWhitespace isEqualToString:@"yes"] && ![showWhitespace isEqualToString:@"no"])) { if (showWhitespace != nil) [self showWorkspaceMessage:@"Enter yes or no."]; return NO; }
    next.editor.show_whitespace = [showWhitespace isEqualToString:@"yes"];
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE;
    for (int action = 0; action < AXYNE_ACTION_COUNT; ++action) {
        AxyneKeyBinding *edited = (AxyneKeyBinding *)axyne_preferences_find_binding(&next, (AxynePreferenceAction)action);
        if (edited == NULL) continue;
        NSString *value = [self askForText:@"Key Bindings"
                                      label:[NSString stringWithFormat:@"%@ (current: %s); enter key, disable, restore, or skip",
                                               [NSString stringWithUTF8String:axyne_preferences_action_name((AxynePreferenceAction)action)], edited->key]];
        if (value == nil) return NO;
        NSString *command = [value lowercaseString];
        if ([value length] == 0 || [command isEqualToString:@"skip"]) continue;
        if ([command isEqualToString:@"disable"]) edited->enabled = 0;
        else if ([command isEqualToString:@"restore"]) {
            AxynePreferences defaults;
            axyne_preferences_defaults(&defaults);
            const AxyneKeyBinding *restored = axyne_preferences_find_binding(&defaults, (AxynePreferenceAction)action);
            if (restored != NULL) *edited = *restored;
        } else {
            const char *key = [value UTF8String];
            if (key == NULL || strlen(key) >= AXYNE_PREFERENCE_KEY_MAX) { [self showWorkspaceMessage:@"The key binding is invalid."]; return NO; }
            (void)snprintf(edited->key, sizeof(edited->key), "%s", key);
            edited->enabled = 1;
        }
        if (workspace) axyne_preferences_mark_binding(&next, (AxynePreferenceAction)action);
    }
    if (path == NULL) { [self showWorkspaceMessage:@"The preference path is unavailable."]; return NO; }
    status = workspace ? axyne_preferences_save_workspace(&next, path, &error) : axyne_preferences_save_global(&next, path, &error);
    if (status != AXYNE_STATUS_OK) { [self showWorkspaceError:@"Unable to save preferences" error:&error]; return NO; }
    _preferences = next;
    if (workspace)
        memcpy(_workspaceBindingPresent, next.binding_present,
               sizeof(_workspaceBindingPresent));
    if (!workspace) {
        _globalPreferences = next;
        if (_workspacePreferencesPath != NULL) {
            AxynePreferences workspacePreferences;
            AxyneStatus workspaceStatus = axyne_preferences_load_workspace(
                _workspacePreferencesPath, &workspacePreferences, &error);
            if (workspaceStatus == AXYNE_STATUS_OK)
                axyne_preferences_apply_workspace(&_preferences, &workspacePreferences);
            else if (workspaceStatus != AXYNE_STATUS_NOT_FOUND)
                [self showWorkspaceError:@"Unable to reload workspace preferences" error:&error];
        }
    }
    [self applyPreferences];
    return YES;
}

- (void)showGlobalPreferences:(id)sender
{
    (void)sender;
    (void)[self showPreferences:NO];
}

- (void)showWorkspacePreferences:(id)sender
{
    (void)sender;
    if (_workspacePreferencesPath == NULL) {
        [self showWorkspaceMessage:@"Open a workspace folder before editing workspace settings."];
        return;
    }
    (void)[self showPreferences:YES];
}

- (void)findOrReplace:(BOOL)replace
{
    NSString *q = [self askForText:replace ? @"Replace" : @"Find" label:@"Find text"];
    if ([q length] == 0 || ![self captureEditor]) return;
    NSString *r = replace ? [self askForText:@"Replace" label:@"Replace with"] : nil;
    if (replace && r == nil) return;
    BOOL replaceAll = NO;
    if (replace) {
        NSAlert *choice = [[[NSAlert alloc] init] autorelease];
        [choice setMessageText:@"Replace all occurrences?"];
        [choice addButtonWithTitle:@"Replace All"];
        [choice addButtonWithTitle:@"Replace Next"];
        [choice addButtonWithTitle:@"Cancel"];
        NSInteger answer = [choice runModal];
        if (answer == NSAlertThirdButtonReturn) return;
        replaceAll = answer == NSAlertFirstButtonReturn;
    }
    size_t length = (size_t)[self sendEditorMessage:SCI_GETTEXTLENGTH wParam:0 lParam:0];
    char *text = malloc(length + 1);
    if (text == NULL) return;
    (void)[self sendEditorMessage:SCI_GETTEXT wParam:length + 1 lParam:(intptr_t)text];
    const char *query = [q UTF8String]; size_t at;
    if (replaceAll) {
        char *output = NULL; size_t outputLength = 0, count = 0;
        if (axyne_search_replace_all(text, length, query, strlen(query),
                [r UTF8String], strlen([r UTF8String]), 0, &output,
                &outputLength, &count, NULL) == AXYNE_STATUS_OK) {
            if (count > 0) {
                (void)[self sendEditorMessage:SCI_BEGINUNDOACTION wParam:0 lParam:0];
                size_t queryLength = strlen(query), replacementLength = strlen([r UTF8String]);
                size_t searchStart = 0, previousSourceEnd = 0, previousLiveEnd = 0;
                size_t match = 0; BOOL first = YES;
                while (axyne_search_find(text, length, query, queryLength,
                                         searchStart, 0, &match) && match >= searchStart) {
                    size_t liveAt = first ? match : previousLiveEnd +
                        (match - previousSourceEnd);
                    (void)[self sendEditorMessage:SCI_SETSEL wParam:liveAt
                        lParam:liveAt + queryLength];
                    (void)[self sendEditorMessage:SCI_REPLACESEL wParam:0
                        lParam:(intptr_t)[r UTF8String]];
                    previousSourceEnd = match + queryLength;
                    previousLiveEnd = liveAt + replacementLength;
                    searchStart = previousSourceEnd;
                    first = NO;
                }
                (void)[self sendEditorMessage:SCI_ENDUNDOACTION wParam:0 lParam:0];
            }
            free(output);
        }
        free(text); return;
    }
    size_t start = (size_t)[self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    if (axyne_search_find(text, length, query, strlen(query), start, 0, &at)) {
        (void)[self sendEditorMessage:SCI_SETSEL wParam:at lParam:at + strlen(query)];
        if (replace) (void)[self sendEditorMessage:SCI_REPLACESEL wParam:0 lParam:(intptr_t)[r UTF8String]];
    } else {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"No match found"]; [alert runModal];
    }
    free(text);
}

- (void)searchFolder:(BOOL)quickFile
{
    NSOpenPanel *folder = [NSOpenPanel openPanel];
    [folder setCanChooseDirectories:YES]; [folder setCanChooseFiles:NO];
    [folder setAllowsMultipleSelection:NO];
    if ([folder runModal] != NSModalResponseOK) return;
    NSString *query = [self askForText:quickFile ? @"Quick File" : @"Search Folder"
                                  label:quickFile ? @"Filename contains" : @"Search text"];
    if ([query length] == 0) return;
    NSPopUpButton *choices = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 480, 28)
                                                        pullsDown:NO] autorelease];
    size_t selectedLine = 0;
    if (quickFile) {
        char **paths = NULL; size_t count = 0;
        if (axyne_search_files([[[folder URL] path] UTF8String], [query UTF8String],
                               &paths, &count, NULL) == AXYNE_STATUS_OK) {
            for (size_t i = 0; i < count; ++i)
                [choices addItemWithTitle:[NSString stringWithUTF8String:paths[i]] ?: @"(invalid path)"];
            if (count > 0) {
                NSAlert *pick = [[[NSAlert alloc] init] autorelease];
                [pick setMessageText:@"Choose a file to open"]; [pick setAccessoryView:choices];
                [pick addButtonWithTitle:@"Open"]; [pick addButtonWithTitle:@"Cancel"];
                if ([pick runModal] == NSAlertFirstButtonReturn)
                    [self openPath:[choices titleOfSelectedItem]];
            }
            axyne_search_paths_destroy(paths, count);
        }
    } else {
        AxyneSearchResults results = {0};
        if (axyne_search_workspace([[[folder URL] path] UTF8String], [query UTF8String],
                                   0, &results, NULL) == AXYNE_STATUS_OK) {
            for (size_t i = 0; i < results.count; ++i) {
                NSString *path = [NSString stringWithUTF8String:results.items[i].path] ?: @"";
                NSString *preview = [NSString stringWithUTF8String:results.items[i].preview] ?: @"";
                [choices addItemWithTitle:[NSString stringWithFormat:@"%@:%zu  %@", path,
                    results.items[i].line, preview]];
                NSDictionary *match = @{
                    @"path": path,
                    @"line": [NSNumber numberWithUnsignedLong:results.items[i].line]
                };
                [[choices itemAtIndex:(NSInteger)i] setRepresentedObject:match];
            }
            if (results.count > 0) {
                NSAlert *pick = [[[NSAlert alloc] init] autorelease];
                [pick setMessageText:[NSString stringWithFormat:@"%zu matches", results.count]];
                [pick setAccessoryView:choices]; [pick addButtonWithTitle:@"Open Match"];
                [pick addButtonWithTitle:@"Cancel"];
                if ([pick runModal] == NSAlertFirstButtonReturn) {
                    NSDictionary *match = [[choices selectedItem] representedObject];
                    NSString *path = [match objectForKey:@"path"];
                    selectedLine = (size_t)[[match objectForKey:@"line"] unsignedLongValue];
                    if (path != nil && selectedLine > 0) {
                        [self openPath:path];
                        AxyneDocument *opened = [self activeDocument];
                        if (opened != NULL && opened->path != NULL &&
                            strcmp(opened->path, [path UTF8String]) == 0) {
                            NSInteger pos = [self sendEditorMessage:SCI_POSITIONFROMLINE
                                wParam:selectedLine > 0 ? selectedLine - 1 : 0 lParam:0];
                            (void)[self sendEditorMessage:SCI_GOTOPOS wParam:(uintptr_t)pos lParam:0];
                        }
                    }
                }
            }
            axyne_search_results_destroy(&results);
        }
    }
}

- (void)terminalAppend:(const char *)bytes length:(size_t)length
                stream:(AxyneProcessStream)stream
{
    if (_terminalOutput == nil || bytes == NULL || length == 0) return;
    NSString *text = [[[NSString alloc] initWithBytes:bytes length:length
                                             encoding:NSUTF8StringEncoding] autorelease];
    if (text == nil) text = @"(invalid UTF-8 output)";
    if (stream == AXYNE_PROCESS_STDERR) text = [@"[stderr] " stringByAppendingString:text];
    NSTextStorage *storage = [_terminalOutput textStorage];
    [storage appendAttributedString:[[[NSAttributedString alloc]
        initWithString:text attributes:@{ NSFontAttributeName:
            [NSFont fontWithName:@"Menlo" size:11],
            NSForegroundColorAttributeName:axyne_preference_color(_preferences.theme.text) }]
        autorelease]];
    if ([storage length] > 1024 * 1024)
        [storage deleteCharactersInRange:NSMakeRange(0, [storage length] - 1024 * 1024)];
    [_terminalOutput scrollRangeToVisible:NSMakeRange([storage length], 0)];
}

- (void)terminalExited:(int)exitCode
{
    char message[96];
    _lastExitCode = exitCode;
    _lastExitFailed = exitCode != 0;
    _hasExitStatus = YES;
    (void)snprintf(message, sizeof(message),
        _lastExitFailed ? "[failed: exit %d]\n" : "[exit %d]\n", exitCode);
    [self terminalAppend:message length:strlen(message)
                   stream:AXYNE_PROCESS_STDOUT];
    if (_terminalProcess != NULL) {
        axyne_process_release(_terminalProcess);
        _terminalProcess = NULL;
    }
    _activeAction = 0;
    [_terminalStart setEnabled:YES];
    [_terminalStop setEnabled:NO];
    [self setNeedsDisplay:YES];
}

- (void)startTerminal:(id)sender
{
    AxyneProcessSpec spec;
    AxyneError error;
    AxyneStatus status;
    (void)sender;
    if (_terminalProcess != NULL) return;
    status = axyne_runner_process_spec(&_terminalRunner,
        axyne_macos_terminal_output, axyne_macos_terminal_exit, self,
        &spec, &error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_process_start(&spec, &_terminalProcess, &error);
    if (status != AXYNE_STATUS_OK) {
        _lastExitFailed = NO;
        _hasExitStatus = NO;
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
        [self setNeedsDisplay:YES];
        return;
    }
    [_terminalStart setEnabled:NO];
    [_terminalStop setEnabled:YES];
    _activeAction = 3;
    _lastExitFailed = NO;
}

- (void)stopTerminal:(id)sender
{
    (void)sender;
    if (_terminalProcess != NULL) (void)axyne_process_terminate(_terminalProcess, NULL);
}

- (void)sendTerminal:(id)sender
{
    const char *value;
    NSMutableData *data;
    AxyneError error;
    (void)sender;
    if (_terminalProcess == NULL || [[_terminalInput stringValue] length] == 0) return;
    value = [[_terminalInput stringValue] UTF8String];
    data = [NSMutableData dataWithBytes:value length:strlen(value)];
    [data appendBytes:"\n" length:1];
    if (axyne_process_write(_terminalProcess, [data bytes], [data length], &error) != AXYNE_STATUS_OK)
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
else [_terminalInput setStringValue:@""];
}

- (BOOL)configureRunner
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    NSView *accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 460, 280)];
    NSTextField *executable = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 246, 460, 24)];
    NSTextField *workingDirectory = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 201, 460, 24)];
    NSTextView *arguments = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 440, 70)];
    NSTextView *environment = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 440, 70)];
    NSScrollView *argumentsScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 122, 460, 70)];
    NSScrollView *environmentScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 32, 460, 70)];
    NSString *initialExecutable = _actionRunner.executable != NULL
        ? [NSString stringWithUTF8String:_actionRunner.executable] : @"";
    NSString *initialWorkingDirectory = _actionRunner.working_directory != NULL
        ? [NSString stringWithUTF8String:_actionRunner.working_directory] : @"";
    [executable setStringValue:initialExecutable ?: @""];
    [workingDirectory setStringValue:initialWorkingDirectory ?: @""];
    [arguments setString:axyne_macos_runner_lines(_actionRunner.arguments,
                                                   _actionRunner.argument_count)];
    [environment setString:axyne_macos_runner_lines(_actionRunner.environment,
                                                     _actionRunner.environment_count)];
    [arguments setFont:[NSFont userFixedPitchFontOfSize:11]];
    [environment setFont:[NSFont userFixedPitchFontOfSize:11]];
    [argumentsScroll setHasVerticalScroller:YES];
    [argumentsScroll setDocumentView:arguments];
    [environmentScroll setHasVerticalScroller:YES];
    [environmentScroll setDocumentView:environment];
    [accessory addSubview:axyne_macos_label(@"Executable", 224)];
    [accessory addSubview:executable];
    [accessory addSubview:axyne_macos_label(@"Arguments (one per line)", 194)];
    [accessory addSubview:argumentsScroll];
    [accessory addSubview:axyne_macos_label(@"Working directory (optional)", 179)];
    [accessory addSubview:workingDirectory];
    [accessory addSubview:axyne_macos_label(
        @"Environment overrides (NAME=VALUE per line)", 104)];
    [accessory addSubview:environmentScroll];
    [alert setMessageText:@"Configure Build/Run Runner"];
    [alert setInformativeText:@"Arguments are passed directly to the executable; no shell is used."];
    [alert setAccessoryView:accessory];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];
    NSInteger response = [alert runModal];
    BOOL accepted = NO;
    if (response == NSAlertFirstButtonReturn) {
        char *executableUTF8 = strdup([[executable stringValue] UTF8String] ?: "");
        char *argumentsUTF8 = strdup([[arguments string] UTF8String] ?: "");
        char *workingDirectoryUTF8 = strdup([[workingDirectory stringValue] UTF8String] ?: "");
        char *environmentUTF8 = strdup([[environment string] UTF8String] ?: "");
        char **argumentValues = NULL;
        char **environmentValues = NULL;
        size_t argumentCount = 0;
        size_t environmentCount = 0;
        AxyneRunnerSpec spec = {0};
        AxyneError error = {0};
        AxyneStatus status = AXYNE_STATUS_OK;
        if (executableUTF8 == NULL || executableUTF8[0] == '\0' ||
            argumentsUTF8 == NULL || workingDirectoryUTF8 == NULL ||
            environmentUTF8 == NULL ||
            !axyne_macos_runner_split_lines(argumentsUTF8, &argumentValues,
                                            &argumentCount) ||
            !axyne_macos_runner_split_lines(environmentUTF8, &environmentValues,
                                            &environmentCount)) {
            status = AXYNE_STATUS_OUT_OF_MEMORY;
            (void)snprintf(error.message, sizeof(error.message),
                           "Unable to read runner configuration.");
        } else {
            spec.executable = executableUTF8;
            spec.arguments = (const char *const *)argumentValues;
            spec.argument_count = argumentCount;
            spec.working_directory = workingDirectoryUTF8[0] != '\0'
                ? workingDirectoryUTF8 : NULL;
            spec.environment = (const char *const *)environmentValues;
            spec.environment_count = environmentCount;
            status = axyne_runner_configure(&_actionRunner, &spec, &error);
        }
        free(executableUTF8);
        free(argumentsUTF8);
        free(workingDirectoryUTF8);
        free(environmentUTF8);
        axyne_macos_runner_values_free(argumentValues, argumentCount);
        axyne_macos_runner_values_free(environmentValues, environmentCount);
        if (status == AXYNE_STATUS_OK) {
            accepted = YES;
        } else {
            NSAlert *errorAlert = [[[NSAlert alloc] init] autorelease];
            [errorAlert setMessageText:@"Invalid runner configuration"];
            [errorAlert setInformativeText:[NSString stringWithUTF8String:
                error.message[0] != '\0' ? error.message :
                "Unable to configure runner."] ?: @""];
            [errorAlert addButtonWithTitle:@"OK"];
            [errorAlert runModal];
        }
    }
    [executable release];
    [workingDirectory release];
    [arguments release];
    [environment release];
    [argumentsScroll release];
    [environmentScroll release];
    [accessory release];
    return accepted;
}

- (void)startAction:(BOOL)run
{
    AxyneDocument *doc = [self activeDocument];
    AxyneProcessSpec processSpec;
    AxyneError error;
    AxyneStatus status;
    if (_terminalProcess != NULL) {
        const char *message = "Build or run is unavailable while a terminal session is active. Stop it first.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    if (![self captureEditor]) return;
    if (doc == NULL || doc->is_untitled || doc->path == NULL || doc->is_dirty) {
        if (![self saveActive]) {
            const char *message = "Save the active document before building or running.\n";
            [self terminalAppend:message length:strlen(message)
                           stream:AXYNE_PROCESS_STDERR];
            return;
        }
        doc = [self activeDocument];
        if (doc == NULL || doc->is_untitled || doc->path == NULL || doc->is_dirty)
            return;
    }
    if (_actionRunner.executable == NULL) {
        const char *message = "Configure the Build/Run Runner before building or running.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    status = axyne_runner_process_spec(&_actionRunner,
            axyne_macos_terminal_output, axyne_macos_terminal_exit, self,
            &processSpec, &error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_process_start(&processSpec, &_terminalProcess, &error);
    if (status != AXYNE_STATUS_OK) {
        _lastExitFailed = NO;
        _hasExitStatus = NO;
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
        [self setNeedsDisplay:YES];
    } else {
        [_terminalOutput setString:(run ? @"[run]\n" : @"[build]\n")];
        _activeAction = run ? 2 : 1;
        _lastExitFailed = NO;
        [_terminalStart setEnabled:NO];
        [_terminalStop setEnabled:YES];
        [self setNeedsDisplay:YES];
    }
}

- (void)buildDocument:(id)sender
{
    (void)sender;
    [self startAction:NO];
}

- (void)runDocument:(id)sender
{
    (void)sender;
    [self startAction:YES];
}

- (void)layout
{
    [super layout];
    NSRect bounds = [self bounds];
    CGFloat editorTop = AXYNE_TOOLBAR + AXYNE_TABS;
    CGFloat bottomTop = NSHeight(bounds) - AXYNE_STATUS - AXYNE_BOTTOM;
    NSRect editorFrame = NSMakeRect(AXYNE_SIDEBAR, editorTop,
        MAX(0.0, NSWidth(bounds) - AXYNE_SIDEBAR),
        MAX(0.0, bottomTop - editorTop));
    [_editorView setFrame:editorFrame];
    CGFloat terminalTop = bottomTop + 30.0;
    CGFloat inputTop = bottomTop + AXYNE_BOTTOM - 28.0;
    [_terminalOutput setFrame:NSMakeRect(12.0, terminalTop,
        MAX(0.0, NSWidth(bounds) - 24.0), AXYNE_BOTTOM - 62.0)];
    [_terminalInput setFrame:NSMakeRect(12.0, inputTop,
        MAX(0.0, NSWidth(bounds) - 260.0), 22.0)];
    [_terminalStart setFrame:NSMakeRect(NSWidth(bounds) - 240.0, inputTop,
        96.0, 22.0)];
    [_terminalStop setFrame:NSMakeRect(NSWidth(bounds) - 138.0, inputTop,
        56.0, 22.0)];
    [_terminalSend setFrame:NSMakeRect(NSWidth(bounds) - 76.0, inputTop,
        64.0, 22.0)];
}

- (void)drawLabel:(NSString *)label at:(NSPoint)point
             size:(CGFloat)size color:(NSColor *)color family:(NSString *)family
{
    NSDictionary *attributes = @{
        NSFontAttributeName: [NSFont fontWithName:family size:size] ?: [NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName: color
    };
    [label drawAtPoint:point withAttributes:attributes];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    CGFloat width = NSWidth(bounds);
    CGFloat height = NSHeight(bounds);
    CGFloat bottomTop = height - AXYNE_STATUS - AXYNE_BOTTOM;
    CGFloat statusTop = height - AXYNE_STATUS;
    NSColor *background = axyne_preference_color(_preferences.theme.background);
    NSColor *panel = axyne_preference_color(_preferences.theme.panel);
    NSColor *muted = axyne_preference_color(_preferences.theme.muted);
    NSColor *text = axyne_preference_color(_preferences.theme.text);
    NSColor *border = axyne_preference_color(_preferences.theme.border);
    NSColor *toolbar = axyne_preference_color(_preferences.theme.toolbar);

    [background setFill];
    NSRectFill(bounds);
    [toolbar setFill];
    NSRectFill(NSMakeRect(0, 0, width, AXYNE_TOOLBAR));
    [toolbar setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR, width, AXYNE_TABS));
    [panel setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR + AXYNE_TABS,
                          AXYNE_SIDEBAR, bottomTop - AXYNE_TOOLBAR - AXYNE_TABS));
    [toolbar setFill];
    NSRectFill(NSMakeRect(0, bottomTop, width, AXYNE_BOTTOM));
    [background setFill];
    NSRectFill(NSMakeRect(0, statusTop, width, AXYNE_STATUS));
    [border setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR - 1, AXYNE_TOOLBAR + AXYNE_TABS,
                          1, bottomTop - AXYNE_TOOLBAR - AXYNE_TABS));
    NSRectFill(NSMakeRect(0, bottomTop, width, 1));

    [self drawLabel:@"▱   ▣    ↶   ↷       ▷  Debug · x64             빌드  ⌘B"
                at:NSMakePoint(14, 13) size:12 color:muted family:@"SF Pro Text"];
    NSRect search = NSMakeRect(MAX(400, width - 360), 7, 348, 26);
    [background setFill];
    [[NSBezierPath bezierPathWithRoundedRect:search xRadius:4 yRadius:4] fill];
    [border setStroke];
    [[NSBezierPath bezierPathWithRoundedRect:search xRadius:4 yRadius:4] stroke];
    [self drawLabel:@"⌕  파일 이동, > 명령 실행"
                at:NSMakePoint(NSMinX(search) + 10, 13) size:11 color:muted family:@"SF Pro Text"];

    [axyne_preference_color(_preferences.theme.accent) setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR + 20, AXYNE_TOOLBAR + AXYNE_TABS,
                          1, AXYNE_TABS));
    CGFloat tabX = AXYNE_SIDEBAR + 12;
    for (size_t i = 0; i < _documents.count; ++i) {
        AxyneDocument *doc = &_documents.documents[i];
        if (i == _documents.active_index) {
            [panel setFill];
            NSRectFill(NSMakeRect(tabX, AXYNE_TOOLBAR, 184, AXYNE_TABS));
            [axyne_preference_color(_preferences.theme.accent) setFill];
            NSRectFill(NSMakeRect(tabX, AXYNE_TOOLBAR, 1, AXYNE_TABS));
        }
        NSString *title = [NSString stringWithUTF8String:doc->title ?: "Untitled"];
        if (doc->is_dirty) title = [@"● " stringByAppendingString:title ?: @"Untitled"];
        [self drawLabel:title at:NSMakePoint(tabX + 12, AXYNE_TOOLBAR + 10)
                    size:12 color:i == _documents.active_index ? text : muted
                 family:@"SF Pro Text"];
        [self drawLabel:@"×" at:NSMakePoint(tabX + 163, AXYNE_TOOLBAR + 10)
                    size:12 color:muted family:@"SF Pro Text"];
        tabX += 184;
    }
    [self drawLabel:@"탐색기" at:NSMakePoint(12, AXYNE_TOOLBAR + AXYNE_TABS + 10)
                size:11 color:muted family:@"SF Pro Text"];
    CGFloat explorerY = AXYNE_TOOLBAR + AXYNE_TABS + 31;
    if (_explorer.root == NULL) {
        [self drawLabel:@"폴더 열기..." at:NSMakePoint(16, explorerY)
                    size:12 color:text family:@"SF Pro Text"];
    } else {
        for (size_t i = 0; i < _explorer.count && explorerY + 22 < bottomTop; ++i) {
            AxyneExplorerNode *node = &_explorer.nodes[i];
            if (_hasExplorerSelection && _explorerSelection == (NSInteger)i) {
                [border setFill];
                NSRectFill(NSMakeRect(0, explorerY - 2, AXYNE_SIDEBAR, 22));
            }
            NSString *name = [NSString stringWithUTF8String:node->name] ?: @"(invalid name)";
            NSString *arrow = node->kind == AXYNE_FILE_KIND_DIRECTORY
                ? (axyne_explorer_is_expanded(&_explorer, node->path) ? @"⌄" : @"›") : @"·";
            NSString *label = [NSString stringWithFormat:@"%@ %@", arrow, name];
            [self drawLabel:label at:NSMakePoint(16 + node->depth * 16, explorerY)
                        size:12 color:text family:@"SF Pro Text"];
            explorerY += 22;
        }
    }
    [self drawLabel:@"출력     문제 1     터미널"
                at:NSMakePoint(12, bottomTop + 9) size:11 color:muted family:@"SF Pro Text"];
    NSString *status = _lastExitFailed
        ? [NSString stringWithFormat:@"✗ 실행 실패 (exit %d)", _lastExitCode]
        : (_activeAction != 0 ? @"● 실행 중"
           : (_hasExitStatus ? @"✓ 실행 완료 (exit 0)" : @"✓ 빌드 준비됨"));
    [self drawLabel:status
                at:NSMakePoint(12, statusTop + 6) size:10 color:muted family:@"SF Pro Text"];
    [self drawLabel:@"줄 1, 열 1     UTF-8    C17"
                at:NSMakePoint(MAX(12, width - 250), statusTop + 6)
                size:10 color:muted family:@"SF Pro Text"];

    if (_editorView == nil) {
        [self drawLabel:@"Required Scintilla framework failed to load"
                    at:NSMakePoint(AXYNE_SIDEBAR + 24, AXYNE_TOOLBAR + AXYNE_TABS + 24)
                    size:12 color:muted family:@"Menlo"];
    }
}

- (void)dealloc
{
    if (_terminalProcess != NULL) {
        axyne_process_release(_terminalProcess);
        _terminalProcess = NULL;
    }
    axyne_runner_destroy(&_terminalRunner);
    axyne_runner_destroy(&_actionRunner);
    if (_watcher != NULL) {
        axyne_watcher_stop(_watcher);
        axyne_watcher_release(_watcher);
    }
    axyne_explorer_destroy(&_explorer);
    [_recentMenu release];
    if (_editorView != nil) {
        for (size_t i = 0; i < _documents.count; ++i) {
            AxyneDocument *doc = &_documents.documents[i];
            if (doc->owns_native_editor_document)
                (void)[self sendEditorMessage:SCI_RELEASEDOCUMENT wParam:0
                    lParam:(intptr_t)doc->native_editor_document];
        }
    }
    axyne_documents_destroy(&_documents);
    [_editorView release];
    [_terminalOutput release];
    [_terminalInput release];
    [_terminalStart release];
    [_terminalStop release];
    [_terminalSend release];
    [_scintillaBundle unload];
    [_scintillaBundle release];
    free(_globalPreferencesPath);
    free(_workspacePreferencesPath);
    [super dealloc];
}

@end

@interface AxyneApplicationDelegate : NSObject <NSApplicationDelegate> {
    NSWindow *_window;
    NSString *_appName;
    BOOL _terminationConfirmed;
}
- (instancetype)initWithAppName:(NSString *)appName;
- (BOOL)windowShouldClose:(NSWindow *)sender;
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender;
@end

@implementation AxyneApplicationDelegate

- (instancetype)initWithAppName:(NSString *)appName
{
    self = [super init];
    if (self != nil) {
        _appName = [appName copy];
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    NSRect frame = NSMakeRect(0, 0, 1440, 900);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:_appName ?: @"Axyne"];
    [_window setMinSize:NSMakeSize(800, 560)];
    AxyneWorkspaceView *workspace = [[[AxyneWorkspaceView alloc]
        initWithFrame:frame] autorelease];
    [_window setContentView:workspace];
    [_window setDelegate:self];
    axyne_install_menu([NSApplication sharedApplication], workspace);
    [_window center];
    [_window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    if (_terminationConfirmed) return YES;
    _terminationConfirmed = [(AxyneWorkspaceView *)[sender contentView]
        confirmCloseAll];
    return _terminationConfirmed;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    if (_terminationConfirmed) return NSTerminateNow;
    if (_window == nil || ![_window isVisible]) return NSTerminateNow;
    _terminationConfirmed = [(AxyneWorkspaceView *)[_window contentView]
        confirmCloseAll];
    return _terminationConfirmed ? NSTerminateNow : NSTerminateCancel;
}

- (void)dealloc
{
    [_window release];
    [_appName release];
    [super dealloc];
}

@end

static void axyne_install_menu(NSApplication *application,
                               AxyneWorkspaceView *workspace)
{
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"Axyne"
        action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Axyne"];
    [appMenu addItemWithTitle:@"About Axyne" action:nil keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *preferencesItem = [appMenu addItemWithTitle:@"Preferences…"
        action:@selector(showGlobalPreferences:) keyEquivalent:@","];
    [preferencesItem setTarget:workspace];
    [appMenu addItemWithTitle:@"Quit Axyne" action:@selector(terminate:)
                 keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];
    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File"
        action:nil keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *newItem = [fileMenu addItemWithTitle:@"New"
        action:@selector(newDocument:) keyEquivalent:@"n"];
    [newItem setTarget:workspace];
    NSMenuItem *openItem = [fileMenu addItemWithTitle:@"Open…"
        action:@selector(openDocument:) keyEquivalent:@"o"];
    [openItem setTarget:workspace];
    NSMenuItem *saveItem = [fileMenu addItemWithTitle:@"Save"
        action:@selector(saveDocument:) keyEquivalent:@"s"];
    [saveItem setTarget:workspace];
    NSMenuItem *saveAsItem = [fileMenu addItemWithTitle:@"Save As…"
        action:@selector(saveDocumentAs:) keyEquivalent:@"S"];
    [saveAsItem setTarget:workspace];
    NSMenuItem *closeItem = [fileMenu addItemWithTitle:@"Close Tab"
        action:@selector(closeDocument:) keyEquivalent:@"w"];
    [closeItem setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *workspaceItem = [fileMenu addItemWithTitle:@"Open Workspace Folder…"
        action:@selector(openWorkspace:) keyEquivalent:@""];
    [workspaceItem setTarget:workspace];
    NSMenuItem *workspacePreferences = [fileMenu addItemWithTitle:@"Workspace Settings…"
        action:@selector(showWorkspacePreferences:) keyEquivalent:@""];
    [workspacePreferences setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *recentItem = [[NSMenuItem alloc] initWithTitle:@"Open Recent"
        action:nil keyEquivalent:@""];
    NSMenu *recentMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
    [recentItem setSubmenu:recentMenu];
    [fileMenu addItem:recentItem];
    [workspace setRecentMenu:recentMenu];
    [fileItem setSubmenu:fileMenu];
    [mainMenu addItem:fileItem];
    [fileItem release]; [recentItem release];
    [recentMenu release]; [fileMenu release];
    NSArray *titles = @[@"Edit", @"View", @"Build", @"Debug", @"Tools", @"Help"];
    for (NSString *title in titles) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
            action:nil keyEquivalent:@""];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:title];
        if ([title isEqualToString:@"Build"]) {
            NSMenuItem *build = [submenu addItemWithTitle:@"Build Active Document"
                action:@selector(buildDocument:) keyEquivalent:@"b"];
            NSMenuItem *run = [submenu addItemWithTitle:@"Run Active Document"
                action:@selector(runDocument:) keyEquivalent:@"r"];
            NSMenuItem *configure = [submenu addItemWithTitle:@"Configure Build/Run Runner…"
                action:@selector(configureRunner) keyEquivalent:@"configure"];
            [build setTarget:workspace]; [run setTarget:workspace];
            [configure setTarget:workspace];
        }
        [item setSubmenu:submenu];
        [submenu release];
        [mainMenu addItem:item];
        [item release];
    }
    [application setMainMenu:mainMenu];
    [mainMenu release];
    [appMenu release];
    [appItem release];
}

int axyne_ui_run(const char *app_name)
{
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSString *title = app_name != NULL
            ? [NSString stringWithUTF8String:app_name] : @"Axyne";
        AxyneApplicationDelegate *delegate =
            [[AxyneApplicationDelegate alloc] initWithAppName:title];
        [application setDelegate:delegate];
        [application run];
        [application setDelegate:nil];
        [delegate release];
    }
    return 0;
}
