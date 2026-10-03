#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "axyne/document.h"
#include "axyne/filesystem.h"
#include "axyne/preferences.h"
#include "Scintilla.h"

/* Reach the production adapter through Objective-C's runtime. No test API is
 * exported by the application and this executable never enters its UI loop. */
@interface NSView (AxyneEditorRuntimeTest)
- (void)loadScintillaView;
- (void)applyPreferences;
- (void)notification:(SCNotification *)notification;
- (BOOL)loadActiveDocument;
- (BOOL)selectDocumentAtIndex:(size_t)index;
- (NSRect)tabFrameAtIndex:(size_t)index;
- (BOOL)captureEditor;
- (void)openPath:(NSString *)path;
- (void)newDocument:(id)sender;
- (void)closeDocument:(id)sender;
- (BOOL)saveActiveToPath:(NSString *)path;
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSView *)content;
- (NSInteger)sendEditorMessage:(unsigned int)message wParam:(uintptr_t)wParam
                        lParam:(intptr_t)lParam;
@end

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

typedef NS_ENUM(int, LoadFault) {
    LoadFaultNone, LoadFaultCreate, LoadFaultShortInsert, LoadFaultInsertStatus,
    LoadFaultBindStatus
};
static LoadFault loadFault;
static size_t alertCount;
static int documentReferences;
typedef NSInteger (*EditorMessageIMP)(id, SEL, unsigned int, uintptr_t, intptr_t);
static EditorMessageIMP productionMessage;

static NSInteger test_message(id workspace, SEL selector, unsigned int message,
                                uintptr_t wParam, intptr_t lParam)
{
    if (loadFault == LoadFaultCreate && message == SCI_CREATEDOCUMENT) return 0;
    if (loadFault == LoadFaultShortInsert && message == SCI_ADDTEXT && wParam > 0)
        --wParam;
    NSInteger result = productionMessage(workspace, selector, message, wParam, lParam);
    if (message == SCI_CREATEDOCUMENT && result != 0) ++documentReferences;
    if (message == SCI_ADDREFDOCUMENT) ++documentReferences;
    if (message == SCI_RELEASEDOCUMENT) --documentReferences;
    if (loadFault == LoadFaultInsertStatus && message == SCI_ADDTEXT)
        productionMessage(workspace, selector, SCI_SETSTATUS, SC_STATUS_BADALLOC, 0);
    if (loadFault == LoadFaultBindStatus && message == SCI_SETDOCPOINTER) {
        // Scintilla may install the pointer before allocating its line state.
        // The resulting pointer equality must not hide the allocation error.
        loadFault = LoadFaultNone;
        productionMessage(workspace, selector, SCI_SETSTATUS, SC_STATUS_BADALLOC, 0);
    }
    return result;
}

static NSModalResponse test_alert(id alert, SEL selector)
{
    (void)selector;
    ++alertCount;
    fprintf(stderr, "Expected native alert: %s\n", [[alert messageText] UTF8String]);
    return NSAlertThirdButtonReturn;
}

static AxyneDocumentSet *document_set(NSView *workspace)
{
    Ivar ivar = class_getInstanceVariable([workspace class], "_documents");
    return (AxyneDocumentSet *)((char *)workspace + ivar_getOffset(ivar));
}

static AxynePreferences *preferences_for(NSView *workspace)
{
    Ivar ivar = class_getInstanceVariable([workspace class], "_preferences");
    return (AxynePreferences *)((char *)workspace + ivar_getOffset(ivar));
}

static NSInteger editor_message(NSView *workspace, unsigned int message,
                                 uintptr_t wParam, intptr_t lParam)
{
    return [workspace sendEditorMessage:message wParam:wParam lParam:lParam];
}

static int editor_equals(NSView *workspace, const char *bytes, size_t length)
{
    if (editor_message(workspace, SCI_GETTEXTLENGTH, 0, 0) != (NSInteger)length)
        return 0;
    char *actual = malloc(length + 1);
    if (actual == NULL) return 0;
    editor_message(workspace, SCI_GETTEXT, length + 1, (intptr_t)actual);
    int equal = memcmp(actual, bytes, length) == 0;
    free(actual);
    return equal;
}

static int file_equals(NSString *path, const char *bytes, size_t length)
{
    NSData *data = [NSData dataWithContentsOfFile:path];
    return data != nil && [data length] == length &&
        memcmp([data bytes], bytes, length) == 0;
}

static int run_tests(NSString *pngPath)
{
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        Class workspaceClass = NSClassFromString(@"AxyneWorkspaceView");
        CHECK(workspaceClass != Nil);

        Method messageMethod = class_getInstanceMethod(workspaceClass,
            @selector(sendEditorMessage:wParam:lParam:));
        productionMessage = (EditorMessageIMP)method_getImplementation(messageMethod);
        Class testClass = objc_allocateClassPair(workspaceClass,
            "AxyneEditorRuntimeFaultView", 0);
        CHECK(testClass != Nil);
        CHECK(class_addMethod(testClass, @selector(sendEditorMessage:wParam:lParam:),
            (IMP)test_message, method_getTypeEncoding(messageMethod)));
        objc_registerClassPair(testClass);
        Method alertMethod = class_getInstanceMethod([NSAlert class], @selector(runModal));
        IMP originalAlert = method_setImplementation(alertMethod, (IMP)test_alert);

        NSView *workspace = [[testClass alloc] initWithFrame:NSMakeRect(0, 0, 1200, 800)];
        CHECK(workspace != nil);
        [workspace loadScintillaView];
        Ivar editorIvar = class_getInstanceVariable(workspaceClass, "_editorView");
        CHECK(editorIvar != NULL);
        id editor = object_getIvar(workspace, editorIvar);
        CHECK(editor != nil);
        CHECK([editor isKindOfClass:NSClassFromString(@"ScintillaView")]);
        CHECK([workspace sendEditorMessage:SCI_GETDOCPOINTER wParam:0 lParam:0] != 0);

        Ivar bundleIvar = class_getInstanceVariable(workspaceClass, "_scintillaBundle");
        NSBundle *bundle = object_getIvar(workspace, bundleIvar);
        CHECK([bundle isLoaded]);
        CHECK([[bundle bundlePath] isEqualToString:[[[NSBundle mainBundle]
            privateFrameworksPath] stringByAppendingPathComponent:@"Scintilla.framework"]]);

        Ivar lexillaIvar = class_getInstanceVariable(workspaceClass, "_lexillaModule");
        void *lexilla = *(void **)((char *)workspace + ivar_getOffset(lexillaIvar));
        CHECK(lexilla != NULL);
        CHECK(dlsym(lexilla, "CreateLexer") != NULL);
        printf("Loaded native Scintilla: %s\n", [[bundle executablePath] fileSystemRepresentation]);
        printf("Loaded bundled Lexilla and CreateLexer\n");
        AxynePreferences *preferences = preferences_for(workspace);
        preferences->editor.tab_width = 6;
        preferences->editor.insert_spaces = 1;
        [workspace applyPreferences];

        // Load bytes on the very first bind, before the view moves to a window.
        // Includes UTF-8, CRLF and embedded NUL so NSString/strlen conversion
        // cannot silently truncate or normalize the file's buffer.
        const char first[] = "first \xed\x95\x9c\xea\xb8\x80\r\nembedded\0tail\n";
        AxyneDocumentSet *documents = document_set(workspace);
        CHECK(axyne_documents_set_contents(documents, 0, first, sizeof(first) - 1, NULL)
            == AXYNE_STATUS_OK);
        CHECK(axyne_documents_mark_clean(documents, 0, NULL) == AXYNE_STATUS_OK);
        CHECK([workspace loadActiveDocument]);
        CHECK(editor_message(workspace, SCI_GETINDENT, 0, 0) == 6);
        CHECK(editor_message(workspace, SCI_GETTABWIDTH, 0, 0) == 6);
        CHECK(editor_message(workspace, SCI_GETUSETABS, 0, 0) == 0);
        CHECK(editor_equals(workspace, first, sizeof(first) - 1));
        CHECK(documents->documents[0].owns_native_editor_document);
        CHECK(editor_message(workspace, SCI_GETCODEPAGE, 0, 0) == SC_CP_UTF8);
        CHECK(editor_message(workspace, SCI_GETMODIFY, 0, 0) == 0);
        CHECK(editor_message(workspace, SCI_CANUNDO, 0, 0) == 0);

        // An unshown native window exercises keyboard focus without driving
        // the desktop, activating Axyne, or using GUI automation.
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1200, 800)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];
        [window setContentView:workspace];
        CHECK([window firstResponder] == [editor content]);

        NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:
            [[NSUUID UUID] UUIDString]];
        CHECK([[NSFileManager defaultManager] createDirectoryAtPath:root
            withIntermediateDirectories:NO attributes:nil error:NULL]);
        NSString *firstPath = [root stringByAppendingPathComponent:@"first.txt"];
        NSString *secondPath = [root stringByAppendingPathComponent:@"두 번째.c"];
        NSString *emptyPath = [root stringByAppendingPathComponent:@"empty.txt"];
        CHECK(axyne_fs_write_file([firstPath fileSystemRepresentation], first,
            sizeof(first) - 1, NULL) == AXYNE_STATUS_OK);
        const char second[] = "int main(void) { return 42; }\r\n";
        CHECK(axyne_fs_write_file([secondPath fileSystemRepresentation], second,
            sizeof(second) - 1, NULL) == AXYNE_STATUS_OK);
        CHECK(axyne_fs_write_file([emptyPath fileSystemRepresentation], "", 0, NULL)
            == AXYNE_STATUS_OK);

        // Closing the initial document must create an independently owned
        // replacement. Opening real files then exercises the production path.
        loadFault = LoadFaultCreate;
        [workspace closeDocument:nil];
        loadFault = LoadFaultNone;
        CHECK(documents->count == 1 && documents->active_index == 0);
        CHECK(editor_equals(workspace, first, sizeof(first) - 1));
        [workspace closeDocument:nil];
        CHECK(documents->count == 1 && documents->documents[0].is_untitled);
        CHECK(documents->documents[0].owns_native_editor_document);
        CHECK(editor_equals(workspace, "", 0));
        // The replacement is an untouched placeholder: no tab, and closing it
        // is a no-op that keeps the same buffer.
        CHECK(axyne_document_tab_hidden(&documents->documents[0]));
        CHECK(NSIsEmptyRect([workspace tabFrameAtIndex:0]));
        {
            void *placeholderBuffer = documents->documents[0].native_editor_document;
            [workspace closeDocument:nil];
            CHECK(documents->count == 1 &&
                  documents->documents[0].native_editor_document == placeholderBuffer);
        }
        CHECK(editor_message(workspace, SCI_GETINDENT, 0, 0) == 6);
        CHECK(editor_message(workspace, SCI_GETUSETABS, 0, 0) == 0);
        [workspace openPath:firstPath];
        CHECK(documents->count == 2 && documents->active_index == 1);
        CHECK(editor_equals(workspace, first, sizeof(first) - 1));
        CHECK(documents->documents[1].length == sizeof(first) - 1);
        CHECK(!documents->documents[1].is_dirty);
        CHECK(editor_message(workspace, SCI_CANUNDO, 0, 0) == 0);
        CHECK(editor_message(workspace, SCI_GETINDENT, 0, 0) == 6);
        CHECK(editor_message(workspace, SCI_GETTABWIDTH, 0, 0) == 6);
        [workspace openPath:secondPath];
        CHECK(documents->count == 3 && documents->active_index == 2);
        CHECK(editor_equals(workspace, second, sizeof(second) - 1));
        CHECK(editor_message(workspace, SCI_GETLEXER, 0, 0) != 0);
        CHECK([window firstResponder] == [editor content]);

        // Keep edits and undo history in the first tab across repeated switches.
        CHECK([workspace selectDocumentAtIndex:1]);
        editor_message(workspace, SCI_GOTOPOS, sizeof(first) - 1, 0);
        editor_message(workspace, SCI_ADDTEXT, 4, (intptr_t)"edit");
        CHECK([workspace captureEditor]);
        CHECK(documents->documents[1].is_dirty);
        CHECK(documents->documents[1].length == sizeof(first) - 1 + 4);
        CHECK(memcmp(documents->documents[1].contents + sizeof(first) - 1, "edit", 4) == 0);
        for (int iteration = 0; iteration < 20; ++iteration) {
            CHECK([workspace selectDocumentAtIndex:0]);
            CHECK(editor_equals(workspace, "", 0));
            CHECK([workspace selectDocumentAtIndex:2]);
            CHECK(editor_equals(workspace, second, sizeof(second) - 1));
            CHECK([workspace selectDocumentAtIndex:1]);
            CHECK(editor_message(workspace, SCI_GETTEXTLENGTH, 0, 0) == sizeof(first) - 1 + 4);
            CHECK(editor_message(workspace, SCI_CANUNDO, 0, 0) != 0);
        }
        editor_message(workspace, SCI_UNDO, 0, 0);
        CHECK(editor_equals(workspace, first, sizeof(first) - 1));
        CHECK(editor_message(workspace, SCI_GETMODIFY, 0, 0) == 0);
        CHECK(!documents->documents[1].is_dirty);
        CHECK([workspace saveActiveToPath:firstPath]);
        CHECK(file_equals(firstPath, first, sizeof(first) - 1));
        editor_message(workspace, SCI_REDO, 0, 0);
        CHECK(documents->documents[1].is_dirty);
        CHECK([workspace saveActiveToPath:firstPath]);
        const char edited[] = "first \xed\x95\x9c\xea\xb8\x80\r\nembedded\0tail\nedit";
        CHECK(file_equals(firstPath, edited, sizeof(edited) - 1));

        // Reopening an already-open path selects its existing native buffer.
        [workspace openPath:secondPath];
        [workspace openPath:firstPath];
        CHECK(documents->count == 3 && documents->active_index == 1);
        CHECK(editor_equals(workspace, edited, sizeof(edited) - 1));
        [workspace closeDocument:nil];
        CHECK(documents->count == 2);
        [workspace openPath:firstPath];
        CHECK(documents->count == 3);
        CHECK(editor_equals(workspace, edited, sizeof(edited) - 1));

        // All load failures preserve the old active tab and displayed bytes.
        size_t previousIndex = documents->active_index;
        intptr_t previousPointer = editor_message(workspace, SCI_GETDOCPOINTER, 0, 0);
        size_t previousAlerts = alertCount;
        [workspace openPath:[root stringByAppendingPathComponent:@"missing.txt"]];
        CHECK(alertCount == previousAlerts + 1);
        CHECK(documents->count == 3 && documents->active_index == previousIndex);
        NSString *faultPath = [root stringByAppendingPathComponent:@"fault.txt"];
        CHECK(axyne_fs_write_file([faultPath fileSystemRepresentation], first,
            sizeof(first) - 1, NULL) == AXYNE_STATUS_OK);
        for (LoadFault fault = LoadFaultCreate; fault <= LoadFaultInsertStatus; ++fault) {
            loadFault = fault;
            previousAlerts = alertCount;
            [workspace openPath:faultPath];
            loadFault = LoadFaultNone;
            CHECK(alertCount == previousAlerts + 1);
            CHECK(documents->count == 3 && documents->active_index == previousIndex);
            CHECK(editor_message(workspace, SCI_GETDOCPOINTER, 0, 0) == previousPointer);
            CHECK(editor_equals(workspace, edited, sizeof(edited) - 1));
            CHECK(documentReferences == (int)documents->count);
        }
        loadFault = LoadFaultCreate;
        [workspace newDocument:nil];
        loadFault = LoadFaultNone;
        CHECK(documents->count == 3 && documents->active_index == previousIndex);
        CHECK(editor_equals(workspace, edited, sizeof(edited) - 1));
        [workspace openPath:emptyPath];
        CHECK(documents->count == 4 && editor_equals(workspace, "", 0));
        CHECK(!documents->documents[documents->active_index].is_dirty);
        CHECK(documentReferences == (int)documents->count);

        previousIndex = documents->active_index;
        previousPointer = editor_message(workspace, SCI_GETDOCPOINTER, 0, 0);
        loadFault = LoadFaultBindStatus;
        CHECK(![workspace selectDocumentAtIndex:1]);
        loadFault = LoadFaultNone;
        CHECK(documents->active_index == previousIndex);
        CHECK(editor_message(workspace, SCI_GETDOCPOINTER, 0, 0) == previousPointer);
        CHECK(editor_equals(workspace, "", 0));
        CHECK(documentReferences == (int)documents->count);
        editor_message(workspace, SCI_SETSTATUS, SC_STATUS_BADALLOC, 0);
        CHECK([workspace selectDocumentAtIndex:1]);
        CHECK(editor_equals(workspace, second, sizeof(second) - 1));
        CHECK(editor_message(workspace, SCI_GETSTATUS, 0, 0) == SC_STATUS_OK);

        // Opening before AppKit attaches the view must also load the saved
        // bytes, then retain that first native document across tab switches.
        NSView *earlyWorkspace = [[testClass alloc] initWithFrame:NSZeroRect];
        CHECK(earlyWorkspace != nil);
        [earlyWorkspace openPath:firstPath];
        AxyneDocumentSet *earlyDocuments = document_set(earlyWorkspace);
        CHECK(earlyDocuments->count == 2 && earlyDocuments->active_index == 1);
        CHECK(editor_equals(earlyWorkspace, edited, sizeof(edited) - 1));
        CHECK(earlyDocuments->documents[1].owns_native_editor_document);
        intptr_t earlyPointer = editor_message(earlyWorkspace, SCI_GETDOCPOINTER, 0, 0);
        int earlyReferences = documentReferences;
        for (int faultIndex = 0; faultIndex < 2; ++faultIndex) {
            loadFault = faultIndex == 0 ? LoadFaultCreate : LoadFaultBindStatus;
            [earlyWorkspace closeDocument:nil];
            loadFault = LoadFaultNone;
            CHECK(earlyDocuments->count == 2 && earlyDocuments->active_index == 1);
            CHECK(earlyDocuments->documents[0].native_editor_document == NULL);
            CHECK(editor_message(earlyWorkspace, SCI_GETDOCPOINTER, 0, 0) == earlyPointer);
            CHECK(editor_equals(earlyWorkspace, edited, sizeof(edited) - 1));
            CHECK(documentReferences == earlyReferences);
        }
        loadFault = LoadFaultCreate;
        CHECK(![earlyWorkspace selectDocumentAtIndex:0]);
        loadFault = LoadFaultNone;
        CHECK(earlyDocuments->active_index == 1);
        CHECK(earlyDocuments->documents[0].native_editor_document == NULL);
        CHECK(editor_equals(earlyWorkspace, edited, sizeof(edited) - 1));
        CHECK([earlyWorkspace selectDocumentAtIndex:0]);
        CHECK(editor_equals(earlyWorkspace, "", 0));
        CHECK([earlyWorkspace selectDocumentAtIndex:1]);
        CHECK(editor_equals(earlyWorkspace, edited, sizeof(edited) - 1));
        [earlyWorkspace closeDocument:nil];
        [earlyWorkspace openPath:firstPath];
        CHECK(editor_equals(earlyWorkspace, edited, sizeof(edited) - 1));
        [earlyWorkspace release];

        // Actual indentation must use the effective profile on fresh buffers.
        NSView *indentWorkspace = [[testClass alloc] initWithFrame:NSZeroRect];
        CHECK(indentWorkspace != nil);
        AxynePreferences *indentPreferences = preferences_for(indentWorkspace);
        indentPreferences->editor.tab_width = 6;
        indentPreferences->editor.insert_spaces = 1;
        [indentWorkspace loadScintillaView];
        [indentWorkspace applyPreferences];
        CHECK([indentWorkspace loadActiveDocument]);
        [indentWorkspace newDocument:nil];
        CHECK(document_set(indentWorkspace)->count == 2);
        CHECK(editor_message(indentWorkspace, SCI_GETINDENT, 0, 0) == 6);
        CHECK(editor_message(indentWorkspace, SCI_GETTABWIDTH, 0, 0) == 6);
        CHECK(editor_message(indentWorkspace, SCI_GETUSETABS, 0, 0) == 0);
        const char line[] = "if (1) {\n";
        editor_message(indentWorkspace, SCI_ADDTEXT, sizeof(line) - 1, (intptr_t)line);
        SCNotification newline = {0};
        newline.nmhdr.code = SCN_CHARADDED;
        newline.ch = '\n';
        newline.position = sizeof(line) - 2;
        [indentWorkspace notification:&newline];
        const char indented[] = "if (1) {\n      ";
        CHECK(editor_equals(indentWorkspace, indented, sizeof(indented) - 1));
        CHECK([indentWorkspace captureEditor]);
        CHECK([indentWorkspace selectDocumentAtIndex:0]);
        CHECK([indentWorkspace selectDocumentAtIndex:1]);
        CHECK(editor_equals(indentWorkspace, indented, sizeof(indented) - 1));
        indentPreferences->editor.tab_width = 3;
        indentPreferences->editor.insert_spaces = 0;
        [indentWorkspace applyPreferences];
        CHECK([indentWorkspace selectDocumentAtIndex:0]);
        CHECK(editor_message(indentWorkspace, SCI_GETINDENT, 0, 0) == 3);
        CHECK(editor_message(indentWorkspace, SCI_GETTABWIDTH, 0, 0) == 3);
        CHECK(editor_message(indentWorkspace, SCI_GETUSETABS, 0, 0) == 1);
        [indentWorkspace release];

        if (pngPath != nil) {
            CHECK([workspace selectDocumentAtIndex:1]);
            [workspace setNeedsLayout:YES];
            [workspace layoutSubtreeIfNeeded];
            NSBitmapImageRep *bitmap = [workspace bitmapImageRepForCachingDisplayInRect:
                [workspace bounds]];
            CHECK(bitmap != nil);
            [workspace cacheDisplayInRect:[workspace bounds] toBitmapImageRep:bitmap];
            NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG
                properties:@{}];
            CHECK(png != nil && [png writeToFile:pngPath atomically:YES]);
            printf("Workspace PNG: %s (%ld x %ld)\n", [pngPath fileSystemRepresentation],
                (long)[bitmap pixelsWide], (long)[bitmap pixelsHigh]);
        }
        [window setContentView:nil];
        [workspace release];
        [window release];
        method_setImplementation(alertMethod, originalAlert);
        CHECK([[NSFileManager defaultManager] removeItemAtPath:root error:NULL]);
        printf("Exact-byte open/save, initial buffer lifetime, tab switching/undo, close/reopen, UTF-8, focus and failed-load rollback passed\n");
    return EXIT_SUCCESS;
}

int main(int argc, const char *argv[])
{
    if (argc > 2) {
        fprintf(stderr, "usage: %s [output.png]\n", argv[0]);
        return EXIT_FAILURE;
    }
    int result;
    @autoreleasepool {
        NSString *pngPath = argc == 2 ? [NSString stringWithUTF8String:argv[1]] : nil;
        result = run_tests(pngPath);
    }
    CHECK(documentReferences == 0);
    return result;
}
