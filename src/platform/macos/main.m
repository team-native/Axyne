#import <AppKit/AppKit.h>

#include "axyne/app.h"

@interface AxyneApplicationDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@end

@implementation AxyneApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;

    NSRect frame = NSMakeRect(0, 0, 960, 640);
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled |
                   NSWindowStyleMaskClosable |
                   NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered
        defer:NO];
    [self.window center];
    [self.window setTitle:[NSString stringWithUTF8String:axyne_app_name()]];
    [self.window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

@end

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    AxyneApp app = {0};
    if (!axyne_app_initialize(&app)) {
        return 1;
    }

    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        AxyneApplicationDelegate *delegate =
            [[AxyneApplicationDelegate alloc] init];
        [application setDelegate:delegate];
        [application run];
    }

    axyne_app_shutdown(&app);
    return 0;
}
