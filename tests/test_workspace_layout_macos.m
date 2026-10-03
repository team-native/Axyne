#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include "axyne/explorer.h"
#include "axyne/ui_design.h"
#include "axyne/preferences.h"
#include "axyne/document.h"

@interface NSView (AxyneWorkspaceLayoutTest)
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSInteger)explorerNodeAtPoint:(NSPoint)point;
- (BOOL)refreshExplorer;
- (void)applyPreferences;
- (void)scrollTabsBy:(CGFloat)delta;
- (NSRect)tabFrameAtIndex:(size_t)index;
- (NSRect)menuBarItemRect:(NSUInteger)index;
- (NSInteger)menuBarIndexAtPoint:(NSPoint)point;
@end

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); return 1; \
} } while (0)

static id field(id view, const char *name)
{
    Ivar ivar = class_getInstanceVariable([view class], name);
    return ivar == NULL ? nil : object_getIvar(view, ivar);
}

/* Never let a native alert block the unattended regression suite. */
static NSModalResponse fail_alert(id alert, SEL selector)
{
    (void)selector;
    fprintf(stderr, "Unexpected alert: %s: %s\n", [[alert messageText] UTF8String],
        [[alert informativeText] UTF8String]);
    return NSAlertThirdButtonReturn;
}

static void *value_field(id view, const char *name)
{
    Ivar ivar = class_getInstanceVariable([view class], name);
    return ivar == NULL ? NULL : (char *)view + ivar_getOffset(ivar);
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
        CHECK([view explorerNodeAtPoint:NSMakePoint(80, top)] == 0);
        CHECK([view explorerNodeAtPoint:NSMakePoint(80, top + AXYNE_UI_ROW - 1)] == 0);
        CHECK([view explorerNodeAtPoint:NSMakePoint(80, top + AXYNE_UI_ROW)] == 1);
        CHECK([view explorerNodeAtPoint:NSMakePoint(AXYNE_UI_SIDEBAR, top)] == NSNotFound);
        CHECK([view explorerNodeAtPoint:NSMakePoint(80, top - 1)] == NSNotFound);
        CHECK([view explorerNodeAtPoint:NSMakePoint(80, bottom)] == NSNotFound);
        CHECK([view explorerNodeAtPoint:NSMakePoint(80, 842 - AXYNE_UI_STATUS)] == NSNotFound);

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
        CHECK([view explorerNodeAtPoint:NSMakePoint(80, top)] == 0);

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
        /* Index 0 is the hidden startup buffer; the first shown tab is 1. */
        CHECK(NSIsEmptyRect([view tabFrameAtIndex:0]));
        CHECK(NSMinX([view tabFrameAtIndex:1]) < AXYNE_UI_SIDEBAR);
        [view scrollTabsBy:-100000];
        CHECK(NSMinX([view tabFrameAtIndex:1]) == AXYNE_UI_SIDEBAR);
        [view scrollTabsBy:100000];
        CHECK(NSMaxX([view tabFrameAtIndex:documents->count - 1]) <= 800);
        [view release];
        CHECK([[NSFileManager defaultManager] removeItemAtPath:root error:NULL]);
        method_setImplementation(alertMethod, originalAlert);
        puts("Toolbar actions/geometry, panel controls, scrolling viewport and resize bounds passed");
    }
    return 0;
}
