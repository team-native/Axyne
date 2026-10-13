#import "terminal_view_macos.h"
#include <stdatomic.h>
@class AxyneTerminalView;
@interface AxyneTerminalLife : NSObject {
@public AxyneTerminalView *view; // Main-thread-only weak target; blocks own the box.
    atomic_int pending;
}
@end
@implementation AxyneTerminalLife
@end
@interface AxyneTerminalView () {
    AxyneTerminal *_terminal;
    AxyneTerminalLife *_life;
    NSFont *_font;
    NSMutableAttributedString *_marked;
    CGFloat _cellWidth, _cellHeight;
    NSInteger _selectionStart, _selectionEnd;
    uint32_t _foreground, _background;
}
@end
static NSColor *color(uint32_t rgb) {
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 255)/255.0 green:((rgb >> 8)&255)/255.0 blue:(rgb&255)/255.0 alpha:1];
}
static NSString *cellString(const AxyneTerminalCell *cell) {
    NSMutableString *text = [NSMutableString string];
    for (int n = 0; n < 6 && cell->chars[n]; ++n) {
        uint32_t c = cell->chars[n];
        if (c <= 0xffff) { unichar u = c; [text appendString:[NSString stringWithCharacters:&u length:1]]; }
        else if (c <= 0x10ffff) { unichar pair[] = {0xd800 + ((c-0x10000)>>10), 0xdc00 + ((c-0x10000)&1023)}; [text appendString:[NSString stringWithCharacters:pair length:2]]; }
    }
    return [text length] ? text : @" ";
}
static void changed(AxyneTerminal *terminal, void *context) {
    (void)terminal;
    AxyneTerminalLife *life = (AxyneTerminalLife *)context;
    if (atomic_exchange(&life->pending, 1)) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        atomic_store(&life->pending, 0);
        [life->view setNeedsDisplay:YES];
        NSView *parent = [life->view superview];
        if ([parent respondsToSelector:@selector(refreshActionControls)])
            [parent performSelector:@selector(refreshActionControls)];
    });
}
static void exited(AxyneTerminal *terminal, int code, void *context) {
    (void)code; changed(terminal, context);
}
@implementation AxyneTerminalView
- (id)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _life = [AxyneTerminalLife new]; _life->view = self;
        atomic_init(&_life->pending, 0);
        _marked = [[NSMutableAttributedString alloc] init];
        _selectionStart = _selectionEnd = -1;
        axyne_terminal_create(24, 80, 2000, &_terminal, NULL);
        [self setFont:[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular] foreground:0xd4d4d4 background:0x181a1f];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (AxyneTerminal *)terminal { return _terminal; }
- (BOOL)running { return axyne_terminal_running(_terminal); }
- (AxyneStatus)start:(const AxyneTerminalSpec *)spec error:(AxyneError *)error {
    AxyneTerminalSpec owned = *spec;
    owned.on_changed = changed; owned.on_exit = exited; owned.user_data = _life;
    AxyneStatus result = axyne_terminal_start(_terminal, &owned, error);
    [self setNeedsDisplay:YES]; return result;
}
- (void)stop { axyne_terminal_terminate(_terminal, NULL); }
- (void)shutdown {
    _life->view = nil;
    if (_terminal) { axyne_terminal_release(_terminal); _terminal = NULL; }
}
- (void)dealloc { [self shutdown]; [_life release]; [_font release]; [_marked release]; [super dealloc]; }
- (void)setFont:(NSFont *)font foreground:(uint32_t)fg background:(uint32_t)bg {
    [font retain]; [_font release]; _font = font; _foreground = fg; _background = bg;
    _cellWidth = MAX(1, ceil([@"M" sizeWithAttributes:@{NSFontAttributeName:font}].width));
    _cellHeight = MAX(1, ceil([font ascender]-[font descender]+[font leading]+2));
    axyne_terminal_set_colors(_terminal, fg, bg); [self resizeGrid]; [self setNeedsDisplay:YES];
}
- (void)resizeGrid {
    int rows = MAX(1, MIN(512, (int)((NSHeight([self bounds])-8)/_cellHeight)));
    int cols = MAX(1, MIN(1024, (int)((NSWidth([self bounds])-8)/_cellWidth)));
    AxyneTerminalSnapshot s = {0};
    if (axyne_terminal_snapshot(_terminal, &s, NULL) == AXYNE_STATUS_OK) {
        BOOL different = s.rows != rows || s.columns != cols; axyne_terminal_snapshot_free(&s);
        if (different) axyne_terminal_resize(_terminal, rows, cols, NULL);
    }
}
- (void)setFrameSize:(NSSize)size { [super setFrameSize:size]; [self resizeGrid]; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty; [color(_background) setFill]; NSRectFill([self bounds]);
    AxyneTerminalSnapshot s = {0}; if (axyne_terminal_snapshot(_terminal, &s, NULL) != AXYNE_STATUS_OK) return;
    NSInteger first = MIN(_selectionStart, _selectionEnd), last = MAX(_selectionStart, _selectionEnd);
    for (int r=0; r<s.rows; ++r) for (int c=0; c<s.columns; ++c) {
        NSInteger index = r*s.columns+c; const AxyneTerminalCell *cell = &s.cells[index];
        if (!cell->width) continue;
        uint32_t fg=cell->foreground, bg=cell->background;
        if (cell->attributes & AXYNE_TERMINAL_REVERSE) { uint32_t temp=fg; fg=bg; bg=temp; }
        NSRect rect=NSMakeRect(4+c*_cellWidth, 4+r*_cellHeight, _cellWidth*MAX(1,cell->width), _cellHeight);
        BOOL selected = first >= 0 && index >= first && index <= last;
        [(selected ? [NSColor selectedTextBackgroundColor] : color(bg)) setFill]; NSRectFill(rect);
        if (!cell->width) continue;
        NSFontTraitMask traits = (cell->attributes & AXYNE_TERMINAL_BOLD ? NSBoldFontMask : 0) | (cell->attributes & AXYNE_TERMINAL_ITALIC ? NSItalicFontMask : 0);
        NSFont *font = traits ? [[NSFontManager sharedFontManager] convertFont:_font toHaveTrait:traits] : _font;
        [cellString(cell) drawAtPoint:rect.origin withAttributes:@{NSFontAttributeName:font, NSForegroundColorAttributeName:selected ? [NSColor selectedTextColor] : color(fg), NSUnderlineStyleAttributeName:@(cell->attributes & AXYNE_TERMINAL_UNDERLINE ? NSUnderlineStyleSingle : 0)}];
    }
    if (s.cursor_visible && [[self window] firstResponder] == self) {
        [color(_foreground) setFill]; NSRectFill(NSMakeRect(4+s.cursor_column*_cellWidth, 4+(s.cursor_row+1)*_cellHeight-2, _cellWidth, 2));
    }
    if ([_marked length]) [_marked drawAtPoint:NSMakePoint(4+s.cursor_column*_cellWidth,4+s.cursor_row*_cellHeight)];
    axyne_terminal_snapshot_free(&s);
}
- (NSInteger)cellAtPoint:(NSPoint)point {
    AxyneTerminalSnapshot s={0}; if (axyne_terminal_snapshot(_terminal,&s,NULL)!=AXYNE_STATUS_OK) return -1;
    int c=MAX(0,MIN(s.columns-1,(int)((point.x-4)/_cellWidth))), r=MAX(0,MIN(s.rows-1,(int)((point.y-4)/_cellHeight)));
    NSInteger index=r*s.columns+c; axyne_terminal_snapshot_free(&s); return index;
}
- (void)mouseDown:(NSEvent *)event {
    [[self window] makeFirstResponder:self]; _selectionStart=_selectionEnd=[self cellAtPoint:[self convertPoint:[event locationInWindow] fromView:nil]]; [self setNeedsDisplay:YES];
}
- (void)mouseDragged:(NSEvent *)event { _selectionEnd=[self cellAtPoint:[self convertPoint:[event locationInWindow] fromView:nil]]; [self setNeedsDisplay:YES]; }
- (void)scrollWheel:(NSEvent *)event { axyne_terminal_scroll(_terminal, (int)round([event scrollingDeltaY])); _selectionStart=_selectionEnd=-1; [self setNeedsDisplay:YES]; }
- (void)copy:(id)sender {
    (void)sender; if (_selectionStart<0) return;
    AxyneTerminalSnapshot s={0}; if (axyne_terminal_snapshot(_terminal,&s,NULL)!=AXYNE_STATUS_OK) return;
    NSInteger first=MIN(_selectionStart,_selectionEnd), last=MIN(MAX(_selectionStart,_selectionEnd),s.rows*s.columns-1);
    NSMutableString *value=[NSMutableString string];
    for (NSInteger i=first;i<=last;++i) { if (i>first && i%s.columns==0) [value appendString:@"\n"]; if (s.cells[i].width) [value appendString:cellString(&s.cells[i])]; }
    [[NSPasteboard generalPasteboard] clearContents]; [[NSPasteboard generalPasteboard] setString:value forType:NSPasteboardTypeString]; axyne_terminal_snapshot_free(&s);
}
- (void)paste:(id)sender {
    (void)sender; NSString *value=[[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    value=[[value stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"] stringByReplacingOccurrencesOfString:@"\n" withString:@"\r"];
    NSData *data=[value dataUsingEncoding:NSUTF8StringEncoding]; axyne_terminal_paste(_terminal,[data bytes],[data length],NULL); [self setNeedsDisplay:YES];
}
- (void)selectAll:(id)sender {
    (void)sender; AxyneTerminalSnapshot s={0};
    if(axyne_terminal_snapshot(_terminal,&s,NULL)==AXYNE_STATUS_OK) {
        _selectionStart=0; _selectionEnd=s.rows*s.columns-1;
        axyne_terminal_snapshot_free(&s); [self setNeedsDisplay:YES];
    }
}
- (BOOL)performKeyEquivalent:(NSEvent *)event {
    if ([[self window] firstResponder]!=self) return NO;
    NSEventModifierFlags flags=[event modifierFlags]; NSString *key=[event charactersIgnoringModifiers];
    if (flags & NSEventModifierFlagCommand) {
        if ([key isEqualToString:@"c"]) { [self copy:nil]; return YES; }
        if ([key isEqualToString:@"v"]) { [self paste:nil]; return YES; }
        if ([key isEqualToString:@"a"]) { [self selectAll:nil]; return YES; }
        return NO;
    }
    [self keyDown:event]; return YES;
}
- (void)keyDown:(NSEvent *)event {
    unsigned mods=([event modifierFlags]&NSEventModifierFlagShift ? AXYNE_TERMINAL_SHIFT : 0) | ([event modifierFlags]&NSEventModifierFlagOption ? AXYNE_TERMINAL_ALT : 0) | ([event modifierFlags]&NSEventModifierFlagControl ? AXYNE_TERMINAL_CTRL : 0);
    AxyneTerminalKey key=0;
    switch ([event keyCode]) {
    case 36: case 76: key=AXYNE_TERMINAL_ENTER; break; case 48:key=AXYNE_TERMINAL_TAB;break; case 51:key=AXYNE_TERMINAL_BACKSPACE;break;
    case 53:key=AXYNE_TERMINAL_ESCAPE;break;case 123:key=AXYNE_TERMINAL_LEFT;break;case 124:key=AXYNE_TERMINAL_RIGHT;break;case 125:key=AXYNE_TERMINAL_DOWN;break;case 126:key=AXYNE_TERMINAL_UP;break;
    case 115:key=AXYNE_TERMINAL_HOME;break;case 119:key=AXYNE_TERMINAL_END;break;case 116:key=AXYNE_TERMINAL_PAGEUP;break;case 121:key=AXYNE_TERMINAL_PAGEDOWN;break;case 117:key=AXYNE_TERMINAL_DELETE;break;
    }
    NSString *chars=[event charactersIgnoringModifiers];
    if (!key && [chars length] && [chars characterAtIndex:0]>=NSF1FunctionKey && [chars characterAtIndex:0]<=NSF24FunctionKey) key=(AxyneTerminalKey)(AXYNE_TERMINAL_F1+[chars characterAtIndex:0]-NSF1FunctionKey);
    if (key) axyne_terminal_key(_terminal,key,mods,NULL);
    else if ((mods&AXYNE_TERMINAL_CTRL) && [chars length]) axyne_terminal_unichar(_terminal,[chars characterAtIndex:0],mods,NULL);
    else [self interpretKeyEvents:@[event]];
    _selectionStart=_selectionEnd=-1; [self setNeedsDisplay:YES];
}
- (void)insertText:(id)value replacementRange:(NSRange)range {
    (void)range; NSString *text=[value isKindOfClass:[NSAttributedString class]] ? [value string] : value;
    for (NSUInteger i=0;i<[text length];++i) { uint32_t c=[text characterAtIndex:i]; if (c>=0xd800 && c<=0xdbff && i+1<[text length]) { uint32_t low=[text characterAtIndex:i+1]; if (low>=0xdc00 && low<=0xdfff) { ++i;c=0x10000+((c-0xd800)<<10)+low-0xdc00; } } axyne_terminal_unichar(_terminal,c,0,NULL); }
    [self unmarkText]; [self setNeedsDisplay:YES];
}
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected replacementRange:(NSRange)range { (void)selected;(void)range; [_marked setAttributedString:[value isKindOfClass:[NSAttributedString class]] ? value : [[[NSAttributedString alloc] initWithString:value attributes:@{NSFontAttributeName:_font,NSForegroundColorAttributeName:color(_foreground),NSUnderlineStyleAttributeName:@1}] autorelease]]; [self setNeedsDisplay:YES]; }
- (void)unmarkText { [_marked deleteCharactersInRange:NSMakeRange(0,[_marked length])]; }
- (BOOL)hasMarkedText { return [_marked length]>0; }
- (NSRange)markedRange { return [_marked length] ? NSMakeRange(0,[_marked length]) : NSMakeRange(NSNotFound,0); }
- (NSRange)selectedRange { return NSMakeRange(NSNotFound,0); }
- (NSArray *)validAttributesForMarkedText { return @[]; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual { (void)range;if(actual)*actual=NSMakeRange(NSNotFound,0);return nil; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { (void)point;return NSNotFound; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual { (void)range;if(actual)*actual=NSMakeRange(0,0); AxyneTerminalSnapshot s={0};axyne_terminal_snapshot(_terminal,&s,NULL);NSRect rect=NSMakeRect(4+s.cursor_column*_cellWidth,4+s.cursor_row*_cellHeight,_cellWidth,_cellHeight);axyne_terminal_snapshot_free(&s);return [[self window] convertRectToScreen:[self convertRect:rect toView:nil]]; }
- (void)doCommandBySelector:(SEL)selector { (void)selector; }
@end
