#import <AppKit/AppKit.h>
#include "axyne/terminal.h"
@interface AxyneTerminalView : NSView <NSTextInputClient>
- (AxyneStatus)start:(const AxyneTerminalSpec *)spec error:(AxyneError *)error;
- (void)stop;
- (void)shutdown;
- (BOOL)running;
- (AxyneTerminal *)terminal;
- (void)setFont:(NSFont *)font foreground:(uint32_t)fg background:(uint32_t)bg;
- (void)copy:(id)sender;
- (void)paste:(id)sender;
- (void)selectAll:(id)sender;
@end
