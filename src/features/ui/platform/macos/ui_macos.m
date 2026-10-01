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
#include "Scintilla.h"

enum { SCI_GETTEXT = 2182, SCI_GETTEXTLENGTH = 2183, SCI_SETTEXT = 2181,
       SCI_GETMODIFY = 2159, SCI_SETSAVEPOINT = 2014,
       SCI_CLEARALL = 2004, SCI_ADDTEXT = 2001, SCI_GETDOCPOINTER = 2357,
       SCI_SETDOCPOINTER = 2358, SCI_CREATEDOCUMENT = 2375,
       SCI_RELEASEDOCUMENT = 2377, SCI_GETCURRENTPOS = 2008,
       SCI_SETSEL = 2160, SCI_REPLACESEL = 2170,
       SCI_POSITIONFROMLINE = 2167, SCI_GOTOPOS = 2025,
       SCI_BEGINUNDOACTION = 2078, SCI_ENDUNDOACTION = 2079 };

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
}
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
- (void)refreshExplorer;
- (void)showWorkspaceError:(NSString *)prefix error:(AxyneError *)error;
- (void)showWorkspaceMessage:(NSString *)message;
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSInteger)explorerNodeAtPoint:(NSPoint)point;
- (void)performExplorerOperation:(AxyneFileKind)kind;
- (NSString *)askForText:(NSString *)title label:(NSString *)label;
@end

static void axyne_install_menu(NSApplication *application,
                               AxyneWorkspaceView *workspace);

typedef struct AxyneMacWorkspaceEvent {
    AxyneWatchEventKind kind;
    char *path;
} AxyneMacWorkspaceEvent;

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

- (void)refreshExplorer
{
    AxyneError error;
    if (_explorer.root == NULL) return;
    if (axyne_explorer_reload(&_explorer, &error) != AXYNE_STATUS_OK) {
        [self showWorkspaceError:@"Unable to refresh workspace" error:&error];
    } else if (_hasExplorerSelection &&
               (size_t)_explorerSelection >= _explorer.count) {
        _hasExplorerSelection = NO;
    }
    [self setNeedsDisplay:YES];
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
    else { _hasExplorerSelection = NO; [self refreshExplorer]; }
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
    else { _hasExplorerSelection = NO; [self refreshExplorer]; }
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
    if (([event modifierFlags] & NSEventModifierFlagCommand) != 0) {
        NSString *key = [[event charactersIgnoringModifiers] lowercaseString];
        BOOL shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
        if ([key isEqualToString:@"f"] && shift) { [self searchFolder:NO]; return YES; }
        if ([key isEqualToString:@"f"]) { [self findOrReplace:NO]; return YES; }
        if ([key isEqualToString:@"h"]) { [self findOrReplace:YES]; return YES; }
        if ([key isEqualToString:@"p"]) { [self searchFolder:YES]; return YES; }
        if ([key isEqualToString:@"n"]) { [self newDocument:nil]; return YES; }
        if ([key isEqualToString:@"o"]) { [self openDocument:nil]; return YES; }
        if ([key isEqualToString:@"s"]) { [self saveDocument:nil]; return YES; }
        if ([key isEqualToString:@"w"]) { [self closeDocument:nil]; return YES; }
    }
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
    NSColor *background = axyne_color(22, 23, 26);
    NSColor *panel = axyne_color(31, 33, 38);
    NSColor *muted = axyne_color(115, 119, 128);
    NSColor *text = axyne_color(199, 201, 206);
    NSColor *border = axyne_color(41, 44, 50);
    NSColor *toolbar = axyne_color(28, 30, 34);

    [background setFill];
    NSRectFill(bounds);
    [toolbar setFill];
    NSRectFill(NSMakeRect(0, 0, width, AXYNE_TOOLBAR));
    [axyne_color(23, 25, 28) setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR, width, AXYNE_TABS));
    [panel setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR + AXYNE_TABS,
                          AXYNE_SIDEBAR, bottomTop - AXYNE_TOOLBAR - AXYNE_TABS));
    [toolbar setFill];
    NSRectFill(NSMakeRect(0, bottomTop, width, AXYNE_BOTTOM));
    [axyne_color(25, 27, 30) setFill];
    NSRectFill(NSMakeRect(0, statusTop, width, AXYNE_STATUS));
    [border setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR - 1, AXYNE_TOOLBAR + AXYNE_TABS,
                          1, bottomTop - AXYNE_TOOLBAR - AXYNE_TABS));
    NSRectFill(NSMakeRect(0, bottomTop, width, 1));

    [self drawLabel:@"▱   ▣    ↶   ↷       ▷  Debug · x64             빌드  ⌘B"
                at:NSMakePoint(14, 13) size:12 color:muted family:@"SF Pro Text"];
    NSRect search = NSMakeRect(MAX(400, width - 360), 7, 348, 26);
    [axyne_color(22, 23, 26) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:search xRadius:4 yRadius:4] fill];
    [border setStroke];
    [[NSBezierPath bezierPathWithRoundedRect:search xRadius:4 yRadius:4] stroke];
    [self drawLabel:@"⌕  파일 이동, > 명령 실행"
                at:NSMakePoint(NSMinX(search) + 10, 13) size:11 color:muted family:@"SF Pro Text"];

    [axyne_color(182, 122, 246) setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR + 20, AXYNE_TOOLBAR + AXYNE_TABS,
                          1, AXYNE_TABS));
    CGFloat tabX = AXYNE_SIDEBAR + 12;
    for (size_t i = 0; i < _documents.count; ++i) {
        AxyneDocument *doc = &_documents.documents[i];
        if (i == _documents.active_index) {
            [panel setFill];
            NSRectFill(NSMakeRect(tabX, AXYNE_TOOLBAR, 184, AXYNE_TABS));
            [axyne_color(182, 122, 246) setFill];
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
                [axyne_color(47, 52, 60) setFill];
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
    [self drawLabel:@"✓ 빌드 준비됨"
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
    [_scintillaBundle unload];
    [_scintillaBundle release];
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
        [item setSubmenu:[[NSMenu alloc] initWithTitle:title]];
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
