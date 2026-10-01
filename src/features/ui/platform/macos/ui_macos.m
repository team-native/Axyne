#import <AppKit/AppKit.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "axyne/document.h"
#include "Scintilla.h"

enum { SCI_GETTEXT = 2182, SCI_GETTEXTLENGTH = 2183, SCI_SETTEXT = 2181,
       SCI_GETMODIFY = 2159, SCI_SETSAVEPOINT = 2014,
       SCI_CLEARALL = 2004, SCI_ADDTEXT = 2001, SCI_GETDOCPOINTER = 2357,
       SCI_SETDOCPOINTER = 2358, SCI_CREATEDOCUMENT = 2375,
       SCI_RELEASEDOCUMENT = 2377 };

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
- (void)captureEditor;
- (void)loadActiveDocument;
- (BOOL)confirmCloseDocumentAtIndex:(size_t)index;
@end

static void axyne_install_menu(NSApplication *application,
                               AxyneWorkspaceView *workspace);

@implementation AxyneWorkspaceView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) {
        if (axyne_documents_initialize(&_documents, NULL) != AXYNE_STATUS_OK) {
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

- (void)captureEditorSnapshot
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || _editorView == nil) return;
    NSInteger length = [self sendEditorMessage:SCI_GETTEXTLENGTH wParam:0 lParam:0];
    if (length < 0 || (uint64_t)length >= SIZE_MAX) return;
    char *text = malloc((size_t)length + 1);
    if (text == NULL) return;
    (void)[self sendEditorMessage:SCI_GETTEXT wParam:(uintptr_t)length + 1
                            lParam:(intptr_t)text];
    (void)axyne_documents_set_contents(&_documents, _documents.active_index,
                                       text, (size_t)length, NULL);
    free(text);
}

- (void)captureEditor
{
    if ([self sendEditorMessage:SCI_GETMODIFY wParam:0 lParam:0] != 0)
        [self captureEditorSnapshot];
}

- (void)loadActiveDocument
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || _editorView == nil) return;
    _loadingEditor = YES;
    if (!_editorDocumentInitialized) {
        doc->native_editor_document = (void *)(uintptr_t)[self
            sendEditorMessage:SCI_GETDOCPOINTER wParam:0 lParam:0];
        _editorDocumentInitialized = YES;
    } else if (doc->native_editor_document == NULL) {
        NSInteger created = [self sendEditorMessage:SCI_CREATEDOCUMENT
            wParam:doc->length lParam:0];
        if (created == 0) { _loadingEditor = NO; return; }
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
        [self captureEditorSnapshot];
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
    [self captureEditor];
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
    [self captureEditor];
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
    [self captureEditor];
    size_t index;
    if (axyne_documents_new(&_documents, &index, NULL) == AXYNE_STATUS_OK) {
        (void)axyne_documents_set_active(&_documents, index, NULL);
        [self loadActiveDocument];
    }
}

- (void)openPath:(NSString *)path
{
    if (path == nil) return;
    [self captureEditor];
    size_t index = 0;
    AxyneError error;
    AxyneStatus status = axyne_documents_open(&_documents,
        [path UTF8String], &index, &error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_OUT_OF_MEMORY) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not open file"];
        [alert setInformativeText:[NSString stringWithUTF8String:error.message] ?: @""];
        [alert runModal];
        return;
    }
    (void)axyne_documents_set_active(&_documents, index, NULL);
    [self loadActiveDocument];
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
        [self captureEditor];
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
    [self captureEditor];
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
    [self captureEditor];
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
            [self captureEditor];
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
    [super mouseDown:event];
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    if (([event modifierFlags] & NSEventModifierFlagCommand) != 0) {
        NSString *key = [[event charactersIgnoringModifiers] lowercaseString];
        if ([key isEqualToString:@"n"]) { [self newDocument:nil]; return YES; }
        if ([key isEqualToString:@"o"]) { [self openDocument:nil]; return YES; }
        if ([key isEqualToString:@"s"]) { [self saveDocument:nil]; return YES; }
        if ([key isEqualToString:@"w"]) { [self closeDocument:nil]; return YES; }
    }
    return [super performKeyEquivalent:event];
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
    [self drawLabel:@"⌄  axyne" at:NSMakePoint(16, AXYNE_TOOLBAR + AXYNE_TABS + 34)
                size:12 color:text family:@"SF Pro Text"];
    [self drawLabel:@"⌄  src" at:NSMakePoint(32, AXYNE_TOOLBAR + AXYNE_TABS + 57)
                size:12 color:text family:@"SF Pro Text"];
    [axyne_color(47, 52, 60) setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR + AXYNE_TABS + 66,
                          AXYNE_SIDEBAR, 22));
    [self drawLabel:@"C  main.c" at:NSMakePoint(52, AXYNE_TOOLBAR + AXYNE_TABS + 69)
                size:12 color:text family:@"SF Pro Text"];
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
