#import <AppKit/AppKit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/preferences.h"
#include "../../preferences_window.h"

/* Figma preferences window (7J8SYhLpybJgpxD3qFqL5u / 24:13953) in AppKit.
 * Manual retain/release, like the rest of the macOS UI. All drawing views are
 * flipped so Figma's top-left metrics map one to one. */

static NSColor *axyne_pw_color(uint32_t rgb)
{
    return [NSColor colorWithSRGBRed:(CGFloat)((rgb >> 16) & 0xff) / 255.0
                               green:(CGFloat)((rgb >> 8) & 0xff) / 255.0
                                blue:(CGFloat)(rgb & 0xff) / 255.0
                               alpha:1.0];
}

static NSFont *axyne_pw_font(CGFloat size, BOOL semibold)
{
    return [NSFont systemFontOfSize:size
                             weight:semibold ? NSFontWeightSemibold : NSFontWeightRegular];
}

static NSDictionary *axyne_pw_attributes(NSFont *font, NSColor *color, BOOL underline)
{
    if (underline)
        return @{NSFontAttributeName: font,
                 NSForegroundColorAttributeName: color,
                 NSUnderlineStyleAttributeName: @(NSUnderlineStyleSingle)};
    return @{NSFontAttributeName: font, NSForegroundColorAttributeName: color};
}

static NSString *axyne_pw_modifier_text(unsigned int modifiers)
{
    NSMutableString *text = [NSMutableString string];
    if (modifiers & AXYNE_KEY_MODIFIER_CONTROL) [text appendString:@"⌃"];
    if (modifiers & AXYNE_KEY_MODIFIER_ALT) [text appendString:@"⌥"];
    if (modifiers & AXYNE_KEY_MODIFIER_SHIFT) [text appendString:@"⇧"];
    if (modifiers & AXYNE_KEY_MODIFIER_COMMAND) [text appendString:@"⌘"];
    return text;
}

static NSString *axyne_pw_action_title(AxynePreferenceAction action)
{
    static NSString *const titles[] = {
        @"새 파일", @"열기", @"저장", @"닫기", @"찾기", @"바꾸기",
        @"작업 영역 검색", @"빠른 파일 열기", @"빌드", @"실행", @"환경 설정"
    };
    if ((int)action >= 0 && action < AXYNE_ACTION_COUNT) return titles[action];
    return [NSString stringWithUTF8String:axyne_preferences_action_name(action)];
}

/* ------------------------------------------------------------------ */
/* Drawing views                                                       */
/* ------------------------------------------------------------------ */

@interface AxynePWBox : NSView {
    NSColor *_fill;
    NSColor *_stroke;
    NSString *_caption;
    CGFloat _radius;
    CGFloat _topInset;
    BOOL _focused;
}
@property(nonatomic, retain) NSColor *fill;
@property(nonatomic, retain) NSColor *stroke;
@property(nonatomic, copy) NSString *caption;
@property(nonatomic) CGFloat radius;
@property(nonatomic) CGFloat topInset;
@property(nonatomic) BOOL focused;
@end

@implementation AxynePWBox
@synthesize fill = _fill, stroke = _stroke, caption = _caption;
@synthesize radius = _radius, topInset = _topInset, focused = _focused;
- (BOOL)isFlipped { return YES; }
- (void)dealloc
{
    [_fill release]; [_stroke release]; [_caption release]; [super dealloc];
}
- (void)setFocused:(BOOL)focused
{
    _focused = focused;
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    NSRect box = NSMakeRect(0, _topInset, bounds.size.width, bounds.size.height - _topInset);
    if (_fill != nil) {
        [_fill setFill];
        if (_radius > 0)
            [[NSBezierPath bezierPathWithRoundedRect:box xRadius:_radius yRadius:_radius] fill];
        else
            NSRectFill(box);
    }
    if (_stroke != nil || _focused) {
        NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(box, 0.5, 0.5)
                                                              xRadius:_radius yRadius:_radius];
        [path setLineWidth:1.0];
        [(_focused ? axyne_pw_color(AXYNE_PW_COLOR_ACCENT) : _stroke) setStroke];
        [path stroke];
    }
    if (_caption != nil) {
        NSDictionary *attributes = axyne_pw_attributes(axyne_pw_font(10, NO),
                                                       axyne_pw_color(AXYNE_PW_COLOR_MUTED), NO);
        NSSize size = [_caption sizeWithAttributes:attributes];
        [axyne_pw_color(AXYNE_PW_COLOR_CONTENT) setFill];
        NSRectFill(NSMakeRect(8, 2, size.width + 6, size.height));
        [_caption drawAtPoint:NSMakePoint(11, 2) withAttributes:attributes];
    }
}
@end

@interface AxynePWTitleStrip : AxynePWBox {
    NSString *_title;
}
@property(nonatomic, copy) NSString *title;
@end

@implementation AxynePWTitleStrip
@synthesize title = _title;
- (void)dealloc { [_title release]; [super dealloc]; }
- (BOOL)mouseDownCanMoveWindow { return YES; }
- (void)mouseDown:(NSEvent *)event
{
    [[self window] performWindowDragWithEvent:event];
}
- (void)drawRect:(NSRect)dirtyRect
{
    NSDictionary *attributes;
    NSSize size;
    [super drawRect:dirtyRect];
    attributes = axyne_pw_attributes(axyne_pw_font(12, NO), axyne_pw_color(AXYNE_PW_COLOR_MUTED), NO);
    size = [_title sizeWithAttributes:attributes];
    [_title drawAtPoint:NSMakePoint(10, floor(([self bounds].size.height - size.height) / 2.0))
         withAttributes:attributes];
}
@end

typedef NS_ENUM(NSInteger, AxynePWKind) {
    AxynePWKindPush = 0,
    AxynePWKindAccent,
    AxynePWKindSmall,
    AxynePWKindLink,
    AxynePWKindSidebar,
    AxynePWKindCheck,
    AxynePWKindRadio,
    AxynePWKindChevron,
    AxynePWKindClose
};

@interface AxynePWButton : NSButton {
    AxynePWKind _kind;
    BOOL _on;
}
@property(nonatomic) AxynePWKind kind;
@property(nonatomic) BOOL on;
@end

@implementation AxynePWButton
@synthesize kind = _kind, on = _on;
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return [self isEnabled]; }
- (BOOL)mouseDownCanMoveWindow { return NO; }
- (void)setOn:(BOOL)on
{
    _on = on;
    [self setNeedsDisplay:YES];
}
- (void)setEnabled:(BOOL)enabled
{
    [super setEnabled:enabled];
    [self setNeedsDisplay:YES];
}
- (void)drawFocusRingMask
{
    [[NSBezierPath bezierPathWithRoundedRect:[self focusRingMaskBounds] xRadius:4 yRadius:4] fill];
}
- (NSRect)focusRingMaskBounds
{
    if (_kind == AxynePWKindCheck || _kind == AxynePWKindRadio) {
        NSRect bounds = [self bounds];
        return NSMakeRect(-2, (bounds.size.height - 14) / 2.0 - 2, 18, 18);
    }
    return [self bounds];
}
- (void)drawTitle:(NSString *)title font:(NSFont *)font color:(NSColor *)color
          centered:(BOOL)centered x:(CGFloat)x underline:(BOOL)underline
{
    NSDictionary *attributes = axyne_pw_attributes(font, color, underline);
    NSSize size = [title sizeWithAttributes:attributes];
    NSRect bounds = [self bounds];
    CGFloat left = centered ? floor((bounds.size.width - size.width) / 2.0) : x;
    [title drawAtPoint:NSMakePoint(left, floor((bounds.size.height - size.height) / 2.0))
        withAttributes:attributes];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    NSString *title = [self title];
    CGFloat alpha = [self isEnabled] ? 1.0 : 0.55;
    switch (_kind) {
    case AxynePWKindPush:
    case AxynePWKindAccent:
    case AxynePWKindSmall: {
        BOOL accent = _kind == AxynePWKindAccent;
        CGFloat radius = _kind == AxynePWKindSmall ? 4 : 5;
        [[axyne_pw_color(accent ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_BUTTON)
            colorWithAlphaComponent:alpha] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:radius yRadius:radius] fill];
        [self drawTitle:title
                   font:axyne_pw_font(_kind == AxynePWKindSmall ? 11 : 12, accent)
                  color:[(accent ? [NSColor whiteColor] : axyne_pw_color(AXYNE_PW_COLOR_TEXT))
                            colorWithAlphaComponent:alpha]
               centered:YES x:0 underline:NO];
        break;
    }
    case AxynePWKindLink:
        [self drawTitle:title
                   font:[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]
                  color:axyne_pw_color(AXYNE_PW_COLOR_ACCENT)
               centered:NO x:0 underline:YES];
        break;
    case AxynePWKindSidebar:
        if (_on) {
            [axyne_pw_color(AXYNE_PW_COLOR_SELECTED) setFill];
            NSRectFill(bounds);
        }
        [self drawTitle:title font:axyne_pw_font(12, NO)
                  color:axyne_pw_color(_on ? AXYNE_PW_COLOR_TEXT : AXYNE_PW_COLOR_MUTED)
               centered:NO x:12 underline:NO];
        break;
    case AxynePWKindCheck: {
        NSRect box = NSMakeRect(0, floor((bounds.size.height - AXYNE_PW_CHECK_SIZE) / 2.0),
                                AXYNE_PW_CHECK_SIZE, AXYNE_PW_CHECK_SIZE);
        [axyne_pw_color(_on ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_CHECK_OFF) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:box xRadius:2 yRadius:2] fill];
        if (_on) {
            NSBezierPath *tick = [NSBezierPath bezierPath];
            [tick moveToPoint:NSMakePoint(box.origin.x + 3.5, box.origin.y + 7.5)];
            [tick lineToPoint:NSMakePoint(box.origin.x + 6.0, box.origin.y + 10.0)];
            [tick lineToPoint:NSMakePoint(box.origin.x + 10.5, box.origin.y + 4.5)];
            [tick setLineWidth:1.7];
            [tick setLineCapStyle:NSLineCapStyleRound];
            [tick setLineJoinStyle:NSLineJoinStyleRound];
            [[NSColor whiteColor] setStroke];
            [tick stroke];
        }
        [self drawTitle:title font:axyne_pw_font(12, NO) color:axyne_pw_color(AXYNE_PW_COLOR_TEXT)
               centered:NO x:AXYNE_PW_CHECK_SIZE + 8 underline:NO];
        break;
    }
    case AxynePWKindRadio: {
        NSRect box = NSMakeRect(0.5, floor((bounds.size.height - AXYNE_PW_CHECK_SIZE) / 2.0) + 0.5,
                                AXYNE_PW_CHECK_SIZE - 1, AXYNE_PW_CHECK_SIZE - 1);
        NSBezierPath *ring = [NSBezierPath bezierPathWithOvalInRect:box];
        [ring setLineWidth:1.0];
        [axyne_pw_color(_on ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_MUTED) setStroke];
        [ring stroke];
        if (_on) {
            [axyne_pw_color(AXYNE_PW_COLOR_ACCENT) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(box.origin.x + 3, box.origin.y + 3, 7, 7)] fill];
        }
        [self drawTitle:title font:axyne_pw_font(12, NO) color:axyne_pw_color(AXYNE_PW_COLOR_TEXT)
               centered:NO x:AXYNE_PW_CHECK_SIZE + 8 underline:NO];
        break;
    }
    case AxynePWKindChevron: {
        NSBezierPath *chevron = [NSBezierPath bezierPath];
        CGFloat cx = floor(bounds.size.width / 2.0), cy = floor(bounds.size.height / 2.0);
        [chevron moveToPoint:NSMakePoint(cx - 3.5, cy - 1.5)];
        [chevron lineToPoint:NSMakePoint(cx, cy + 2.0)];
        [chevron lineToPoint:NSMakePoint(cx + 3.5, cy - 1.5)];
        [chevron setLineWidth:1.3];
        [chevron setLineCapStyle:NSLineCapStyleRound];
        [chevron setLineJoinStyle:NSLineJoinStyleRound];
        [axyne_pw_color(AXYNE_PW_COLOR_MUTED) setStroke];
        [chevron stroke];
        break;
    }
    case AxynePWKindClose: {
        NSBezierPath *cross = [NSBezierPath bezierPath];
        CGFloat cx = floor(bounds.size.width / 2.0), cy = floor(bounds.size.height / 2.0);
        [cross moveToPoint:NSMakePoint(cx - 4, cy - 4)];
        [cross lineToPoint:NSMakePoint(cx + 4, cy + 4)];
        [cross moveToPoint:NSMakePoint(cx + 4, cy - 4)];
        [cross lineToPoint:NSMakePoint(cx - 4, cy + 4)];
        [cross setLineWidth:1.3];
        [cross setLineCapStyle:NSLineCapStyleRound];
        [axyne_pw_color(AXYNE_PW_COLOR_MUTED) setStroke];
        [cross stroke];
        break;
    }
    }
}
- (id)accessibilityValue
{
    if (_kind == AxynePWKindCheck || _kind == AxynePWKindRadio) return @(_on ? 1 : 0);
    return [super accessibilityValue];
}
@end

@class AxynePWController;

/* Text field that reports focus to its bordered container. */
@interface AxynePWTextField : NSTextField
@end

@implementation AxynePWTextField
- (BOOL)becomeFirstResponder
{
    BOOL accepted = [super becomeFirstResponder];
    if (accepted && [[self superview] isKindOfClass:[AxynePWBox class]])
        [(AxynePWBox *)[self superview] setFocused:YES];
    return accepted;
}
- (void)textDidEndEditing:(NSNotification *)notification
{
    [super textDidEndEditing:notification];
    if ([[self superview] isKindOfClass:[AxynePWBox class]])
        [(AxynePWBox *)[self superview] setFocused:NO];
}
@end

@interface AxynePWPanel : NSPanel {
    AxynePWController *_controller; /* not retained */
}
@property(nonatomic, assign) AxynePWController *controller;
@end

/* ------------------------------------------------------------------ */
/* Controller                                                          */
/* ------------------------------------------------------------------ */

enum { AXYNE_PW_PAGE_EDITOR = 0, AXYNE_PW_PAGE_THEME, AXYNE_PW_PAGE_KEYS, AXYNE_PW_PAGE_COUNT };
enum { AXYNE_PW_RESULT_CLOSE = 0, AXYNE_PW_RESULT_OPEN_FILE = 1 };

@interface AxynePWController : NSObject <NSTextFieldDelegate> {
    AxynePWPanel *_panel;
    BOOL _workspace;
    AxynePreferences _base;
    AxynePreferences _draft;
    AxynePreferencesWindowHooks _hooks;
    NSView *_pages[AXYNE_PW_PAGE_COUNT];
    AxynePWButton *_sidebar[AXYNE_PW_PAGE_COUNT];
    NSInteger _page;
    AxynePWBox *_fontBox, *_sizeBox, *_tabBox;
    NSTextField *_fontField, *_sizeField, *_tabField;
    AxynePWButton *_check[6];
    AxynePWButton *_theme[3];
    NSTextField *_keyField[AXYNE_ACTION_COUNT];
    NSTextField *_keyName[AXYNE_ACTION_COUNT];
    NSTextField *_keyModifiers[AXYNE_ACTION_COUNT];
    AxynePWButton *_keyToggle[AXYNE_ACTION_COUNT];
    AxynePWButton *_keyRestore[AXYNE_ACTION_COUNT];
    AxynePWButton *_okButton, *_cancelButton, *_applyButton;
    NSMutableArray *_fontFamilies;
}
- (id)initWithWorkspace:(BOOL)workspace preferences:(const AxynePreferences *)initial
                  hooks:(const AxynePreferencesWindowHooks *)hooks;
- (NSInteger)runOverOwner:(NSWindow *)owner;
- (void)cancel:(id)sender;
@end

@implementation AxynePWPanel
@synthesize controller = _controller;
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
- (void)cancelOperation:(id)sender { [(id)_controller cancel:sender]; }
@end

@implementation AxynePWController

- (NSTextField *)labelWithText:(NSString *)text font:(NSFont *)font color:(uint32_t)rgb
{
    NSTextField *label = [NSTextField labelWithString:text];
    [label setFont:font];
    [label setTextColor:axyne_pw_color(rgb)];
    [label sizeToFit];
    return label;
}

- (AxynePWButton *)buttonKind:(AxynePWKind)kind title:(NSString *)title frame:(NSRect)frame
                       action:(SEL)action tag:(NSInteger)tag
{
    AxynePWButton *button = [[[AxynePWButton alloc] initWithFrame:frame] autorelease];
    [button setButtonType:NSButtonTypeMomentaryChange];
    [button setBordered:NO];
    [button setTitle:title != nil ? title : @""];
    [button setTarget:self];
    [button setAction:action];
    [button setTag:tag];
    [button setKind:kind];
    [button setFocusRingType:NSFocusRingTypeExterior];
    if (title != nil) [button setAccessibilityLabel:title];
    if (kind == AxynePWKindCheck) [button setAccessibilityRole:NSAccessibilityCheckBoxRole];
    if (kind == AxynePWKindRadio) [button setAccessibilityRole:NSAccessibilityRadioButtonRole];
    return button;
}

/* A 30px labelled value box (Figma "입력 필드"). Returns the box; *field
 * receives the editable text field inside it. */
- (AxynePWBox *)boxWithWidth:(CGFloat)width x:(CGFloat)x y:(CGFloat)y page:(NSView *)page
                      field:(NSTextField **)field trailing:(CGFloat)trailing label:(NSString *)label
{
    AxynePWBox *box = [[[AxynePWBox alloc] initWithFrame:
        NSMakeRect(x, y, width, AXYNE_PW_FIELD_HEIGHT)] autorelease];
    AxynePWTextField *text = [[[AxynePWTextField alloc] initWithFrame:
        NSMakeRect(10, 7, width - 20 - trailing, 16)] autorelease];
    [box setFill:axyne_pw_color(AXYNE_PW_COLOR_FIELD)];
    [box setStroke:axyne_pw_color(AXYNE_PW_COLOR_BORDER)];
    [box setRadius:3];
    [text setBordered:NO];
    [text setDrawsBackground:NO];
    [text setFocusRingType:NSFocusRingTypeNone];
    [text setFont:axyne_pw_font(11, NO)];
    [text setTextColor:axyne_pw_color(AXYNE_PW_COLOR_TEXT)];
    [text setDelegate:self];
    [[text cell] setScrollable:YES];
    [[text cell] setUsesSingleLineMode:YES];
    [text setAccessibilityLabel:label];
    [box addSubview:text];
    [page addSubview:box];
    if (field != NULL) *field = text;
    return box;
}

- (AxynePWBox *)fieldWithLabel:(NSString *)label x:(CGFloat)x y:(CGFloat)y width:(CGFloat)width
                         page:(NSView *)page field:(NSTextField **)field
                    trailing:(CGFloat)trailing
{
    NSTextField *caption = [self labelWithText:label font:axyne_pw_font(11, NO)
                                          color:AXYNE_PW_COLOR_MUTED];
    [caption setFrameOrigin:NSMakePoint(x, y)];
    [page addSubview:caption];
    return [self boxWithWidth:width x:x y:y + 19 page:page field:field trailing:trailing label:label];
}

- (AxynePWBox *)groupAt:(NSRect)frame caption:(NSString *)caption page:(NSView *)page
{
    AxynePWBox *group = [[[AxynePWBox alloc] initWithFrame:
        NSMakeRect(frame.origin.x, frame.origin.y - 9, frame.size.width, frame.size.height + 9)]
        autorelease];
    [group setStroke:axyne_pw_color(AXYNE_PW_COLOR_BORDER)];
    [group setRadius:5];
    [group setTopInset:9];
    [group setCaption:caption];
    [page addSubview:group];
    return group;
}

- (NSView *)newPage
{
    AxynePWBox *page = [[[AxynePWBox alloc] initWithFrame:
        NSMakeRect(AXYNE_PW_SIDEBAR_WIDTH, AXYNE_PW_TITLE_HEIGHT,
                   AXYNE_PW_WIDTH - AXYNE_PW_SIDEBAR_WIDTH,
                   AXYNE_PW_HEIGHT - AXYNE_PW_TITLE_HEIGHT - AXYNE_PW_FOOTER_HEIGHT)] autorelease];
    [page setFill:axyne_pw_color(AXYNE_PW_COLOR_CONTENT)];
    return page;
}

- (void)addHeading:(NSString *)text to:(NSView *)page
{
    NSTextField *heading = [self labelWithText:text font:axyne_pw_font(16, YES)
                                          color:AXYNE_PW_COLOR_TEXT];
    [heading setFrameOrigin:NSMakePoint(AXYNE_PW_CONTENT_LEFT, AXYNE_PW_CONTENT_TOP)];
    [page addSubview:heading];
}

- (void)buildEditorPage:(NSView *)page
{
    /* Figma "편집기 설정": 20px padding, 16px gaps, option columns at x=15 and
     * x=200 inside the group box. */
    CGFloat groupWidth = [page frame].size.width - AXYNE_PW_CONTENT_LEFT - 18;
    CGFloat y = AXYNE_PW_CONTENT_TOP + 19 + 16;
    static NSString *const labels[6] = {
        @"줄 번호", @"현재 행 강조", @"공백 및 탭 표시", @"공백으로 탭 입력",
        @"자동 들여쓰기", @"줄 바꿈"
    };
    [self addHeading:@"편집기" to:page];
    _fontBox = [self fieldWithLabel:@"글꼴" x:AXYNE_PW_CONTENT_LEFT y:y width:240 page:page
                              field:&_fontField trailing:20];
    {
        AxynePWButton *chevron = [self buttonKind:AxynePWKindChevron title:nil
            frame:NSMakeRect(240 - 28, 0, 28, AXYNE_PW_FIELD_HEIGHT)
            action:@selector(showFontMenu:) tag:0];
        [chevron setAccessibilityLabel:@"글꼴 목록"];
        [_fontBox addSubview:chevron];
    }
    _sizeBox = [self fieldWithLabel:@"크기 (pt)" x:AXYNE_PW_CONTENT_LEFT + 256 y:y width:120
                               page:page field:&_sizeField trailing:0];
    _tabBox = [self fieldWithLabel:@"탭 크기" x:AXYNE_PW_CONTENT_LEFT + 392 y:y width:116
                              page:page field:&_tabField trailing:0];
    y += 49 + 16;
    (void)[self groupAt:NSMakeRect(AXYNE_PW_CONTENT_LEFT, y, groupWidth, 94) caption:@"표시" page:page];
    for (int i = 0; i < 6; ++i) {
        AxynePWButton *button = [self buttonKind:AxynePWKindCheck title:labels[i]
            frame:NSMakeRect(AXYNE_PW_CONTENT_LEFT + (i % 2 == 0 ? 15 : 200),
                             y + 15 + (i / 2) * 26, 170, 14)
            action:@selector(toggleOption:) tag:i];
        _check[i] = button;
        [page addSubview:button];
    }
    /* The 렌더링 group is Windows only; nothing is built here. */
}

- (void)buildThemePage:(NSView *)page
{
    CGFloat groupWidth = [page frame].size.width - AXYNE_PW_CONTENT_LEFT - 18;
    CGFloat y = AXYNE_PW_CONTENT_TOP + 19 + 16 + 9;
    static NSString *const labels[3] = {@"어둡게", @"밝게", @"시스템 설정 따르기"};
    [self addHeading:@"글꼴 및 색" to:page];
    (void)[self groupAt:NSMakeRect(AXYNE_PW_CONTENT_LEFT, y, groupWidth, 94) caption:@"테마" page:page];
    for (int i = 0; i < 3; ++i) {
        _theme[i] = [self buttonKind:AxynePWKindRadio title:labels[i]
            frame:NSMakeRect(AXYNE_PW_CONTENT_LEFT + 15, y + 15 + i * 26, 300, 14)
            action:@selector(selectTheme:) tag:i];
        [page addSubview:_theme[i]];
    }
}

- (void)buildKeysPage:(NSView *)page
{
    CGFloat y = AXYNE_PW_CONTENT_TOP + 19 + 16;
    NSTextField *caption;
    [self addHeading:@"키 바인딩" to:page];
    caption = [self labelWithText:@"동작" font:axyne_pw_font(11, NO) color:AXYNE_PW_COLOR_MUTED];
    [caption setFrameOrigin:NSMakePoint(AXYNE_PW_CONTENT_LEFT, y)];
    [page addSubview:caption];
    caption = [self labelWithText:@"단축키" font:axyne_pw_font(11, NO) color:AXYNE_PW_COLOR_MUTED];
    [caption setFrameOrigin:NSMakePoint(216, y)];
    [page addSubview:caption];
    y += 19;
    for (size_t i = 0; i < _draft.binding_count; ++i) {
        AxynePreferenceAction action = _draft.bindings[i].action;
        NSTextField *name = [self labelWithText:axyne_pw_action_title(action) font:axyne_pw_font(12, NO)
                                          color:AXYNE_PW_COLOR_TEXT];
        NSTextField *mods = [self labelWithText:@"⌘⇧⌥⌃" font:axyne_pw_font(12, NO)
                                          color:AXYNE_PW_COLOR_MUTED];
        NSTextField *field = nil;
        [name setFrameOrigin:NSMakePoint(AXYNE_PW_CONTENT_LEFT, y + 7)];
        [mods setFrameOrigin:NSMakePoint(216, y + 7)];
        [page addSubview:name];
        [page addSubview:mods];
        (void)[self boxWithWidth:110 x:276 y:y page:page field:&field trailing:0
                           label:axyne_pw_action_title(action)];
        _keyName[action] = name;
        _keyModifiers[action] = mods;
        _keyField[action] = field;
        _keyToggle[action] = [self buttonKind:AxynePWKindSmall title:@"비활성화"
            frame:NSMakeRect(396, y, 70, AXYNE_PW_FIELD_HEIGHT) action:@selector(toggleKey:)
            tag:(NSInteger)action];
        _keyRestore[action] = [self buttonKind:AxynePWKindSmall title:@"기본값 복원"
            frame:NSMakeRect(472, y, 72, AXYNE_PW_FIELD_HEIGHT) action:@selector(restoreKey:)
            tag:(NSInteger)action];
        [page addSubview:_keyToggle[action]];
        [page addSubview:_keyRestore[action]];
        y += 32;
    }
}

- (id)initWithWorkspace:(BOOL)workspace preferences:(const AxynePreferences *)initial
                  hooks:(const AxynePreferencesWindowHooks *)hooks
{
    self = [super init];
    if (self == nil) return nil;
    _workspace = workspace;
    _base = *initial;
    _draft = *initial;
    if (hooks != NULL) _hooks = *hooks;
    [self buildWindow];
    [self loadControls];
    [self selectPageIndex:AXYNE_PW_PAGE_EDITOR];
    [self updateButtons];
    return self;
}

- (void)buildWindow
{
    NSRect frame = NSMakeRect(0, 0, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT);
    AxynePWBox *root;
    AxynePWTitleStrip *strip;
    AxynePWBox *sidebar, *footer, *line;
    static NSString *const pageTitles[AXYNE_PW_PAGE_COUNT] = {@"편집기", @"글꼴 및 색", @"키 바인딩"};
    CGFloat buttonY = (AXYNE_PW_FOOTER_HEIGHT - AXYNE_PW_BUTTON_HEIGHT) / 2.0;
    CGFloat right = AXYNE_PW_WIDTH - 12;
    _panel = [[AxynePWPanel alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless
                                               backing:NSBackingStoreBuffered defer:NO];
    [_panel setController:self];
    [_panel setReleasedWhenClosed:NO];
    [_panel setHasShadow:YES];
    [_panel setTitle:_workspace ? @"작업 영역 설정" : @"환경 설정"];
    [_panel setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
    [_panel setBackgroundColor:axyne_pw_color(AXYNE_PW_COLOR_CONTENT)];
    root = [[[AxynePWBox alloc] initWithFrame:frame] autorelease];
    [root setFill:axyne_pw_color(AXYNE_PW_COLOR_CONTENT)];
    [_panel setContentView:root];

    for (int i = 0; i < AXYNE_PW_PAGE_COUNT; ++i) {
        _pages[i] = [self newPage];
        [root addSubview:_pages[i]];
    }
    [self buildEditorPage:_pages[AXYNE_PW_PAGE_EDITOR]];
    [self buildThemePage:_pages[AXYNE_PW_PAGE_THEME]];
    [self buildKeysPage:_pages[AXYNE_PW_PAGE_KEYS]];

    sidebar = [[[AxynePWBox alloc] initWithFrame:NSMakeRect(0, AXYNE_PW_TITLE_HEIGHT,
        AXYNE_PW_SIDEBAR_WIDTH, AXYNE_PW_HEIGHT - AXYNE_PW_TITLE_HEIGHT - AXYNE_PW_FOOTER_HEIGHT)]
        autorelease];
    [sidebar setFill:axyne_pw_color(AXYNE_PW_COLOR_SIDEBAR)];
    [root addSubview:sidebar];
    for (int i = 0; i < AXYNE_PW_PAGE_COUNT; ++i) {
        _sidebar[i] = [self buttonKind:AxynePWKindSidebar title:pageTitles[i]
            frame:NSMakeRect(0, i * AXYNE_PW_ITEM_HEIGHT, AXYNE_PW_SIDEBAR_WIDTH, AXYNE_PW_ITEM_HEIGHT)
            action:@selector(selectPage:) tag:i];
        [sidebar addSubview:_sidebar[i]];
    }

    strip = [[[AxynePWTitleStrip alloc] initWithFrame:
        NSMakeRect(0, 0, AXYNE_PW_WIDTH, AXYNE_PW_TITLE_HEIGHT)] autorelease];
    [strip setFill:axyne_pw_color(AXYNE_PW_COLOR_CHROME)];
    [strip setTitle:_workspace ? @"작업 영역 설정" : @"환경 설정"];
    [root addSubview:strip];
    {
        AxynePWButton *close = [self buttonKind:AxynePWKindClose title:nil
            frame:NSMakeRect(AXYNE_PW_WIDTH - 12 - 14 - 7, 4, 28, 28)
            action:@selector(cancel:) tag:0];
        [close setAccessibilityLabel:@"닫기"];
        [strip addSubview:close];
    }

    footer = [[[AxynePWBox alloc] initWithFrame:NSMakeRect(0,
        AXYNE_PW_HEIGHT - AXYNE_PW_FOOTER_HEIGHT, AXYNE_PW_WIDTH, AXYNE_PW_FOOTER_HEIGHT)] autorelease];
    [footer setFill:axyne_pw_color(AXYNE_PW_COLOR_CHROME)];
    [root addSubview:footer];
    line = [[[AxynePWBox alloc] initWithFrame:NSMakeRect(0, 0, AXYNE_PW_WIDTH, 1)] autorelease];
    [line setFill:axyne_pw_color(AXYNE_PW_COLOR_CHROME_LINE)];
    [footer addSubview:line];
    [footer addSubview:[self buttonKind:AxynePWKindLink title:@"settings.json 열기"
        frame:NSMakeRect(12, (AXYNE_PW_FOOTER_HEIGHT - 20) / 2.0, 170, 20)
        action:@selector(openSettings:) tag:0]];
    _applyButton = [self buttonKind:AxynePWKindPush title:@"적용"
        frame:NSMakeRect(right - AXYNE_PW_BUTTON_WIDTH, buttonY, AXYNE_PW_BUTTON_WIDTH, AXYNE_PW_BUTTON_HEIGHT)
        action:@selector(apply:) tag:0];
    _cancelButton = [self buttonKind:AxynePWKindPush title:@"취소"
        frame:NSMakeRect(right - 2 * AXYNE_PW_BUTTON_WIDTH - AXYNE_PW_BUTTON_GAP, buttonY,
                         AXYNE_PW_BUTTON_WIDTH, AXYNE_PW_BUTTON_HEIGHT)
        action:@selector(cancel:) tag:0];
    _okButton = [self buttonKind:AxynePWKindAccent title:@"확인"
        frame:NSMakeRect(right - 3 * AXYNE_PW_BUTTON_WIDTH - 2 * AXYNE_PW_BUTTON_GAP, buttonY,
                         AXYNE_PW_BUTTON_WIDTH, AXYNE_PW_BUTTON_HEIGHT)
        action:@selector(ok:) tag:0];
    [_okButton setKeyEquivalent:@"\r"];
    [footer addSubview:_okButton];
    [footer addSubview:_cancelButton];
    [footer addSubview:_applyButton];
}

- (void)dealloc
{
    [_panel setController:nil];
    [_panel close];
    [_panel release];
    [_fontFamilies release];
    [super dealloc];
}

/* ---- state <-> controls ---- */

- (int *)optionInPreferences:(AxynePreferences *)preferences tag:(NSInteger)tag
{
    switch (tag) {
    case 0: return &preferences->editor.line_numbers;
    case 1: return &preferences->editor.highlight_current_line;
    case 2: return &preferences->editor.show_whitespace;
    case 3: return &preferences->editor.insert_spaces;
    case 4: return &preferences->editor.auto_indent;
    case 5: return &preferences->editor.word_wrap;
    default: return NULL;
    }
}

- (void)refreshKeyRow:(AxynePreferenceAction)action
{
    const AxyneKeyBinding *binding = axyne_preferences_find_binding(&_draft, action);
    CGFloat alpha;
    if (binding == NULL || _keyField[action] == nil) return;
    alpha = binding->enabled ? 1.0 : 0.5;
    [_keyModifiers[action] setStringValue:axyne_pw_modifier_text(binding->modifiers)];
    [_keyModifiers[action] sizeToFit];
    [_keyName[action] setAlphaValue:alpha];
    [_keyModifiers[action] setAlphaValue:alpha];
    [_keyField[action] setAlphaValue:alpha];
    [_keyToggle[action] setTitle:binding->enabled ? @"비활성화" : @"활성화"];
    [_keyToggle[action] setAccessibilityLabel:[_keyToggle[action] title]];
    [_keyToggle[action] setNeedsDisplay:YES];
}

- (void)loadControls
{
    [_fontField setStringValue:[NSString stringWithUTF8String:_draft.editor.font_family]];
    [_sizeField setStringValue:[NSString stringWithFormat:@"%u", _draft.editor.font_size]];
    [_tabField setStringValue:[NSString stringWithFormat:@"%u", _draft.editor.tab_width]];
    for (int i = 0; i < 6; ++i)
        [_check[i] setOn:*[self optionInPreferences:&_draft tag:i] != 0];
    for (int i = 0; i < 3; ++i)
        [_theme[i] setOn:(int)_draft.theme.preset == i];
    for (size_t i = 0; i < _draft.binding_count; ++i) {
        AxynePreferenceAction action = _draft.bindings[i].action;
        if (_keyField[action] == nil) continue;
        [_keyField[action] setStringValue:[NSString stringWithUTF8String:_draft.bindings[i].key]];
        [self refreshKeyRow:action];
    }
}

static BOOL axyne_pw_parse_unsigned(NSString *text, unsigned long *value)
{
    NSString *trimmed = [text stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceCharacterSet]];
    const char *utf8 = [trimmed UTF8String];
    char *end = NULL;
    if (utf8 == NULL || utf8[0] < '0' || utf8[0] > '9') return NO;
    *value = strtoul(utf8, &end, 10);
    return end != NULL && *end == '\0';
}

/* Reads the text fields into `out` (booleans, theme and binding state are
 * kept in _draft as they change). Returns NO with a message, the page and
 * the offending view when a validation rule fails. */
- (BOOL)collectInto:(AxynePreferences *)out message:(NSString **)message
              page:(NSInteger *)page focus:(NSView **)focus
{
    unsigned long value = 0;
    const char *utf8;
    *out = _draft;
    if (!axyne_pw_parse_unsigned([_sizeField stringValue], &value) ||
        axyne_preferences_check_font_size(value) != AXYNE_PREFERENCE_CHECK_OK) {
        if (message) *message = @"글꼴 크기는 6에서 72 사이여야 합니다.";
        if (page) *page = AXYNE_PW_PAGE_EDITOR;
        if (focus) *focus = _sizeField;
        return NO;
    }
    out->editor.font_size = (unsigned int)value;
    if (!axyne_pw_parse_unsigned([_tabField stringValue], &value) ||
        axyne_preferences_check_tab_width(value) != AXYNE_PREFERENCE_CHECK_OK) {
        if (message) *message = @"탭 크기는 1에서 16 사이여야 합니다.";
        if (page) *page = AXYNE_PW_PAGE_EDITOR;
        if (focus) *focus = _tabField;
        return NO;
    }
    out->editor.tab_width = (unsigned int)value;
    utf8 = [[_fontField stringValue] UTF8String];
    if (axyne_preferences_check_font_family(utf8) != AXYNE_PREFERENCE_CHECK_OK) {
        if (message) *message = @"글꼴 이름이 올바르지 않습니다.";
        if (page) *page = AXYNE_PW_PAGE_EDITOR;
        if (focus) *focus = _fontField;
        return NO;
    }
    (void)snprintf(out->editor.font_family, sizeof(out->editor.font_family), "%s", utf8);
    for (size_t i = 0; i < out->binding_count; ++i) {
        AxynePreferenceAction action = out->bindings[i].action;
        NSString *text = _keyField[action] != nil ? [_keyField[action] stringValue] : @"";
        const char *key = [text UTF8String];
        /* An empty field keeps the current key (the old "skip" answer). */
        if (key != NULL && key[0] != '\0' &&
            axyne_preferences_check_key(key) != AXYNE_PREFERENCE_CHECK_OK) {
            if (message) *message = @"단축키가 올바르지 않습니다.";
            if (page) *page = AXYNE_PW_PAGE_KEYS;
            if (focus) *focus = _keyField[action];
            return NO;
        }
    }
    return YES;
}

static BOOL axyne_pw_differs(const AxynePreferences *a, const AxynePreferences *b)
{
    unsigned char bindings[AXYNE_ACTION_COUNT];
    if (axyne_preferences_changed_fields(a, b, bindings) != 0) return YES;
    for (int i = 0; i < AXYNE_ACTION_COUNT; ++i)
        if (bindings[i]) return YES;
    return NO;
}

- (BOOL)isDirty
{
    AxynePreferences current;
    if (![self collectInto:&current message:NULL page:NULL focus:NULL]) return YES;
    return axyne_pw_differs(&_base, &current);
}

- (void)updateButtons
{
    [_applyButton setEnabled:[self isDirty]];
}

- (void)selectPageIndex:(NSInteger)page
{
    _page = page;
    for (int i = 0; i < AXYNE_PW_PAGE_COUNT; ++i) {
        [_pages[i] setHidden:i != page];
        [_sidebar[i] setOn:i == page];
    }
}

/* ---- actions ---- */

- (void)selectPage:(id)sender
{
    [self selectPageIndex:[sender tag]];
}

- (void)toggleOption:(id)sender
{
    int *option = [self optionInPreferences:&_draft tag:[sender tag]];
    if (option == NULL) return;
    *option = !*option;
    [(AxynePWButton *)sender setOn:*option != 0];
    [self updateButtons];
}

- (void)selectTheme:(id)sender
{
    AxyneThemePreset preset = (AxyneThemePreset)[sender tag];
    if (_draft.theme.preset != preset)
        axyne_preferences_select_theme(&_draft.theme, preset);
    for (int i = 0; i < 3; ++i) [_theme[i] setOn:(int)_draft.theme.preset == i];
    [self updateButtons];
}

- (void)toggleKey:(id)sender
{
    AxynePreferenceAction action = (AxynePreferenceAction)[sender tag];
    AxyneKeyBinding *binding = (AxyneKeyBinding *)axyne_preferences_find_binding(&_draft, action);
    if (binding == NULL) return;
    binding->enabled = !binding->enabled;
    [self refreshKeyRow:action];
    [self updateButtons];
}

- (void)restoreKey:(id)sender
{
    AxynePreferenceAction action = (AxynePreferenceAction)[sender tag];
    const AxyneKeyBinding *binding;
    axyne_preferences_restore_binding(&_draft, action);
    binding = axyne_preferences_find_binding(&_draft, action);
    if (binding != NULL) [_keyField[action] setStringValue:[NSString stringWithUTF8String:binding->key]];
    [self refreshKeyRow:action];
    [self updateButtons];
}

- (void)pickFont:(id)sender
{
    NSString *family = [sender representedObject];
    [_fontField setStringValue:family != nil ? family : @""];
    [self updateButtons];
}

- (void)showFontMenu:(id)sender
{
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@"글꼴"] autorelease];
    NSMenuItem *item;
    if (_fontFamilies == nil) {
        _fontFamilies = [[NSMutableArray alloc] init];
        for (NSString *family in [[NSFontManager sharedFontManager] availableFontFamilies]) {
            NSFont *font = [NSFont fontWithName:family size:12];
            if (font != nil && [font isFixedPitch]) [_fontFamilies addObject:family];
        }
    }
    item = [menu addItemWithTitle:@"기본 글꼴" action:@selector(pickFont:) keyEquivalent:@""];
    [item setTarget:self];
    for (NSString *family in _fontFamilies) {
        item = [menu addItemWithTitle:family action:@selector(pickFont:) keyEquivalent:@""];
        [item setRepresentedObject:family];
        [item setTarget:self];
    }
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, [(NSView *)sender bounds].size.height)
                            inView:sender];
}

- (void)showProblem:(NSString *)message page:(NSInteger)page focus:(NSView *)view
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:message];
    [alert addButtonWithTitle:@"확인"];
    [alert runModal];
    [self selectPageIndex:page];
    if (view != nil) [_panel makeFirstResponder:view];
}

- (BOOL)commit
{
    AxynePreferences current, saved;
    NSString *message = nil;
    NSInteger page = 0;
    NSView *focus = nil;
    if (![self collectInto:&current message:&message page:&page focus:&focus]) {
        [self showProblem:message page:page focus:focus];
        return NO;
    }
    if (!axyne_pw_differs(&_base, &current)) return YES;
    axyne_preferences_prepare_save(&saved, &_base, &current, _workspace);
    if (_hooks.save == NULL || !_hooks.save(_hooks.context, &saved)) return NO;
    _base = saved;
    _draft = saved;
    [self loadControls];
    [self updateButtons];
    return YES;
}

- (void)finish:(NSInteger)code
{
    [NSApp stopModalWithCode:code];
}

- (void)ok:(id)sender
{
    (void)sender;
    [_panel makeFirstResponder:nil];
    if ([self commit]) [self finish:AXYNE_PW_RESULT_CLOSE];
}

- (void)apply:(id)sender
{
    (void)sender;
    [_panel makeFirstResponder:nil];
    (void)[self commit];
}

- (void)cancel:(id)sender
{
    (void)sender;
    [self finish:AXYNE_PW_RESULT_CLOSE];
}

- (void)openSettings:(id)sender
{
    (void)sender;
    [self finish:AXYNE_PW_RESULT_OPEN_FILE];
}

/* ---- text field delegate ---- */

- (void)controlTextDidChange:(NSNotification *)notification
{
    NSTextField *field = [notification object];
    for (size_t i = 0; i < _draft.binding_count; ++i) {
        AxynePreferenceAction action = _draft.bindings[i].action;
        if (field == _keyField[action]) {
            const char *key = [[field stringValue] UTF8String];
            AxyneKeyBinding *binding = &_draft.bindings[i];
            /* Typing a key enables the binding, as the old prompt did. */
            if (key != NULL && axyne_preferences_check_key(key) == AXYNE_PREFERENCE_CHECK_OK) {
                (void)snprintf(binding->key, sizeof(binding->key), "%s", key);
                binding->enabled = 1;
                [self refreshKeyRow:action];
            }
        }
    }
    [self updateButtons];
}

- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView
    doCommandBySelector:(SEL)selector
{
    (void)control; (void)textView;
    if (selector == @selector(insertNewline:)) { [self ok:nil]; return YES; }
    if (selector == @selector(cancelOperation:)) { [self cancel:nil]; return YES; }
    return NO;
}

- (NSInteger)runOverOwner:(NSWindow *)owner
{
    NSInteger result;
    if (owner != nil) {
        NSRect o = [owner frame];
        [_panel setFrameOrigin:NSMakePoint(NSMidX(o) - AXYNE_PW_WIDTH / 2.0,
                                           NSMidY(o) - AXYNE_PW_HEIGHT / 2.0)];
    } else {
        [_panel center];
    }
    [_panel makeKeyAndOrderFront:nil];
    result = [NSApp runModalForWindow:_panel];
    [_panel orderOut:nil];
    if (owner != nil) [owner makeKeyAndOrderFront:nil];
    return result;
}

@end

int axyne_preferences_window_show(void *native_owner, int workspace,
                                  const AxynePreferences *initial,
                                  const AxynePreferencesWindowHooks *hooks)
{
    AxynePWController *controller;
    NSInteger result;
    if (initial == NULL) return 0;
    controller = [[AxynePWController alloc] initWithWorkspace:workspace != 0
                                                  preferences:initial hooks:hooks];
    if (controller == nil) return 0;
    result = [controller runOverOwner:(NSWindow *)native_owner];
    [controller release];
    return result == AXYNE_PW_RESULT_OPEN_FILE;
}
