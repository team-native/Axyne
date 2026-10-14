#import <AppKit/AppKit.h>
#import "../src/features/ui/platform/macos/terminal_view_macos.h"
#include <assert.h>
#include <string.h>
static void pump(void) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]]; }
static BOOL contains(AxyneTerminal *terminal, const char *text) {
    AxyneTerminalSnapshot s={0}; if(axyne_terminal_snapshot(terminal,&s,NULL)!=AXYNE_STATUS_OK)return NO;
    NSMutableString *result=[NSMutableString string];
    for(int i=0;i<s.rows*s.columns;++i) {uint32_t c=s.cells[i].chars[0];if(c&&c<128)[result appendFormat:@"%c",(char)c];else[result appendString:@" "];}
    BOOL found=[result containsString:[NSString stringWithUTF8String:text]];axyne_terminal_snapshot_free(&s);return found;
}
int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,800,400) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];
        AxyneTerminalView *grid=[[AxyneTerminalView alloc] initWithFrame:NSMakeRect(0,0,800,400)]; [window setContentView:grid];
        assert(![grid isKindOfClass:[NSTextField class]] && ![grid isKindOfClass:[NSTextView class]]);
        assert([window makeFirstResponder:grid]);
        AxyneTerminal *terminal=[grid terminal];
        const char *sample="\033[31mANSI\033[0m \xe4\xb8\xad";axyne_terminal_feed(terminal,sample,strlen(sample));
        NSBitmapImageRep *bitmap=[grid bitmapImageRepForCachingDisplayInRect:[grid bounds]]; [grid cacheDisplayInRect:[grid bounds] toBitmapImageRep:bitmap];
        assert([bitmap pixelsWide]>0);
        AxyneTerminalSnapshot s={0};assert(axyne_terminal_snapshot(terminal,&s,NULL)==AXYNE_STATUS_OK);assert(s.cells[0].chars[0]=='A'&&s.cells[5].chars[0]==0x4e2d);axyne_terminal_snapshot_free(&s);
        axyne_terminal_clear(terminal);
        const char *args[]={"-f","-i"};AxyneTerminalSpec spec={0};spec.executable="/bin/zsh";spec.arguments=args;spec.argument_count=2;spec.working_directory="/tmp";
        assert([grid start:&spec error:NULL]==AXYNE_STATUS_OK);
        for(int i=0;i<20;++i)pump();
        [grid setMarkedText:@"中" selectedRange:NSMakeRange(1,0) replacementRange:NSMakeRange(NSNotFound,0)];assert([grid hasMarkedText]);[grid unmarkText];assert(![grid hasMarkedText]);
        [grid insertText:@"printf '%s%s\\n' NATIVE_ INPUT" replacementRange:NSMakeRange(NSNotFound,0)];
        NSEvent *enter=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:[window windowNumber] context:nil characters:@"\r" charactersIgnoringModifiers:@"\r" isARepeat:NO keyCode:36];
        [grid keyDown:enter];for(int i=0;i<100&&!contains(terminal,"NATIVE_INPUT");++i)pump();assert(contains(terminal,"NATIVE_INPUT"));
        [grid insertText:@"sleep 20" replacementRange:NSMakeRange(NSNotFound,0)];[grid keyDown:enter];for(int i=0;i<10;++i)pump();
        NSEvent *ctrlC=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagControl timestamp:0 windowNumber:[window windowNumber] context:nil characters:@"\003" charactersIgnoringModifiers:@"c" isARepeat:NO keyCode:8];
        assert([grid performKeyEquivalent:ctrlC]);
        [grid insertText:@"printf '%s%s\\n' NATIVE_ INTERRUPT" replacementRange:NSMakeRange(NSNotFound,0)];[grid keyDown:enter];for(int i=0;i<100&&!contains(terminal,"NATIVE_INTERRUPT");++i)pump();assert(contains(terminal,"NATIVE_INTERRUPT"));
        [grid setFrameSize:NSMakeSize(640,320)];assert([grid terminal]==terminal&&[grid running]);
        [grid shutdown];[window setContentView:nil];[grid release];[window close];[window release];for(int i=0;i<10;++i)pump();
        puts("Native terminal grid/render/keyboard tests passed");
    }return 0;
}
