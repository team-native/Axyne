#import <AppKit/AppKit.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <pthread.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>

#include "axyne/document.h"
#include "axyne/search.h"
#include "axyne/explorer.h"
#include "axyne/watcher.h"
#include "axyne/process.h"
#include "axyne/runner.h"
#include "axyne/debugger.h"
#include "axyne/preferences.h"
#include "axyne/git.h"
#include "axyne/lsp.h"
#include "axyne/ui_design.h"
#include "Scintilla.h"
#include "../../editor_document.h"

@interface NSObject (AxyneScintillaMessages)
- (NSView *)content;
- (NSInteger)message:(unsigned int)message wParam:(uintptr_t)wParam
               lParam:(intptr_t)lParam;
@end

static const CGFloat AXYNE_SIDEBAR = AXYNE_UI_SIDEBAR;
/* These bands follow the 1440x900 Figma work area: toolbar 38px, tabs 34px,
 * output panel 230px, and status bar 24px. The macOS titlebar remains owned
 * by AppKit so its traffic-light controls stay native and accessible. */
static const CGFloat AXYNE_MENU = AXYNE_UI_MENU;
static const CGFloat AXYNE_TOOLBAR = AXYNE_UI_TOOLBAR;
static const CGFloat AXYNE_TABS = AXYNE_UI_TABS;
static const CGFloat AXYNE_STATUS = AXYNE_UI_STATUS;
static const CGFloat AXYNE_BOTTOM = AXYNE_UI_PANEL;
static const CGFloat AXYNE_CONTENT_TOP = AXYNE_MENU + AXYNE_TOOLBAR;

typedef struct AxyneMacGitRun AxyneMacGitRun;
typedef struct AxyneMacGitCompletion AxyneMacGitCompletion;

/* Native buttons keep AppKit's actions and accessibility while drawing the
 * flat, precisely centered Figma toolbar rather than an OS bezel. */
@interface AxyneChromeButton : NSButton
@property(nonatomic, retain) NSColor *fillColor;
@property(nonatomic, retain) NSColor *labelColor;
@end

@implementation AxyneChromeButton
@synthesize fillColor = _fillColor, labelColor = _labelColor;
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    if (_fillColor != nil) {
        [_fillColor setFill];
        [[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:3 yRadius:3] fill];
    }
    if ([[self cell] isHighlighted]) {
        [[NSColor colorWithWhite:1 alpha:0.08] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:3 yRadius:3] fill];
    }
    NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [style setAlignment:NSTextAlignmentCenter];
    [style setLineBreakMode:NSLineBreakByTruncatingTail];
    NSColor *color = _labelColor != nil ? _labelColor : [NSColor labelColor];
    if (![self isEnabled]) color = [color colorWithAlphaComponent:0.45];
    NSDictionary *attributes = @{NSFontAttributeName:[self font],
        NSForegroundColorAttributeName:color, NSParagraphStyleAttributeName:style};
    CGFloat height = [[self title] sizeWithAttributes:attributes].height;
    [[self title] drawInRect:NSMakeRect(4, (NSHeight(bounds) - height) / 2,
        MAX(0, NSWidth(bounds) - 8), height) withAttributes:attributes];
}
- (void)dealloc
{
    [_fillColor release]; [_labelColor release]; [super dealloc];
}
@end

static NSColor *axyne_preference_color(uint32_t value);

@interface AxyneMenuItemView : NSView {
    NSMenuItem *_menuItem;
    BOOL _highlighted;
}
- (instancetype)initWithMenuItem:(NSMenuItem *)menuItem;
- (void)highlight:(BOOL)highlight;
@end

@interface AxynePopupMenuDelegate : NSObject <NSMenuDelegate>
@end

@implementation AxyneMenuItemView

- (instancetype)initWithMenuItem:(NSMenuItem *)menuItem
{
    self = [super initWithFrame:NSMakeRect(0, 0, 280, 28)];
    if (self != nil) {
        _menuItem = menuItem;
        [self setAutoresizingMask:NSViewWidthSizable];
    }
    return self;
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)highlight:(BOOL)highlight
{
    _highlighted = highlight;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    BOOL highlighted = _highlighted || [_menuItem isHighlighted];
    [highlighted ? axyne_preference_color(0x2a2e35) :
        axyne_preference_color(0x1f2126) setFill];
    NSRectFill(bounds);

    NSDictionary *attributes = @{
        NSFontAttributeName:[NSFont systemFontOfSize:12],
        NSForegroundColorAttributeName:axyne_preference_color(0xc7c9ce)
    };
    [[_menuItem title] drawAtPoint:NSMakePoint(12, 7) withAttributes:attributes];

    NSString *key = [_menuItem keyEquivalent];
    if ([key length] != 0) {
        NSUInteger modifiers = [_menuItem keyEquivalentModifierMask];
        NSMutableString *shortcut = [NSMutableString string];
        if ((modifiers & NSEventModifierFlagCommand) != 0) [shortcut appendString:@"⌘"];
        if ((modifiers & NSEventModifierFlagShift) != 0) [shortcut appendString:@"⇧"];
        if ((modifiers & NSEventModifierFlagOption) != 0) [shortcut appendString:@"⌥"];
        if ((modifiers & NSEventModifierFlagControl) != 0) [shortcut appendString:@"⌃"];
        unichar keyCode = [key characterAtIndex:0];
        NSString *keyLabel = nil;
        switch (keyCode) {
            case NSF5FunctionKey: keyLabel = @"F5"; break;
            case NSF6FunctionKey: keyLabel = @"F6"; break;
            case NSF9FunctionKey: keyLabel = @"F9"; break;
            case NSF10FunctionKey: keyLabel = @"F10"; break;
            default: keyLabel = [key uppercaseString]; break;
        }
        [shortcut appendString:keyLabel];
        NSDictionary *shortcutAttributes = @{
            NSFontAttributeName:[NSFont systemFontOfSize:11],
            NSForegroundColorAttributeName:axyne_preference_color(0x8b919b)
        };
        NSSize shortcutSize = [shortcut sizeWithAttributes:shortcutAttributes];
        [shortcut drawAtPoint:NSMakePoint(NSMaxX(bounds) - shortcutSize.width - 12, 8)
            withAttributes:shortcutAttributes];
    }
}

- (void)dealloc
{
    [super dealloc];
}
@end

@implementation AxynePopupMenuDelegate

- (void)menu:(NSMenu *)menu willHighlightItem:(NSMenuItem *)item
{
    for (NSMenuItem *candidate in [menu itemArray]) {
        AxyneMenuItemView *view = (AxyneMenuItemView *)[candidate view];
        if ([view isKindOfClass:[AxyneMenuItemView class]])
            [view highlight:(candidate == item)];
    }
}

@end

@interface AxyneMenuSeparatorView : NSView
@end

@implementation AxyneMenuSeparatorView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [axyne_preference_color(0x292c32) setFill];
    NSRectFill(NSMakeRect(10, 3, MAX(0, NSWidth([self bounds]) - 20), 1));
}
@end

static void axyne_style_popup_menu(NSMenu *menu)
{
    [menu setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameAqua]];
    [menu setAutoenablesItems:YES];
    NSArray *items = [[menu itemArray] copy];
    for (NSUInteger index = 0; index < [items count]; ++index) {
        NSMenuItem *item = [items objectAtIndex:index];
        if ([item isSeparatorItem]) {
            NSMenuItem *replacement = [[[NSMenuItem alloc] initWithTitle:@""
                action:nil keyEquivalent:@""] autorelease];
            AxyneMenuSeparatorView *separator = [[[AxyneMenuSeparatorView alloc]
                initWithFrame:NSMakeRect(0, 0, 280, 8)] autorelease];
            [replacement setEnabled:NO];
            [replacement setView:separator];
            [menu removeItemAtIndex:index];
            [menu insertItem:replacement atIndex:index];
            continue;
        }
        if ([item submenu] != nil) axyne_style_popup_menu([item submenu]);
        NSDictionary *attributes = @{
            NSFontAttributeName:[NSFont systemFontOfSize:12],
            NSForegroundColorAttributeName:axyne_preference_color(0x202124)
        };
        [item setAttributedTitle:[[[NSAttributedString alloc]
            initWithString:[item title] attributes:attributes] autorelease]];
    }
    [items release];
}

static NSColor *axyne_color(CGFloat red, CGFloat green, CGFloat blue)
{
    return [NSColor colorWithCalibratedRed:red / 255.0
                                     green:green / 255.0
                                      blue:blue / 255.0
                                     alpha:1.0];
}

@interface AxyneWorkspaceView : NSView <NSMenuItemValidation> {
    NSView *_editorView;
    NSImageView *_imagePreview;
    NSBundle *_scintillaBundle;
    NSButton *_newButton;
    NSButton *_openButton;
    NSButton *_saveButton;
    NSButton *_undoButton;
    NSButton *_redoButton;
    NSButton *_buildButton;
    NSButton *_runButton;
    NSButton *_targetButton;
    NSButton *_searchButton;
    NSButton *_outputTab;
    NSButton *_problemsTab;
    NSButton *_terminalTab;
    NSButton *_clearOutput;
    NSScrollView *_terminalScroll;
    NSTextField *_problemSummary;
    NSInteger _panelMode;
    NSInteger _activeMenuIndex;
    NSInteger _explorerFirstRow;
    CGFloat _tabScroll;
    void *_lexillaModule;
    void *(*_createLexer)(const char *name);
    AxyneDocumentSet _documents;
    AxyneExplorer _explorer;
    AxyneWatcher *_watcher;
    NSInteger _explorerSelection;
    BOOL _hasExplorerSelection;
    NSMenu *_recentMenu;
    BOOL _loadingEditor;
    AxyneRunnerConfig _terminalRunner;
    AxyneRunnerConfig _actionRunner;
    NSString *_runnerPreset;
    AxyneDebugger _debugger;
    AxyneProcess *_terminalProcess;
    AxyneProcess *_gitProcess;
    AxyneMacGitRun *_gitRun;
    NSTextView *_terminalOutput;
    NSTextField *_terminalInput;
    NSButton *_terminalStart;
    NSButton *_terminalStop;
    NSButton *_terminalSend;
    NSButton *_debugStart;
    NSButton *_debugPause;
    NSButton *_debugContinue;
    NSButton *_debugNext;
    NSButton *_debugBreakpoint;
    int _activeAction;
    int _lastExitCode;
    BOOL _lastExitFailed;
    BOOL _hasExitStatus;
    AxynePreferences _globalPreferences;
    AxynePreferences _preferences;
    char *_globalPreferencesPath;
    char *_workspacePreferencesPath;
    unsigned char _workspaceBindingPresent[AXYNE_ACTION_COUNT];
    AxyneLspClient *_lsp;
    NSString *_lspStatus;
}
@end

static NSColor *axyne_preference_color(uint32_t value)
{
    return axyne_color((CGFloat)((value >> 16) & 0xff),
                       (CGFloat)((value >> 8) & 0xff),
                       (CGFloat)(value & 0xff));
}

static BOOL axyne_macos_reference_surfaces(const AxyneThemePreferences *theme);
static uint32_t axyne_macos_output_background(const AxyneThemePreferences *theme);

static intptr_t axyne_editor_color(uint32_t rgb)
{
    return (intptr_t)(((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff));
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

static BOOL axyne_macos_reference_surfaces(const AxyneThemePreferences *theme)
{
    return theme->background == 0x16171a && theme->panel == 0x1f2126 &&
        theme->toolbar == 0x1c1e22;
}

static uint32_t axyne_macos_output_background(const AxyneThemePreferences *theme)
{
    return axyne_macos_reference_surfaces(theme) ? 0x1d1f23 : theme->background;
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

static const char *axyne_macos_editor_lexer(const char *path)
{
    const char *extension;
    const char *slash;
    if (path == NULL || path[0] == '\0') return "cpp";
    slash = strrchr(path, '/');
    if ((slash == NULL ? path : slash + 1) != NULL &&
        strcasecmp(slash == NULL ? path : slash + 1, "CMakeLists.txt") == 0)
        return "cmake";
    extension = strrchr(path, '.');
    if (extension != NULL && strcasecmp(extension, ".cmake") == 0)
        return "cmake";
    slash = strrchr(path, '/');
    if (extension == NULL || (slash != NULL && extension < slash)) return "cpp";
    if (strcasecmp(extension, ".c") == 0 || strcasecmp(extension, ".h") == 0 ||
        strcasecmp(extension, ".cc") == 0 || strcasecmp(extension, ".cpp") == 0 ||
        strcasecmp(extension, ".cxx") == 0 || strcasecmp(extension, ".hpp") == 0 ||
        strcasecmp(extension, ".m") == 0 || strcasecmp(extension, ".mm") == 0)
        return "cpp";
    if (strcasecmp(extension, ".py") == 0) return "python";
    if (strcasecmp(extension, ".js") == 0 || strcasecmp(extension, ".jsx") == 0 ||
        strcasecmp(extension, ".ts") == 0 || strcasecmp(extension, ".tsx") == 0)
        return "javascript";
    if (strcasecmp(extension, ".json") == 0) return "json";
    if (strcasecmp(extension, ".html") == 0 || strcasecmp(extension, ".htm") == 0 ||
        strcasecmp(extension, ".xml") == 0) return "hypertext";
    if (strcasecmp(extension, ".css") == 0) return "css";
    if (strcasecmp(extension, ".sh") == 0 || strcasecmp(extension, ".bash") == 0)
        return "bash";
    if (strcasecmp(extension, ".md") == 0 || strcasecmp(extension, ".markdown") == 0)
        return "markdown";
    if (strcasecmp(extension, ".yaml") == 0 || strcasecmp(extension, ".yml") == 0)
        return "yaml";
    if (strcasecmp(extension, ".java") == 0) return "java";
    if (strcasecmp(extension, ".rs") == 0) return "rust";
    if (strcasecmp(extension, ".go") == 0) return "cpp";
    return "null";
}

static BOOL axyne_macos_is_image_path(const char *path)
{
    const char *extension = path == NULL ? NULL : strrchr(path, '.');
    if (extension == NULL) return NO;
    return strcasecmp(extension, ".png") == 0 || strcasecmp(extension, ".jpg") == 0 ||
        strcasecmp(extension, ".jpeg") == 0 || strcasecmp(extension, ".gif") == 0 ||
        strcasecmp(extension, ".tif") == 0 || strcasecmp(extension, ".tiff") == 0 ||
        strcasecmp(extension, ".bmp") == 0 || strcasecmp(extension, ".webp") == 0 ||
        strcasecmp(extension, ".svg") == 0 || strcasecmp(extension, ".ico") == 0;
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
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)closeDocument:(id)sender;
- (void)closeDocumentAtIndex:(size_t)index;
- (void)openRecent:(id)sender;
- (BOOL)confirmCloseAll;
- (void)notification:(SCNotification *)notification;
- (void)setRecentMenu:(NSMenu *)menu;
- (void)refreshRecentMenu;
- (BOOL)captureEditor;
- (void)refreshActionControls;
- (BOOL)validateMenuItem:(NSMenuItem *)menuItem;
- (BOOL)loadActiveDocument;
- (BOOL)selectDocumentAtIndex:(size_t)index;
- (NSInteger)sendEditorMessage:(unsigned int)message wParam:(uintptr_t)wParam
                        lParam:(intptr_t)lParam;
- (void)loadScintillaView;
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
- (void)scrollTabsBy:(CGFloat)delta;
- (void)showWorkspaceError:(NSString *)prefix error:(AxyneError *)error;
- (void)showWorkspaceMessage:(NSString *)message;
- (void)drawLabel:(NSString *)label at:(NSPoint)point
             size:(CGFloat)size color:(NSColor *)color family:(NSString *)family;
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSInteger)explorerNodeAtPoint:(NSPoint)point;
- (NSRect)tabFrameAtIndex:(size_t)index;
- (NSRect)displayTabFrameAtIndex:(size_t)index;
- (void)selectPanel:(id)sender;
- (void)clearOutput:(id)sender;
- (void)quickFile:(id)sender;
- (void)showRunnerMenu:(id)sender;
- (void)selectRunnerPreset:(id)sender;
- (void)configureRunnerAction:(id)sender;
- (void)performExplorerOperation:(AxyneFileKind)kind;
- (NSString *)askForText:(NSString *)title label:(NSString *)label;
- (void)startTerminal:(id)sender;
- (void)stopTerminal:(id)sender;
- (void)sendTerminal:(id)sender;
- (void)terminalAppend:(const char *)bytes length:(size_t)length
                stream:(AxyneProcessStream)stream;
- (void)terminalExited:(AxyneProcess *)process exitCode:(int)exitCode;
- (BOOL)configureRunner;
- (void)buildDocument:(id)sender;
- (void)runDocument:(id)sender;
- (void)startDebugger:(id)sender;
- (void)debugCommand:(id)sender;
- (void)toggleBreakpoint:(id)sender;
- (void)showGlobalPreferences:(id)sender;
- (void)showWorkspacePreferences:(id)sender;
- (void)applyPreferences;
- (void)applySystemAppearance;
- (void)applyEditorLexer;
- (void)updateLineNumberMargin;
- (void)updateBraceHighlight;
- (void)autoIndentFromNotification:(SCNotification *)notification;
- (BOOL)showPreferences:(BOOL)workspace;
- (void)showGitStatus:(id)sender;
- (void)showGitDiff:(id)sender;
- (void)stageAllGitChanges:(id)sender;
- (void)unstageAllGitChanges:(id)sender;
- (void)completeGitOperation:(AxyneMacGitCompletion *)completion
                         run:(AxyneMacGitRun *)run;
- (void)startGitOperationWithEmptyMessage:(const char *)emptyMessage
                                arguments:(const char *const *)arguments
                                   count:(size_t)argumentCount;
- (BOOL)ensureLsp;
- (BOOL)openLspForActive;
- (void)syncLspActive;
- (void)navigateLspReferences:(id)sender;
- (void)setLspStatus:(NSString *)status;
@end

static intptr_t axyne_macos_editor_message(void *editor, unsigned int message,
                                           uintptr_t wParam, intptr_t lParam)
{
    return [(AxyneWorkspaceView *)editor sendEditorMessage:message
        wParam:wParam lParam:lParam];
}

struct AxyneMacGitRun {
    AxyneWorkspaceView *view;
    AxyneProcess *process;
    char *output;
    size_t length;
    size_t capacity;
    const char *empty_message;
    int allocation_failed;
    int output_truncated;
    int exit_code;
    pthread_mutex_t lock;
    int cancelled;
    int references;
};

struct AxyneMacGitCompletion {
    AxyneWorkspaceView *view;
    AxyneProcess *process;
    char *output;
    size_t length;
    const char *empty_message;
    int allocation_failed;
    int output_truncated;
    int exit_code;
    char failure_message[128];
};

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

static void axyne_macos_lsp_status(AxyneWorkspaceView *view, const char *text)
{
    char *copy;
    if (view == nil || text == NULL) return;
    copy = strdup(text);
    if (copy == NULL) return;
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSString *status = [NSString stringWithUTF8String:copy];
        [view setLspStatus:status != nil ? status : @"LSP"];
        free(copy);
        [view release];
    });
}

static void axyne_macos_lsp_diagnostics(AxyneLspClient *client, const char *path,
                                        const AxyneLspDiagnostic *diagnostics,
                                        size_t count, void *user_data)
{
    char text[192];
    (void)client; (void)diagnostics;
    (void)snprintf(text, sizeof(text), "LSP: %zu diagnostics%s%s", count,
                   path == NULL ? "" : " in ", path == NULL ? "" : path);
    axyne_macos_lsp_status((AxyneWorkspaceView *)user_data, text);
}

static void axyne_macos_lsp_navigation(AxyneLspClient *client, uint64_t request_id,
                                       const AxyneLspLocation *locations,
                                       size_t count, void *user_data)
{
    char text[160];
    (void)client; (void)locations;
    (void)snprintf(text, sizeof(text), "LSP: request %llu returned %zu location%s",
                   (unsigned long long)request_id, count, count == 1 ? "" : "s");
    axyne_macos_lsp_status((AxyneWorkspaceView *)user_data, text);
}

static void axyne_macos_lsp_error(AxyneLspClient *client, AxyneStatus status,
                                  const char *message, void *user_data)
{
    char text[192];
    (void)client;
    (void)snprintf(text, sizeof(text), "LSP error (%d): %s", (int)status,
                   message == NULL ? "unknown error" : message);
    axyne_macos_lsp_status((AxyneWorkspaceView *)user_data, text);
}

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
    if (view == nil) return;
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [view terminalExited:process exitCode:exit_code];
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
        NSString *value = [NSString stringWithUTF8String:values[i]];
        if (i != 0) [result appendString:@"\n"];
        [result appendString:value != nil ? value : @""];
    }
    return result;
}

static NSTextField *axyne_macos_label(NSString *text, CGFloat y)
{
    NSTextField *field = [NSTextField labelWithString:text];
    [field setFrame:NSMakeRect(0, y, 460, 20)];
    return field;
}

@interface AxyneFlippedView : NSView
@end

@implementation AxyneFlippedView
- (BOOL)isFlipped { return YES; }
@end

static NSButton *axyne_macos_toolbar_button(NSString *title, id target,
                                            SEL action)
{
    NSButton *button = [[[AxyneChromeButton alloc] initWithFrame:NSZeroRect] autorelease];
    [button setTitle:title];
    [button setTarget:target];
    [button setAction:action];
    [button setBordered:NO];
    [button setBezelStyle:NSBezelStyleTexturedRounded];
    [button setFont:[NSFont systemFontOfSize:12.0]];
    [button setContentTintColor:axyne_color(115, 119, 128)];
    [button setToolTip:title];
    return [button retain];
}

static NSString *axyne_macos_file_badge(const char *name)
{
    return [NSString stringWithUTF8String:axyne_ui_file_badge(name).label];
}

static void axyne_macos_style_editor_scrollbars(NSView *view)
{
    if ([view isKindOfClass:[NSScrollView class]]) {
        NSScrollView *scroll = (NSScrollView *)view;
        [scroll setBorderType:NSNoBorder];
        [scroll setDrawsBackground:NO];
        [scroll setScrollerStyle:NSScrollerStyleOverlay];
        [scroll setScrollerKnobStyle:NSScrollerKnobStyleDark];
        [scroll setAutohidesScrollers:YES];
        [[scroll verticalScroller] setControlSize:NSControlSizeSmall];
        [[scroll horizontalScroller] setControlSize:NSControlSizeSmall];
    }
    for (NSView *child in [view subviews])
        axyne_macos_style_editor_scrollbars(child);
}

@implementation AxyneWorkspaceView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) {
        _activeMenuIndex = -1;
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
        if (axyne_debugger_initialize(&_debugger, NULL) != AXYNE_STATUS_OK ||
            axyne_debugger_configure_default(&_debugger, NULL) != AXYNE_STATUS_OK) {
            axyne_runner_destroy(&_terminalRunner);
            axyne_runner_destroy(&_actionRunner);
            axyne_documents_destroy(&_documents);
            axyne_explorer_destroy(&_explorer);
            [self release];
            return nil;
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
        _terminalScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        [_terminalScroll setBorderType:NSNoBorder];
        [_terminalScroll setHasVerticalScroller:YES];
        [_terminalScroll setAutohidesScrollers:YES];
        [_terminalScroll setDocumentView:_terminalOutput];
        [_terminalOutput setVerticallyResizable:YES];
        [_terminalOutput setHorizontallyResizable:NO];
        [_terminalOutput setAutoresizingMask:NSViewWidthSizable];
        [[_terminalOutput textContainer] setWidthTracksTextView:YES];
        [_terminalOutput setTextContainerInset:NSMakeSize(0, 6)];
        [self addSubview:_terminalScroll];
        _newButton = axyne_macos_toolbar_button(@"▱", self, @selector(newDocument:));
        _openButton = axyne_macos_toolbar_button(@"▰", self, @selector(openDocument:));
        _saveButton = axyne_macos_toolbar_button(@"▣", self, @selector(saveDocument:));
        _undoButton = axyne_macos_toolbar_button(@"↶", self, @selector(undo:));
        _redoButton = axyne_macos_toolbar_button(@"↷", self, @selector(redo:));
        _targetButton = axyne_macos_toolbar_button(@"⌄  Debug · x64 (MSVC)", self, @selector(showRunnerMenu:));
        _searchButton = axyne_macos_toolbar_button(@"⌕  파일 이동, > 명령 실행                 Ctrl+P", self, @selector(quickFile:));
        _buildButton = axyne_macos_toolbar_button(@"빌드  Ctrl+B", self, @selector(buildDocument:));
        _runButton = axyne_macos_toolbar_button(@"▷  실행 F5", self, @selector(runDocument:));
        [self addSubview:_newButton]; [self addSubview:_openButton];
        [self addSubview:_saveButton]; [self addSubview:_undoButton];
        [self addSubview:_redoButton]; [self addSubview:_buildButton];
        [self addSubview:_runButton];
        [self addSubview:_targetButton]; [self addSubview:_searchButton];
        _outputTab = axyne_macos_toolbar_button(@"출력", self, @selector(selectPanel:));
        _problemsTab = axyne_macos_toolbar_button(@"문제", self, @selector(selectPanel:));
        _terminalTab = axyne_macos_toolbar_button(@"터미널", self, @selector(selectPanel:));
        [_outputTab setTag:0]; [_problemsTab setTag:1]; [_terminalTab setTag:2];
        [self addSubview:_outputTab]; [self addSubview:_problemsTab]; [self addSubview:_terminalTab];
        _clearOutput = axyne_macos_toolbar_button(@"⊘", self, @selector(clearOutput:));
        [_clearOutput setToolTip:@"출력 지우기"];
        [self addSubview:_clearOutput];
        _problemSummary = [[NSTextField labelWithString:@"LSP 진단 없음"] retain];
        [self addSubview:_problemSummary];
        _terminalInput = [[NSTextField alloc] initWithFrame:NSZeroRect];
        [_terminalInput setPlaceholderString:@"Terminal input"];
        [_terminalInput setTarget:self]; [_terminalInput setAction:@selector(sendTerminal:)];
        [self addSubview:_terminalInput];
        _terminalStart = axyne_macos_toolbar_button(@"▷", self, @selector(startTerminal:));
        [_terminalStart setToolTip:@"터미널 시작"];
        [_terminalStart setTarget:self]; [_terminalStart setAction:@selector(startTerminal:)];
        [self addSubview:_terminalStart];
        _terminalStop = axyne_macos_toolbar_button(@"×", self, @selector(stopTerminal:));
        [_terminalStop setToolTip:@"프로세스 중지"]; [_terminalStop setTarget:self];
        [_terminalStop setAction:@selector(stopTerminal:)]; [_terminalStop setEnabled:NO];
        [self addSubview:_terminalStop];
        _terminalSend = axyne_macos_toolbar_button(@"전송", self, @selector(sendTerminal:));
        [_terminalSend setTarget:self];
        [_terminalSend setAction:@selector(sendTerminal:)];
        [self addSubview:_terminalSend];
        _debugStart = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugStart setTitle:@"Debug"]; [_debugStart setTarget:self];
        [_debugStart setAction:@selector(startDebugger:)];
        [self addSubview:_debugStart];
        _debugPause = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugPause setTitle:@"Pause"]; [_debugPause setTarget:self];
        [_debugPause setAction:@selector(debugCommand:)]; [_debugPause setTag:1];
        [_debugPause setEnabled:NO]; [self addSubview:_debugPause];
        _debugContinue = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugContinue setTitle:@"Continue"]; [_debugContinue setTarget:self];
        [_debugContinue setAction:@selector(debugCommand:)]; [_debugContinue setTag:0];
        [_debugContinue setEnabled:NO]; [self addSubview:_debugContinue];
        _debugNext = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugNext setTitle:@"Next"]; [_debugNext setTarget:self];
        [_debugNext setAction:@selector(debugCommand:)]; [_debugNext setTag:2];
        [_debugNext setEnabled:NO]; [self addSubview:_debugNext];
        _debugBreakpoint = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugBreakpoint setTitle:@"Breakpoint"]; [_debugBreakpoint setTarget:self];
        [_debugBreakpoint setAction:@selector(toggleBreakpoint:)];
        [_debugBreakpoint setEnabled:NO]; [self addSubview:_debugBreakpoint];
        for (NSButton *button in @[_debugStart, _debugPause, _debugContinue,
                                   _debugNext, _debugBreakpoint]) [button setHidden:YES];
        [_newButton setToolTip:@"새 파일 (⌘N)"];
        [_openButton setToolTip:@"파일 열기 (⌘O)"];
        [_saveButton setToolTip:@"저장 (⌘S)"];
        [_undoButton setToolTip:@"실행 취소 (⌘Z)"];
        [_redoButton setToolTip:@"다시 실행 (⇧⌘Z)"];
        [_searchButton setToolTip:@"파일 이동, 명령 실행 (⌘P)"];
        _runnerPreset = [@"Debug · x64 (MSVC)" retain];
        [self applyPreferences];
        [self refreshActionControls];
    }
    return self;
}

- (AxyneDocument *)activeDocument
{
    if (_documents.count == 0 || _documents.active_index >= _documents.count)
        return NULL;
    return &_documents.documents[_documents.active_index];
}

- (void)refreshActionControls
{
    AxyneDocument *document = [self activeDocument];
    BOOL terminalActive = _terminalProcess != NULL;
    BOOL debuggerActive = axyne_debugger_is_active(&_debugger);
    BOOL savedDocument = document != NULL && !document->is_untitled &&
        document->path != NULL && !document->is_dirty;
    [_terminalStart setEnabled:!terminalActive && !debuggerActive];
    [_terminalStop setEnabled:terminalActive];
    [_terminalSend setEnabled:terminalActive];
    [_debugStart setEnabled:!terminalActive && !debuggerActive && savedDocument];
    [_debugPause setEnabled:debuggerActive && savedDocument];
    [_debugContinue setEnabled:debuggerActive && savedDocument];
    [_debugNext setEnabled:debuggerActive && savedDocument];
    [_debugBreakpoint setEnabled:savedDocument];
    [_buildButton setEnabled:document != NULL && !terminalActive && !debuggerActive];
    [_runButton setEnabled:document != NULL && !terminalActive && !debuggerActive];
    [_saveButton setEnabled:document != NULL];
    NSString *target = _runnerPreset != nil ? _runnerPreset : @"Debug · x64 (MSVC)";
    [_targetButton setTitle:[NSString stringWithFormat:@"▷  %@  ˅", target]];
    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];
}

- (void)quickFile:(id)sender { (void)sender; [self searchFolder:YES]; }
- (void)showRunnerMenu:(id)sender
{
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@"Runner"] autorelease];
    NSArray *presets = @[@"Debug · x64 (MSVC)", @"Debug · x86 (MSVC)",
        @"Debug · ARM64 (MSVC)"];
    for (NSString *preset in presets) {
        NSMenuItem *item = [menu addItemWithTitle:preset
            action:@selector(selectRunnerPreset:) keyEquivalent:@""];
        [item setTarget:self];
        [item setState:[preset isEqualToString:_runnerPreset]
            ? NSControlStateValueOn : NSControlStateValueOff];
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *configure = [menu addItemWithTitle:@"Runner 설정…"
        action:@selector(configureRunnerAction:) keyEquivalent:@""];
    [configure setTarget:self];
    axyne_style_popup_menu(menu);
    NSRect frame = [sender frame];
    [menu popUpMenuPositioningItem:nil
        atLocation:NSMakePoint(0, NSHeight(frame)) inView:sender];
}
- (void)selectRunnerPreset:(id)sender
{
    [_runnerPreset release];
    _runnerPreset = [[sender title] copy];
    [_targetButton setTitle:[NSString stringWithFormat:@"⌄  %@", _runnerPreset]];
    [self refreshActionControls];
}
- (void)configureRunnerAction:(id)sender { (void)sender; (void)[self configureRunner]; [self refreshActionControls]; }
- (void)clearOutput:(id)sender { (void)sender; [_terminalOutput setString:@""]; }
- (void)selectPanel:(id)sender
{
    _panelMode = [sender tag];
    [_problemSummary setStringValue:_lspStatus != nil ? _lspStatus : @"LSP 진단 없음"];
    [self setNeedsLayout:YES]; [self setNeedsDisplay:YES];
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem
{
    SEL action = [menuItem action];
    AxyneDocument *document = [self activeDocument];
    BOOL hasDocument = document != NULL;
    BOOL savedDocument = hasDocument && !document->is_untitled &&
        document->path != NULL && !document->is_dirty;
    BOOL terminalActive = _terminalProcess != NULL;
    BOOL debuggerActive = axyne_debugger_is_active(&_debugger);
    if (action == @selector(saveDocument:) ||
        action == @selector(saveDocumentAs:) ||
        action == @selector(closeDocument:)) return hasDocument;
    if (action == @selector(buildDocument:) ||
        action == @selector(runDocument:))
        return hasDocument && !terminalActive && !debuggerActive;
    if (action == @selector(startDebugger:))
        return savedDocument && !terminalActive && !debuggerActive;
    if (action == @selector(debugCommand:))
        return debuggerActive && savedDocument;
    if (action == @selector(toggleBreakpoint:)) return savedDocument;
    if (action == @selector(startTerminal:))
        return !terminalActive && !debuggerActive;
    if (action == @selector(stopTerminal:) ||
        action == @selector(sendTerminal:)) return terminalActive;
    if (action == @selector(showWorkspacePreferences:))
        return _workspacePreferencesPath != NULL;
    if (action == @selector(showGitStatus:) ||
        action == @selector(showGitDiff:) ||
        action == @selector(stageAllGitChanges:) ||
        action == @selector(unstageAllGitChanges:))
        return _explorer.root != NULL && _gitProcess == NULL;
    if (action == @selector(navigateLspReferences:)) return savedDocument;
    return YES;
}

- (NSInteger)sendEditorMessage:(unsigned int)message wParam:(uintptr_t)wParam
                         lParam:(intptr_t)lParam
{
    if (_editorView == nil) return 0;
    return [_editorView message:message wParam:wParam lParam:lParam];
}

- (void)applyEditorLexer
{
    AxyneDocument *document = [self activeDocument];
    const char *language = axyne_macos_editor_lexer(
        document == NULL ? NULL : document->path);
    void *lexer;
    if (_editorView == nil || _createLexer == NULL) return;
    lexer = _createLexer(language);
    if (lexer == NULL && strcmp(language, "null") != 0)
        lexer = _createLexer("null");
    if (lexer != NULL) {
        (void)[self sendEditorMessage:SCI_SETILEXER wParam:0
                                 lParam:(intptr_t)lexer];
        if (strcmp(language, "cpp") == 0) {
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:0 lParam:(intptr_t)
                "auto break case const continue default do else enum extern for goto "
                "if inline register restrict return sizeof static struct switch typedef "
                "union volatile while"];
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:1 lParam:(intptr_t)
                "void char short int long float double signed unsigned bool size_t "
                "ssize_t ptrdiff_t intptr_t uintptr_t FILE HWND HDC HMENU HFONT "
                "HBRUSH HICON HANDLE HINSTANCE LRESULT WPARAM LPARAM UINT DWORD "
                "WORD BYTE BOOL WCHAR LPCSTR LPCWSTR LPSTR LPWSTR COLORREF RECT POINT SIZE"];
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:3
                lParam:(intptr_t)"NULL true false TRUE FALSE"];
            const unsigned int styles[] = { 1, 2, 3, 15, 4, 5, 6, 7, 9, 10, 11, 16, 19 };
            const uint32_t colors[] = { 0x7a828e, 0x7a828e, 0x7a828e, 0xe3b57d,
                0x738ed9, 0xd98e73, 0xa3c98a, 0xa3c98a, 0xc79ad9, 0xd5d8dd,
                0xd5d8dd, 0x7db5e3, 0x8cc7c0 };
            BOOL reference = axyne_macos_reference_surfaces(&_preferences.theme);
            for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); ++i) {
                uint32_t color = reference ? colors[i] : (i < 4 ? _preferences.theme.muted :
                    (styles[i] == 10 || styles[i] == 11 ? _preferences.theme.editor_text :
                     _preferences.theme.accent));
                (void)[self sendEditorMessage:SCI_STYLESETFORE wParam:styles[i]
                    lParam:axyne_editor_color(color)];
            }
        } else if (strcmp(language, "python") == 0) {
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:0 lParam:(intptr_t)
                "and as assert async await break case class continue def del elif else "
                "except finally for from global if import in is lambda match nonlocal "
                "not or pass raise return try while with yield"];
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:1 lParam:(intptr_t)
                "False None True self int str float bool list dict set tuple print len range"];
        } else if (strcmp(language, "javascript") == 0) {
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:0 lParam:(intptr_t)
                "as async await break case catch class const continue debugger default "
                "delete do else export extends finally for from function if import in "
                "instanceof let new of return static super switch this throw typeof var "
                "void while with yield"];
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:1 lParam:(intptr_t)
                "true false null undefined console window document Promise Array Object"];
        } else if (strcmp(language, "json") == 0) {
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:0 lParam:(intptr_t)
                "true false null"];
        } else if (strcmp(language, "cmake") == 0) {
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:0 lParam:(intptr_t)
                "add_compile_definitions add_compile_options add_custom_command "
                "add_custom_target add_definitions add_dependencies add_executable "
                "add_library add_link_options add_subdirectory add_test "
                "aux_source_directory build_command cmake_host_system_information "
                "cmake_minimum_required configure_file create_test_sourcelist "
                "define_property enable_language enable_testing execute_process "
                "export file find_file find_library find_package find_path "
                "find_program fltk_wrap_ui function get_cmake_property "
                "get_directory_property get_filename_component get_property "
                "get_source_file_property get_target_property get_test_property "
                "include include_directories include_external_msproject "
                "include_guard include_regular_expression install link_directories "
                "link_libraries list load_cache macro mark_as_advanced math "
                "message option project qt_wrap_cpp qt_wrap_ui remove_definitions "
                "return separate_arguments set set_directory_properties "
                "set_property set_source_files_properties set_target_properties "
                "set_tests_properties site_name source_group string target_compile_definitions "
                "target_compile_features target_compile_options target_include_directories "
                "target_link_directories target_link_libraries target_link_options "
                "target_precompile_headers target_sources try_compile try_run "
                "unset variable_watch while"];
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:1 lParam:(intptr_t)
                "ARCHIVE DESTINATION COMPONENT CONFIGURATIONS EXCLUDE_FROM_ALL "
                "FILES FILES_MATCHING GLOB GLOB_RECURSE INCLUDES PATTERN PERMISSIONS "
                "PROGRAMS RENAME TARGETS USE_SOURCE_PERMISSIONS VERSION OPTIONAL "
                "REQUIRED QUIET CONFIG CONFIGURE_DEPENDS PUBLIC PRIVATE INTERFACE "
                "BEFORE AFTER SYSTEM BUILD_INTERFACE INSTALL_INTERFACE"];
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:2 lParam:(intptr_t)
                "CMAKE_BUILD_TYPE CMAKE_CXX_STANDARD CMAKE_C_STANDARD CMAKE_INSTALL_PREFIX "
                "CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_SOURCE_DIR "
                "CMAKE_BINARY_DIR CMAKE_CURRENT_SOURCE_DIR CMAKE_CURRENT_BINARY_DIR"];
        } else if (strcmp(language, "yaml") == 0) {
            [self sendEditorMessage:SCI_SETKEYWORDS wParam:0 lParam:(intptr_t)
                "true false null yes no on off name on push pull_request permissions "
                "jobs runs-on strategy matrix steps uses with if run env needs outputs "
                "branches paths tags permissions contents read write"];
        }
        const unsigned int commonStyles[] = {1, 2, 3, 4, 5, 6, 7, 8};
        const uint32_t commonColors[] = {0x7a828e, 0xd9b36c, 0xc79ad9, 0xa3c98a,
            0xd98e73, 0x738ed9, 0xd5d8dd, 0x7db5e3};
        for (size_t i = 0; i < sizeof(commonStyles) / sizeof(commonStyles[0]); ++i)
            [self sendEditorMessage:SCI_STYLESETFORE wParam:commonStyles[i]
                lParam:axyne_editor_color(commonColors[i])];
        if (strcmp(language, "cmake") == 0) {
            const unsigned int cmakeStyles[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                12, 13, 14};
            const uint32_t cmakeColors[] = {0x7a828e, 0xd98e73, 0xd9b36c, 0xd9b36c,
                0xa3c98a, 0x738ed9, 0xe3b57d, 0xc79ad9, 0xd98e73, 0xd98e73,
                0xd98e73, 0xd98e73, 0xd9b36c, 0xd9b36c};
            for (size_t i = 0; i < sizeof(cmakeStyles) / sizeof(cmakeStyles[0]); ++i)
                [self sendEditorMessage:SCI_STYLESETFORE wParam:cmakeStyles[i]
                    lParam:axyne_editor_color(cmakeColors[i])];
        }
        if (strcmp(language, "yaml") == 0) {
            const unsigned int yamlStyles[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
            const uint32_t yamlColors[] = {0x7a828e, 0x7a828e, 0xd9b36c, 0xd9b36c,
                0xa3c98a, 0x738ed9, 0xd98e73, 0xc79ad9, 0xd9b36c, 0xd5d8dd,
                0x8cc7c0};
            for (size_t i = 0; i < sizeof(yamlStyles) / sizeof(yamlStyles[0]); ++i)
                [self sendEditorMessage:SCI_STYLESETFORE wParam:yamlStyles[i]
                    lParam:axyne_editor_color(yamlColors[i])];
        }
        if (strcmp(language, "markdown") == 0) {
            const unsigned int markdownStyles[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
            const uint32_t markdownColors[] = {0x7a828e, 0xd9b36c, 0xd9b36c,
                0xd9b36c, 0xc79ad9, 0xc79ad9, 0xa66bf0, 0xa66bf0, 0xa66bf0,
                0xa66bf0, 0xa66bf0, 0x8cc7c0, 0xd98e73, 0xd98e73, 0x738ed9,
                0xd98e73, 0x7a828e, 0x7a828e, 0x7db5e3, 0xa3c98a, 0xa3c98a};
            for (size_t i = 0; i < sizeof(markdownStyles) / sizeof(markdownStyles[0]); ++i)
                [self sendEditorMessage:SCI_STYLESETFORE wParam:markdownStyles[i]
                    lParam:axyne_editor_color(markdownColors[i])];
            for (unsigned int style = 6; style <= 11; ++style)
                [self sendEditorMessage:SCI_STYLESETBOLD wParam:style lParam:1];
        }
        (void)[self sendEditorMessage:SCI_COLOURISE wParam:0 lParam:-1];
    }
}

- (void)updateLineNumberMargin
{
    NSInteger lineCount;
    NSInteger width;
    char digits[32];
    if (_editorView == nil) return;
    lineCount = [self sendEditorMessage:SCI_GETLINECOUNT wParam:0 lParam:0];
    if (lineCount < 1) lineCount = 1;
    (void)snprintf(digits, sizeof(digits), "%lld", (long long)lineCount);
    width = [self sendEditorMessage:SCI_TEXTWIDTH wParam:33
                              lParam:(intptr_t)digits];
    if (width < 1) width = 32;
    (void)[self sendEditorMessage:SCI_SETMARGINWIDTHN wParam:0
                             lParam:width + 10];
}

- (void)updateBraceHighlight
{
    NSInteger caret;
    NSInteger brace = -1;
    NSInteger match;
    if (_editorView == nil) return;
    caret = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    if (caret >= 0) {
        int character = (int)[self sendEditorMessage:SCI_GETCHARAT
            wParam:(uintptr_t)caret lParam:0];
        if (character == '{' || character == '}' || character == '(' ||
            character == ')' || character == '[' || character == ']')
            brace = caret;
    }
    if (brace < 0 && caret > 0) {
        int character = (int)[self sendEditorMessage:SCI_GETCHARAT
            wParam:(uintptr_t)(caret - 1) lParam:0];
        if (character == '{' || character == '}' || character == '(' ||
            character == ')' || character == '[' || character == ']')
            brace = caret - 1;
    }
    if (brace < 0) {
        (void)[self sendEditorMessage:SCI_BRACEHIGHLIGHT wParam:(uintptr_t)-1
                                 lParam:-1];
        return;
    }
    match = [self sendEditorMessage:SCI_BRACEMATCH wParam:(uintptr_t)brace
                              lParam:0];
    if (match >= 0)
        (void)[self sendEditorMessage:SCI_BRACEHIGHLIGHT
                                 wParam:(uintptr_t)brace lParam:match];
    else
        (void)[self sendEditorMessage:SCI_BRACEBADLIGHT
                                 wParam:(uintptr_t)brace lParam:0];
}

- (void)autoIndentFromNotification:(SCNotification *)notification
{
    NSInteger line;
    NSInteger previousLine;
    NSInteger indentation;
    NSInteger lineStart;
    NSInteger previousStart;
    NSInteger position;
    unsigned int tabWidth = _preferences.editor.tab_width;
    int lastCharacter = 0;
    if (_editorView == nil || notification == NULL) return;
    if (tabWidth == 0) tabWidth = 4;
    if (notification->ch == '\n') {
        line = [self sendEditorMessage:SCI_LINEFROMPOSITION
                                 wParam:(uintptr_t)(notification->position + 1)
                                 lParam:0];
        if (line <= 0) return;
        previousLine = line - 1;
        indentation = [self sendEditorMessage:SCI_GETLINEINDENTATION
            wParam:(uintptr_t)previousLine lParam:0];
        lineStart = [self sendEditorMessage:SCI_POSITIONFROMLINE
            wParam:(uintptr_t)line lParam:0];
        previousStart = [self sendEditorMessage:SCI_POSITIONFROMLINE
            wParam:(uintptr_t)previousLine lParam:0];
        position = lineStart - 1;
        while (position >= previousStart) {
            int character = (int)[self sendEditorMessage:SCI_GETCHARAT
                wParam:(uintptr_t)position lParam:0];
            if (character != ' ' && character != '\t' && character != '\r' &&
                character != '\n') {
                lastCharacter = character;
                break;
            }
            --position;
        }
        if (lastCharacter == '{') indentation += (NSInteger)tabWidth;
        (void)[self sendEditorMessage:SCI_SETLINEINDENTATION
            wParam:(uintptr_t)line lParam:indentation];
    } else if (notification->ch == '}') {
        line = [self sendEditorMessage:SCI_LINEFROMPOSITION
            wParam:(uintptr_t)notification->position lParam:0];
        indentation = [self sendEditorMessage:SCI_GETLINEINDENTATION
            wParam:(uintptr_t)line lParam:0];
        if (indentation >= (NSInteger)tabWidth)
            (void)[self sendEditorMessage:SCI_SETLINEINDENTATION
                wParam:(uintptr_t)line lParam:indentation - tabWidth];
    }
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
    NSFont *terminalFont = [NSFont fontWithName:fontName size:fontSize];
    if (terminalFont == nil)
        terminalFont = [NSFont userFixedPitchFontOfSize:fontSize];
    [_terminalOutput setFont:terminalFont];
    [_terminalOutput setTextColor:axyne_preference_color(_preferences.theme.text)];
    [_terminalOutput setBackgroundColor:axyne_preference_color(
        axyne_macos_output_background(&_preferences.theme))];
    [_terminalScroll setBackgroundColor:[_terminalOutput backgroundColor]];
    [_problemSummary setTextColor:axyne_preference_color(_preferences.theme.text)];
    [_terminalInput setTextColor:axyne_preference_color(_preferences.theme.text)];
    [_terminalInput setBackgroundColor:axyne_preference_color(_preferences.theme.panel)];
    [_terminalInput setDrawsBackground:NO];
    [_terminalInput setBezeled:NO];
    [_terminalInput setFocusRingType:NSFocusRingTypeNone];
    [_terminalInput setFont:[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular]];
    for (NSButton *button in @[_terminalStart, _terminalStop, _terminalSend,
                               _debugStart, _debugPause, _debugContinue,
                               _debugNext, _debugBreakpoint]) {
        [button setBezelStyle:NSBezelStyleTexturedRounded];
        [button setContentTintColor:axyne_preference_color(_preferences.theme.text)];
    }
    for (NSButton *button in @[_newButton, _openButton, _saveButton,
                               _undoButton, _redoButton, _buildButton,
                               _runButton, _targetButton, _searchButton,
                               _outputTab, _problemsTab, _terminalTab,
                               _terminalStart, _terminalStop, _terminalSend, _clearOutput]) {
        [(AxyneChromeButton *)button setLabelColor:axyne_preference_color(_preferences.theme.muted)];
        [(AxyneChromeButton *)button setFillColor:nil];
        [button setFont:[NSFont systemFontOfSize:11]];
        [button setNeedsDisplay:YES];
    }
    for (NSButton *button in @[_newButton, _openButton, _saveButton, _undoButton, _redoButton])
        [button setFont:[NSFont systemFontOfSize:14]];
    NSColor *control = axyne_preference_color(axyne_macos_reference_surfaces(&_preferences.theme)
        ? 0x24262b : _preferences.theme.toolbar);
    [(AxyneChromeButton *)_targetButton setFillColor:control];
    [(AxyneChromeButton *)_buildButton setFillColor:control];
    [(AxyneChromeButton *)_buildButton setLabelColor:axyne_preference_color(_preferences.theme.text)];
    [(AxyneChromeButton *)_runButton setFillColor:axyne_preference_color(_preferences.theme.accent)];
    [(AxyneChromeButton *)_runButton setLabelColor:axyne_preference_color(_preferences.theme.background)];
    [(AxyneChromeButton *)_searchButton setFillColor:axyne_preference_color(_preferences.theme.background)];
    if (_editorView != nil) {
        [self sendEditorMessage:SCI_STYLESETFORE wParam:32
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:32
                              lParam:axyne_editor_color(_preferences.theme.editor_background)];
        /* STYLECLEARALL copies the complete default style, including its font. */
        [self sendEditorMessage:SCI_STYLESETSIZE wParam:32 lParam:(intptr_t)fontSize];
        [self sendEditorMessage:SCI_STYLESETFONT wParam:32 lParam:(intptr_t)fontUTF8];
        [self sendEditorMessage:SCI_STYLECLEARALL wParam:0 lParam:0];
        [self sendEditorMessage:SCI_STYLESETFORE wParam:33
            lParam:axyne_editor_color(axyne_macos_reference_surfaces(&_preferences.theme)
                ? 0x5a606a : _preferences.theme.muted)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:33
                              lParam:axyne_editor_color(_preferences.theme.editor_background)];
        [self sendEditorMessage:SCI_STYLESETFORE wParam:STYLE_BRACELIGHT
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:STYLE_BRACELIGHT
                              lParam:axyne_editor_color(_preferences.theme.accent)];
        [self sendEditorMessage:SCI_STYLESETFORE wParam:STYLE_BRACEBAD
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:STYLE_BRACEBAD
                              lParam:axyne_editor_color(_preferences.theme.accent)];
        [self sendEditorMessage:SCI_SETSELFORE wParam:0
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_SETSELBACK wParam:1
                              lParam:axyne_editor_color(_preferences.theme.accent)];
        [self sendEditorMessage:SCI_SETCARETFORE
            wParam:axyne_editor_color(_preferences.theme.accent) lParam:0];
        [self sendEditorMessage:SCI_SETCARETLINEVISIBLE wParam:1 lParam:0];
        [self sendEditorMessage:SCI_SETCARETLINEBACK
            wParam:axyne_editor_color(axyne_macos_reference_surfaces(&_preferences.theme)
                ? 0x202328 : _preferences.theme.toolbar) lParam:0];
        [self sendEditorMessage:SCI_SETINDENT wParam:_preferences.editor.tab_width lParam:0];
        [self sendEditorMessage:SCI_SETTABWIDTH wParam:_preferences.editor.tab_width lParam:0];
        [self sendEditorMessage:SCI_SETUSETABS wParam:_preferences.editor.insert_spaces ? 0 : 1 lParam:0];
        [self sendEditorMessage:SCI_SETWRAPMODE wParam:_preferences.editor.word_wrap ? 1 : 0 lParam:0];
        [self sendEditorMessage:SCI_SETVIEWWS wParam:_preferences.editor.show_whitespace ? 1 : 0 lParam:0];
        [self updateLineNumberMargin];
        [self updateBraceHighlight];
        [self applyEditorLexer];
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
    BOOL changed = doc->length != (size_t)length ||
        memcmp(doc->contents, text, (size_t)length) != 0;
    BOOL modified = [self sendEditorMessage:SCI_GETMODIFY wParam:0 lParam:0] != 0;
    AxyneStatus status = changed ? axyne_documents_set_contents(&_documents,
        _documents.active_index, text, (size_t)length, NULL) : AXYNE_STATUS_OK;
    free(text);
    if (status == AXYNE_STATUS_OK) {
        doc->is_dirty = modified;
        if (changed) [self syncLspActive];
    }
    return status == AXYNE_STATUS_OK;
}

- (void)setLspStatus:(NSString *)status
{
    [_lspStatus release];
    _lspStatus = [status copy];
    [_problemSummary setStringValue:_lspStatus != nil ? _lspStatus : @"LSP 진단 없음"];
    [self setNeedsDisplay:YES];
}

- (BOOL)ensureLsp
{
    const char *command = getenv("AXYNE_LSP_COMMAND");
    AxyneLspConfig config = {0};
    AxyneError error;
    if (_lsp != NULL) return YES;
    if (command == NULL || command[0] == '\0') {
        [self setLspStatus:@"LSP: set AXYNE_LSP_COMMAND to a local server executable"];
        return NO;
    }
    config.command = command;
    config.language_id = "plaintext";
    config.on_diagnostics = axyne_macos_lsp_diagnostics;
    config.on_navigation = axyne_macos_lsp_navigation;
    config.on_error = axyne_macos_lsp_error;
    config.user_data = self;
    if (axyne_lsp_create(&config, &_lsp, &error) != AXYNE_STATUS_OK) {
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
        return NO;
    }
    return YES;
}

- (BOOL)openLspForActive
{
    AxyneDocument *doc = [self activeDocument];
    AxyneError error;
    AxyneStatus status;
    if (doc == NULL || doc->is_untitled || doc->path == NULL || ![self ensureLsp]) return NO;
    status = axyne_lsp_did_open(_lsp, doc, &error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_BUSY) {
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
        return NO;
    }
    return YES;
}

- (void)syncLspActive
{
    AxyneDocument *doc = [self activeDocument];
    AxyneError error;
    AxyneStatus status;
    if (_lsp == NULL || doc == NULL || doc->is_untitled || doc->path == NULL) return;
    status = axyne_lsp_did_change(_lsp, doc, &error);
    if (status == AXYNE_STATUS_NOT_FOUND) status = axyne_lsp_did_open(_lsp, doc, &error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_BUSY)
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
}

- (void)navigateLspReferences:(id)sender
{
    AxyneDocument *doc;
    AxyneLspPosition position;
    AxyneError error;
    AxyneStatus status;
    uint64_t requestID = 0;
    NSInteger current, line, lineStart;
    BOOL references = [sender tag] != 0;
    if (![self captureEditor] || ![self openLspForActive]) return;
    doc = [self activeDocument];
    current = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    line = [self sendEditorMessage:SCI_LINEFROMPOSITION wParam:(uintptr_t)current lParam:0];
    lineStart = [self sendEditorMessage:SCI_POSITIONFROMLINE wParam:(uintptr_t)line lParam:0];
    position.line = (size_t)line;
    position.character = axyne_lsp_utf16_character(
        doc->contents + (size_t)lineStart, (size_t)(current - lineStart),
        (size_t)(current - lineStart));
    status = references ? axyne_lsp_references(_lsp, doc, position, &requestID, &error) :
        axyne_lsp_definition(_lsp, doc, position, &requestID, &error);
    if (status != AXYNE_STATUS_OK)
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
    else
        [self setLspStatus:[NSString stringWithFormat:@"LSP: request %llu sent",
                            (unsigned long long)requestID]];
}

- (BOOL)captureEditor
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || doc->native_editor_document == NULL) return YES;
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
    _loadingEditor = YES;
    BOOL loaded = axyne_editor_load_document(doc, axyne_macos_editor_message, self);
    _loadingEditor = NO;
    if (!loaded) return NO;
    BOOL imageDocument = axyne_macos_is_image_path(doc->path);
    if (imageDocument) {
        NSImage *image = [[[NSImage alloc] initWithContentsOfFile:
            [NSString stringWithUTF8String:doc->path]] autorelease];
        [_imagePreview setImage:image];
        [_imagePreview setHidden:image == nil];
        [_editorView setHidden:image != nil];
    } else {
        [_imagePreview setImage:nil];
        [_imagePreview setHidden:YES];
    }
    [self applyPreferences];
    [self applyEditorLexer];
    [self updateLineNumberMargin];
    [self updateBraceHighlight];
    [self setNeedsDisplay:YES];
    [self setNeedsLayout:YES];
    [self updateWindowTitle];
    [self refreshActionControls];
    if ([self window] != nil)
        [[self window] makeFirstResponder:[(id)_editorView content]];
    return YES;
}

- (BOOL)selectDocumentAtIndex:(size_t)index
{
    size_t previousIndex = _documents.active_index;
    if (axyne_documents_set_active(&_documents, index, NULL) != AXYNE_STATUS_OK)
        return NO;
    if ([self loadActiveDocument]) return YES;
    (void)axyne_documents_set_active(&_documents, previousIndex, NULL);
    return NO;
}

- (void)updateWindowTitle
{
    AxyneDocument *doc = [self activeDocument];
    NSString *name = doc != NULL && doc->title != NULL
        ? [NSString stringWithUTF8String:doc->title] : @"Untitled";
    if (name == nil) name = @"Untitled";
    [[self window] setTitle:[NSString stringWithFormat:@"%@%@ - Axyne",
        doc != NULL && doc->is_dirty ? @"● " : @"", name]];
}

- (void)notification:(SCNotification *)notification
{
    AxyneDocument *doc = [self activeDocument];
    if (_loadingEditor || notification == NULL || doc == NULL) return;
    if (notification->nmhdr.code == SCN_CHARADDED)
        [self autoIndentFromNotification:notification];
    if (notification->nmhdr.code == SCN_UPDATEUI) {
        [self updateLineNumberMargin];
        [self updateBraceHighlight];
        [self setNeedsDisplay:YES];
    }
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
    [self refreshActionControls];
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
        NSString *detail = [NSString stringWithUTF8String:error.message];
        [alert setMessageText:@"Could not save file"];
        [alert setInformativeText:detail != nil ? detail : @""];
        [alert runModal];
        return NO;
    }
    (void)[self sendEditorMessage:SCI_SETSAVEPOINT wParam:0 lParam:0];
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    [self refreshRecentMenu];
    [self refreshActionControls];
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
        NSString *detail = [NSString stringWithUTF8String:error.message];
        [alert setMessageText:@"Could not save file"];
        [alert setInformativeText:detail != nil ? detail : @""];
        [alert runModal];
        return NO;
    }
    (void)[self sendEditorMessage:SCI_SETSAVEPOINT wParam:0 lParam:0];
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    [self refreshRecentMenu];
    [self refreshActionControls];
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

- (void)undo:(id)sender
{
    (void)sender;
    (void)[self sendEditorMessage:SCI_UNDO wParam:0 lParam:0];
}

- (void)redo:(id)sender
{
    (void)sender;
    (void)[self sendEditorMessage:SCI_REDO wParam:0 lParam:0];
}

- (void)openPath:(NSString *)path
{
    if (path == nil) return;
    [self loadScintillaView];
    if (![self captureEditor]) return;
    size_t previousCount = _documents.count;
    size_t previousIndex = _documents.active_index;
    size_t index = 0;
    AxyneError error;
    AxyneStatus status = axyne_documents_open(&_documents,
        [path UTF8String], &index, &error);
    if (status != AXYNE_STATUS_OK) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        NSString *detail = [NSString stringWithUTF8String:error.message];
        [alert setMessageText:@"Could not open file"];
        [alert setInformativeText:detail != nil ? detail : @""];
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
    [self updateWindowTitle];
    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];
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
    NSString *detail = error != NULL
        ? [NSString stringWithUTF8String:error->message] : nil;
    [alert setMessageText:prefix != nil ? prefix : @"Workspace operation failed"];
    [alert setInformativeText:detail != nil ? detail : @""];
    [alert runModal];
}

- (void)showWorkspaceMessage:(NSString *)message
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:message != nil ? message : @"Workspace operation failed"];
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
    _explorerFirstRow = 0;
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
    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];
    return YES;
}

- (NSInteger)explorerNodeAtPoint:(NSPoint)point
{
    const CGFloat explorerTop = AXYNE_CONTENT_TOP + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER;
    const CGFloat bottom = NSHeight([self bounds]) - AXYNE_STATUS - AXYNE_BOTTOM;
    NSInteger row;
    if (_explorer.root == NULL || point.x < 0 || point.x >= AXYNE_SIDEBAR ||
        point.y < explorerTop || point.y >= bottom) return NSNotFound;
    row = (NSInteger)((point.y - explorerTop) / AXYNE_UI_ROW) + _explorerFirstRow;
    if (explorerTop + (row - _explorerFirstRow + 1) * AXYNE_UI_ROW > bottom)
        return NSNotFound;
    return row >= 0 && (size_t)row < _explorer.count ? row : NSNotFound;
}

- (void)scrollWheel:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (point.x >= AXYNE_SIDEBAR && point.x < NSWidth([self bounds]) &&
        point.y >= AXYNE_CONTENT_TOP && point.y < AXYNE_CONTENT_TOP + AXYNE_TABS) {
        CGFloat delta = [event scrollingDeltaX] != 0 ? [event scrollingDeltaX] :
            [event scrollingDeltaY];
        [self scrollTabsBy:-delta * ([event hasPreciseScrollingDeltas] ? 1 : 40)];
        return;
    }
    CGFloat top = AXYNE_CONTENT_TOP + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER;
    CGFloat bottom = NSHeight([self bounds]) - AXYNE_STATUS - AXYNE_BOTTOM;
    if (point.x < AXYNE_SIDEBAR && point.y >= top && point.y < bottom) {
        NSInteger visible = MAX(1, (NSInteger)((bottom - top) / AXYNE_UI_ROW));
        NSInteger maximum = MAX(0, (NSInteger)_explorer.count - visible);
        NSInteger delta = (NSInteger)ceil(fabs([event scrollingDeltaY]) /
            ([event hasPreciseScrollingDeltas] ? AXYNE_UI_ROW : 1));
        if ([event scrollingDeltaY] > 0) delta = -delta;
        _explorerFirstRow = MIN(maximum, MAX(0, _explorerFirstRow + delta));
        [self setNeedsDisplay:YES];
        return;
    }
    [super scrollWheel:event];
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
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:
            path != nil ? path : @"(Invalid path)"
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
    NSString *name = [NSString stringWithUTF8String:
        doc->title != NULL ? doc->title : "Untitled"];
    if (name == nil) name = @"Untitled";
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:[NSString stringWithFormat:@"Save changes to %@?", name]];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Discard"];
    [alert addButtonWithTitle:@"Cancel"];
    NSInteger result = [alert runModal];
    if (result == NSAlertFirstButtonReturn) {
        if (![self captureEditor]) return NO;
        if (![self selectDocumentAtIndex:index]) return NO;
        return [self saveActive];
    }
    return result == NSAlertSecondButtonReturn;
}

- (void)closeDocument:(id)sender
{
    (void)sender;
    [self closeDocumentAtIndex:_documents.active_index];
}

- (void)closeDocumentAtIndex:(size_t)index
{
    if (index >= _documents.count) return;
    if (![self captureEditor]) return;
    if (![self confirmCloseDocumentAtIndex:index]) return;
    /* Prepare the replacement before dropping the last tab. Allocation or
     * native initialization failure must leave the current document open. */
    if (_documents.count == 1) {
        size_t replacement;
        if (axyne_documents_new(&_documents, &replacement, NULL) != AXYNE_STATUS_OK)
            return;
        if (![self loadActiveDocument]) {
            (void)axyne_documents_close(&_documents, replacement, NULL);
            (void)axyne_documents_set_active(&_documents, index, NULL);
            return;
        }
    } else if (_documents.active_index == index) {
        size_t successor = index + 1 < _documents.count ? index + 1 : index - 1;
        if (![self selectDocumentAtIndex:successor]) return;
    }
    AxyneDocument *doc = &_documents.documents[index];
    if (_lsp != NULL) (void)axyne_lsp_did_close(_lsp, doc, NULL);
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
    NSString *frameworksPath = [[NSBundle mainBundle] privateFrameworksPath];
    NSString *frameworkPath = [frameworksPath
        stringByAppendingPathComponent:@"Scintilla.framework"];
    NSBundle *bundle = [NSBundle bundleWithPath:frameworkPath];
    if (bundle == nil || ![bundle load]) return;
    _scintillaBundle = [bundle retain];

    Class scintillaClass = NSClassFromString(@"ScintillaView");
    if (scintillaClass != Nil) {
        _editorView = [[scintillaClass alloc] initWithFrame:NSZeroRect];
        [(id)_editorView setDelegate:self];
        [_editorView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [_editorView setAccessibilityElement:YES];
        [_editorView setAccessibilityRole:NSAccessibilityTextAreaRole];
        [_editorView setAccessibilityLabel:@"Source editor"];
        [_editorView setAccessibilityRoleDescription:@"source editor"];
        [_editorView setFocusRingType:NSFocusRingTypeNone];
        (void)[self sendEditorMessage:SCI_SETMARGINTYPEN wParam:0
                                 lParam:SC_MARGIN_NUMBER];
        (void)[self sendEditorMessage:SCI_SETMARGINMASKN wParam:0 lParam:0];
        (void)[self sendEditorMessage:SCI_SETMARGINSENSITIVEN wParam:0 lParam:0];
        (void)[self sendEditorMessage:SCI_STYLECLEARALL wParam:0 lParam:0];
        (void)[self sendEditorMessage:SCI_SETINDENTATIONGUIDES
                                 wParam:SC_IV_LOOKBOTH lParam:0];
        (void)[self sendEditorMessage:SCI_SETBACKSPACEUNINDENTS wParam:1 lParam:0];
        (void)[self sendEditorMessage:SCI_SETTABINDENTS wParam:1 lParam:0];
        axyne_macos_style_editor_scrollbars(_editorView);
        [self addSubview:_editorView];
        _imagePreview = [[NSImageView alloc] initWithFrame:NSZeroRect];
        [_imagePreview setImageScaling:NSImageScaleProportionallyUpOrDown];
        [_imagePreview setImageAlignment:NSImageAlignCenter];
        [_imagePreview setHidden:YES];
        [_imagePreview setImageFrameStyle:NSImageFrameNone];
        [self addSubview:_imagePreview positioned:NSWindowAbove relativeTo:_editorView];
        [self setNeedsLayout:YES];

        NSString *lexillaPath = [frameworksPath
            stringByAppendingPathComponent:@"Lexilla.dylib"];
        if (lexillaPath != nil) {
            _lexillaModule = dlopen([lexillaPath fileSystemRepresentation],
                                    RTLD_NOW | RTLD_LOCAL);
            if (_lexillaModule != NULL)
                _createLexer = (void *(*)(const char *))dlsym(
                    _lexillaModule, "CreateLexer");
            if (_createLexer == NULL && _lexillaModule != NULL) {
                dlclose(_lexillaModule);
                _lexillaModule = NULL;
            }
        }
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
    if (point.y < AXYNE_MENU) {
        NSArray *labels = @[@"파일(F)", @"편집(E)", @"보기(V)", @"빌드(B)",
            @"디버그(D)", @"도구(T)", @"도움말(H)"];
        CGFloat x = 8;
        for (NSUInteger i = 0; i < [labels count]; ++i) {
            NSString *label = labels[i];
            CGFloat itemWidth = [label sizeWithAttributes:@{
                NSFontAttributeName:[NSFont systemFontOfSize:14]}].width + 20;
            if (point.x >= x && point.x < x + itemWidth) {
                _activeMenuIndex = (NSInteger)i;
                [self setNeedsDisplay:YES];
                NSMenu *mainMenu = [[NSApplication sharedApplication] mainMenu];
                NSMenuItem *item = [mainMenu itemAtIndex:i + 1];
                if ([item submenu] != nil)
                    [[item submenu] popUpMenuPositioningItem:nil
                        atLocation:NSMakePoint(x, AXYNE_MENU) inView:self];
                _activeMenuIndex = -1;
                [self setNeedsDisplay:YES];
                return;
            }
            x += itemWidth + 2;
        }
        return;
    }
    if (point.y >= AXYNE_CONTENT_TOP && point.y < AXYNE_CONTENT_TOP + AXYNE_TABS &&
        point.x >= AXYNE_SIDEBAR) {
        for (size_t index = 0; index < _documents.count; ++index) {
            AxyneDocument *document = &_documents.documents[index];
            if (document->is_untitled && document->length == 0 &&
                !document->is_dirty)
                continue;
            NSRect tab = [self displayTabFrameAtIndex:index];
            if (!NSPointInRect(point, tab)) continue;
            if (![self captureEditor]) return;
            if (point.x >= NSMaxX(tab) - 24) {
                [self closeDocumentAtIndex:index];
            } else {
                [self selectDocumentAtIndex:index];
            }
            return;
        }
    }
    if (point.x < AXYNE_SIDEBAR && point.y >= AXYNE_CONTENT_TOP + AXYNE_TABS &&
        point.y < NSHeight([self bounds]) - AXYNE_STATUS - AXYNE_BOTTOM) {
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
    if (point.x >= AXYNE_SIDEBAR || point.y < AXYNE_CONTENT_TOP + AXYNE_TABS ||
        point.y >= NSHeight([self bounds]) - AXYNE_STATUS - AXYNE_BOTTOM) {
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
    const char *fontFamilyUTF8 = [fontFamily UTF8String];
    (void)snprintf(next.editor.font_family, sizeof(next.editor.font_family), "%s",
                   fontFamilyUTF8 != NULL ? fontFamilyUTF8 : "");
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

static int axyne_macos_git_append(AxyneMacGitRun *run,
                                  const char *bytes, size_t length)
{
    size_t required;
    char *grown;
    size_t capacity;
    if (run == NULL || bytes == NULL || length == 0) return 1;
    if (length > SIZE_MAX - run->length - 1) return 0;
    required = run->length + length + 1;
    if (required > AXYNE_GIT_OUTPUT_LIMIT + 1) return 0;
    if (required > run->capacity) {
        capacity = run->capacity == 0 ? 4096 : run->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        grown = (char *)realloc(run->output, capacity);
        if (grown == NULL) return 0;
        run->output = grown;
        run->capacity = capacity;
    }
    memcpy(run->output + run->length, bytes, length);
    run->length += length;
    run->output[run->length] = '\0';
    return 1;
}

static void axyne_macos_git_output(AxyneProcess *process,
                                   AxyneProcessStream stream,
                                   const char *bytes, size_t length,
                                   void *user_data)
{
    AxyneMacGitRun *run = (AxyneMacGitRun *)user_data;
    (void)stream;
    if (run != NULL && bytes != NULL && !run->allocation_failed &&
        !run->output_truncated) {
        if (run->length >= AXYNE_GIT_OUTPUT_LIMIT ||
            length > AXYNE_GIT_OUTPUT_LIMIT - run->length) {
            run->output_truncated = 1;
            (void)axyne_process_terminate(process, NULL);
        } else if (!axyne_macos_git_append(run, bytes, length)) {
            run->allocation_failed = 1;
            (void)axyne_process_terminate(process, NULL);
        }
    }
}

static void axyne_macos_git_free(AxyneMacGitRun *run)
{
    if (run == NULL) return;
    free(run->output);
    (void)pthread_mutex_destroy(&run->lock);
    free(run);
}

static void axyne_macos_git_release(AxyneMacGitRun *run)
{
    int free_run = 0;
    if (run == NULL) return;
    (void)pthread_mutex_lock(&run->lock);
    if (--run->references == 0) free_run = 1;
    (void)pthread_mutex_unlock(&run->lock);
    if (free_run) axyne_macos_git_free(run);
}

static void axyne_macos_git_cleanup(AxyneMacGitRun *run)
{
    if (run == NULL) return;
    axyne_process_release(run->process);
    axyne_macos_git_release(run);
}

static void axyne_macos_git_exit(AxyneProcess *process, int exit_code,
                                 void *user_data)
{
    AxyneMacGitRun *run = (AxyneMacGitRun *)user_data;
    AxyneMacGitCompletion *completion;
    AxyneWorkspaceView *view = nil;
    size_t output_size;
    (void)process;
    if (run == NULL) return;

    (void)pthread_mutex_lock(&run->lock);
    if (!run->cancelled && run->view != nil) {
        view = run->view;
        ++run->references;
        [view retain];
    }
    (void)pthread_mutex_unlock(&run->lock);
    if (view == nil) return;

    completion = (AxyneMacGitCompletion *)calloc(1, sizeof(*completion));
    if (completion == NULL) {
        [view release];
        axyne_macos_git_release(run);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            axyne_macos_git_cleanup(run);
        });
        return;
    }
    completion->view = view;
    completion->process = run->process;
    completion->empty_message = run->empty_message;
    completion->allocation_failed = run->allocation_failed;
    completion->output_truncated = run->output_truncated;
    completion->exit_code = exit_code;
    if (exit_code != 0) {
        (void)snprintf(completion->failure_message,
                       sizeof(completion->failure_message),
                       "Git command failed with exit code %d", exit_code);
    }
    if (!completion->allocation_failed && run->output != NULL && run->length != 0) {
        output_size = run->length + 1;
        completion->output = (char *)malloc(output_size);
        if (completion->output == NULL) {
            completion->allocation_failed = 1;
        } else {
            memcpy(completion->output, run->output, output_size);
            completion->length = run->length;
        }
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        [completion->view completeGitOperation:completion run:run];
    });
}

- (void)startGitOperationWithEmptyMessage:(const char *)empty_message
                                arguments:(const char *const *)arguments
                                   count:(size_t)argument_count
{
    static const char *const utf8_environment[] = {
        "LANG=C.UTF-8", "LC_ALL=C.UTF-8"
    };
    AxyneMacGitRun *run;
    AxyneProcessSpec spec;
    AxyneError error;
    AxyneStatus status;
    if (_explorer.root == NULL) {
        [self showWorkspaceMessage:@"Open a workspace folder before using Git."];
        return;
    }
    if (_gitProcess != NULL) {
        [self showWorkspaceMessage:@"A Git operation is already running."];
        return;
    }
    run = (AxyneMacGitRun *)calloc(1, sizeof(*run));
    if (run == NULL || pthread_mutex_init(&run->lock, NULL) != 0) {
        free(run);
        [self showWorkspaceMessage:@"Unable to allocate Git operation."];
        return;
    }
    run->references = 1;
    run->view = self;
    run->empty_message = empty_message;
    memset(&spec, 0, sizeof(spec));
    spec.executable = "git";
    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = _explorer.root;
    spec.environment = utf8_environment;
    spec.environment_count = 2;
    spec.on_output = axyne_macos_git_output;
    spec.on_exit = axyne_macos_git_exit;
    spec.user_data = run;
    status = axyne_process_start(&spec, &run->process, &error);
    if (status != AXYNE_STATUS_OK) {
        (void)pthread_mutex_destroy(&run->lock);
        free(run);
        NSString *message = [NSString stringWithUTF8String:error.message];
        [self showWorkspaceMessage:message != nil
            ? message : @"Unable to start Git operation."];
        return;
    }
    _gitProcess = run->process;
    _gitRun = run;
}

- (void)completeGitOperation:(AxyneMacGitCompletion *)completion
                         run:(AxyneMacGitRun *)run
{
    const char *text;
    if (completion == NULL || run == NULL) return;
    if (completion->allocation_failed) {
        text = "Unable to allocate Git output.";
    } else if (completion->output_truncated) {
        text = "Git output exceeded the 16 MiB limit.";
    } else if (completion->length != 0) {
        text = completion->output;
    } else if (completion->exit_code == 0) {
        text = completion->empty_message;
    } else {
        text = completion->failure_message;
    }
    [_terminalOutput setString:@""];
    if (text != NULL)
        [self terminalAppend:text length:strlen(text) stream:AXYNE_PROCESS_STDOUT];
    if (_gitRun == run) {
        _gitRun = NULL;
        _gitProcess = NULL;
    }
    axyne_process_release(completion->process);
    axyne_macos_git_release(run);
    axyne_macos_git_release(run);
    free(completion->output);
    free(completion);
    [self setNeedsDisplay:YES];
    [self release];
}

- (void)showGitStatus:(id)sender
{
    (void)sender;
    static const char *const arguments[] = {
        "--no-pager", "status", "--short", "--branch"
    };
    [self startGitOperationWithEmptyMessage:"No Git status output."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
}

- (void)showGitDiff:(id)sender
{
    (void)sender;
    static const char *const arguments[] = {
        "--no-pager", "diff", "--no-color"
    };
    [self startGitOperationWithEmptyMessage:"No Git differences."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
}

- (void)stageAllGitChanges:(id)sender
{
    (void)sender;
    static const char *const arguments[] = { "add", "--all" };
    [self startGitOperationWithEmptyMessage:"All workspace changes staged."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
}

- (void)unstageAllGitChanges:(id)sender
{
    (void)sender;
    static const char *const arguments[] = { "reset", "--mixed" };
    [self startGitOperationWithEmptyMessage:"All changes unstaged."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
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
    NSString *searchRoot = nil;
    if (quickFile && _explorer.root != NULL) {
        searchRoot = [NSString stringWithUTF8String:_explorer.root];
    } else {
        NSOpenPanel *folder = [NSOpenPanel openPanel];
        [folder setCanChooseDirectories:YES]; [folder setCanChooseFiles:NO];
        [folder setAllowsMultipleSelection:NO];
        if ([folder runModal] != NSModalResponseOK) return;
        searchRoot = [[folder URL] path];
    }
    if ([searchRoot length] == 0) {
        [self showWorkspaceMessage:@"폴더를 먼저 열어주세요."];
        return;
    }
    NSString *query = [self askForText:quickFile ? @"Quick File" : @"Search Folder"
                                  label:quickFile ? @"Filename contains" : @"Search text"];
    if ([query length] == 0) return;
    NSPopUpButton *choices = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 480, 28)
                                                        pullsDown:NO] autorelease];
    size_t selectedLine = 0;
    if (quickFile) {
        char **paths = NULL; size_t count = 0;
        if (axyne_search_files([searchRoot UTF8String], [query UTF8String],
                               &paths, &count, NULL) == AXYNE_STATUS_OK) {
            for (size_t i = 0; i < count; ++i) {
                NSString *path = [NSString stringWithUTF8String:paths[i]];
                [choices addItemWithTitle:path != nil ? path : @"(invalid path)"];
            }
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
        if (axyne_search_workspace([searchRoot UTF8String], [query UTF8String],
                                   0, &results, NULL) == AXYNE_STATUS_OK) {
            for (size_t i = 0; i < results.count; ++i) {
                NSString *path = [NSString stringWithUTF8String:results.items[i].path];
                NSString *preview = [NSString stringWithUTF8String:results.items[i].preview];
                if (path == nil) path = @"";
                if (preview == nil) preview = @"";
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
            [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName:axyne_preference_color(
                stream == AXYNE_PROCESS_STDERR ? 0xe5a445 : _preferences.theme.text) }]
        autorelease]];
    if ([storage length] > 1024 * 1024)
        [storage deleteCharactersInRange:NSMakeRange(0, [storage length] - 1024 * 1024)];
    [_terminalOutput scrollRangeToVisible:NSMakeRange([storage length], 0)];
}

- (void)terminalExited:(AxyneProcess *)process exitCode:(int)exitCode
{
    char message[96];
    _lastExitCode = exitCode;
    _lastExitFailed = exitCode != 0;
    _hasExitStatus = YES;
    (void)snprintf(message, sizeof(message),
        _lastExitFailed ? "[failed: exit %d]\n" : "[exit %d]\n", exitCode);
    [self terminalAppend:message length:strlen(message)
                   stream:AXYNE_PROCESS_STDOUT];
    if (_terminalProcess == process) {
        axyne_process_release(_terminalProcess);
        _terminalProcess = NULL;
    }
    axyne_debugger_release(&_debugger);
    _activeAction = 0;
    [_terminalStart setEnabled:YES];
    [_terminalStop setEnabled:NO];
    [_debugStart setEnabled:YES];
    [_debugPause setEnabled:NO];
    [_debugContinue setEnabled:NO];
    [_debugNext setEnabled:NO];
    [_debugBreakpoint setEnabled:NO];
    [self refreshActionControls];
    [self setNeedsDisplay:YES];
}

- (void)startDebugger:(id)sender
{
    AxyneDocument *document;
    AxyneError error;
    (void)sender;
    if (axyne_debugger_is_active(&_debugger) || ![self captureEditor]) return;
    if (_terminalProcess != NULL) {
        const char *message = "Debugger is unavailable while a terminal session is active. Stop the terminal first.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    document = [self activeDocument];
    if (document == NULL || document->is_untitled || document->path == NULL ||
        document->is_dirty) {
        const char *message =
            "The debugger requires a saved, clean, non-untitled document.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    if (axyne_debugger_start(&_debugger, document,
            axyne_macos_terminal_output, axyne_macos_terminal_exit, self,
            &error) != AXYNE_STATUS_OK) {
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    [self terminalAppend:"[debugger]\n" length:12 stream:AXYNE_PROCESS_STDOUT];
    [_debugStart setEnabled:NO]; [_debugPause setEnabled:YES];
    [_debugContinue setEnabled:YES]; [_debugNext setEnabled:YES];
    [_debugBreakpoint setEnabled:YES]; _activeAction = 4;
    [self refreshActionControls];
}

- (void)debugCommand:(id)sender
{
    AxyneDebuggerCommand command = (AxyneDebuggerCommand)[sender tag];
    AxyneError error;
    if (axyne_debugger_command(&_debugger, command, &error) != AXYNE_STATUS_OK)
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
}

- (void)toggleBreakpoint:(id)sender
{
    AxyneDocument *document = [self activeDocument];
    AxyneError error;
    size_t position;
    size_t line;
    (void)sender;
    if (document == NULL || document->path == NULL) return;
    position = (size_t)[self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    line = (size_t)[self sendEditorMessage:2166 wParam:position lParam:0] + 1;
    if (axyne_debugger_toggle_breakpoint(&_debugger, document->path, line,
                                         &error) != AXYNE_STATUS_OK)
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
}

- (void)startTerminal:(id)sender
{
    AxyneProcessSpec spec;
    AxyneError error;
    AxyneStatus status;
    (void)sender;
    if (_terminalProcess != NULL || axyne_debugger_is_active(&_debugger)) {
        const char *message = "Terminal is unavailable while the debugger session is active. Stop the debugger first.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
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
    [self refreshActionControls];
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
    NSView *accessory = [[AxyneFlippedView alloc] initWithFrame:NSMakeRect(0, 0, 460, 330)];
    NSTextField *executable = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 28, 460, 28)];
    NSTextField *workingDirectory = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 188, 460, 28)];
    NSTextView *arguments = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 440, 74)];
    NSTextView *environment = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 440, 74)];
    NSScrollView *argumentsScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 88, 460, 74)];
    NSScrollView *environmentScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 252, 460, 74)];
    NSString *initialExecutable = _actionRunner.executable != NULL
        ? [NSString stringWithUTF8String:_actionRunner.executable] : @"";
    NSString *initialWorkingDirectory = _actionRunner.working_directory != NULL
        ? [NSString stringWithUTF8String:_actionRunner.working_directory] : @"";
    [executable setStringValue:initialExecutable != nil ? initialExecutable : @""];
    [workingDirectory setStringValue:initialWorkingDirectory != nil
        ? initialWorkingDirectory : @""];
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
    [accessory addSubview:axyne_macos_label(@"Executable", 4)];
    [accessory addSubview:executable];
    [accessory addSubview:axyne_macos_label(@"Arguments (one per line)", 64)];
    [accessory addSubview:argumentsScroll];
    [accessory addSubview:axyne_macos_label(@"Working directory (optional)", 168)];
    [accessory addSubview:workingDirectory];
    [accessory addSubview:axyne_macos_label(
        @"Environment overrides (NAME=VALUE per line)", 232)];
    [accessory addSubview:environmentScroll];
    [alert setMessageText:@"Configure Build/Run Runner"];
    [alert setInformativeText:@"Arguments are passed directly to the executable; no shell is used."];
    [alert setAccessoryView:accessory];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];
    NSInteger response = [alert runModal];
    BOOL accepted = NO;
    if (response == NSAlertFirstButtonReturn) {
        const char *executableText = [[executable stringValue] UTF8String];
        const char *argumentsText = [[arguments string] UTF8String];
        const char *workingDirectoryText = [[workingDirectory stringValue] UTF8String];
        const char *environmentText = [[environment string] UTF8String];
        char *executableUTF8 = strdup(executableText != NULL ? executableText : "");
        char *argumentsUTF8 = strdup(argumentsText != NULL ? argumentsText : "");
        char *workingDirectoryUTF8 = strdup(
            workingDirectoryText != NULL ? workingDirectoryText : "");
        char *environmentUTF8 = strdup(environmentText != NULL ? environmentText : "");
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
            NSString *detail = [NSString stringWithUTF8String:
                error.message[0] != '\0' ? error.message :
                "Unable to configure runner."];
            [errorAlert setInformativeText:detail != nil ? detail : @""];
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
    if (_terminalProcess != NULL || axyne_debugger_is_active(&_debugger)) {
        const char *message = "Build or run is unavailable while a terminal or debugger session is active. Stop it first.\n";
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
        [self refreshActionControls];
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

- (NSRect)tabFrameAtIndex:(size_t)index
{
    CGFloat x = AXYNE_SIDEBAR - _tabScroll;
    NSFont *font = [NSFont systemFontOfSize:12];
    for (size_t i = 0; i < _documents.count; ++i) {
        AxyneDocument *doc = &_documents.documents[i];
        NSString *title = [NSString stringWithUTF8String:doc->title != NULL ? doc->title : "Untitled"];
        CGFloat nameWidth = [title sizeWithAttributes:@{NSFontAttributeName:font}].width;
        CGFloat badgeWidth = [axyne_macos_file_badge(doc->title)
            length] != 0 ? 24 : 0;
        CGFloat width = MIN(240, MAX(100, nameWidth + badgeWidth + 54));
        if (i == index) return NSMakeRect(x, AXYNE_CONTENT_TOP, width, AXYNE_TABS);
        x += width;
    }
    return NSZeroRect;
}

- (NSRect)displayTabFrameAtIndex:(size_t)index
{
    CGFloat x = AXYNE_SIDEBAR - _tabScroll;
    NSFont *font = [NSFont systemFontOfSize:12];
    for (size_t i = 0; i < _documents.count; ++i) {
        AxyneDocument *doc = &_documents.documents[i];
        if (doc->is_untitled && doc->length == 0 &&
            !doc->is_dirty)
            continue;
        NSString *title = [NSString stringWithUTF8String:doc->title != NULL ? doc->title : "Untitled"];
        CGFloat nameWidth = [title sizeWithAttributes:@{NSFontAttributeName:font}].width;
        CGFloat badgeWidth = [axyne_macos_file_badge(doc->title) length] != 0 ? 24 : 0;
        CGFloat width = MIN(240, MAX(100, nameWidth + badgeWidth + 54));
        if (i == index) return NSMakeRect(x, AXYNE_CONTENT_TOP, width, AXYNE_TABS);
        x += width;
    }
    return NSZeroRect;
}

- (void)scrollTabsBy:(CGFloat)delta
{
    if (_documents.count == 0) { _tabScroll = 0; return; }
    NSRect last = [self tabFrameAtIndex:_documents.count - 1];
    CGFloat maximum = MAX(0, NSMaxX(last) + _tabScroll - NSWidth([self bounds]));
    _tabScroll = MIN(maximum, MAX(0, _tabScroll + delta));
    [self setNeedsDisplay:YES];
}

- (void)layout
{
    [super layout];
    NSRect bounds = [self bounds];
    CGFloat width = NSWidth(bounds);
    CGFloat bottomTop = NSHeight(bounds) - AXYNE_STATUS - AXYNE_BOTTOM;
    CGFloat editorTop = AXYNE_CONTENT_TOP + AXYNE_TABS;
    AxyneDocument *activeDocument = [self activeDocument];
    BOOL emptyWorkspace = activeDocument != NULL && activeDocument->is_untitled &&
        activeDocument->length == 0 && !activeDocument->is_dirty;
    [_editorView setFrame:NSMakeRect(AXYNE_SIDEBAR, editorTop,
        MAX(0, width - AXYNE_SIDEBAR), MAX(0, bottomTop - editorTop))];
    BOOL imageDocument = axyne_macos_is_image_path(activeDocument == NULL
        ? NULL : activeDocument->path);
    BOOL imagePreviewVisible = imageDocument && [_imagePreview image] != nil;
    [_editorView setHidden:emptyWorkspace || imagePreviewVisible];
    [_imagePreview setHidden:!imagePreviewVisible];
    [_imagePreview setFrame:NSMakeRect(AXYNE_SIDEBAR, editorTop,
        MAX(0, width - AXYNE_SIDEBAR), MAX(0, bottomTop - editorTop))];
    NSInteger visibleRows = MAX(1, (NSInteger)((bottomTop - editorTop -
        AXYNE_UI_EXPLORER_HEADER) / AXYNE_UI_ROW));
    _explorerFirstRow = MIN(_explorerFirstRow, MAX(0, (NSInteger)_explorer.count - visibleRows));
    BOOL terminal = _panelMode == 2;
    CGFloat inputTop = NSHeight(bounds) - AXYNE_STATUS - 28;
    [_terminalScroll setFrame:NSMakeRect(AXYNE_SIDEBAR + 16, bottomTop + 32,
        MAX(0, width - AXYNE_SIDEBAR - 24), terminal ? 164 : 198)];
    [_terminalScroll setHidden:_panelMode == 1];
    [_terminalInput setFrame:NSMakeRect(AXYNE_SIDEBAR + 16, inputTop,
        MAX(0, width - AXYNE_SIDEBAR - 88), 22)];
    [_terminalSend setFrame:NSMakeRect(width - 64, inputTop, 52, 22)];
    [_terminalInput setHidden:!terminal]; [_terminalSend setHidden:!terminal];
    [_terminalInput setBezeled:NO];
    [_problemSummary setFrame:NSMakeRect(AXYNE_SIDEBAR + 16, bottomTop + 42,
        MAX(0, width - AXYNE_SIDEBAR - 32), 22)];
    [_problemSummary setHidden:_panelMode != 1];
    [_outputTab setFrame:NSMakeRect(AXYNE_SIDEBAR + 8, bottomTop, 38, 32)];
    [_problemsTab setFrame:NSMakeRect(AXYNE_SIDEBAR + 48, bottomTop, 38, 32)];
    [_terminalTab setFrame:NSMakeRect(AXYNE_SIDEBAR + 88, bottomTop, 50, 32)];
    [_terminalStart setFrame:NSMakeRect(width - 100, bottomTop + 2, 28, 28)];
    [_clearOutput setFrame:NSMakeRect(width - 68, bottomTop + 2, 28, 28)];
    [_terminalStop setFrame:NSMakeRect(width - 36, bottomTop + 2, 28, 28)];
    [_newButton setHidden:NO]; [_openButton setHidden:NO]; [_saveButton setHidden:NO];
    [_undoButton setHidden:NO]; [_redoButton setHidden:NO]; [_searchButton setHidden:NO];
    CGFloat x = 12;
    for (NSButton *button in @[_newButton, _openButton, _saveButton, _undoButton, _redoButton]) {
        [button setFrame:NSMakeRect(x, AXYNE_MENU + 5, 28, 28)];
        x += 32;
    }
    x += 15;
    [_targetButton setFrame:NSMakeRect(x, AXYNE_MENU + 6, 162, 26)]; x += 172;
    [_buildButton setFrame:NSMakeRect(x, AXYNE_MENU + 6, 92, 26)]; x += 100;
    [_runButton setFrame:NSMakeRect(x, AXYNE_MENU + 6, 88, 26)]; x += 96;
    CGFloat searchLeft = MAX(x + 8, width - 348);
    [_searchButton setFrame:NSMakeRect(searchLeft, AXYNE_MENU + 6,
        MAX(0, width - 8 - searchLeft), 26)];
    for (NSButton *button in @[_outputTab, _problemsTab, _terminalTab]) {
        [(AxyneChromeButton *)button setLabelColor:axyne_preference_color(
            [button tag] == _panelMode ? _preferences.theme.text : _preferences.theme.muted)];
        [button setNeedsDisplay:YES];
    }
    if (_documents.count != 0) {
        NSRect active = [self tabFrameAtIndex:_documents.active_index];
        if (NSMaxX(active) > width) _tabScroll += NSMaxX(active) - width;
        else if (NSMinX(active) < AXYNE_SIDEBAR)
            _tabScroll = MAX(0, _tabScroll - AXYNE_SIDEBAR + NSMinX(active));
    }
}

- (void)drawLabel:(NSString *)label at:(NSPoint)point
             size:(CGFloat)size color:(NSColor *)color family:(NSString *)family
{
    NSDictionary *attributes = @{
        NSFontAttributeName: ([NSFont fontWithName:family size:size] != nil
            ? [NSFont fontWithName:family size:size]
            : [NSFont systemFontOfSize:size]),
        NSForegroundColorAttributeName: color
    };
    [label drawAtPoint:point withAttributes:attributes];
}

- (void)drawFileBadge:(const char *)name inRect:(NSRect)rect tab:(BOOL)tab
{
    AxyneFileBadge badge = axyne_ui_file_badge(name);
    NSString *label = [NSString stringWithUTF8String:badge.label];
    if ([label length] == 0) {
        /* Unspecified file types get a neutral document outline rather than
         * an arbitrary extension-sized word shifting the file name. */
        NSRect icon = NSMakeRect(NSMinX(rect) + 6, NSMinY(rect) + 3, 8, 10);
        [axyne_preference_color(badge.color) setStroke];
        [[NSBezierPath bezierPathWithRect:icon] stroke];
        return;
    }
    NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [style setAlignment:NSTextAlignmentCenter];
    CGFloat size = tab ? 11 : 9;
    NSFont *font = tab ? [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightBold] :
        [NSFont systemFontOfSize:size];
    [label drawInRect:rect withAttributes:@{NSFontAttributeName:font,
        NSForegroundColorAttributeName:axyne_preference_color(badge.color),
        NSParagraphStyleAttributeName:style}];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    CGFloat width = NSWidth(bounds), height = NSHeight(bounds);
    CGFloat bottomTop = height - AXYNE_STATUS - AXYNE_BOTTOM;
    CGFloat statusTop = height - AXYNE_STATUS;
    CGFloat editorTop = AXYNE_CONTENT_TOP + AXYNE_TABS;
    NSColor *background = axyne_preference_color(_preferences.theme.background);
    NSColor *panel = axyne_preference_color(_preferences.theme.panel);
    NSColor *muted = axyne_preference_color(_preferences.theme.muted);
    NSColor *text = axyne_preference_color(_preferences.theme.text);
    NSColor *border = axyne_preference_color(_preferences.theme.border);
    NSColor *toolbar = axyne_preference_color(_preferences.theme.toolbar);
    BOOL reference = axyne_macos_reference_surfaces(&_preferences.theme);
    NSColor *tabBackground = axyne_preference_color(reference ? 0x17191c : _preferences.theme.toolbar);
    [background setFill]; NSRectFill(bounds);
    [background setFill]; NSRectFill(NSMakeRect(0, 0, width, AXYNE_MENU));
    NSColor *menuText = axyne_preference_color(0xc7c9ce);
    CGFloat menuX = 8;
    NSArray *menuLabels = @[@"파일(F)", @"편집(E)", @"보기(V)", @"빌드(B)",
        @"디버그(D)", @"도구(T)", @"도움말(H)"];
    for (NSUInteger menuIndex = 0; menuIndex < [menuLabels count]; ++menuIndex) {
        NSString *label = [menuLabels objectAtIndex:menuIndex];
        CGFloat itemWidth = [label sizeWithAttributes:@{
            NSFontAttributeName:[NSFont systemFontOfSize:14]}].width + 20;
        if ((NSInteger)menuIndex == _activeMenuIndex) {
            [axyne_preference_color(0x2a2e35) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(menuX, 3,
                itemWidth, AXYNE_MENU - 6) xRadius:4 yRadius:4] fill];
        }
        [self drawLabel:label at:NSMakePoint(menuX + 10, 4) size:14
            color:menuText family:@"SF Pro Text"];
        menuX += itemWidth + 2;
    }
    [toolbar setFill]; NSRectFill(NSMakeRect(0, AXYNE_MENU, width, AXYNE_TOOLBAR));
    [tabBackground setFill]; NSRectFill(NSMakeRect(0, AXYNE_CONTENT_TOP, width, AXYNE_TABS));
    [panel setFill]; NSRectFill(NSMakeRect(0, editorTop, AXYNE_SIDEBAR, statusTop - editorTop));
    [axyne_preference_color(reference ? 0x191b1f : _preferences.theme.toolbar) setFill];
    NSRectFill(NSMakeRect(0, AXYNE_CONTENT_TOP, AXYNE_SIDEBAR, AXYNE_TABS));
    [tabBackground setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR, bottomTop, width - AXYNE_SIDEBAR, 32));
    [axyne_preference_color(axyne_macos_output_background(&_preferences.theme)) setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR, bottomTop + 32, width - AXYNE_SIDEBAR, 198));
    [toolbar setFill]; NSRectFill(NSMakeRect(0, statusTop, width, AXYNE_STATUS));
    [border setFill];
    NSRectFill(NSMakeRect(0, AXYNE_MENU + AXYNE_TOOLBAR - 1, width, 1));
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR - 1, editorTop, 1, statusTop - editorTop));
    NSRectFill(NSMakeRect(0, bottomTop, width, 1));
    AxyneDocument *activeDocument = [self activeDocument];
    BOOL emptyWorkspace = activeDocument != NULL && activeDocument->is_untitled &&
        activeDocument->length == 0 && !activeDocument->is_dirty;
    if (emptyWorkspace) {
        CGFloat centerX = AXYNE_SIDEBAR + (width - AXYNE_SIDEBAR) / 2.0;
        CGFloat centerY = editorTop + (bottomTop - editorTop) / 2.0;
        NSString *title = @"파일을 열어 시작하세요";
        NSString *hint = @"⌘O 파일 열기    ⌘N 새 파일";
        NSFont *titleFont = [NSFont systemFontOfSize:16 weight:NSFontWeightSemibold];
        NSFont *hintFont = [NSFont systemFontOfSize:12];
        NSDictionary *titleAttributes = @{NSFontAttributeName:titleFont,
            NSForegroundColorAttributeName:text};
        NSDictionary *hintAttributes = @{NSFontAttributeName:hintFont,
            NSForegroundColorAttributeName:muted};
        NSSize titleSize = [title sizeWithAttributes:titleAttributes];
        NSSize hintSize = [hint sizeWithAttributes:hintAttributes];
        [title drawAtPoint:NSMakePoint(centerX - titleSize.width / 2.0,
            centerY + 8) withAttributes:titleAttributes];
        [hint drawAtPoint:NSMakePoint(centerX - hintSize.width / 2.0,
            centerY - 18) withAttributes:hintAttributes];
    }
    if (![_searchButton isHidden]) {
        [axyne_preference_color(reference ? 0x3a3d44 : _preferences.theme.border) setStroke];
        [[NSBezierPath bezierPathWithRoundedRect:[_searchButton frame] xRadius:4 yRadius:4] stroke];
    }
    NSButton *selected = _panelMode == 0 ? _outputTab : (_panelMode == 1 ? _problemsTab : _terminalTab);
    [axyne_preference_color(reference ? 0xa66bf0 : _preferences.theme.accent) setFill];
    NSRectFill(NSMakeRect(NSMinX([selected frame]), bottomTop + 29, NSWidth([selected frame]), 3));
    NSString *shell = _terminalRunner.executable == NULL ? @"" :
        [[NSString stringWithUTF8String:_terminalRunner.executable] lastPathComponent];
    [self drawLabel:shell at:NSMakePoint(AXYNE_SIDEBAR + 148, bottomTop + 10)
        size:11 color:muted family:@"SF Pro Text"];

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(AXYNE_SIDEBAR, AXYNE_CONTENT_TOP, MAX(0, width - AXYNE_SIDEBAR), AXYNE_TABS));
    for (size_t i = 0; i < _documents.count; ++i) {
        AxyneDocument *doc = &_documents.documents[i];
        if (doc->is_untitled && doc->length == 0 &&
            !doc->is_dirty)
            continue;
        NSRect frame = [self displayTabFrameAtIndex:i];
        if (NSMaxX(frame) <= AXYNE_SIDEBAR || NSMinX(frame) >= width) continue;
        BOOL active = i == _documents.active_index;
        if (active) {
            [axyne_preference_color(_preferences.theme.editor_background) setFill]; NSRectFill(frame);
            [axyne_preference_color(reference ? 0xa66bf0 : _preferences.theme.accent) setFill];
            NSRectFill(NSMakeRect(NSMinX(frame), NSMinY(frame), NSWidth(frame), 2));
        }
        CGFloat badgeX = NSMinX(frame) + 14;
        [self drawFileBadge:doc->title inRect:NSMakeRect(badgeX, AXYNE_CONTENT_TOP + 10, 20, 16) tab:YES];
        NSString *title = [NSString stringWithUTF8String:doc->title != NULL ? doc->title : "Untitled"];
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(NSMakeRect(badgeX + 26, AXYNE_CONTENT_TOP + 4, NSWidth(frame) - 64, 28));
        [self drawLabel:title != nil ? title : @"Untitled"
            at:NSMakePoint(badgeX + 26, AXYNE_CONTENT_TOP + 10)
            size:12 color:active ? axyne_preference_color(reference ? 0xe6e7ea : _preferences.theme.text) : muted family:@"SF Pro Text"];
        [NSGraphicsContext restoreGraphicsState];
        [self drawLabel:doc->is_dirty ? @"●" : @"×"
            at:NSMakePoint(NSMaxX(frame) - 20, AXYNE_CONTENT_TOP + 10)
            size:doc->is_dirty ? 8 : 13 color:muted family:@"SF Pro Text"];
    }
    [NSGraphicsContext restoreGraphicsState];
    [self drawLabel:@"탐색기" at:NSMakePoint(12, editorTop + 8)
        size:11 color:axyne_preference_color(reference ? 0x8b919b : _preferences.theme.muted) family:@"SF Pro Text"];
    CGFloat explorerY = editorTop + AXYNE_UI_EXPLORER_HEADER;
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(0, explorerY, AXYNE_SIDEBAR - 1, MAX(0, bottomTop - explorerY)));
    if (_explorer.root == NULL) {
        [self drawLabel:@"폴더 열기…" at:NSMakePoint(16, explorerY + 3)
            size:12 color:text family:@"SF Pro Text"];
    } else {
        for (size_t i = (size_t)_explorerFirstRow; i < _explorer.count &&
            explorerY + AXYNE_UI_ROW <= bottomTop; ++i, explorerY += AXYNE_UI_ROW) {
            AxyneExplorerNode *node = &_explorer.nodes[i];
            BOOL selectedRow = _hasExplorerSelection && _explorerSelection == (NSInteger)i;
            if (selectedRow) {
                [axyne_preference_color(reference ? 0x2f343c : _preferences.theme.editor_background) setFill];
                NSRectFill(NSMakeRect(0, explorerY, AXYNE_SIDEBAR, AXYNE_UI_ROW));
            }
            CGFloat x = 8 + node->depth * AXYNE_UI_INDENT;
            CGFloat nameX;
            if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
                [self drawLabel:axyne_explorer_is_expanded(&_explorer, node->path) ? @"⌄" : @"›"
                    at:NSMakePoint(x, explorerY + 4) size:11 color:muted family:@"SF Pro Text"];
                nameX = x + 16;
            } else {
                [self drawFileBadge:node->name inRect:NSMakeRect(x, explorerY + 4, 20, 14) tab:NO];
                nameX = x + 26;
            }
            NSString *name = [NSString stringWithUTF8String:node->name];
            [self drawLabel:name != nil ? name : @"(invalid name)"
                at:NSMakePoint(nameX, explorerY + 3) size:12
                color:selectedRow ? text : axyne_preference_color(reference ? 0xc4c8ce : _preferences.theme.text)
                family:@"SF Pro Text"];
        }
    }
    [NSGraphicsContext restoreGraphicsState];
    NSString *status = _lastExitFailed ? [NSString stringWithFormat:@"✗ 실행 실패 (%d)", _lastExitCode] :
        (_activeAction != 0 ? @"● 실행 중" : (_hasExitStatus ? @"✓ 실행 완료" : @"준비"));
    [self drawLabel:status at:NSMakePoint(12, statusTop + 5) size:11
        color:_lastExitFailed ? axyne_preference_color(0xe5a445) :
              (_hasExitStatus ? axyne_preference_color(0xa3c98a) : muted) family:@"SF Pro Text"];
    NSInteger caret = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    NSInteger line = [self sendEditorMessage:SCI_LINEFROMPOSITION wParam:caret lParam:0] + 1;
    NSInteger column = [self sendEditorMessage:SCI_GETCOLUMN wParam:caret lParam:0] + 1;
    NSString *editing = [NSString stringWithFormat:@"줄 %ld, 열 %ld    %@: %u    UTF-8",
        (long)line, (long)column, _preferences.editor.insert_spaces ? @"공백" : @"탭",
        _preferences.editor.tab_width];
    [self drawLabel:editing at:NSMakePoint(MAX(180, width - 320), statusTop + 5)
        size:11 color:text family:@"SF Pro Text"];
    if (_editorView == nil)
        [self drawLabel:@"Required Scintilla framework failed to load"
            at:NSMakePoint(AXYNE_SIDEBAR + 24, editorTop + 24) size:12 color:muted family:@"Menlo"];
}
- (void)dealloc
{
    if (_gitRun != NULL) {
        AxyneMacGitRun *run = _gitRun;
        (void)pthread_mutex_lock(&run->lock);
        run->cancelled = 1;
        run->view = nil;
        (void)pthread_mutex_unlock(&run->lock);
        (void)axyne_process_terminate(run->process, NULL);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            axyne_macos_git_cleanup(run);
        });
        _gitRun = NULL;
        _gitProcess = NULL;
    }
    if (_lsp != NULL) {
        axyne_lsp_destroy(_lsp);
        _lsp = NULL;
    }
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
    /* Scintilla's active buffer also holds a reference to its Lexilla lexer.
     * Destroy it before unloading Lexilla, including when AppKit retains the
     * editor subview until the superclass tears down its children. */
    [(id)_editorView setDelegate:nil];
    (void)[self sendEditorMessage:SCI_SETILEXER wParam:0 lParam:0];
    axyne_documents_destroy(&_documents);
    [_editorView removeFromSuperview];
    [_editorView release];
    _editorView = nil;
    [_imagePreview removeFromSuperview];
    [_imagePreview release];
    _imagePreview = nil;
    [_newButton release]; [_openButton release]; [_saveButton release];
    [_undoButton release]; [_redoButton release];
    [_buildButton release]; [_runButton release];
    [_targetButton release]; [_searchButton release];
    [_runnerPreset release];
    [_outputTab release]; [_problemsTab release]; [_terminalTab release];
    [_clearOutput release]; [_problemSummary release];
    [_terminalScroll release];
    [_terminalOutput release];
    [_terminalInput release];
    [_terminalStart release];
    [_terminalStop release];
    [_terminalSend release];
    [_debugStart release];
    [_debugPause release];
    [_debugContinue release];
    [_debugNext release];
    [_debugBreakpoint release];
    axyne_debugger_destroy(&_debugger);
    [_lspStatus release];
    if (_lexillaModule != NULL) dlclose(_lexillaModule);
    [_scintillaBundle unload];
    [_scintillaBundle release];
    free(_globalPreferencesPath);
    free(_workspacePreferencesPath);
    [super dealloc];
}

@end

@interface AxyneApplicationDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
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
    [_window setTitle:_appName != nil ? _appName : @"Axyne"];
    [_window setTitlebarAppearsTransparent:YES];
    [_window setTitleVisibility:NSWindowTitleVisible];
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
    [appMenu addItemWithTitle:@"Axyne 정보" action:nil keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *preferencesItem = [appMenu addItemWithTitle:@"환경설정…"
        action:@selector(showGlobalPreferences:) keyEquivalent:@","];
    [preferencesItem setTarget:workspace];
    [appMenu addItemWithTitle:@"Quit Axyne" action:@selector(terminate:)
                 keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];
    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"파일(F)"
        action:nil keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"파일(F)"];
    NSMenuItem *newItem = [fileMenu addItemWithTitle:@"새 파일"
        action:@selector(newDocument:) keyEquivalent:@"n"];
    [newItem setTarget:workspace];
    NSMenuItem *openItem = [fileMenu addItemWithTitle:@"파일 열기…"
        action:@selector(openDocument:) keyEquivalent:@"o"];
    [openItem setTarget:workspace];
    NSMenuItem *saveItem = [fileMenu addItemWithTitle:@"저장"
        action:@selector(saveDocument:) keyEquivalent:@"s"];
    [saveItem setTarget:workspace];
    NSMenuItem *saveAsItem = [fileMenu addItemWithTitle:@"다른 이름으로 저장…"
        action:@selector(saveDocumentAs:) keyEquivalent:@"S"];
    [saveAsItem setTarget:workspace];
    NSMenuItem *closeItem = [fileMenu addItemWithTitle:@"탭 닫기"
        action:@selector(closeDocument:) keyEquivalent:@"w"];
    [closeItem setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *workspaceItem = [fileMenu addItemWithTitle:@"작업 폴더 열기…"
        action:@selector(openWorkspace:) keyEquivalent:@""];
    [workspaceItem setTarget:workspace];
    NSMenuItem *workspacePreferences = [fileMenu addItemWithTitle:@"워크스페이스 설정…"
        action:@selector(showWorkspacePreferences:) keyEquivalent:@""];
    [workspacePreferences setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *gitStatus = [fileMenu addItemWithTitle:@"Git 상태"
        action:@selector(showGitStatus:) keyEquivalent:@""];
    NSMenuItem *gitDiff = [fileMenu addItemWithTitle:@"Git 차이"
        action:@selector(showGitDiff:) keyEquivalent:@""];
    NSMenuItem *gitStage = [fileMenu addItemWithTitle:@"모든 Git 변경사항 스테이지"
        action:@selector(stageAllGitChanges:) keyEquivalent:@""];
    NSMenuItem *gitUnstage = [fileMenu addItemWithTitle:@"모든 Git 변경사항 언스테이지"
        action:@selector(unstageAllGitChanges:) keyEquivalent:@""];
    [gitStatus setTarget:workspace]; [gitDiff setTarget:workspace];
    [gitStage setTarget:workspace]; [gitUnstage setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *recentItem = [[NSMenuItem alloc] initWithTitle:@"최근 파일"
        action:nil keyEquivalent:@""];
    NSMenu *recentMenu = [[NSMenu alloc] initWithTitle:@"최근 파일"];
    [recentItem setSubmenu:recentMenu];
    [fileMenu addItem:recentItem];
    [workspace setRecentMenu:recentMenu];
    [fileItem setSubmenu:fileMenu];
    [mainMenu addItem:fileItem];
    [fileItem release]; [recentItem release];
    [recentMenu release]; [fileMenu release];
    NSArray *titles = @[@"편집(E)", @"보기(V)", @"빌드(B)", @"디버그(D)",
        @"도구(T)", @"도움말(H)"];
    for (NSString *title in titles) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
            action:nil keyEquivalent:@""];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:title];
        if ([title hasPrefix:@"편집"]) {
            [submenu addItemWithTitle:@"실행 취소" action:@selector(undo:)
                         keyEquivalent:@"z"];
            NSMenuItem *redo = [submenu addItemWithTitle:@"다시 실행"
                action:@selector(redo:) keyEquivalent:@"Z"];
            [redo setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                          NSEventModifierFlagShift];
            [submenu addItem:[NSMenuItem separatorItem]];
            [submenu addItemWithTitle:@"잘라내기" action:@selector(cut:)
                         keyEquivalent:@"x"];
            [submenu addItemWithTitle:@"복사" action:@selector(copy:)
                         keyEquivalent:@"c"];
            [submenu addItemWithTitle:@"붙여넣기" action:@selector(paste:)
                         keyEquivalent:@"v"];
            [submenu addItemWithTitle:@"전체 선택" action:@selector(selectAll:)
                         keyEquivalent:@"a"];
        } else if ([title hasPrefix:@"빌드"]) {
            NSMenuItem *build = [submenu addItemWithTitle:@"활성 문서 빌드"
                action:@selector(buildDocument:) keyEquivalent:@"b"];
            NSMenuItem *run = [submenu addItemWithTitle:@"활성 문서 실행"
                action:@selector(runDocument:) keyEquivalent:@"r"];
            NSMenuItem *configure = [submenu addItemWithTitle:@"빌드/실행 Runner 설정…"
                action:@selector(configureRunnerAction:) keyEquivalent:@""];
            [build setTarget:workspace]; [run setTarget:workspace];
            [configure setTarget:workspace];
        } else if ([title hasPrefix:@"디버그"]) {
            NSMenuItem *start = [submenu addItemWithTitle:@"디버거 시작"
                action:@selector(startDebugger:)
                keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSF5FunctionKey]];
            NSMenuItem *pause = [submenu addItemWithTitle:@"일시 정지"
                action:@selector(debugCommand:)
                keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSF6FunctionKey]];
            NSMenuItem *resume = [submenu addItemWithTitle:@"계속"
                action:@selector(debugCommand:) keyEquivalent:@""];
            [resume setTag:0]; [resume setTarget:workspace];
            NSMenuItem *next = [submenu addItemWithTitle:@"다음 단계"
                action:@selector(debugCommand:)
                keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSF10FunctionKey]];
            [start setTarget:workspace]; [pause setTarget:workspace];
            [next setTarget:workspace]; [pause setTag:1]; [next setTag:2];
            NSMenuItem *toggle = [submenu addItemWithTitle:@"중단점 전환"
                action:@selector(toggleBreakpoint:)
                keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSF9FunctionKey]];
            [toggle setTarget:workspace];
            [start setKeyEquivalentModifierMask:0];
            [pause setKeyEquivalentModifierMask:0];
            [next setKeyEquivalentModifierMask:0];
            [toggle setKeyEquivalentModifierMask:0];
        } else if ([title hasPrefix:@"보기"]) {
            NSArray *panels = @[@"출력", @"문제", @"터미널"];
            for (NSInteger i = 0; i < (NSInteger)[panels count]; ++i) {
                NSMenuItem *panelItem = [submenu addItemWithTitle:panels[(NSUInteger)i]
                    action:@selector(selectPanel:) keyEquivalent:@""];
                [panelItem setTag:i]; [panelItem setTarget:workspace];
            }
        } else if ([title hasPrefix:@"도구"]) {
            NSMenuItem *definition = [submenu addItemWithTitle:@"LSP: 정의로 이동"
                action:@selector(navigateLspReferences:) keyEquivalent:@"d"];
            NSMenuItem *references = [submenu addItemWithTitle:@"LSP: 참조 찾기"
                action:@selector(navigateLspReferences:) keyEquivalent:@"r"];
            [definition setTarget:workspace]; [definition setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
            [references setTarget:workspace]; [references setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
            [references setTag:1];
        } else if ([title hasPrefix:@"도움말"]) {
            NSMenuItem *about = [submenu addItemWithTitle:@"Axyne 정보"
                action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
            [about setTarget:application];
        }
        [item setSubmenu:submenu];
        [submenu release];
        [mainMenu addItem:item];
        [item release];
    }
    for (NSMenuItem *item in [mainMenu itemArray])
        if ([item submenu] != nil) axyne_style_popup_menu([item submenu]);
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
