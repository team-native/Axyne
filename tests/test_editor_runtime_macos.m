#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "Scintilla.h"

/* Reach the production adapter through Objective-C's runtime. No test API is
 * exported by the application and this executable never enters its UI loop. */
@interface NSView (AxyneEditorRuntimeTest)
- (void)loadScintillaView;
- (NSInteger)sendEditorMessage:(unsigned int)message wParam:(uintptr_t)wParam
                        lParam:(intptr_t)lParam;
@end

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        Class workspaceClass = NSClassFromString(@"AxyneWorkspaceView");
        CHECK(workspaceClass != Nil);
        NSView *workspace = [[workspaceClass alloc] initWithFrame:NSZeroRect];
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
        [workspace release];
    }
    return EXIT_SUCCESS;
}
