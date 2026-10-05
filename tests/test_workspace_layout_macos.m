#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include "axyne/explorer.h"
#include "axyne/ui_design.h"
#include "axyne/preferences.h"
#include "axyne/document.h"
#include "axyne/palette_controller.h"

@interface NSView (AxyneWorkspaceLayoutTest)
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSInteger)explorerNodeAtPoint:(NSPoint)point;
- (BOOL)refreshExplorer;
- (void)applyPreferences;
- (void)scrollTabsBy:(CGFloat)delta;
- (NSRect)tabFrameAtIndex:(size_t)index;
- (NSRect)menuBarItemRect:(NSUInteger)index;
- (NSInteger)menuBarIndexAtPoint:(NSPoint)point;
- (void)openPaletteWithInput:(NSString *)input;
- (void)paletteSyncInput;
- (void)paletteDismiss;
- (void)layoutParts;
@end

static id field(id view, const char *name)
{
    Ivar ivar = class_getInstanceVariable([view class], name);
    return ivar == NULL ? nil : object_getIvar(view, ivar);
}

static void *value_field(id view, const char *name)
{
    Ivar ivar = class_getInstanceVariable([view class], name);
    return ivar == NULL ? NULL : (char *)view + ivar_getOffset(ivar);
}

/* Failure diagnostics: every failed check dumps the document set, the tab
 * frames and the explorer rows, so a CI log shows the actual state. */
static NSView *diagnosticView;

static void dump_state(void)
{
    if (diagnosticView == nil) return;
    AxyneDocumentSet *documents = value_field(diagnosticView, "_documents");
    AxyneExplorer *explorer = value_field(diagnosticView, "_explorer");
    fprintf(stderr, "  view bounds=%s sidebar=%g\n",
        [NSStringFromRect([diagnosticView bounds]) UTF8String],
        (double)AXYNE_UI_SIDEBAR);
    if (documents != NULL) {
        fprintf(stderr, "  documents: count=%zu active=%zu visible=%zu\n",
            documents->count, documents->active_index,
            axyne_documents_visible_count(documents));
        for (size_t i = 0; i < documents->count && i < 32; ++i) {
            const AxyneDocument *doc = &documents->documents[i];
            fprintf(stderr, "    #%zu title=%s length=%zu dirty=%d untitled=%d "
                "requested=%d hidden=%d tab=%s\n", i,
                doc->title != NULL ? doc->title : "(null)", doc->length,
                doc->is_dirty, doc->is_untitled, doc->tab_requested,
                axyne_document_tab_hidden(doc),
                [NSStringFromRect([diagnosticView tabFrameAtIndex:i]) UTF8String]);
        }
    }
    if (explorer != NULL) {
        fprintf(stderr, "  explorer: root=%s count=%zu\n",
            explorer->root != NULL ? explorer->root : "(null)", explorer->count);
        for (size_t i = 0; i < explorer->count && i < 4; ++i)
            fprintf(stderr, "    row %zu depth=%zu name=%s\n", i,
                explorer->nodes[i].depth, explorer->nodes[i].name);
    }
}

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
    dump_state(); return 1; \
} } while (0)

#define CHECK_EMPTY_RECT(expression) do { \
    NSRect rect_ = (expression); \
    if (!NSIsEmptyRect(rect_)) { \
        fprintf(stderr, "FAIL %s:%d: %s is %s, expected an empty rect\n", \
            __FILE__, __LINE__, #expression, [NSStringFromRect(rect_) UTF8String]); \
        dump_state(); return 1; \
    } \
} while (0)

#define CHECK_X(expression, relation, bound) do { \
    CGFloat actual_ = NSMinX((expression)); \
    if (!(actual_ relation (CGFloat)(bound))) { \
        fprintf(stderr, "FAIL %s:%d: minX of %s is %g, expected %s %g\n", \
            __FILE__, __LINE__, #expression, (double)actual_, #relation, \
            (double)(bound)); \
        dump_state(); return 1; \
    } \
} while (0)

#define CHECK_MAX_X(expression, limit) do { \
    CGFloat actual_ = NSMaxX((expression)); \
    if (!(actual_ <= (CGFloat)(limit))) { \
        fprintf(stderr, "FAIL %s:%d: maxX of %s is %g, expected <= %g\n", \
            __FILE__, __LINE__, #expression, (double)actual_, (double)(limit)); \
        dump_state(); return 1; \
    } \
} while (0)

#define CHECK_ROW(point, expected) do { \
    NSInteger actual_ = [view explorerNodeAtPoint:(point)]; \
    NSInteger want_ = (NSInteger)(expected); \
    if (actual_ != want_) { \
        fprintf(stderr, "FAIL %s:%d: explorerNodeAtPoint:%s is %ld, expected %ld\n", \
            __FILE__, __LINE__, #point, (long)actual_, (long)want_); \
        dump_state(); return 1; \
    } \
} while (0)

/* Never let a native alert block the unattended regression suite. */
static NSModalResponse fail_alert(id alert, SEL selector)
{
    (void)selector;
    fprintf(stderr, "Unexpected alert: %s: %s\n", [[alert messageText] UTF8String],
        [[alert informativeText] UTF8String]);
    return NSAlertThirdButtonReturn;
}

int main(void)
{
    fprintf(stderr, "Starting native workspace layout regression\n");
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        Method alertMethod = class_getInstanceMethod([NSAlert class], @selector(runModal));
        IMP originalAlert = method_setImplementation(alertMethod, (IMP)fail_alert);
        Class type = NSClassFromString(@"AxyneWorkspaceView");
        CHECK(type != Nil);
        NSView *view = [[type alloc] initWithFrame:NSMakeRect(0, 0, 1440, 842)];
        CHECK(view != nil);
        diagnosticView = view;
        fprintf(stderr, "Checking toolbar and panels\n");
        [view setNeedsLayout:YES]; [view layoutSubtreeIfNeeded];
        NSRect previous = NSZeroRect;
        const char *toolbar[] = {"_newButton", "_openButton", "_saveButton",
            "_undoButton", "_redoButton", "_targetButton", "_buildButton", "_runButton", "_searchButton"};
        for (size_t i = 0; i < sizeof(toolbar) / sizeof(toolbar[0]); ++i) {
            NSButton *button = field(view, toolbar[i]);
            CHECK(button != nil && [button target] == view && [button action] != NULL);
            CHECK([view respondsToSelector:[button action]]);
            NSRect frame = [button frame];
            CHECK(NSMinY(frame) >= AXYNE_UI_MENU &&
                NSMaxY(frame) <= AXYNE_UI_MENU + AXYNE_UI_TOOLBAR);
            CHECK(NSMinX(frame) >= NSMaxX(previous) && NSMaxX(frame) <= 1440);
            previous = frame;
        }
        CHECK(![field(view, "_searchButton") isHidden]);
        fprintf(stderr, "Checking menu bar\n");
        {
            CGFloat edge = 0;
            for (NSUInteger i = 0; i < AXYNE_UI_MENU_COUNT; ++i) {
                NSRect item = [view menuBarItemRect:i];
                NSPoint inside = NSMakePoint(NSMidX(item), NSMidY(item));
                CHECK(NSWidth(item) > 2 * AXYNE_UI_MENU_PAD);
                CHECK(NSMinY(item) >= 0 && NSMaxY(item) <= AXYNE_UI_MENU);
                CHECK(NSMinX(item) >= edge && NSMaxX(item) <= 1440);
                CHECK([view menuBarIndexAtPoint:inside] == (NSInteger)i);
                CHECK([view menuBarIndexAtPoint:NSMakePoint(NSMidX(item), AXYNE_UI_MENU)] == -1);
                edge = NSMaxX(item);
            }
            CHECK(NSIsEmptyRect([view menuBarItemRect:AXYNE_UI_MENU_COUNT]));
            CHECK([view menuBarIndexAtPoint:NSMakePoint(1439, 10)] == -1);
        }
        NSScrollView *output = field(view, "_terminalScroll");
        CHECK(NSMinX([output frame]) >= AXYNE_UI_SIDEBAR);
        CHECK(NSMaxY([output frame]) <= 842 - AXYNE_UI_STATUS);
        CHECK([output hasVerticalScroller]);
        fprintf(stderr, "Checking terminal controls\n");
        NSButton *terminal = field(view, "_terminalTab");
        [terminal performClick:nil];
        [view layoutSubtreeIfNeeded];
        CHECK(![field(view, "_terminalInput") isHidden]);
        CHECK(![field(view, "_terminalSend") isHidden]);
        CHECK(NSMaxY([output frame]) <= NSMinY([field(view, "_terminalInput") frame]));
        CHECK([field(view, "_terminalInput") action] == NSSelectorFromString(@"sendTerminal:"));
        [field(view, "_problemsTab") performClick:nil];
        [view layoutSubtreeIfNeeded];
        CHECK([output isHidden] && ![field(view, "_problemSummary") isHidden]);
        [field(view, "_outputTab") performClick:nil];
        [view layoutSubtreeIfNeeded];
        CHECK(![output isHidden] && [field(view, "_terminalInput") isHidden]);
        /* The Git selector must fit the minimum supported sidebar, and
         * shrink safely below it instead of intercepting editor clicks. */
        {
            Class panelClass = NSClassFromString(@"AxyneGitPanelView");
            CHECK(panelClass != Nil);
            NSView *panel = [[panelClass alloc] initWithFrame:NSMakeRect(0, 0, 159, 600)];
            CHECK(panel != nil);
            const CGFloat widths[] = {159, 200, 320, 100};
            for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
                [panel setFrameSize:NSMakeSize(widths[i], 600)];
                [panel layoutParts];
                NSSegmentedControl *selector = field(panel, "_graphSelector");
                CHECK(selector != nil && [selector segmentCount] == 2);
                CHECK([[selector labelForSegment:0] isEqualToString:@"Compact"]);
                CHECK([[selector labelForSegment:1] isEqualToString:@"Full"]);
                CHECK(NSMinX([selector frame]) >= 0);
                CHECK(NSMaxX([selector frame]) <= widths[i]);
                CHECK(NSWidth([selector frame]) > 0);
            }
            [panel release];
        }
        fprintf(stderr, "Preparing fixture\n");
        const char *debug[] = {"_debugStart", "_debugPause", "_debugContinue", "_debugNext", "_debugBreakpoint"};
        for (size_t i = 0; i < sizeof(debug) / sizeof(debug[0]); ++i)
            CHECK([field(view, debug[i]) isHidden]);

        /* Foundation normalizes /private/var back to /var; use POSIX realpath
         * for the no-follow workspace filesystem contract. */
        char *temporaryPath = realpath([NSTemporaryDirectory() fileSystemRepresentation], NULL);
        CHECK(temporaryPath != NULL);
        NSString *root = [[NSString stringWithUTF8String:temporaryPath] stringByAppendingPathComponent:
            [@"axyne-layout-" stringByAppendingString:[[NSUUID UUID] UUIDString]]];
        free(temporaryPath);
        CHECK([[NSFileManager defaultManager] createDirectoryAtPath:root
            withIntermediateDirectories:NO attributes:nil error:NULL]);
        for (int i = 0; i < 40; ++i) {
            NSString *path = [root stringByAppendingPathComponent:[NSString stringWithFormat:@"file%02d.c", i]];
            CHECK([@"int a;\n" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL]);
        }
        fprintf(stderr, "Selecting fixture workspace\n");
        CHECK([view selectWorkspaceURL:[NSURL fileURLWithPath:root]]);
        fprintf(stderr, "Checking explorer fixture: %s\n", [root UTF8String]);
        [view layoutSubtreeIfNeeded];
        CGFloat top = AXYNE_UI_MENU + AXYNE_UI_TOOLBAR + AXYNE_UI_TABS +
            AXYNE_UI_EXPLORER_HEADER;
        CGFloat bottom = 842 - AXYNE_UI_STATUS - AXYNE_UI_PANEL;
        CHECK_ROW(NSMakePoint(80, top), 0);
        CHECK_ROW(NSMakePoint(80, top + AXYNE_UI_ROW - 1), 0);
        CHECK_ROW(NSMakePoint(80, top + AXYNE_UI_ROW), 1);
        CHECK_ROW(NSMakePoint(AXYNE_UI_SIDEBAR, top), NSNotFound);
        CHECK_ROW(NSMakePoint(80, top - 1), NSNotFound);
        CHECK_ROW(NSMakePoint(80, bottom), NSNotFound);
        CHECK_ROW(NSMakePoint(80, 842 - AXYNE_UI_STATUS), NSNotFound);

        /* The command palette replaces the toolbar search field while open. */
        fprintf(stderr, "Checking command palette\n");
        {
            AxynePaletteController *palette = value_field(view, "_palette");
            CHECK(palette != NULL && !palette->active);
            [view openPaletteWithInput:@""];
            [view layoutSubtreeIfNeeded];
            CHECK(palette->active && palette->mode == AXYNE_PALETTE_MODE_FILE);
            NSTextField *paletteField = field(view, "_paletteField");
            CHECK(paletteField != nil && [paletteField delegate] == (id)view);
            CHECK([field(view, "_searchButton") isHidden]);
            NSRect box = [field(view, "_searchButton") frame];
            CHECK(NSMinX([paletteField frame]) > NSMinX(box));
            CHECK(NSMaxX([paletteField frame]) < NSMaxX(box));
            CHECK(NSMinY([paletteField frame]) >= AXYNE_UI_MENU &&
                  NSMaxY([paletteField frame]) <= AXYNE_UI_MENU + AXYNE_UI_TOOLBAR);
            /* the workspace walk is incremental; finish it for the check */
            while (axyne_palette_ctl_walk_step(palette, 100000, NULL)) {}
            CHECK(palette->path_count == 40);
            [paletteField setStringValue:@"file07"];
            [view paletteSyncInput];
            CHECK(palette->mode == AXYNE_PALETTE_MODE_FILE && palette->list.count == 1);
            [paletteField setStringValue:@">save"];
            [view paletteSyncInput];
            CHECK(palette->mode == AXYNE_PALETTE_MODE_COMMAND && palette->list.count > 0);
            [paletteField setStringValue:@":7"];
            [view paletteSyncInput];
            CHECK(palette->mode == AXYNE_PALETTE_MODE_LINE && palette->line_valid);
            [view paletteDismiss];
            CHECK(!palette->active && field(view, "_paletteField") == nil);
            [view layoutSubtreeIfNeeded];
            CHECK(![field(view, "_searchButton") isHidden]);
        }

        /* A reload after external deletions clamps the old scroll position. */
        NSInteger *firstRow = value_field(view, "_explorerFirstRow");
        CHECK(firstRow != NULL);
        *firstRow = 20;
        for (int i = 5; i < 40; ++i) {
            NSString *path = [root stringByAppendingPathComponent:[NSString stringWithFormat:@"file%02d.c", i]];
            CHECK([[NSFileManager defaultManager] removeItemAtPath:path error:NULL]);
        }
        CHECK([view refreshExplorer]);
        [view layoutSubtreeIfNeeded];
        CHECK(*firstRow == 0);
        CHECK_ROW(NSMakePoint(80, top), 0);

        AxynePreferences *preferences = value_field(view, "_preferences");
        CHECK(preferences != NULL);
        preferences->theme.background = 0xffffff;
        preferences->theme.text = 0x24272d;
        [view applyPreferences];
        NSColor *customBackground = [[field(view, "_terminalOutput") backgroundColor]
            colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
        CHECK([customBackground redComponent] > 0.99 && [customBackground greenComponent] > 0.99);

        [view setFrameSize:NSMakeSize(800, 560)];
        [view setNeedsLayout:YES]; [view layoutSubtreeIfNeeded];
        NSButton *search = field(view, "_searchButton");
        CHECK([search isHidden] || NSMinX([search frame]) > NSMaxX([field(view, "_runButton") frame]));
        CHECK(NSMaxY([output frame]) <= 560 - AXYNE_UI_STATUS);
        CHECK(NSMinX([output frame]) >= AXYNE_UI_SIDEBAR);
        AxyneDocumentSet *documents = value_field(view, "_documents");
        CHECK(documents != NULL);
        for (int i = 0; i < 12; ++i)
            CHECK(axyne_documents_new(documents, NULL, NULL) == AXYNE_STATUS_OK);
        [view setNeedsLayout:YES]; [view layoutSubtreeIfNeeded];
        fprintf(stderr, "Checking tab strip scrolling\n");
        /* Index 0 is the hidden startup buffer; the first shown tab is 1. */
        CHECK(documents->count == 13);
        CHECK_EMPTY_RECT([view tabFrameAtIndex:0]);
        CHECK_X([view tabFrameAtIndex:1], <, AXYNE_UI_SIDEBAR);
        [view scrollTabsBy:-100000];
        CHECK_X([view tabFrameAtIndex:1], ==, AXYNE_UI_SIDEBAR);
        [view scrollTabsBy:100000];
        CHECK_MAX_X([view tabFrameAtIndex:documents->count - 1], 800);
        diagnosticView = nil;
        [view release];
        CHECK([[NSFileManager defaultManager] removeItemAtPath:root error:NULL]);
        method_setImplementation(alertMethod, originalAlert);
        puts("Toolbar actions/geometry, panel controls, scrolling viewport and resize bounds passed");
    }
    return 0;
}
