#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include "axyne/explorer.h"
#include "axyne/ui_design.h"
#include "axyne/preferences.h"
#include "axyne/document.h"
#include "axyne/problems.h"
#include "axyne/lsp.h"

@interface NSView (AxyneWorkspaceLayoutTest)
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSInteger)explorerNodeAtPoint:(NSPoint)point;
- (BOOL)refreshExplorer;
- (void)applyPreferences;
- (void)scrollTabsBy:(CGFloat)delta;
- (NSRect)tabFrameAtIndex:(size_t)index;
- (void)refreshProblems;
- (void)beginBuildProblems;
- (void)feedBuildProblems:(const char *)bytes length:(size_t)length stream:(int)stream;
- (void)finishBuildProblems;
- (void)applyLspDiagnostics:(const AxyneLspDiagnostic *)diagnostics
                      count:(size_t)count path:(const char *)path;
- (void)problemsView:(id)problemsView activateRowAtIndex:(size_t)index;
- (void)problemsViewFilterChanged:(id)problemsView;
- (size_t)rowCount;
- (NSInteger)selectedRowIndex;
- (void)moveSelectionBy:(NSInteger)delta;
- (NSInteger)rowIndexAtPoint:(NSPoint)point;
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
            CHECK(NSMinY(frame) >= 0 && NSMaxY(frame) <= AXYNE_UI_TOOLBAR);
            CHECK(NSMinX(frame) >= NSMaxX(previous) && NSMaxX(frame) <= 1440);
            previous = frame;
        }
        CHECK(![field(view, "_searchButton") isHidden]);
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
        NSView *problemsView = field(view, "_problemsView");
        CHECK(problemsView != nil);
        CHECK([output isHidden] && ![problemsView isHidden]);
        CHECK([field(view, "_terminalInput") isHidden] && [field(view, "_terminalSend") isHidden]);
        {
            /* The list fills the 198px area below the 32px panel header. */
            NSRect list = [problemsView frame];
            CGFloat panelTop = 842 - AXYNE_UI_STATUS - AXYNE_UI_PANEL;
            CHECK(NSMinX(list) == AXYNE_UI_SIDEBAR && NSMaxX(list) == 1440);
            CHECK(NSMinY(list) == panelTop + AXYNE_UI_PANEL_HEADER);
            CHECK(NSMaxY(list) == 842 - AXYNE_UI_STATUS);
        }
        CHECK([[field(view, "_problemsTab") title] isEqualToString:@"문제"]);
        CHECK([problemsView rowCount] == 0);
        fprintf(stderr, "Checking problems panel\n");
        {
            AxyneProblemList *problems = value_field(view, "_problems");
            AxyneProblem item = {0};
            CHECK(problems != NULL);
            item.severity = AXYNE_PROBLEM_ERROR; item.line = 40; item.column = 32;
            item.path = (char *)"/tmp/axyne-problems/main.c"; item.message = (char *)"error one";
            CHECK(axyne_problems_append(problems, AXYNE_PROBLEM_ORIGIN_BUILD, &item, NULL) == AXYNE_STATUS_OK);
            item.severity = AXYNE_PROBLEM_WARNING; item.line = 27; item.column = 17;
            item.message = (char *)"warning one";
            CHECK(axyne_problems_append(problems, AXYNE_PROBLEM_ORIGIN_BUILD, &item, NULL) == AXYNE_STATUS_OK);
            item.severity = AXYNE_PROBLEM_INFORMATION; item.line = 8; item.column = 1;
            item.path = (char *)"/tmp/axyne-problems/CMakeLists.txt"; item.message = (char *)"info one";
            CHECK(axyne_problems_append(problems, AXYNE_PROBLEM_ORIGIN_BUILD, &item, NULL) == AXYNE_STATUS_OK);
            [view refreshProblems];
            [view layoutSubtreeIfNeeded];
            CHECK([[field(view, "_problemsTab") title] isEqualToString:@"문제  3"]);
            CHECK([field(problemsView, "_summary") isEqualToString:@"오류 1개 · 경고 1개 · 정보 1개"]);
            /* No active file: two groups (main.c is first, it holds the error): 2 groups + 3 problems. */
            CHECK([problemsView rowCount] == 5);
            CHECK([problemsView rowIndexAtPoint:NSMakePoint(100, 36 + 30 + 1)] == 1);
            CHECK([problemsView rowIndexAtPoint:NSMakePoint(100, 10)] == -1);
            CHECK([problemsView rowIndexAtPoint:NSMakePoint(100, 36 + 30 * 5)] == -1);
            /* The widened tab label keeps the tabs from overlapping. */
            CHECK(NSMaxX([field(view, "_problemsTab") frame]) <= NSMinX([field(view, "_terminalTab") frame]));
            /* Keyboard selection clamps at both ends. */
            [problemsView moveSelectionBy:-1];
            CHECK([problemsView selectedRowIndex] == 4);
            [problemsView moveSelectionBy:1];
            CHECK([problemsView selectedRowIndex] == 4);
            for (int i = 0; i < 10; ++i) [problemsView moveSelectionBy:-1];
            CHECK([problemsView selectedRowIndex] == 0);
            /* Activating a group row collapses it and back. */
            [view problemsView:problemsView activateRowAtIndex:0];
            CHECK([problemsView rowCount] == 3);
            [view problemsView:problemsView activateRowAtIndex:0];
            CHECK([problemsView rowCount] == 5);
            /* The filter narrows by path, message or code. */
            NSTextField *filter = field(problemsView, "_filter");
            CHECK(filter != nil);
            [filter setStringValue:@"cmake"];
            [view problemsViewFilterChanged:problemsView];
            CHECK([problemsView rowCount] == 2);
            [filter setStringValue:@""];
            [view problemsViewFilterChanged:problemsView];
            CHECK([problemsView rowCount] == 5);
            axyne_problems_clear_all(problems);
            [view refreshProblems];
            CHECK([problemsView rowCount] == 0);
            CHECK([[field(view, "_problemsTab") title] isEqualToString:@"문제"]);

            /* Streamed build output becomes BUILD problems and never removes LSP ones. */
            AxyneLspDiagnostic diagnostic = {0};
            diagnostic.severity = AXYNE_LSP_DIAGNOSTIC_ERROR;
            diagnostic.range.start.line = 2; diagnostic.range.start.character = 4;
            diagnostic.message = (char *)"lsp problem";
            [view applyLspDiagnostics:&diagnostic count:1 path:"/tmp/axyne-problems/lsp.c"];
            CHECK(problems->count == 1 && problems->items[0].origin == AXYNE_PROBLEM_ORIGIN_LSP);
            [view beginBuildProblems];
            const char *chunk1 = "[build]\nsrc/a.c:3:5: err";
            const char *chunk2 = "or: broken\n";
            [view feedBuildProblems:chunk1 length:strlen(chunk1) stream:0];
            [view feedBuildProblems:chunk2 length:strlen(chunk2) stream:0];
            [view finishBuildProblems];
            CHECK(problems->count == 2);
            CHECK(problems->items[1].origin == AXYNE_PROBLEM_ORIGIN_BUILD && problems->items[1].line == 3);
            [view beginBuildProblems];
            CHECK(problems->count == 1);
            [view finishBuildProblems];
            axyne_problems_clear_all(problems);
            [view refreshProblems];
        }
        [field(view, "_outputTab") performClick:nil];
        [view layoutSubtreeIfNeeded];
        CHECK(![output isHidden] && [field(view, "_terminalInput") isHidden]);
        CHECK([[field(view, "_problemsView") isKindOfClass:[NSView class]] &&
               [field(view, "_problemsView") isHidden]);
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
        CGFloat top = AXYNE_UI_TOOLBAR + AXYNE_UI_TABS + AXYNE_UI_EXPLORER_HEADER;
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
        CHECK(NSMinX([view tabFrameAtIndex:0]) < AXYNE_UI_SIDEBAR);
        [view scrollTabsBy:-100000];
        CHECK(NSMinX([view tabFrameAtIndex:0]) == AXYNE_UI_SIDEBAR);
        [view scrollTabsBy:100000];
        CHECK(NSMaxX([view tabFrameAtIndex:documents->count - 1]) <= 800);
        [view release];
        CHECK([[NSFileManager defaultManager] removeItemAtPath:root error:NULL]);
        method_setImplementation(alertMethod, originalAlert);
        puts("Toolbar actions/geometry, panel controls, problems panel, scrolling viewport and resize bounds passed");
    }
    return 0;
}
