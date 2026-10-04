#import <AppKit/AppKit.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <pthread.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>

#include "axyne/document.h"
#include "axyne/search.h"
#include "axyne/explorer.h"
#include "axyne/watcher.h"
#include "axyne/process.h"
#include "axyne/runner.h"
#include "axyne/debugger.h"
#include "axyne/preferences.h"
#include "axyne/git.h"
#include "axyne/lsp.h"
#include "axyne/palette_controller.h"
#include "axyne/ui_design.h"
#include "axyne/syntax.h"
#include "Scintilla.h"
#include "../../editor_document.h"
#include "../../editor_actions.h"
#include "../../debugger_actions.h"
#include "../../preferences_window.h"
#include <unistd.h>

@interface NSObject (AxyneScintillaMessages)
- (NSView *)content;
- (NSInteger)message:(unsigned int)message wParam:(uintptr_t)wParam
               lParam:(intptr_t)lParam;
@end

/* These bands follow the 1440x900 Figma work area: menu bar 26px, toolbar
 * 38px, tabs 34px,
 * output panel 230px, and status bar 24px. The macOS titlebar remains owned
 * by AppKit so its traffic-light controls stay native and accessible. */
static const CGFloat AXYNE_MENU = AXYNE_UI_MENU;
static const CGFloat AXYNE_TOOLBAR = AXYNE_UI_TOOLBAR;
/* Every band below the in-window menu bar and toolbar starts here. */
static const CGFloat AXYNE_CONTENT_TOP = AXYNE_UI_MENU + AXYNE_UI_TOOLBAR;
static const CGFloat AXYNE_TABS = AXYNE_UI_TABS;
static const CGFloat AXYNE_STATUS = AXYNE_UI_STATUS;

/* Title of the top-level menu at bar position index, from the table shared
 * with the Windows adapter. */
static NSString *axyne_macos_menu_label(NSUInteger index)
{
    const AxyneMenuTitle *title = axyne_ui_menu_title(index);
    NSString *label = title != NULL ? [NSString stringWithUTF8String:title->label] : nil;
    return label != nil ? label : @"";
}

/* Menu text at the Figma 12pt size. Only the font is set so AppKit keeps
 * choosing the colour (which also dims disabled entries) and draws the
 * key-equivalent column itself. */
static void axyne_macos_style_menu(NSMenu *menu)
{
    NSDictionary *attributes = @{NSFontAttributeName:[NSFont systemFontOfSize:12]};
    for (NSMenuItem *item in [menu itemArray]) {
        NSAttributedString *title;
        if ([item isSeparatorItem]) continue;
        if ([item submenu] != nil) axyne_macos_style_menu([item submenu]);
        title = [[NSAttributedString alloc] initWithString:[item title]
            attributes:attributes];
        [item setAttributedTitle:title];
        [title release];
    }
}

@class AxyneWorkspaceView;
typedef struct AxyneMacPreferencesContext {
    AxyneWorkspaceView *view;
    BOOL workspace;
} AxyneMacPreferencesContext;
typedef struct AxyneMacGitRun AxyneMacGitRun;
typedef struct AxyneMacGitCompletion AxyneMacGitCompletion;

/* Native buttons keep AppKit's actions and accessibility while drawing the
 * flat, precisely centered Figma toolbar rather than an OS bezel. */
@interface AxyneChromeButton : NSButton
@property(nonatomic, retain) NSColor *fillColor;
@property(nonatomic, retain) NSColor *labelColor;
@property(nonatomic, retain) NSColor *strokeColor;
@property(nonatomic, retain) NSAttributedString *richTitle;
@property(nonatomic, retain) NSAttributedString *trailingTitle;
@property(nonatomic) CGFloat cornerRadius;
@property(nonatomic) CGFloat contentInset;
@property(nonatomic) BOOL leadingAligned;
@end

@implementation AxyneChromeButton
@synthesize fillColor = _fillColor, labelColor = _labelColor;
@synthesize strokeColor = _strokeColor, richTitle = _richTitle;
@synthesize trailingTitle = _trailingTitle, cornerRadius = _cornerRadius;
@synthesize contentInset = _contentInset, leadingAligned = _leadingAligned;
- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) { _cornerRadius = 3; _contentInset = 4; }
    return self;
}
- (void)drawRichTitleInBounds:(NSRect)bounds
{
    NSSize size = [_richTitle size];
    NSSize trailing = _trailingTitle != nil ? [_trailingTitle size] : NSZeroSize;
    CGFloat x = _leadingAligned ? _contentInset :
        MAX(_contentInset, (NSWidth(bounds) - size.width) / 2);
    CGFloat available = NSWidth(bounds) - x - _contentInset -
        (trailing.width > 0 ? trailing.width + 8 : 0);
    [NSGraphicsContext saveGraphicsState];
    if (![self isEnabled])
        CGContextSetAlpha([[NSGraphicsContext currentContext] CGContext], 0.45);
    NSRectClip(bounds);
    if (available > 0)
        [_richTitle drawWithRect:NSMakeRect(x, (NSHeight(bounds) - size.height) / 2,
            available, size.height)
            options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine];
    if (trailing.width > 0)
        [_trailingTitle drawAtPoint:NSMakePoint(
            NSWidth(bounds) - _contentInset - trailing.width,
            (NSHeight(bounds) - trailing.height) / 2)];
    [NSGraphicsContext restoreGraphicsState];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    if (_fillColor != nil) {
        [_fillColor setFill];
        [[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:_cornerRadius
            yRadius:_cornerRadius] fill];
    }
    if (_strokeColor != nil) {
        /* Stroke inside the button so its own fill cannot cover the border. */
        NSBezierPath *outline = [NSBezierPath bezierPathWithRoundedRect:
            NSInsetRect(bounds, 0.5, 0.5) xRadius:MAX(0, _cornerRadius - 0.5)
            yRadius:MAX(0, _cornerRadius - 0.5)];
        [outline setLineWidth:1];
        [_strokeColor setStroke];
        [outline stroke];
    }
    if ([[self cell] isHighlighted]) {
        [[NSColor colorWithWhite:1 alpha:0.08] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:_cornerRadius
            yRadius:_cornerRadius] fill];
    }
    if (_richTitle != nil) { [self drawRichTitleInBounds:bounds]; return; }
    NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [style setAlignment:NSTextAlignmentCenter];
    [style setLineBreakMode:NSLineBreakByTruncatingTail];
    NSColor *color = _labelColor != nil ? _labelColor : [NSColor labelColor];
    if (![self isEnabled]) color = [color colorWithAlphaComponent:0.45];
    NSDictionary *attributes = @{NSFontAttributeName:[self font],
        NSForegroundColorAttributeName:color, NSParagraphStyleAttributeName:style};
    CGFloat height = [[self title] sizeWithAttributes:attributes].height;
    [[self title] drawInRect:NSMakeRect(4, (NSHeight(bounds) - height) / 2,
        MAX(0, NSWidth(bounds) - 8), height) withAttributes:attributes];
}
- (void)dealloc
{
    [_fillColor release]; [_labelColor release]; [_strokeColor release];
    [_richTitle release]; [_trailingTitle release]; [super dealloc];
}
@end

/* Tab to activate when `closing` goes away: the next shown tab, else the
 * previous shown one, else any neighbour (only hidden buffers remain). */
static size_t axyne_macos_successor_index(const AxyneDocumentSet *set, size_t closing)
{
    for (size_t next = closing + 1; next < set->count; ++next)
        if (!axyne_document_tab_hidden(&set->documents[next])) return next;
    for (size_t previous = closing; previous > 0; --previous)
        if (!axyne_document_tab_hidden(&set->documents[previous - 1])) return previous - 1;
    return closing + 1 < set->count ? closing + 1 : closing - 1;
}

static NSColor *axyne_color(CGFloat red, CGFloat green, CGFloat blue)
{
    return [NSColor colorWithCalibratedRed:red / 255.0
                                     green:green / 255.0
                                      blue:blue / 255.0
                                     alpha:1.0];
}

/* Each segment is @[text, font, color]; lets one native button show the
 * multi-color labels the Figma toolbar uses without extra subviews. */
static NSAttributedString *axyne_macos_segments(NSArray *segments)
{
    NSMutableAttributedString *result = [[[NSMutableAttributedString alloc] init] autorelease];
    for (NSArray *segment in segments) {
        NSDictionary *attributes = @{ NSFontAttributeName: [segment objectAtIndex:1],
            NSForegroundColorAttributeName: [segment objectAtIndex:2] };
        [result appendAttributedString:[[[NSAttributedString alloc]
            initWithString:[segment objectAtIndex:0] attributes:attributes] autorelease]];
    }
    return result;
}

/* ---- command palette overlay (Figma 80:70) ---------------------------------
 * AxynePaletteOverlay covers the whole workspace view: below the toolbar it
 * dims the body to ~45% and draws the 561px popup (header, rows, footer) from
 * the shared AxynePaletteController. Clicks outside the popup dismiss it. The
 * text field lives in AxyneWorkspaceView, over the toolbar search field. */
enum {
    AXYNE_PAL_WIDTH = 561,
    AXYNE_PAL_HEADER = 31,
    AXYNE_PAL_ROW = 32,
    AXYNE_PAL_LIST_PAD = 4,
    AXYNE_PAL_FOOTER = 33,
    AXYNE_PAL_MARGIN = 7,
    AXYNE_PAL_GAP = 3
};

@protocol AxynePaletteOwner <NSObject>
- (void)paletteActivateRow:(size_t)row;
- (void)paletteChipClicked:(NSInteger)mode;
- (void)paletteDismiss;
/* YES when the point belongs to the toolbar search field (not an outside click). */
- (BOOL)paletteClaimsPoint:(NSPoint)point;
@end

static NSColor *axyne_palette_rgb(uint32_t rgb, CGFloat alpha)
{
    return [NSColor colorWithSRGBRed:(CGFloat)((rgb >> 16) & 0xff) / 255.0
                               green:(CGFloat)((rgb >> 8) & 0xff) / 255.0
                                blue:(CGFloat)(rgb & 0xff) / 255.0
                               alpha:alpha];
}

static NSString *axyne_palette_string(const char *utf8, size_t length)
{
    NSString *value = [[[NSString alloc] initWithBytes:utf8 length:length
                                              encoding:NSUTF8StringEncoding] autorelease];
    return value != nil ? value : @"";
}

static CGFloat axyne_palette_text_width(NSString *text, NSFont *font)
{
    return [text sizeWithAttributes:@{NSFontAttributeName:font}].width;
}

/* Single line, vertically centered in `rect`, truncated with an ellipsis. */
static void axyne_palette_draw_text(NSString *text, NSFont *font, NSColor *color,
                                    NSRect rect, NSTextAlignment alignment)
{
    if ([text length] == 0 || NSWidth(rect) <= 0) return;
    NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [style setAlignment:alignment];
    [style setLineBreakMode:NSLineBreakByTruncatingTail];
    CGFloat height = [text sizeWithAttributes:@{NSFontAttributeName:font}].height;
    NSRect target = NSMakeRect(NSMinX(rect), NSMinY(rect) + (NSHeight(rect) - height) / 2,
                               NSWidth(rect), height);
    [text drawInRect:target withAttributes:@{NSFontAttributeName:font,
        NSForegroundColorAttributeName:color, NSParagraphStyleAttributeName:style}];
}

/* Footer chips in popup-local coordinates (mode order: file, >, @, :). */
static NSRect axyne_palette_chip_rect(NSInteger index, CGFloat popupHeight)
{
    static const CGFloat widths[] = {36, 45, 45, 58};
    CGFloat left = 8;
    for (NSInteger i = 0; i < index; ++i) left += widths[i] + 4;
    return NSMakeRect(left, popupHeight - 28, widths[index], 22);
}

typedef NS_ENUM(NSInteger, AxynePaletteHit) {
    AxynePaletteHitNone = 0, AxynePaletteHitRow, AxynePaletteHitChip
};

@interface AxynePaletteOverlay : NSView {
    AxynePaletteController *_controller;
    id<AxynePaletteOwner> _owner;
    NSTrackingArea *_tracking;
    CGFloat _wheelRemainder;
}
@property(nonatomic, assign) AxynePaletteController *controller;
@property(nonatomic, assign) id<AxynePaletteOwner> owner;
- (NSRect)popupRect;
@end

@implementation AxynePaletteOverlay
@synthesize controller = _controller, owner = _owner;

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) {
        [self setWantsLayer:YES];
        [self setAccessibilityElement:YES];
        [self setAccessibilityRole:NSAccessibilityListRole];
        [self setAccessibilityLabel:@"명령 팔레트 결과"];
    }
    return self;
}

- (void)dealloc
{
    if (_tracking != nil) {
        [self removeTrackingArea:_tracking];
        [_tracking release];
    }
    [super dealloc];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_tracking != nil) {
        [self removeTrackingArea:_tracking];
        [_tracking release];
        _tracking = nil;
    }
    _tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (NSRect)popupRect
{
    NSRect bounds = [self bounds];
    CGFloat rows = _controller != NULL ? (CGFloat)axyne_palette_ctl_visible_rows(_controller) : 1;
    CGFloat width = MIN((CGFloat)AXYNE_PAL_WIDTH, NSWidth(bounds) - 16);
    CGFloat height = 2 + AXYNE_PAL_HEADER + 2 * AXYNE_PAL_LIST_PAD +
        rows * AXYNE_PAL_ROW + AXYNE_PAL_FOOTER;
    if (width < 0) width = 0;
    return NSMakeRect(NSWidth(bounds) - AXYNE_PAL_MARGIN - width,
                      AXYNE_UI_TOOLBAR + AXYNE_PAL_GAP, width, height);
}

- (AxynePaletteHit)hitAtPoint:(NSPoint)point index:(size_t *)index
{
    NSRect popup = [self popupRect];
    NSPoint local = NSMakePoint(point.x - NSMinX(popup), point.y - NSMinY(popup));
    if (_controller == NULL || !NSPointInRect(point, popup)) return AxynePaletteHitNone;
    for (NSInteger i = 0; i < 4; ++i) {
        if (NSPointInRect(local, axyne_palette_chip_rect(i, NSHeight(popup)))) {
            *index = (size_t)i;
            return AxynePaletteHitChip;
        }
    }
    CGFloat rowsTop = 1 + AXYNE_PAL_HEADER + AXYNE_PAL_LIST_PAD;
    CGFloat rows = (CGFloat)axyne_palette_ctl_visible_rows(_controller);
    if (local.y >= rowsTop && local.y < rowsTop + rows * AXYNE_PAL_ROW) {
        size_t row = _controller->scroll + (size_t)((local.y - rowsTop) / AXYNE_PAL_ROW);
        if (row < axyne_palette_ctl_row_count(_controller)) {
            *index = row;
            return AxynePaletteHitRow;
        }
    }
    return AxynePaletteHitNone;
}

- (void)mouseMoved:(NSEvent *)event
{
    size_t index = 0;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_controller != NULL && _controller->list.count > 0 &&
        [self hitAtPoint:point index:&index] == AxynePaletteHitRow &&
        index != _controller->selection) {
        axyne_palette_ctl_select(_controller, index);
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseDown:(NSEvent *)event
{
    size_t index = 0;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    AxynePaletteHit hit = [self hitAtPoint:point index:&index];
    /* The owner may remove this view while handling the click. */
    [[self retain] autorelease];
    if (hit == AxynePaletteHitRow) [_owner paletteActivateRow:index];
    else if (hit == AxynePaletteHitChip) [_owner paletteChipClicked:(NSInteger)index];
    else if ([_owner paletteClaimsPoint:point]) return;
    else if (!NSPointInRect(point, [self popupRect])) [_owner paletteDismiss];
}

- (void)rightMouseDown:(NSEvent *)event
{
    (void)event;
    [[self retain] autorelease];
    [_owner paletteDismiss];
}

- (void)scrollWheel:(NSEvent *)event
{
    CGFloat delta = [event scrollingDeltaY];
    if (![event hasPreciseScrollingDeltas]) delta *= AXYNE_PAL_ROW;
    _wheelRemainder += delta;
    NSInteger rows = (NSInteger)(_wheelRemainder / AXYNE_PAL_ROW);
    if (rows != 0 && _controller != NULL) {
        _wheelRemainder -= (CGFloat)rows * AXYNE_PAL_ROW;
        axyne_palette_ctl_scroll(_controller, (int)-rows);
        [self setNeedsDisplay:YES];
    }
}

- (void)drawRow:(const AxynePaletteItem *)item inRect:(NSRect)row selected:(BOOL)selected
{
    NSFont *regular11 = [NSFont systemFontOfSize:11];
    NSFont *regular13 = [NSFont systemFontOfSize:13];
    NSFont *semibold13 = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    CGFloat right = NSMaxX(row) - 10;
    if (selected) {
        [axyne_palette_rgb(0x3b2d55, 1) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:row xRadius:4 yRadius:4] fill];
    }
    NSRect badge = NSMakeRect(NSMinX(row) + 10, NSMinY(row), 24, NSHeight(row));
    if (item->badge != NULL && item->badge[0] != '\0') {
        axyne_palette_draw_text(axyne_palette_string(item->badge, strlen(item->badge)),
            [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightBold],
            axyne_palette_rgb(item->badge_color, 1), badge, NSTextAlignmentCenter);
    } else if (item->kind == AXYNE_PALETTE_ITEM_FILE) {
        NSRect icon = NSMakeRect(NSMinX(badge) + 8, NSMinY(badge) + (NSHeight(badge) - 10) / 2, 8, 10);
        [axyne_palette_rgb(item->badge_color, 1) setStroke];
        [[NSBezierPath bezierPathWithRect:NSInsetRect(icon, 0.5, 0.5)] stroke];
    }
    CGFloat x = NSMinX(row) + 10 + 24 + 10;
    if (item->kind == AXYNE_PALETTE_ITEM_COMMAND && item->detail != NULL && item->detail[0] != '\0') {
        /* shortcut hint at the right edge keeps command labels aligned */
        NSString *shortcut = axyne_palette_string(item->detail, strlen(item->detail));
        CGFloat width = ceil(axyne_palette_text_width(shortcut, regular11));
        axyne_palette_draw_text(shortcut, regular11, axyne_palette_rgb(0x8b919b, 1),
            NSMakeRect(right - width, NSMinY(row), width, NSHeight(row)), NSTextAlignmentRight);
        right -= width + 12;
    }
    /* label with the matched part emphasized (byte offsets are UTF-8) */
    size_t length = strlen(item->label), start = item->match_start, count = item->match_len;
    if (count == 0 || start > length || count > length - start) { start = 0; count = 0; }
    NSMutableAttributedString *label = [[[NSMutableAttributedString alloc] init] autorelease];
    size_t offsets[3] = {0, start, start + count};
    size_t lengths[3] = {start, count, length - start - count};
    for (int piece = 0; piece < 3; ++piece) {
        if (lengths[piece] == 0) continue;
        NSString *text = axyne_palette_string(item->label + offsets[piece], lengths[piece]);
        NSDictionary *attributes = @{
            NSFontAttributeName: piece == 1 ? semibold13 : regular13,
            NSForegroundColorAttributeName: piece == 1 ? axyne_palette_rgb(0xc9a2f7, 1)
                                                       : axyne_palette_rgb(0xd5d8dd, 1)};
        [label appendAttributedString:[[[NSAttributedString alloc]
            initWithString:text attributes:attributes] autorelease]];
    }
    CGFloat labelWidth = MIN(ceil([label size].width), MAX(0, right - x));
    CGFloat labelHeight = [label size].height;
    if (labelWidth > 0) {
        NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
        [style setLineBreakMode:NSLineBreakByTruncatingTail];
        [label addAttribute:NSParagraphStyleAttributeName value:style
                      range:NSMakeRange(0, [label length])];
        [label drawInRect:NSMakeRect(x, NSMinY(row) + (NSHeight(row) - labelHeight) / 2,
                                     labelWidth, labelHeight)];
    }
    if (item->kind != AXYNE_PALETTE_ITEM_COMMAND && item->detail != NULL &&
        item->detail[0] != '\0' && x + labelWidth + 10 < right) {
        axyne_palette_draw_text(axyne_palette_string(item->detail, strlen(item->detail)),
            regular11, axyne_palette_rgb(0x8b919b, 1),
            NSMakeRect(x + labelWidth + 10, NSMinY(row), right - x - labelWidth - 10, NSHeight(row)),
            NSTextAlignmentLeft);
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (_controller == NULL || !_controller->active) return;
    NSRect bounds = [self bounds];
    const AxynePaletteController *c = _controller;
    /* the body below the toolbar is dimmed; the toolbar stays untouched */
    [axyne_palette_rgb(0x16171a, 0.55) setFill];
    NSRectFillUsingOperation(NSMakeRect(0, AXYNE_UI_TOOLBAR, NSWidth(bounds),
        MAX(0, NSHeight(bounds) - AXYNE_UI_TOOLBAR)), NSCompositingOperationSourceOver);

    NSRect popup = [self popupRect];
    NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(popup, 0.5, 0.5)
                                                          xRadius:6 yRadius:6];
    [NSGraphicsContext saveGraphicsState];
    [axyne_palette_rgb(0x202328, 1) setFill];
    [shape fill];
    [shape addClip];
    CGFloat rows = (CGFloat)axyne_palette_ctl_visible_rows(c);
    CGFloat rowsTop = NSMinY(popup) + 1 + AXYNE_PAL_HEADER + AXYNE_PAL_LIST_PAD;

    /* header: mode title and match count */
    NSRect header = NSMakeRect(NSMinX(popup) + 14, NSMinY(popup) + 1,
                               NSWidth(popup) - 28, AXYNE_PAL_HEADER - 1);
    axyne_palette_draw_text(axyne_palette_string(axyne_palette_ctl_title(c->mode),
            strlen(axyne_palette_ctl_title(c->mode))),
        [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold],
        axyne_palette_rgb(0xc9a2f7, 1), header, NSTextAlignmentLeft);
    if (c->list.count > 0) {
        axyne_palette_draw_text([NSString stringWithFormat:@"%zu개 일치", c->list.count],
            [NSFont systemFontOfSize:11], axyne_palette_rgb(0x8b919b, 1), header,
            NSTextAlignmentRight);
    }
    [axyne_palette_rgb(0x2a2d33, 1) setFill];
    NSRectFill(NSMakeRect(NSMinX(popup) + 1, NSMinY(popup) + AXYNE_PAL_HEADER,
                          NSWidth(popup) - 2, 1));

    /* rows, or one message row (no results, unsupported file, line hint) */
    if (c->list.count > 0) {
        for (NSInteger i = 0; i < (NSInteger)rows; ++i) {
            size_t index = c->scroll + (size_t)i;
            if (index >= c->list.count) break;
            NSRect row = NSMakeRect(NSMinX(popup) + 1 + AXYNE_PAL_LIST_PAD,
                rowsTop + i * AXYNE_PAL_ROW, NSWidth(popup) - 2 - 2 * AXYNE_PAL_LIST_PAD,
                AXYNE_PAL_ROW - 1);
            [self drawRow:&c->list.items[index] inRect:row selected:index == c->selection];
        }
        if (c->list.count > (size_t)rows) {
            CGFloat track = rows * AXYNE_PAL_ROW;
            CGFloat thumb = MAX(16, track * rows / (CGFloat)c->list.count);
            CGFloat top = rowsTop + (track - thumb) * (CGFloat)c->scroll /
                (CGFloat)(c->list.count - (size_t)rows);
            [axyne_palette_rgb(0x3a3e46, 1) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(NSMaxX(popup) - 6, top, 3, thumb)
                xRadius:1.5 yRadius:1.5] fill];
        }
    } else if (c->message[0] != '\0') {
        NSRect row = NSMakeRect(NSMinX(popup) + 1 + AXYNE_PAL_LIST_PAD, rowsTop,
            NSWidth(popup) - 2 - 2 * AXYNE_PAL_LIST_PAD, AXYNE_PAL_ROW - 1);
        if (c->message_actionable) {
            [axyne_palette_rgb(0x3b2d55, 1) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:row xRadius:4 yRadius:4] fill];
        }
        axyne_palette_draw_text(axyne_palette_string(c->message, strlen(c->message)),
            [NSFont systemFontOfSize:13],
            c->message_actionable ? axyne_palette_rgb(0xd5d8dd, 1) : axyne_palette_rgb(0x8b919b, 1),
            NSInsetRect(row, 10, 0), NSTextAlignmentLeft);
    }

    /* footer: filter chips (file, >, @, :) and key hints */
    [axyne_palette_rgb(0x2a2d33, 1) setFill];
    NSRectFill(NSMakeRect(NSMinX(popup) + 1, NSMaxY(popup) - 1 - 32 - 1, NSWidth(popup) - 2, 1));
    {
        NSArray *glyphs = @[@"", @">", @"@", @":"];
        NSArray *labels = @[@"파일", @"명령", @"기호", @"줄 이동"];
        NSFont *mono = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        NSFont *sans = [NSFont systemFontOfSize:11];
        CGFloat chipsRight = 0;
        for (NSInteger i = 0; i < 4; ++i) {
            NSRect chip = axyne_palette_chip_rect(i, NSHeight(popup));
            chip.origin.x += NSMinX(popup);
            chip.origin.y += NSMinY(popup);
            NSString *glyph = glyphs[(NSUInteger)i], *label = labels[(NSUInteger)i];
            CGFloat glyphWidth = [glyph length] > 0 ? ceil(axyne_palette_text_width(glyph, mono)) : 0;
            CGFloat labelWidth = ceil(axyne_palette_text_width(label, sans));
            CGFloat gap = glyphWidth > 0 ? 3 : 0;
            CGFloat x = NSMinX(chip) + (NSWidth(chip) - (glyphWidth + gap + labelWidth)) / 2;
            [(i == (NSInteger)c->mode ? axyne_palette_rgb(0x3b2d55, 1)
                                      : axyne_palette_rgb(0x2a2e35, 1)) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:chip xRadius:3 yRadius:3] fill];
            if (glyphWidth > 0)
                axyne_palette_draw_text(glyph, mono, axyne_palette_rgb(0xc9a2f7, 1),
                    NSMakeRect(x, NSMinY(chip), glyphWidth + 2, NSHeight(chip)), NSTextAlignmentLeft);
            axyne_palette_draw_text(label, sans, axyne_palette_rgb(0xc4c8ce, 1),
                NSMakeRect(x + glyphWidth + gap, NSMinY(chip), NSMaxX(chip) - x - glyphWidth - gap - 2,
                           NSHeight(chip)), NSTextAlignmentLeft);
            chipsRight = NSMaxX(chip);
        }
        NSString *hint = c->mode == AXYNE_PALETTE_MODE_COMMAND ? @"↑↓ 이동 · Enter 실행 · Esc 닫기"
            : (c->mode == AXYNE_PALETTE_MODE_FILE ? @"↑↓ 이동 · Enter 열기 · Esc 닫기"
                                                  : @"↑↓ 이동 · Enter 이동 · Esc 닫기");
        axyne_palette_draw_text(hint, sans, axyne_palette_rgb(0x8b919b, 1),
            NSMakeRect(chipsRight + 12, NSMaxY(popup) - 28, NSMaxX(popup) - 8 - chipsRight - 12, 22),
            NSTextAlignmentRight);
    }
    [NSGraphicsContext restoreGraphicsState];
    [axyne_palette_rgb(0x3a3e46, 1) setStroke];
    [shape setLineWidth:1];
    [shape stroke];
}

@end

@interface AxyneWorkspaceView : NSView <NSMenuItemValidation> {
    NSView *_editorView;
    NSBundle *_scintillaBundle;
    NSString *_editorLoadError;
    NSButton *_newButton;
    NSButton *_openButton;
    NSButton *_saveButton;
    NSButton *_undoButton;
    NSButton *_redoButton;
    NSButton *_buildButton;
    NSButton *_runButton;
    NSButton *_targetButton;
    NSButton *_searchButton;
    NSButton *_outputTab;
    NSButton *_problemsTab;
    NSButton *_terminalTab;
    NSButton *_clearOutput;
    NSScrollView *_terminalScroll;
    NSTextField *_problemSummary;
    NSInteger _panelMode;
    NSInteger _activeMenuIndex; /* bar item whose popup is open, or -1 */
    NSInteger _hoverMenuIndex;  /* bar item under the pointer, or -1 */
    NSTrackingArea *_menuTracking;
    NSInteger _explorerFirstRow;
    CGFloat _tabScroll;
    void *_lexillaModule;
    void *(*_createLexer)(const char *name);
    AxyneDocumentSet _documents;
    AxyneExplorer _explorer;
    AxyneWatcher *_watcher;
    NSInteger _explorerSelection;
    BOOL _hasExplorerSelection;
    NSMenu *_recentMenu;
    BOOL _loadingEditor;
    AxyneRunnerConfig _terminalRunner;
    AxyneRunnerConfig _actionRunner;
    AxyneDebugger _debugger;
    AxyneProcess *_terminalProcess;
    AxyneProcess *_gitProcess;
    AxyneMacGitRun *_gitRun;
    NSTextView *_terminalOutput;
    NSTextField *_terminalInput;
    NSButton *_terminalStart;
    NSButton *_terminalStop;
    NSButton *_terminalSend;
    NSButton *_debugStart;
    NSButton *_debugPause;
    NSButton *_debugContinue;
    NSButton *_debugNext;
    NSButton *_debugBreakpoint;
    int _activeAction;
    int _lastExitCode;
    BOOL _lastExitFailed;
    BOOL _hasExitStatus;
    AxynePreferences _globalPreferences;
    AxynePreferences _preferences;
    char *_globalPreferencesPath;
    char *_workspacePreferencesPath;
    unsigned char _workspaceBindingPresent[AXYNE_ACTION_COUNT];
    AxyneLspClient *_lsp;
    NSString *_lspStatus;
    BOOL _explorerHidden; /* View > Explorer; zero-initialised means visible */
    BOOL _panelHidden;    /* View > Bottom Panel */
    AxynePaletteController _palette;
    AxynePaletteOverlay *_paletteOverlay;
    NSTextField *_paletteField;
    NSTimer *_paletteTimer;
}
@end

static NSColor *axyne_preference_color(uint32_t value)
{
    return axyne_color((CGFloat)((value >> 16) & 0xff),
                       (CGFloat)((value >> 8) & 0xff),
                       (CGFloat)(value & 0xff));
}

static intptr_t axyne_editor_color(uint32_t rgb)
{
    return (intptr_t)(((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff));
}

/* Subtle current-line tint: editor background nudged 8% toward the text. */
static uint32_t axyne_macos_caret_line_color(const AxyneThemePreferences *theme)
{
    uint32_t result = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t back = (theme->editor_background >> shift) & 0xffu;
        uint32_t text = (theme->editor_text >> shift) & 0xffu;
        uint32_t mixed = (back * 92u + text * 8u) / 100u;
        result |= (mixed & 0xffu) << shift;
    }
    return result;
}

static void axyne_macos_select_theme(AxyneThemePreferences *theme,
                                     AxyneThemePreset preset)
{
    theme->preset = preset;
    if (preset == AXYNE_THEME_LIGHT) {
        theme->background = 0xf5f6f8; theme->panel = 0xffffff;
        theme->toolbar = 0xe9ebef; theme->border = 0xd3d7de;
        theme->text = 0x24272d; theme->muted = 0x68707d;
        theme->accent = 0x7650b5; theme->editor_background = 0xffffff;
        theme->editor_text = 0x24272d;
    } else {
        theme->background = 0x16171a; theme->panel = 0x1f2126;
        theme->toolbar = 0x1c1e22; theme->border = 0x292c32;
        theme->text = 0xc7c9ce; theme->muted = 0x737780;
        theme->accent = 0xb67af6; theme->editor_background = 0x1a1c20;
        theme->editor_text = 0xcbced6;
    }
}

static BOOL axyne_macos_prefers_dark(NSView *view)
{
    NSAppearance *appearance = [view effectiveAppearance];
    NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:
        @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    return [match isEqualToString:NSAppearanceNameDarkAqua];
}

/* Perceived luminance below mid-grey: scrollers use the dark appearance. */
static BOOL axyne_macos_color_is_dark(uint32_t rgb)
{
    uint32_t red = (rgb >> 16) & 0xffu, green = (rgb >> 8) & 0xffu, blue = rgb & 0xffu;
    return (red * 299u + green * 587u + blue * 114u) / 1000u < 128u;
}

/* The NSScrollView inside ScintillaView, found without relying on a private
 * accessor: use `scrollView` when exposed, otherwise search the subviews. */
static NSScrollView *axyne_macos_find_scroll_view(NSView *view)
{
    if ([view isKindOfClass:[NSScrollView class]]) return (NSScrollView *)view;
    for (NSView *child in [view subviews]) {
        NSScrollView *found = axyne_macos_find_scroll_view(child);
        if (found != nil) return found;
    }
    return nil;
}

static BOOL axyne_macos_reference_surfaces(const AxyneThemePreferences *theme)
{
    return theme->background == 0x16171a && theme->panel == 0x1f2126 &&
        theme->toolbar == 0x1c1e22;
}

static uint32_t axyne_macos_output_background(const AxyneThemePreferences *theme)
{
    return axyne_macos_reference_surfaces(theme) ? 0x1d1f23 : theme->background;
}

static char *axyne_macos_global_preferences_path(void)
{
    NSArray *directories = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString *base = [directories count] != 0 ? [directories objectAtIndex:0] : nil;
    NSString *directory;
    NSString *path;
    if (base == nil) return NULL;
    directory = [base stringByAppendingPathComponent:@"Axyne"];
    path = [directory stringByAppendingPathComponent:@"preferences.json"];
    return strdup([path UTF8String]);
}

static char *axyne_macos_workspace_preferences_path(const char *root)
{
    NSString *rootPath; NSString *directory; NSString *path;
    if (root == NULL) return NULL;
    rootPath = [NSString stringWithUTF8String:root];
    if (rootPath == nil) return NULL;
    directory = [rootPath stringByAppendingPathComponent:@".axyne"];
    path = [directory stringByAppendingPathComponent:@"preferences.json"];
    return strdup([path UTF8String]);
}

static BOOL axyne_macos_binding_matches(const AxynePreferences *preferences,
                                        AxynePreferenceAction action,
                                        NSString *key, NSEvent *event)
{
    const AxyneKeyBinding *binding = axyne_preferences_find_binding(
        preferences, action);
    unsigned int modifiers = 0;
    NSString *expected;
    if (binding == NULL || !binding->enabled) return NO;
    if (([event modifierFlags] & NSEventModifierFlagCommand) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_COMMAND;
    if (([event modifierFlags] & NSEventModifierFlagControl) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_CONTROL;
    if (([event modifierFlags] & NSEventModifierFlagShift) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_SHIFT;
    if (([event modifierFlags] & NSEventModifierFlagOption) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_ALT;
    if (binding->modifiers != modifiers) return NO;
    expected = [[NSString stringWithUTF8String:binding->key] lowercaseString];
    if ([expected isEqualToString:@"f5"])
        return [event keyCode] == 96;
    return [expected isEqualToString:[key lowercaseString]];
}

/* Cmd+Shift+P: the quick-file binding plus Shift opens the palette with ">". */
static BOOL axyne_macos_palette_shift_matches(const AxynePreferences *preferences,
                                              NSString *key, NSEvent *event)
{
    const AxyneKeyBinding *binding = axyne_preferences_find_binding(
        preferences, AXYNE_ACTION_QUICK_FILE);
    unsigned int modifiers = 0;
    if (binding == NULL || !binding->enabled || (binding->modifiers & AXYNE_KEY_MODIFIER_SHIFT) != 0)
        return NO;
    if (([event modifierFlags] & NSEventModifierFlagCommand) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_COMMAND;
    if (([event modifierFlags] & NSEventModifierFlagControl) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_CONTROL;
    if (([event modifierFlags] & NSEventModifierFlagShift) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_SHIFT;
    if (([event modifierFlags] & NSEventModifierFlagOption) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_ALT;
    if (modifiers != (binding->modifiers | AXYNE_KEY_MODIFIER_SHIFT)) return NO;
    return [[[NSString stringWithUTF8String:binding->key] lowercaseString]
        isEqualToString:[key lowercaseString]];
}

@interface AxyneWorkspaceView (AxyneActions)
- (void)newDocument:(id)sender;
- (void)openDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)saveDocumentAs:(id)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)closeDocument:(id)sender;
- (void)closeDocumentAtIndex:(size_t)index;
- (void)openRecent:(id)sender;
- (void)openPath:(NSString *)path asPreview:(BOOL)preview;
- (BOOL)confirmCloseAll;
- (void)notification:(SCNotification *)notification;
- (void)setRecentMenu:(NSMenu *)menu;
- (void)refreshRecentMenu;
- (BOOL)captureEditor;
- (void)refreshActionControls;
- (BOOL)validateMenuItem:(NSMenuItem *)menuItem;
- (BOOL)loadActiveDocument;
- (BOOL)selectDocumentAtIndex:(size_t)index;
- (NSInteger)sendEditorMessage:(unsigned int)message wParam:(uintptr_t)wParam
                        lParam:(intptr_t)lParam;
- (void)loadScintillaView;
- (BOOL)requireEditorFor:(NSString *)action;
- (BOOL)editorReadyForSave;
- (AxyneExplorerNode *)selectedExplorerNode;
- (void)editCut:(id)sender;
- (void)editCopy:(id)sender;
- (void)editPaste:(id)sender;
- (void)editSelectAll:(id)sender;
- (void)findInDocument:(id)sender;
- (void)replaceInDocument:(id)sender;
- (void)searchWorkspace:(id)sender;
- (void)openHelp:(id)sender;
- (CGFloat)sidebarWidth;
- (CGFloat)panelHeight;
- (NSRect)menuBarItemRect:(NSUInteger)index;
- (NSInteger)menuBarIndexAtPoint:(NSPoint)point;
- (void)openMenuBarMenu:(NSUInteger)index;
- (void)setMenuHover:(NSInteger)index;
- (BOOL)editorActionable;
- (void)goToLine:(id)sender;
- (void)selectLine:(id)sender;
- (void)toggleLineComment:(id)sender;
- (void)duplicateLine:(id)sender;
- (void)moveLineUp:(id)sender;
- (void)moveLineDown:(id)sender;
- (void)indentSelection:(id)sender;
- (void)outdentSelection:(id)sender;
- (void)zoomInEditor:(id)sender;
- (void)zoomOutEditor:(id)sender;
- (void)zoomResetEditor:(id)sender;
- (void)toggleWordWrap:(id)sender;
- (void)toggleExplorer:(id)sender;
- (void)togglePanel:(id)sender;
- (void)cancelBuild:(id)sender;
- (void)stopDebugger:(id)sender;
- (void)clearBreakpoints:(id)sender;
- (BOOL)preferencesFileExists;
- (void)openPreferencesFile:(id)sender;
- (void)showKeyboardShortcuts:(id)sender;
- (void)reportIssue:(id)sender;
- (void)showSettingsFolder:(id)sender;
- (BOOL)confirmCloseDocumentAtIndex:(size_t)index;
- (void)findOrReplace:(BOOL)replace;
- (void)searchFolder:(BOOL)quickFile;
- (void)openWorkspace:(id)sender;
- (void)newExplorerFile:(id)sender;
- (void)newExplorerFolder:(id)sender;
- (void)renameExplorerItem:(id)sender;
- (void)removeExplorerItem:(id)sender;
- (void)workspaceEvent;
- (BOOL)refreshExplorer;
- (void)scrollTabsBy:(CGFloat)delta;
- (void)showWorkspaceError:(NSString *)prefix error:(AxyneError *)error;
- (void)showWorkspaceMessage:(NSString *)message;
- (BOOL)selectWorkspaceURL:(NSURL *)url;
- (NSInteger)explorerNodeAtPoint:(NSPoint)point;
- (NSRect)tabFrameAtIndex:(size_t)index;
- (void)selectPanel:(id)sender;
- (void)clearOutput:(id)sender;
- (void)quickFile:(id)sender;
- (void)configureRunnerAction:(id)sender;
- (void)performExplorerOperation:(AxyneFileKind)kind;
- (NSString *)askForText:(NSString *)title label:(NSString *)label;
- (void)startTerminal:(id)sender;
- (void)stopTerminal:(id)sender;
- (void)sendTerminal:(id)sender;
- (void)terminalAppend:(const char *)bytes length:(size_t)length
                stream:(AxyneProcessStream)stream;
- (void)terminalExited:(AxyneProcess *)process exitCode:(int)exitCode;
- (BOOL)configureRunner;
- (void)buildDocument:(id)sender;
- (void)runDocument:(id)sender;
- (void)startDebugger:(id)sender;
- (void)debugCommand:(id)sender;
- (void)toggleBreakpoint:(id)sender;
- (void)showGlobalPreferences:(id)sender;
- (void)showWorkspacePreferences:(id)sender;
- (void)applyPreferences;
- (void)applyEditorScrollers;
- (void)applySystemAppearance;
- (void)updateChromeTitles;
- (void)applyEditorLexer;
- (void)updateLineNumberMargin;
- (void)updateBraceHighlight;
- (void)autoIndentFromNotification:(SCNotification *)notification;
- (BOOL)showPreferences:(BOOL)workspace;
- (BOOL)savePreferences:(AxynePreferences *)edited workspace:(BOOL)workspace;
- (void)showGitStatus:(id)sender;
- (void)showGitDiff:(id)sender;
- (void)stageAllGitChanges:(id)sender;
- (void)unstageAllGitChanges:(id)sender;
- (void)completeGitOperation:(AxyneMacGitCompletion *)completion
                         run:(AxyneMacGitRun *)run;
- (void)selectOutputPanel;
- (void)startGitOperationWithEmptyMessage:(const char *)emptyMessage
                                arguments:(const char *const *)arguments
                                   count:(size_t)argumentCount;
- (BOOL)ensureLsp;
- (BOOL)openLspForActive;
- (void)syncLspActive;
- (void)navigateLspReferences:(id)sender;
- (void)setLspStatus:(NSString *)status;
@end

@interface AxyneWorkspaceView (AxynePalette) <AxynePaletteOwner, NSTextFieldDelegate>
- (void)openPaletteWithInput:(NSString *)input;
- (void)closePaletteRestoringFocus:(BOOL)restore;
- (void)layoutPalette;
- (void)drawPaletteFieldInRect:(NSRect)box;
- (void)paletteSyncInput;
- (void)paletteSetText:(NSString *)text;
- (void)paletteTick:(NSTimer *)timer;
- (void)paletteEnter;
- (void)paletteDismissWithoutFocus;
- (void)paletteFocusField;
- (void)paletteGotoLine:(size_t)line column:(size_t)column;
- (void)paletteRunCommand:(AxynePaletteCommandId)command;
@end

static int axyne_macos_palette_document(void *user, char **path, char **text,
                                        size_t *length, size_t *lineCount);


static intptr_t axyne_macos_editor_message(void *editor, unsigned int message,
                                           uintptr_t wParam, intptr_t lParam)
{
    return [(AxyneWorkspaceView *)editor sendEditorMessage:message
        wParam:wParam lParam:lParam];
}

struct AxyneMacGitRun {
    AxyneWorkspaceView *view;
    /* Written by axyne_process_start before the worker thread exists, so the
     * exit callback may read it; the exit callback also receives the same
     * pointer as its first argument. */
    AxyneProcess *process;
    AxyneGitCapture capture;
    const char *const *arguments;
    size_t argument_count;
    const char *empty_message;
    pthread_mutex_t lock;
    int cancelled;
    int process_released;
    int references;
};

struct AxyneMacGitCompletion {
    AxyneWorkspaceView *view;
    AxyneProcess *process;
    /* Formatted on the worker thread; NULL when allocation failed. */
    char *report;
};

static void axyne_install_menu(NSApplication *application,
                               AxyneWorkspaceView *workspace);

typedef struct AxyneMacWorkspaceEvent {
    AxyneWatchEventKind kind;
    char *path;
} AxyneMacWorkspaceEvent;

typedef struct AxyneMacTerminalMessage {
    char *bytes;
    size_t length;
    AxyneProcessStream stream;
} AxyneMacTerminalMessage;

static void axyne_macos_lsp_status(AxyneWorkspaceView *view, const char *text)
{
    char *copy;
    if (view == nil || text == NULL) return;
    copy = strdup(text);
    if (copy == NULL) return;
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSString *status = [NSString stringWithUTF8String:copy];
        [view setLspStatus:status != nil ? status : @"LSP"];
        free(copy);
        [view release];
    });
}

static void axyne_macos_lsp_diagnostics(AxyneLspClient *client, const char *path,
                                        const AxyneLspDiagnostic *diagnostics,
                                        size_t count, void *user_data)
{
    char text[192];
    (void)client; (void)diagnostics;
    (void)snprintf(text, sizeof(text), "LSP: %zu diagnostics%s%s", count,
                   path == NULL ? "" : " in ", path == NULL ? "" : path);
    axyne_macos_lsp_status((AxyneWorkspaceView *)user_data, text);
}

static void axyne_macos_lsp_navigation(AxyneLspClient *client, uint64_t request_id,
                                       const AxyneLspLocation *locations,
                                       size_t count, void *user_data)
{
    char text[160];
    (void)client; (void)locations;
    (void)snprintf(text, sizeof(text), "LSP: request %llu returned %zu location%s",
                   (unsigned long long)request_id, count, count == 1 ? "" : "s");
    axyne_macos_lsp_status((AxyneWorkspaceView *)user_data, text);
}

static void axyne_macos_lsp_error(AxyneLspClient *client, AxyneStatus status,
                                  const char *message, void *user_data)
{
    char text[192];
    (void)client;
    (void)snprintf(text, sizeof(text), "LSP error (%d): %s", (int)status,
                   message == NULL ? "unknown error" : message);
    axyne_macos_lsp_status((AxyneWorkspaceView *)user_data, text);
}

static void axyne_macos_terminal_output(AxyneProcess *process,
                                         AxyneProcessStream stream,
                                         const char *bytes, size_t length,
                                         void *user_data)
{
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    AxyneMacTerminalMessage *message;
    (void)process;
    if (view == nil || bytes == NULL || length == 0 ||
        length > SIZE_MAX - sizeof(*message) - 1) return;
    message = (AxyneMacTerminalMessage *)malloc(sizeof(*message) + length + 1);
    if (message == NULL) return;
    message->bytes = (char *)(message + 1);
    memcpy(message->bytes, bytes, length);
    message->bytes[length] = '\0';
    message->length = length;
    message->stream = stream;
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [view terminalAppend:message->bytes length:message->length
                       stream:message->stream];
        [view release];
        free(message);
    });
}

static void axyne_macos_terminal_exit(AxyneProcess *process, int exit_code,
                                      void *user_data)
{
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    if (view == nil) return;
    [view retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [view terminalExited:process exitCode:exit_code];
        [view release];
    });
}

static void axyne_macos_watch_callback(const AxyneWatchEvent *event,
                                       void *user_data)
{
    AxyneMacWorkspaceEvent *copy;
    size_t length;
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user_data;
    if (event == NULL || event->path == NULL || view == nil) return;
    copy = (AxyneMacWorkspaceEvent *)calloc(1, sizeof(*copy));
    if (copy == NULL) return;
    length = strlen(event->path);
    copy->path = (char *)malloc(length + 1);
    if (copy->path == NULL) { free(copy); return; }
    memcpy(copy->path, event->path, length + 1);
    copy->kind = event->kind;
    dispatch_async(dispatch_get_main_queue(), ^{
        [view workspaceEvent];
        free(copy->path);
        free(copy);
    });
}

static void axyne_macos_runner_values_free(char **values, size_t count)
{
    size_t i;
    if (values == NULL) return;
    for (i = 0; i < count; ++i) free(values[i]);
    free(values);
}

static int axyne_macos_runner_split_lines(char *text, char ***values,
                                          size_t *count)
{
    char **items = NULL;
    size_t item_count = 0;
    char *cursor = text;
    if (values == NULL || count == NULL) return 0;
    *values = NULL;
    *count = 0;
    if (text == NULL) return 1;
    while (*cursor != '\0') {
        char *start = cursor;
        char *copy;
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') ++cursor;
        if (cursor != start) {
            copy = (char *)malloc((size_t)(cursor - start) + 1);
            if (copy == NULL) {
                axyne_macos_runner_values_free(items, item_count);
                return 0;
            }
            memcpy(copy, start, (size_t)(cursor - start));
            copy[cursor - start] = '\0';
            {
                char **grown = (char **)realloc(items,
                    (item_count + 1) * sizeof(*grown));
                if (grown == NULL) {
                    free(copy);
                    axyne_macos_runner_values_free(items, item_count);
                    return 0;
                }
                items = grown;
            }
            items[item_count++] = copy;
        }
        while (*cursor == '\r' || *cursor == '\n') ++cursor;
    }
    *values = items;
    *count = item_count;
    return 1;
}

static NSString *axyne_macos_runner_lines(char **values, size_t count)
{
    NSMutableString *result = [NSMutableString string];
    for (size_t i = 0; i < count; ++i) {
        NSString *value = [NSString stringWithUTF8String:values[i]];
        if (i != 0) [result appendString:@"\n"];
        [result appendString:value != nil ? value : @""];
    }
    return result;
}

static NSTextField *axyne_macos_label(NSString *text, CGFloat y)
{
    NSTextField *field = [NSTextField labelWithString:text];
    [field setFrame:NSMakeRect(0, y, 460, 20)];
    return field;
}

static NSButton *axyne_macos_toolbar_button(NSString *title, id target,
                                            SEL action)
{
    NSButton *button = [[[AxyneChromeButton alloc] initWithFrame:NSZeroRect] autorelease];
    [button setTitle:title];
    [button setTarget:target];
    [button setAction:action];
    [button setBordered:NO];
    [button setBezelStyle:NSBezelStyleTexturedRounded];
    [button setFont:[NSFont systemFontOfSize:12.0]];
    [button setContentTintColor:axyne_color(115, 119, 128)];
    [button setToolTip:title];
    return [button retain];
}

@implementation AxyneWorkspaceView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) {
        _activeMenuIndex = -1;
        _hoverMenuIndex = -1;
        axyne_palette_ctl_init(&_palette, 1, axyne_macos_palette_document, self);
        if (axyne_explorer_initialize(&_explorer, NULL) != AXYNE_STATUS_OK ||
            axyne_documents_initialize(&_documents, NULL) != AXYNE_STATUS_OK) {
            axyne_explorer_destroy(&_explorer);
            [self release];
            return nil;
        }
        {
            AxyneRunnerSpec spec = {0};
            spec.executable = "/bin/sh";
            if (axyne_runner_initialize(&_terminalRunner, NULL) != AXYNE_STATUS_OK ||
                axyne_runner_configure(&_terminalRunner, &spec, NULL) != AXYNE_STATUS_OK ||
                axyne_runner_initialize(&_actionRunner, NULL) != AXYNE_STATUS_OK) {
                axyne_documents_destroy(&_documents);
                axyne_explorer_destroy(&_explorer);
                [self release];
                return nil;
            }
        }
        if (axyne_debugger_initialize(&_debugger, NULL) != AXYNE_STATUS_OK ||
            axyne_debugger_configure_default(&_debugger, NULL) != AXYNE_STATUS_OK) {
            axyne_runner_destroy(&_terminalRunner);
            axyne_runner_destroy(&_actionRunner);
            axyne_documents_destroy(&_documents);
            axyne_explorer_destroy(&_explorer);
            [self release];
            return nil;
        }
        {
            AxyneError preferenceError;
            AxyneStatus status;
            axyne_preferences_defaults(&_globalPreferences);
            _globalPreferencesPath = axyne_macos_global_preferences_path();
            status = _globalPreferencesPath == NULL ? AXYNE_STATUS_NOT_FOUND :
                axyne_preferences_load_global(_globalPreferencesPath,
                                               &_globalPreferences,
                                               &preferenceError);
            _preferences = _globalPreferences;
            if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_NOT_FOUND)
                [self showWorkspaceError:@"Unable to load preferences" error:&preferenceError];
        }
        _terminalOutput = [[NSTextView alloc] initWithFrame:NSZeroRect];
        [_terminalOutput setEditable:NO];
        [_terminalOutput setSelectable:YES];
        [_terminalOutput setFont:[NSFont fontWithName:@"Menlo" size:11]];
        [_terminalOutput setTextColor:axyne_preference_color(_preferences.theme.text)];
        [_terminalOutput setBackgroundColor:axyne_preference_color(_preferences.theme.background)];
        _terminalScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        [_terminalScroll setBorderType:NSNoBorder];
        [_terminalScroll setHasVerticalScroller:YES];
        [_terminalScroll setAutohidesScrollers:YES];
        [_terminalScroll setDocumentView:_terminalOutput];
        [_terminalOutput setVerticallyResizable:YES];
        [_terminalOutput setHorizontallyResizable:NO];
        [_terminalOutput setAutoresizingMask:NSViewWidthSizable];
        [[_terminalOutput textContainer] setWidthTracksTextView:YES];
        [_terminalOutput setTextContainerInset:NSMakeSize(0, 6)];
        [self addSubview:_terminalScroll];
        _newButton = axyne_macos_toolbar_button(@"▱", self, @selector(newDocument:));
        _openButton = axyne_macos_toolbar_button(@"▰", self, @selector(openDocument:));
        _saveButton = axyne_macos_toolbar_button(@"▣", self, @selector(saveDocument:));
        _undoButton = axyne_macos_toolbar_button(@"↶", self, @selector(undo:));
        _redoButton = axyne_macos_toolbar_button(@"↷", self, @selector(redo:));
        _targetButton = axyne_macos_toolbar_button(@"Runner 설정", self, @selector(configureRunnerAction:));
        _searchButton = axyne_macos_toolbar_button(@"파일 이동", self, @selector(quickFile:));
        _buildButton = axyne_macos_toolbar_button(@"빌드  ⌘B", self, @selector(buildDocument:));
        _runButton = axyne_macos_toolbar_button(@"실행  F5", self, @selector(runDocument:));
        [self addSubview:_newButton]; [self addSubview:_openButton];
        [self addSubview:_saveButton]; [self addSubview:_undoButton];
        [self addSubview:_redoButton]; [self addSubview:_buildButton];
        [self addSubview:_runButton];
        [self addSubview:_targetButton]; [self addSubview:_searchButton];
        _outputTab = axyne_macos_toolbar_button(@"출력", self, @selector(selectPanel:));
        _problemsTab = axyne_macos_toolbar_button(@"문제", self, @selector(selectPanel:));
        _terminalTab = axyne_macos_toolbar_button(@"터미널", self, @selector(selectPanel:));
        [_outputTab setTag:0]; [_problemsTab setTag:1]; [_terminalTab setTag:2];
        [self addSubview:_outputTab]; [self addSubview:_problemsTab]; [self addSubview:_terminalTab];
        _clearOutput = axyne_macos_toolbar_button(@"⊘", self, @selector(clearOutput:));
        [_clearOutput setToolTip:@"출력 지우기"];
        [self addSubview:_clearOutput];
        _problemSummary = [[NSTextField labelWithString:@"LSP 진단 없음"] retain];
        [self addSubview:_problemSummary];
        _terminalInput = [[NSTextField alloc] initWithFrame:NSZeroRect];
        [_terminalInput setPlaceholderString:@"Terminal input"];
        [_terminalInput setTarget:self]; [_terminalInput setAction:@selector(sendTerminal:)];
        [self addSubview:_terminalInput];
        _terminalStart = axyne_macos_toolbar_button(@"▷", self, @selector(startTerminal:));
        [_terminalStart setToolTip:@"터미널 시작"];
        [_terminalStart setTarget:self]; [_terminalStart setAction:@selector(startTerminal:)];
        [self addSubview:_terminalStart];
        _terminalStop = axyne_macos_toolbar_button(@"×", self, @selector(stopTerminal:));
        [_terminalStop setToolTip:@"프로세스 중지"]; [_terminalStop setTarget:self];
        [_terminalStop setAction:@selector(stopTerminal:)]; [_terminalStop setEnabled:NO];
        [self addSubview:_terminalStop];
        _terminalSend = axyne_macos_toolbar_button(@"전송", self, @selector(sendTerminal:));
        [_terminalSend setTarget:self];
        [_terminalSend setAction:@selector(sendTerminal:)];
        [self addSubview:_terminalSend];
        _debugStart = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugStart setTitle:@"Debug"]; [_debugStart setTarget:self];
        [_debugStart setAction:@selector(startDebugger:)];
        [self addSubview:_debugStart];
        _debugPause = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugPause setTitle:@"Pause"]; [_debugPause setTarget:self];
        [_debugPause setAction:@selector(debugCommand:)]; [_debugPause setTag:1];
        [_debugPause setEnabled:NO]; [self addSubview:_debugPause];
        _debugContinue = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugContinue setTitle:@"Continue"]; [_debugContinue setTarget:self];
        [_debugContinue setAction:@selector(debugCommand:)]; [_debugContinue setTag:0];
        [_debugContinue setEnabled:NO]; [self addSubview:_debugContinue];
        _debugNext = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugNext setTitle:@"Next"]; [_debugNext setTarget:self];
        [_debugNext setAction:@selector(debugCommand:)]; [_debugNext setTag:2];
        [_debugNext setEnabled:NO]; [self addSubview:_debugNext];
        _debugBreakpoint = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_debugBreakpoint setTitle:@"Breakpoint"]; [_debugBreakpoint setTarget:self];
        [_debugBreakpoint setAction:@selector(toggleBreakpoint:)];
        [_debugBreakpoint setEnabled:NO]; [self addSubview:_debugBreakpoint];
        for (NSButton *button in @[_debugStart, _debugPause, _debugContinue,
                                   _debugNext, _debugBreakpoint]) [button setHidden:YES];
        [_newButton setToolTip:@"새 파일 (⌘N)"];
        [_openButton setToolTip:@"파일 열기 (⌘O)"];
        [_saveButton setToolTip:@"저장 (⌘S)"];
        [_undoButton setToolTip:@"실행 취소 (⌘Z)"];
        [_redoButton setToolTip:@"다시 실행 (⇧⌘Z)"];
        [_searchButton setToolTip:@"파일 이동 (⌘P)"];
        [self applyPreferences];
        [self refreshActionControls];
    }
    return self;
}

- (void)updateChromeTitles
{
    BOOL reference = axyne_macos_reference_surfaces(&_preferences.theme);
    AxyneChromeButton *target = (AxyneChromeButton *)_targetButton;
    AxyneChromeButton *run = (AxyneChromeButton *)_runButton;
    AxyneChromeButton *search = (AxyneChromeButton *)_searchButton;
    if (target == nil || run == nil || search == nil) return;
    NSColor *muted = axyne_preference_color(_preferences.theme.muted);
    NSColor *text = axyne_preference_color(_preferences.theme.text);
    NSColor *placeholder = axyne_preference_color(reference ? 0x4f535b : _preferences.theme.muted);
    NSColor *shortcut = axyne_preference_color(reference ? 0x6c727c : _preferences.theme.muted);
    NSColor *onAccent = axyne_preference_color(reference ? 0x181a1f : _preferences.theme.background);
    NSFont *small = [NSFont systemFontOfSize:11];
    [target setLeadingAligned:YES]; [target setContentInset:10];
    [target setRichTitle:axyne_macos_segments(@[
        @[@"▷  ", small, text], @[[target title], small, muted],
        @[@"  ⌄", [NSFont systemFontOfSize:10], muted]])];
    [run setContentInset:14];
    [run setRichTitle:axyne_macos_segments(@[
        @[@"▷  ", small, onAccent],
        @[@"실행  F5", [NSFont boldSystemFontOfSize:11], onAccent]])];
    [search setLeadingAligned:YES]; [search setContentInset:10];
    [search setRichTitle:axyne_macos_segments(@[
        @[@"⌕  ", [NSFont systemFontOfSize:12], muted],
        @[@"파일 이동", small, placeholder]])];
    [search setTrailingTitle:axyne_macos_segments(@[
        @[@"⌘P", [NSFont systemFontOfSize:10], shortcut]])];
    for (NSButton *button in @[target, run, search]) [button setNeedsDisplay:YES];
}

- (AxyneDocument *)activeDocument
{
    if (_documents.count == 0 || _documents.active_index >= _documents.count)
        return NULL;
    return &_documents.documents[_documents.active_index];
}

- (void)refreshActionControls
{
    AxyneDocument *document = [self activeDocument];
    BOOL terminalActive = _terminalProcess != NULL;
    BOOL debuggerActive = axyne_debugger_is_active(&_debugger);
    BOOL savedDocument = document != NULL && !document->is_untitled &&
        document->path != NULL && !document->is_dirty;
    [_terminalStart setEnabled:!terminalActive && !debuggerActive];
    [_terminalStop setEnabled:terminalActive];
    [_terminalSend setEnabled:terminalActive];
    [_debugStart setEnabled:!terminalActive && !debuggerActive && savedDocument];
    [_debugPause setEnabled:debuggerActive && savedDocument];
    [_debugContinue setEnabled:debuggerActive && savedDocument];
    [_debugNext setEnabled:debuggerActive && savedDocument];
    [_debugBreakpoint setEnabled:savedDocument];
    [_buildButton setEnabled:document != NULL && !terminalActive && !debuggerActive];
    [_runButton setEnabled:document != NULL && !terminalActive && !debuggerActive];
    [_saveButton setEnabled:document != NULL];
    NSString *target = _actionRunner.executable == NULL ? @"Runner 설정" :
        [[NSString stringWithUTF8String:_actionRunner.executable] lastPathComponent];
    [_targetButton setTitle:target];
    [self updateChromeTitles];
    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];
}

- (void)quickFile:(id)sender { (void)sender; [self openPaletteWithInput:@""]; }
- (void)configureRunnerAction:(id)sender { (void)sender; (void)[self configureRunner]; [self refreshActionControls]; }
- (void)clearOutput:(id)sender { (void)sender; [_terminalOutput setString:@""]; }
- (void)selectPanel:(id)sender
{
    _panelMode = [sender tag];
    _panelHidden = NO;
    [_problemSummary setStringValue:_lspStatus != nil ? _lspStatus : @"LSP 진단 없음"];
    [self setNeedsLayout:YES]; [self setNeedsDisplay:YES];
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem
{
    SEL action = [menuItem action];
    AxyneDocument *document = [self activeDocument];
    BOOL hasDocument = document != NULL;
    BOOL savedDocument = hasDocument && !document->is_untitled &&
        document->path != NULL && !document->is_dirty;
    BOOL terminalActive = _terminalProcess != NULL;
    BOOL debuggerActive = axyne_debugger_is_active(&_debugger);
    if (action == @selector(saveDocument:) ||
        action == @selector(saveDocumentAs:))
        return hasDocument;
    if (action == @selector(closeDocument:))
        return hasDocument && !axyne_document_tab_hidden(document);
    if (action == @selector(buildDocument:) ||
        action == @selector(runDocument:))
        return hasDocument && !terminalActive && !debuggerActive;
    if (action == @selector(startDebugger:))
        return savedDocument && !terminalActive && !debuggerActive;
    if (action == @selector(debugCommand:))
        return debuggerActive && savedDocument;
    if (action == @selector(toggleBreakpoint:)) return savedDocument;
    if (action == @selector(startTerminal:))
        return !terminalActive && !debuggerActive;
    if (action == @selector(stopTerminal:) ||
        action == @selector(sendTerminal:)) return terminalActive;
    if (action == @selector(showWorkspacePreferences:))
        return _workspacePreferencesPath != NULL;
    if (action == @selector(showGitStatus:) ||
        action == @selector(showGitDiff:) ||
        action == @selector(stageAllGitChanges:) ||
        action == @selector(unstageAllGitChanges:))
        return _explorer.root != NULL && _gitProcess == NULL;
    if (action == @selector(navigateLspReferences:)) return savedDocument;
    if (action == @selector(findInDocument:) ||
        action == @selector(replaceInDocument:))
        return hasDocument && _editorView != nil;
    if (action == @selector(goToLine:) || action == @selector(selectLine:) ||
        action == @selector(duplicateLine:) || action == @selector(moveLineUp:) ||
        action == @selector(moveLineDown:) || action == @selector(indentSelection:) ||
        action == @selector(outdentSelection:))
        return [self editorActionable];
    if (action == @selector(toggleLineComment:))
        return [self editorActionable] &&
            axyne_editor_comment_token(document->path) != NULL;
    if (action == @selector(zoomInEditor:) || action == @selector(zoomOutEditor:) ||
        action == @selector(zoomResetEditor:)) return _editorView != nil;
    if (action == @selector(toggleWordWrap:)) {
        [menuItem setState:_preferences.editor.word_wrap
            ? NSControlStateValueOn : NSControlStateValueOff];
        return _editorView != nil;
    }
    if (action == @selector(toggleExplorer:)) {
        [menuItem setState:_explorerHidden ? NSControlStateValueOff : NSControlStateValueOn];
        return YES;
    }
    if (action == @selector(togglePanel:)) {
        [menuItem setState:_panelHidden ? NSControlStateValueOff : NSControlStateValueOn];
        return YES;
    }
    if (action == @selector(selectPanel:)) {
        [menuItem setState:!_panelHidden && [menuItem tag] == _panelMode
            ? NSControlStateValueOn : NSControlStateValueOff];
        return YES;
    }
    if (action == @selector(cancelBuild:))
        return _activeAction == 1 && _terminalProcess != NULL;
    if (action == @selector(stopDebugger:)) return debuggerActive;
    if (action == @selector(clearBreakpoints:))
        return axyne_debugger_enabled_breakpoints(&_debugger) != 0;
    if (action == @selector(openPreferencesFile:)) return [self preferencesFileExists];
    if (action == @selector(undo:) || action == @selector(redo:)) {
        if (_editorView == nil) return NO;
        return [self sendEditorMessage:action == @selector(undo:)
            ? SCI_CANUNDO : SCI_CANREDO wParam:0 lParam:0] != 0;
    }
    if (action == @selector(editCut:) || action == @selector(editCopy:) ||
        action == @selector(editPaste:) || action == @selector(editSelectAll:)) {
        SEL standard = action == @selector(editCut:) ? @selector(cut:) :
            action == @selector(editCopy:) ? @selector(copy:) :
            action == @selector(editPaste:) ? @selector(paste:) :
            @selector(selectAll:);
        NSResponder *external = [self externalTextResponder];
        if (external != nil) return [external respondsToSelector:standard];
        if (_editorView == nil) return NO;
        if (action == @selector(editPaste:))
            return [self sendEditorMessage:SCI_CANPASTE wParam:0 lParam:0] != 0;
        if (action == @selector(editSelectAll:)) return YES;
        return [self sendEditorMessage:SCI_GETSELECTIONSTART wParam:0 lParam:0] !=
            [self sendEditorMessage:SCI_GETSELECTIONEND wParam:0 lParam:0];
    }
    return YES;
}

/* Returns a text-editing responder (such as the terminal input's field
 * editor) that should receive Edit commands instead of the source editor. */
- (NSResponder *)externalTextResponder
{
    /* Use the key window: prompts and the preferences window are separate
     * windows whose field editors must receive Edit commands. */
    NSWindow *keyWindow = [NSApp keyWindow] != nil ? [NSApp keyWindow] : [self window];
    NSResponder *responder = [keyWindow firstResponder];
    if (responder == nil || ![responder isKindOfClass:[NSText class]]) return nil;
    if (_editorView != nil && [responder isKindOfClass:[NSView class]] &&
        [(NSView *)responder isDescendantOf:_editorView]) return nil;
    return responder;
}

- (NSInteger)sendEditorMessage:(unsigned int)message wParam:(uintptr_t)wParam
                         lParam:(intptr_t)lParam
{
    if (_editorView == nil) return 0;
    return [_editorView message:message wParam:wParam lParam:lParam];
}

- (void)applyEditorLexer
{
    AxyneDocument *document = [self activeDocument];
    const AxyneSyntaxLanguage *language = axyne_syntax_for_path(
        document == NULL ? NULL : document->path);
    BOOL reference = axyne_macos_reference_surfaces(&_preferences.theme);
    void *lexer;
    if (_editorView == nil || _createLexer == NULL) return;
    lexer = _createLexer(language->lexer);
    if (lexer == NULL) {
        NSLog(@"Axyne: Lexilla CreateLexer(\"%s\") failed; using plain text",
              language->lexer);
        lexer = _createLexer("null");
    }
    if (lexer == NULL) {
        NSLog(@"Axyne: Lexilla CreateLexer(\"null\") failed; no lexer applied");
        return;
    }
    (void)[self sendEditorMessage:SCI_SETILEXER wParam:0
                             lParam:(intptr_t)lexer];
    /* The previous language may have coloured styles this one does not use.
     * STYLECLEARALL already ran (after the font was set) in applyPreferences;
     * here the lexer-owned ids return to the text colour so a language switch
     * cannot leak colours. Ids 32-39 are Scintilla's own and are left alone. */
    for (unsigned int style = 0; style < 128; ++style) {
        if (style >= 32 && style < 40) continue;
        (void)[self sendEditorMessage:SCI_STYLESETFORE wParam:style
            lParam:axyne_editor_color(_preferences.theme.editor_text)];
    }
    for (unsigned int set = 0; set < AXYNE_SYNTAX_KEYWORD_SETS; ++set) {
        if (language->keywords[set] == NULL) continue;
        (void)[self sendEditorMessage:SCI_SETKEYWORDS wParam:set
            lParam:(intptr_t)language->keywords[set]];
    }
    for (size_t i = 0; i < language->style_count; ++i) {
        uint32_t color = language->styles[i].color;
        if (!reference && color == AXYNE_SYNTAX_PLAIN)
            color = _preferences.theme.editor_text;
        (void)[self sendEditorMessage:SCI_STYLESETFORE
            wParam:language->styles[i].style lParam:axyne_editor_color(color)];
    }
    (void)[self sendEditorMessage:SCI_COLOURISE wParam:0 lParam:-1];
}

- (void)updateLineNumberMargin
{
    NSInteger lineCount;
    NSInteger width;
    char digits[32];
    if (_editorView == nil) return;
    if (!_preferences.editor.line_numbers) {
        (void)[self sendEditorMessage:SCI_SETMARGINWIDTHN wParam:0 lParam:0];
        return;
    }
    lineCount = [self sendEditorMessage:SCI_GETLINECOUNT wParam:0 lParam:0];
    if (lineCount < 1) lineCount = 1;
    (void)snprintf(digits, sizeof(digits), "%lld", (long long)lineCount);
    width = [self sendEditorMessage:SCI_TEXTWIDTH wParam:33
                              lParam:(intptr_t)digits];
    if (width < 1) width = 32;
    (void)[self sendEditorMessage:SCI_SETMARGINWIDTHN wParam:0
                             lParam:MAX(52, width + 10)];
}

- (void)updateBraceHighlight
{
    NSInteger caret;
    NSInteger brace = -1;
    NSInteger match;
    if (_editorView == nil) return;
    caret = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    if (caret >= 0) {
        int character = (int)[self sendEditorMessage:SCI_GETCHARAT
            wParam:(uintptr_t)caret lParam:0];
        if (character == '{' || character == '}' || character == '(' ||
            character == ')' || character == '[' || character == ']')
            brace = caret;
    }
    if (brace < 0 && caret > 0) {
        int character = (int)[self sendEditorMessage:SCI_GETCHARAT
            wParam:(uintptr_t)(caret - 1) lParam:0];
        if (character == '{' || character == '}' || character == '(' ||
            character == ')' || character == '[' || character == ']')
            brace = caret - 1;
    }
    if (brace < 0) {
        (void)[self sendEditorMessage:SCI_BRACEHIGHLIGHT wParam:(uintptr_t)-1
                                 lParam:-1];
        return;
    }
    match = [self sendEditorMessage:SCI_BRACEMATCH wParam:(uintptr_t)brace
                              lParam:0];
    if (match >= 0)
        (void)[self sendEditorMessage:SCI_BRACEHIGHLIGHT
                                 wParam:(uintptr_t)brace lParam:match];
    else
        (void)[self sendEditorMessage:SCI_BRACEBADLIGHT
                                 wParam:(uintptr_t)brace lParam:0];
}

- (void)autoIndentFromNotification:(SCNotification *)notification
{
    NSInteger line;
    NSInteger previousLine;
    NSInteger indentation;
    NSInteger lineStart;
    NSInteger previousStart;
    NSInteger position;
    unsigned int tabWidth = _preferences.editor.tab_width;
    int lastCharacter = 0;
    if (_editorView == nil || notification == NULL) return;
    if (!_preferences.editor.auto_indent) return;
    if (tabWidth == 0) tabWidth = 4;
    if (notification->ch == '\n') {
        line = [self sendEditorMessage:SCI_LINEFROMPOSITION
                                 wParam:(uintptr_t)(notification->position + 1)
                                 lParam:0];
        if (line <= 0) return;
        previousLine = line - 1;
        indentation = [self sendEditorMessage:SCI_GETLINEINDENTATION
            wParam:(uintptr_t)previousLine lParam:0];
        lineStart = [self sendEditorMessage:SCI_POSITIONFROMLINE
            wParam:(uintptr_t)line lParam:0];
        previousStart = [self sendEditorMessage:SCI_POSITIONFROMLINE
            wParam:(uintptr_t)previousLine lParam:0];
        position = lineStart - 1;
        while (position >= previousStart) {
            int character = (int)[self sendEditorMessage:SCI_GETCHARAT
                wParam:(uintptr_t)position lParam:0];
            if (character != ' ' && character != '\t' && character != '\r' &&
                character != '\n') {
                lastCharacter = character;
                break;
            }
            --position;
        }
        if (lastCharacter == '{') indentation += (NSInteger)tabWidth;
        (void)[self sendEditorMessage:SCI_SETLINEINDENTATION
            wParam:(uintptr_t)line lParam:indentation];
    } else if (notification->ch == '}') {
        line = [self sendEditorMessage:SCI_LINEFROMPOSITION
            wParam:(uintptr_t)notification->position lParam:0];
        indentation = [self sendEditorMessage:SCI_GETLINEINDENTATION
            wParam:(uintptr_t)line lParam:0];
        if (indentation >= (NSInteger)tabWidth)
            (void)[self sendEditorMessage:SCI_SETLINEINDENTATION
                wParam:(uintptr_t)line lParam:indentation - tabWidth];
    }
}

- (void)applyPreferences
{
    [self applySystemAppearance];
    NSString *fontName = _preferences.editor.font_family[0] != '\0'
        ? [NSString stringWithUTF8String:_preferences.editor.font_family]
        : @"Menlo";
    const char *fontUTF8 = [fontName UTF8String];
    unsigned int fontSize = _preferences.editor.font_size;
    if (fontSize < 6 || fontSize > 72) fontSize = 11;
    NSFont *terminalFont = [NSFont fontWithName:fontName size:fontSize];
    if (terminalFont == nil)
        terminalFont = [NSFont userFixedPitchFontOfSize:fontSize];
    [_terminalOutput setFont:terminalFont];
    [_terminalOutput setTextColor:axyne_preference_color(
        axyne_macos_reference_surfaces(&_preferences.theme) ? 0xa9aeb6
                                                           : _preferences.theme.text)];
    [_terminalOutput setBackgroundColor:axyne_preference_color(
        axyne_macos_output_background(&_preferences.theme))];
    [_terminalScroll setBackgroundColor:[_terminalOutput backgroundColor]];
    [_problemSummary setTextColor:axyne_preference_color(_preferences.theme.text)];
    [_terminalInput setTextColor:axyne_preference_color(_preferences.theme.text)];
    [_terminalInput setBackgroundColor:axyne_preference_color(_preferences.theme.panel)];
    [_terminalInput setDrawsBackground:YES];
    for (NSButton *button in @[_terminalStart, _terminalStop, _terminalSend,
                               _debugStart, _debugPause, _debugContinue,
                               _debugNext, _debugBreakpoint]) {
        [button setBezelStyle:NSBezelStyleTexturedRounded];
        [button setContentTintColor:axyne_preference_color(_preferences.theme.text)];
    }
    for (NSButton *button in @[_newButton, _openButton, _saveButton,
                               _undoButton, _redoButton, _buildButton,
                               _runButton, _targetButton, _searchButton,
                               _outputTab, _problemsTab, _terminalTab,
                               _terminalStart, _terminalStop, _terminalSend, _clearOutput]) {
        [(AxyneChromeButton *)button setLabelColor:axyne_preference_color(_preferences.theme.muted)];
        [(AxyneChromeButton *)button setFillColor:nil];
        [button setFont:[NSFont systemFontOfSize:11]];
        [button setNeedsDisplay:YES];
    }
    for (NSButton *button in @[_newButton, _openButton, _saveButton, _undoButton, _redoButton])
        [button setFont:[NSFont systemFontOfSize:14]];
    NSColor *control = axyne_preference_color(axyne_macos_reference_surfaces(&_preferences.theme)
        ? 0x24262b : _preferences.theme.toolbar);
    [(AxyneChromeButton *)_targetButton setFillColor:control];
    [(AxyneChromeButton *)_buildButton setFillColor:control];
    [(AxyneChromeButton *)_buildButton setLabelColor:axyne_preference_color(_preferences.theme.text)];
    [(AxyneChromeButton *)_runButton setFillColor:axyne_preference_color(_preferences.theme.accent)];
    [(AxyneChromeButton *)_runButton setLabelColor:axyne_preference_color(_preferences.theme.background)];
    [(AxyneChromeButton *)_searchButton setFillColor:axyne_preference_color(_preferences.theme.background)];
    BOOL reference = axyne_macos_reference_surfaces(&_preferences.theme);
    [(AxyneChromeButton *)_runButton setCornerRadius:4];
    [(AxyneChromeButton *)_searchButton setCornerRadius:4];
    [(AxyneChromeButton *)_searchButton setStrokeColor:axyne_preference_color(
        reference ? 0x3a3d44 : _preferences.theme.border)];
    [self updateChromeTitles];
    if (_editorView != nil) {
        [self sendEditorMessage:SCI_STYLESETFORE wParam:32
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:32
                              lParam:axyne_editor_color(_preferences.theme.editor_background)];
        /* STYLECLEARALL copies style 32 into every style, so the font must
         * be set first or lexer styles never inherit it. */
        [self sendEditorMessage:SCI_STYLESETSIZE wParam:32 lParam:(intptr_t)fontSize];
        [self sendEditorMessage:SCI_STYLESETFONT wParam:32 lParam:(intptr_t)fontUTF8];
        [self sendEditorMessage:SCI_STYLECLEARALL wParam:0 lParam:0];
        [self sendEditorMessage:SCI_STYLESETFORE wParam:33
                              lParam:axyne_editor_color(reference ? 0x5a606a : _preferences.theme.muted)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:33
                              lParam:axyne_editor_color(reference
                                  ? _preferences.theme.editor_background : _preferences.theme.panel)];
        [self sendEditorMessage:SCI_STYLESETFORE wParam:STYLE_BRACELIGHT
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:STYLE_BRACELIGHT
                              lParam:axyne_editor_color(_preferences.theme.accent)];
        [self sendEditorMessage:SCI_STYLESETFORE wParam:STYLE_BRACEBAD
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_STYLESETBACK wParam:STYLE_BRACEBAD
                              lParam:axyne_editor_color(_preferences.theme.accent)];
        [self sendEditorMessage:SCI_SETSELFORE wParam:0
                              lParam:axyne_editor_color(_preferences.theme.editor_text)];
        [self sendEditorMessage:SCI_SETSELBACK wParam:1
                              lParam:axyne_editor_color(_preferences.theme.accent)];
        /* SCI_SETCARETFORE takes the colour in wParam; lParam is ignored. */
        [self sendEditorMessage:SCI_SETCARETFORE
                              wParam:(uintptr_t)axyne_editor_color(_preferences.theme.accent)
                              lParam:0];
        [self sendEditorMessage:SCI_SETCARETLINEVISIBLE wParam:1 lParam:0];
        [self sendEditorMessage:SCI_SETCARETLINEBACK
                              wParam:(uintptr_t)axyne_editor_color(reference
                                  ? 0x202328 : _preferences.theme.toolbar)
                              lParam:0];
        /* Figma code rows are 19px at 13pt; scale that ratio to the chosen
         * size by padding the font's natural line height. */
        [self sendEditorMessage:SCI_SETEXTRAASCENT wParam:0 lParam:0];
        [self sendEditorMessage:SCI_SETEXTRADESCENT wParam:0 lParam:0];
        NSInteger naturalHeight = [self sendEditorMessage:SCI_TEXTHEIGHT wParam:0 lParam:0];
        NSInteger extraHeight = (NSInteger)lround(fontSize * 19.0 / 13.0) - naturalHeight;
        if (extraHeight > 0) {
            [self sendEditorMessage:SCI_SETEXTRAASCENT
                                  wParam:(uintptr_t)((extraHeight + 1) / 2) lParam:0];
             [self sendEditorMessage:SCI_SETEXTRADESCENT
                                   wParam:(uintptr_t)(extraHeight / 2) lParam:0];
         }
        [self sendEditorMessage:SCI_SETINDENT wParam:_preferences.editor.tab_width lParam:0];
        [self sendEditorMessage:SCI_SETTABWIDTH wParam:_preferences.editor.tab_width lParam:0];
        [self sendEditorMessage:SCI_SETUSETABS wParam:_preferences.editor.insert_spaces ? 0 : 1 lParam:0];
        [self sendEditorMessage:SCI_SETWRAPMODE wParam:_preferences.editor.word_wrap ? 1 : 0 lParam:0];
        [self sendEditorMessage:SCI_SETVIEWWS wParam:_preferences.editor.show_whitespace ? 1 : 0 lParam:0];
        [self sendEditorMessage:SCI_SETCARETLINEBACK
                         wParam:(uintptr_t)axyne_editor_color(axyne_macos_caret_line_color(&_preferences.theme))
                         lParam:0];
        [self sendEditorMessage:SCI_SETCARETLINEVISIBLE
                         wParam:_preferences.editor.highlight_current_line ? 1 : 0
                         lParam:0];
        [self applyEditorScrollers];
        [self updateLineNumberMargin];
        [self updateBraceHighlight];
        [self applyEditorLexer];
    }
    [self setNeedsDisplay:YES];
}

- (void)applyEditorScrollers
{
    if (_editorView == nil) return;
    BOOL dark = axyne_macos_color_is_dark(_preferences.theme.editor_background);
    /* Scintilla's default scroll width is 2000, which keeps the horizontal bar
     * permanently scrollable. Start at 1 and let tracking grow it only to the
     * widest line seen; this call also shrinks it again on every document load. */
    NSInteger xoffset = [self sendEditorMessage:SCI_GETXOFFSET wParam:0 lParam:0];
    (void)[self sendEditorMessage:SCI_SETSCROLLWIDTH wParam:1 lParam:0];
    (void)[self sendEditorMessage:SCI_SETSCROLLWIDTHTRACKING wParam:1 lParam:0];
    /* The reset clamps the horizontal position; restoring a non-zero offset
     * makes Scintilla widen the scroll range again for the long line. */
    if (xoffset > 0) (void)[self sendEditorMessage:SCI_SETXOFFSET wParam:(uintptr_t)xoffset lParam:0];
    NSScrollView *scroll = nil;
    if ([_editorView respondsToSelector:@selector(scrollView)])
        scroll = [(id)_editorView scrollView];
    if (![scroll isKindOfClass:[NSScrollView class]])
        scroll = axyne_macos_find_scroll_view(_editorView);
    /* NSAppearance assignment needs macOS 10.14; older systems keep the default. */
    if ([_editorView respondsToSelector:@selector(setAppearance:)]) {
        NSAppearance *appearance = [NSAppearance appearanceNamed:
            dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
        if (appearance != nil) {
            [_editorView setAppearance:appearance];
            if (scroll != nil) [scroll setAppearance:appearance];
        }
    }
    if (scroll != nil) {
        /* Overlay scrollers float above the text, so no opaque track is drawn
         * and the horizontal bar no longer starts right of the line-number
         * ruler (which left a blank corner with legacy scrollers). */
        [scroll setScrollerStyle:NSScrollerStyleOverlay];
        [scroll setAutohidesScrollers:YES];
        [scroll setScrollerKnobStyle:dark ? NSScrollerKnobStyleLight
                                          : NSScrollerKnobStyleDark];
        [scroll tile];
    }
}

- (void)applySystemAppearance
{
    if (_preferences.theme.preset == AXYNE_THEME_SYSTEM) {
        axyne_macos_select_theme(&_preferences.theme,
            axyne_macos_prefers_dark(self) ? AXYNE_THEME_DARK : AXYNE_THEME_LIGHT);
        _preferences.theme.preset = AXYNE_THEME_SYSTEM;
    }
}

- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    if (_preferences.theme.preset == AXYNE_THEME_SYSTEM)
        [self applyPreferences];
}

- (BOOL)captureEditorSnapshot
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || _editorView == nil) return YES;
    NSInteger length = [self sendEditorMessage:SCI_GETTEXTLENGTH wParam:0 lParam:0];
    if (length < 0 || (uint64_t)length >= SIZE_MAX) return NO;
    char *text = malloc((size_t)length + 1);
    if (text == NULL) return NO;
    (void)[self sendEditorMessage:SCI_GETTEXT wParam:(uintptr_t)length + 1
                            lParam:(intptr_t)text];
    BOOL changed = doc->length != (size_t)length ||
        memcmp(doc->contents, text, (size_t)length) != 0;
    BOOL modified = [self sendEditorMessage:SCI_GETMODIFY wParam:0 lParam:0] != 0;
    AxyneStatus status = changed ? axyne_documents_set_contents(&_documents,
        _documents.active_index, text, (size_t)length, NULL) : AXYNE_STATUS_OK;
    free(text);
    if (status == AXYNE_STATUS_OK) {
        if (modified) (void)axyne_documents_mark_dirty(&_documents,
            _documents.active_index, NULL);
        else doc->is_dirty = 0;
        if (changed) [self syncLspActive];
    }
    return status == AXYNE_STATUS_OK;
}

- (void)setLspStatus:(NSString *)status
{
    [_lspStatus release];
    _lspStatus = [status copy];
    [_problemSummary setStringValue:_lspStatus != nil ? _lspStatus : @"LSP 진단 없음"];
    [self setNeedsDisplay:YES];
}

- (BOOL)ensureLsp
{
    const char *command = getenv("AXYNE_LSP_COMMAND");
    AxyneLspConfig config = {0};
    AxyneError error;
    if (_lsp != NULL) return YES;
    if (command == NULL || command[0] == '\0') {
        [self setLspStatus:@"LSP: set AXYNE_LSP_COMMAND to a local server executable"];
        return NO;
    }
    config.command = command;
    config.language_id = "plaintext";
    config.on_diagnostics = axyne_macos_lsp_diagnostics;
    config.on_navigation = axyne_macos_lsp_navigation;
    config.on_error = axyne_macos_lsp_error;
    config.user_data = self;
    if (axyne_lsp_create(&config, &_lsp, &error) != AXYNE_STATUS_OK) {
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
        return NO;
    }
    return YES;
}

- (BOOL)openLspForActive
{
    AxyneDocument *doc = [self activeDocument];
    AxyneError error;
    AxyneStatus status;
    if (doc == NULL || doc->is_untitled || doc->path == NULL || ![self ensureLsp]) return NO;
    status = axyne_lsp_did_open(_lsp, doc, &error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_BUSY) {
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
        return NO;
    }
    return YES;
}

- (void)syncLspActive
{
    AxyneDocument *doc = [self activeDocument];
    AxyneError error;
    AxyneStatus status;
    if (_lsp == NULL || doc == NULL || doc->is_untitled || doc->path == NULL) return;
    status = axyne_lsp_did_change(_lsp, doc, &error);
    if (status == AXYNE_STATUS_NOT_FOUND) status = axyne_lsp_did_open(_lsp, doc, &error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_BUSY)
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
}

- (void)navigateLspReferences:(id)sender
{
    AxyneDocument *doc;
    AxyneLspPosition position;
    AxyneError error;
    AxyneStatus status;
    uint64_t requestID = 0;
    NSInteger current, line, lineStart;
    BOOL references = [sender tag] != 0;
    if (![self captureEditor] || ![self openLspForActive]) return;
    doc = [self activeDocument];
    current = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    line = [self sendEditorMessage:SCI_LINEFROMPOSITION wParam:(uintptr_t)current lParam:0];
    lineStart = [self sendEditorMessage:SCI_POSITIONFROMLINE wParam:(uintptr_t)line lParam:0];
    position.line = (size_t)line;
    position.character = axyne_lsp_utf16_character(
        doc->contents + (size_t)lineStart, (size_t)(current - lineStart),
        (size_t)(current - lineStart));
    status = references ? axyne_lsp_references(_lsp, doc, position, &requestID, &error) :
        axyne_lsp_definition(_lsp, doc, position, &requestID, &error);
    if (status != AXYNE_STATUS_OK)
        [self setLspStatus:[NSString stringWithFormat:@"LSP: %s", error.message]];
    else
        [self setLspStatus:[NSString stringWithFormat:@"LSP: request %llu sent",
                            (unsigned long long)requestID]];
}

- (BOOL)captureEditor
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || doc->native_editor_document == NULL) return YES;
    if ([self captureEditorSnapshot]) return YES;
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:@"Could not capture editor contents"];
    [alert setInformativeText:@"The operation was cancelled. Your edits remain open in the editor."];
    [alert runModal];
    return NO;
}

- (BOOL)loadActiveDocument
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || _editorView == nil) return NO;
    _loadingEditor = YES;
    BOOL loaded = axyne_editor_load_document(doc, axyne_macos_editor_message, self);
    _loadingEditor = NO;
    if (!loaded) return NO;
    [self applyPreferences];
    [self applyEditorLexer];
    [self updateLineNumberMargin];
    [self updateBraceHighlight];
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    [self refreshActionControls];
    if ([self window] != nil)
        [[self window] makeFirstResponder:[(id)_editorView content]];
    return YES;
}

- (BOOL)selectDocumentAtIndex:(size_t)index
{
    size_t previousIndex = _documents.active_index;
    if (axyne_documents_set_active(&_documents, index, NULL) != AXYNE_STATUS_OK)
        return NO;
    if ([self loadActiveDocument]) return YES;
    (void)axyne_documents_set_active(&_documents, previousIndex, NULL);
    return NO;
}

- (void)updateWindowTitle
{
    AxyneDocument *doc = [self activeDocument];
    NSString *name = doc != NULL && doc->title != NULL
        ? [NSString stringWithUTF8String:doc->title] : @"Untitled";
    if (name == nil) name = @"Untitled";
    [[self window] setTitle:[NSString stringWithFormat:@"%@%@ - Axyne",
        doc != NULL && doc->is_dirty ? @"● " : @"", name]];
}

- (void)notification:(SCNotification *)notification
{
    AxyneDocument *doc = [self activeDocument];
    if (_loadingEditor || notification == NULL || doc == NULL) return;
    if (notification->nmhdr.code == SCN_CHARADDED)
        [self autoIndentFromNotification:notification];
    if (notification->nmhdr.code == SCN_UPDATEUI) {
        [self updateLineNumberMargin];
        [self updateBraceHighlight];
        [self setNeedsDisplay:YES];
    }
    if (notification->nmhdr.code == SCN_MODIFIED &&
        [self sendEditorMessage:SCI_GETMODIFY wParam:0 lParam:0] != 0) {
        if (!doc->is_dirty) {
            (void)axyne_documents_mark_dirty(&_documents, _documents.active_index, NULL);
            [self setNeedsDisplay:YES];
            [self updateWindowTitle];
        }
    } else if (notification->nmhdr.code == SCN_SAVEPOINTREACHED) {
        (void)axyne_documents_mark_clean(&_documents,
            _documents.active_index, NULL);
        [self setNeedsDisplay:YES];
        [self updateWindowTitle];
    } else if (notification->nmhdr.code == SCN_SAVEPOINTLEFT) {
        (void)axyne_documents_mark_dirty(&_documents,
            _documents.active_index, NULL);
        [self setNeedsDisplay:YES];
        [self updateWindowTitle];
    }
    [self refreshActionControls];
}

- (BOOL)editorReadyForSave
{
    AxyneDocument *doc = [self activeDocument];
    if (doc != NULL && _editorView != nil && doc->native_editor_document != NULL)
        return YES;
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:@"Could not save file"];
    [alert setInformativeText:_editorLoadError != nil ? _editorLoadError :
        @"The editor is not available for this document, so nothing was written to disk."];
    [alert runModal];
    return NO;
}

- (BOOL)saveActiveToPath:(NSString *)path
{
    if (path == nil || ![self editorReadyForSave] || ![self captureEditor])
        return NO;
    const char *utf8Path = [path UTF8String];
    AxyneError error;
    AxyneStatus status = axyne_documents_save_as(&_documents,
        _documents.active_index, utf8Path, &error);
    if (status != AXYNE_STATUS_OK) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        NSString *detail = [NSString stringWithUTF8String:error.message];
        [alert setMessageText:@"Could not save file"];
        [alert setInformativeText:detail != nil ? detail : @""];
        [alert runModal];
        return NO;
    }
    (void)[self sendEditorMessage:SCI_SETSAVEPOINT wParam:0 lParam:0];
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    [self refreshRecentMenu];
    [self refreshActionControls];
    return YES;
}

- (BOOL)saveActive
{
    AxyneDocument *doc = [self activeDocument];
    if (doc == NULL || ![self editorReadyForSave]) return NO;
    if (doc->is_untitled) {
        NSSavePanel *panel = [NSSavePanel savePanel];
        if ([panel runModal] != NSModalResponseOK) return NO;
        return [self saveActiveToPath:[[panel URL] path]];
    }
    if (![self captureEditor]) return NO;
    AxyneError error;
    AxyneStatus status = axyne_documents_save(&_documents,
        _documents.active_index, &error);
    if (status != AXYNE_STATUS_OK) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        NSString *detail = [NSString stringWithUTF8String:error.message];
        [alert setMessageText:@"Could not save file"];
        [alert setInformativeText:detail != nil ? detail : @""];
        [alert runModal];
        return NO;
    }
    (void)[self sendEditorMessage:SCI_SETSAVEPOINT wParam:0 lParam:0];
    [self setNeedsDisplay:YES];
    [self updateWindowTitle];
    [self refreshRecentMenu];
    [self refreshActionControls];
    return YES;
}

- (void)newDocument:(id)sender
{
    (void)sender;
    if (![self requireEditorFor:@"create a new document"]) return;
    if (![self captureEditor]) return;
    size_t previousIndex = _documents.active_index;
    size_t previousCount = _documents.count;
    size_t index = 0;
    AxyneError error;
    memset(&error, 0, sizeof(error));
    AxyneStatus status = axyne_documents_new(&_documents, &index, &error);
    NSString *failure = nil;
    if (status != AXYNE_STATUS_OK) {
        NSString *detail = [NSString stringWithUTF8String:error.message];
        failure = detail != nil && [detail length] > 0 ? detail :
            @"The document list could not allocate a new document.";
    } else {
        (void)axyne_documents_set_active(&_documents, index, NULL);
        if (![self loadActiveDocument]) {
            if (_documents.count > previousCount)
                (void)axyne_documents_close(&_documents, index, NULL);
            (void)axyne_documents_set_active(&_documents, previousIndex, NULL);
            failure = @"Scintilla could not create the editor document.";
        }
    }
    if (failure != nil) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not create a new document"];
        [alert setInformativeText:failure];
        [alert runModal];
    }
}

- (void)undo:(id)sender
{
    (void)sender;
    (void)[self sendEditorMessage:SCI_UNDO wParam:0 lParam:0];
}

- (void)redo:(id)sender
{
    (void)sender;
    (void)[self sendEditorMessage:SCI_REDO wParam:0 lParam:0];
}

- (void)openPath:(NSString *)path
{
    [self openPath:path asPreview:NO];
}

/* Explorer clicks open preview tabs; every other entry point is a normal
 * tab. A clean preview is replaced in place without a prompt and its native
 * Scintilla document, LSP state and heap fields are released once the new
 * document is displayed. */
- (void)openPath:(NSString *)path asPreview:(BOOL)preview
{
    if (path == nil) return;
    if (![self requireEditorFor:@"open a file"]) return;
    if (![self captureEditor]) return;
    size_t previousCount = _documents.count;
    size_t previousIndex = _documents.active_index;
    size_t index = 0;
    int replaced = 0;
    AxyneDocument evicted;
    AxyneError error;
    memset(&evicted, 0, sizeof(evicted));
    memset(&error, 0, sizeof(error));
    AxyneStatus status = preview
        ? axyne_documents_open_preview(&_documents, [path UTF8String], &index,
                                       &evicted, &replaced, &error)
        : axyne_documents_open(&_documents, [path UTF8String], &index, &error);
    if (status != AXYNE_STATUS_OK) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        NSString *detail = [NSString stringWithUTF8String:error.message];
        [alert setMessageText:@"Could not open file"];
        [alert setInformativeText:detail != nil ? detail : @""];
        [alert runModal];
        return;
    }
    (void)axyne_documents_set_active(&_documents, index, NULL);
    if (![self loadActiveDocument]) {
        if (replaced)
            axyne_documents_revert_preview_open(&_documents, index, &evicted, replaced);
        else if (_documents.count > previousCount)
            (void)axyne_documents_close(&_documents, index, NULL);
        (void)axyne_documents_set_active(&_documents, previousIndex, NULL);
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not open file"];
        [alert setInformativeText:@"Scintilla could not create the document."];
        [alert runModal];
        return;
    }
    if (replaced) {
        if (_lsp != NULL) (void)axyne_lsp_did_close(_lsp, &evicted, NULL);
        if (evicted.owns_native_editor_document)
            (void)[self sendEditorMessage:SCI_RELEASEDOCUMENT wParam:0
                lParam:(intptr_t)evicted.native_editor_document];
        axyne_document_dispose(&evicted);
    }
    [self refreshRecentMenu];
    [self setNeedsDisplay:YES];
}

- (void)openDocument:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] == NSModalResponseOK)
        [self openPath:[[panel URL] path]];
}

- (void)showWorkspaceError:(NSString *)prefix error:(AxyneError *)error
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    NSString *detail = error != NULL
        ? [NSString stringWithUTF8String:error->message] : nil;
    [alert setMessageText:prefix != nil ? prefix : @"Workspace operation failed"];
    [alert setInformativeText:detail != nil ? detail : @""];
    [alert runModal];
}

- (void)showWorkspaceMessage:(NSString *)message
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:message != nil ? message : @"Workspace operation failed"];
    [alert runModal];
}

- (BOOL)selectWorkspaceURL:(NSURL *)url
{
    const char *path;
    AxyneWatcher *watcher = NULL;
    AxyneError error;
    AxyneStatus status;
    if (url == nil) return NO;
    path = [[url path] UTF8String];
    if (path == NULL) return NO;
    status = axyne_watcher_start(path, axyne_macos_watch_callback, self,
                                 &watcher, &error);
    if (status != AXYNE_STATUS_OK) {
        [self showWorkspaceError:@"Unable to watch workspace" error:&error];
        return NO;
    }
    status = axyne_explorer_set_root(&_explorer, path, &error);
    if (status != AXYNE_STATUS_OK) {
        axyne_watcher_stop(watcher); axyne_watcher_release(watcher);
        [self showWorkspaceError:@"Unable to open workspace" error:&error];
        return NO;
    }
    if (_watcher != NULL) {
        axyne_watcher_stop(_watcher);
        axyne_watcher_release(_watcher);
    }
    _watcher = watcher;
    _hasExplorerSelection = NO;
    _explorerFirstRow = 0;
    _preferences = _globalPreferences;
    memset(_workspaceBindingPresent, 0, sizeof(_workspaceBindingPresent));
    free(_workspacePreferencesPath);
    _workspacePreferencesPath = axyne_macos_workspace_preferences_path(path);
    if (_workspacePreferencesPath != NULL) {
        AxynePreferences workspacePreferences;
        status = axyne_preferences_load(_workspacePreferencesPath,
                                        &workspacePreferences, &error);
        if (status == AXYNE_STATUS_OK) {
            /* Keep the file's binding mask separate from the normalized
             * snapshot used to calculate effective workspace preferences. */
            memcpy(_workspaceBindingPresent, workspacePreferences.binding_present,
                   sizeof(_workspaceBindingPresent));
            axyne_preferences_mark_all(&workspacePreferences);
            axyne_preferences_apply_workspace(&_preferences,
                                               &workspacePreferences);
            [self applyPreferences];
        } else if (status != AXYNE_STATUS_NOT_FOUND) {
            [self showWorkspaceError:@"Unable to load workspace preferences" error:&error];
            [self applyPreferences];
        } else [self applyPreferences];
    }
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)openWorkspace:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:YES];
    [panel setCanChooseFiles:NO];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] == NSModalResponseOK)
        (void)[self selectWorkspaceURL:[panel URL]];
}

- (void)workspaceEvent
{
    [self refreshExplorer];
}

- (BOOL)refreshExplorer
{
    AxyneError error;
    if (_explorer.root == NULL) return NO;
    if (axyne_explorer_reload(&_explorer, &error) != AXYNE_STATUS_OK) {
        [self showWorkspaceError:@"Unable to refresh workspace" error:&error];
        [self setNeedsDisplay:YES];
        return NO;
    } else if (_hasExplorerSelection &&
               (size_t)_explorerSelection >= _explorer.count) {
        _hasExplorerSelection = NO;
    }
    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];
    return YES;
}

- (NSInteger)explorerNodeAtPoint:(NSPoint)point
{
    const CGFloat explorerTop = AXYNE_CONTENT_TOP + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER;
    const CGFloat bottom = NSHeight([self bounds]) - AXYNE_STATUS - [self panelHeight];
    NSInteger row;
    if (_explorer.root == NULL || point.x < 0 || point.x >= [self sidebarWidth] ||
        point.y < explorerTop || point.y >= bottom) return NSNotFound;
    row = (NSInteger)((point.y - explorerTop) / AXYNE_UI_ROW) + _explorerFirstRow;
    if (explorerTop + (row - _explorerFirstRow + 1) * AXYNE_UI_ROW > bottom)
        return NSNotFound;
    return row >= 0 && (size_t)row < _explorer.count ? row : NSNotFound;
}

- (void)scrollWheel:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (point.x >= [self sidebarWidth] && point.x < NSWidth([self bounds]) &&
        point.y >= AXYNE_CONTENT_TOP && point.y < AXYNE_CONTENT_TOP + AXYNE_TABS) {
        CGFloat delta = [event scrollingDeltaX] != 0 ? [event scrollingDeltaX] :
            [event scrollingDeltaY];
        [self scrollTabsBy:-delta * ([event hasPreciseScrollingDeltas] ? 1 : 40)];
        return;
    }
    CGFloat top = AXYNE_CONTENT_TOP + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER;
    CGFloat bottom = NSHeight([self bounds]) - AXYNE_STATUS - [self panelHeight];
    if (point.x < [self sidebarWidth] && point.y >= top && point.y < bottom) {
        NSInteger visible = MAX(1, (NSInteger)((bottom - top) / AXYNE_UI_ROW));
        NSInteger maximum = MAX(0, (NSInteger)_explorer.count - visible);
        NSInteger delta = (NSInteger)ceil(fabs([event scrollingDeltaY]) /
            ([event hasPreciseScrollingDeltas] ? AXYNE_UI_ROW : 1));
        if ([event scrollingDeltaY] > 0) delta = -delta;
        _explorerFirstRow = MIN(maximum, MAX(0, _explorerFirstRow + delta));
        [self setNeedsDisplay:YES];
        return;
    }
    [super scrollWheel:event];
}

- (void)newExplorerFile:(id)sender
{
    (void)sender;
    [self performExplorerOperation:AXYNE_FILE_KIND_FILE];
}

- (void)newExplorerFolder:(id)sender
{
    (void)sender;
    [self performExplorerOperation:AXYNE_FILE_KIND_DIRECTORY];
}

- (AxyneExplorerNode *)selectedExplorerNode
{
    if (!_hasExplorerSelection) return NULL;
    if (_explorerSelection < 0 || (size_t)_explorerSelection >= _explorer.count) {
        _hasExplorerSelection = NO;
        return NULL;
    }
    return &_explorer.nodes[(size_t)_explorerSelection];
}

- (void)renameExplorerItem:(id)sender
{
    (void)sender;
    AxyneExplorerNode *node = [self selectedExplorerNode];
    if (node == NULL) {
        [self showWorkspaceMessage:@"Select a file or folder in the explorer first."];
        return;
    }
    if (_explorer.root != NULL && strcmp(node->path, _explorer.root) == 0) {
        [self showWorkspaceMessage:@"The workspace root cannot be renamed or deleted."];
        return;
    }
    /* The prompt below is modal and the watcher may reload the explorer, so
     * keep private copies instead of holding the node pointer across it. */
    NSString *nodePath = [NSString stringWithUTF8String:node->path];
    NSString *nodeName = [NSString stringWithUTF8String:node->name];
    if (nodePath == nil || nodeName == nil) return;
    NSString *name = [self askForText:@"Rename" label:@"New name"];
    if ([name length] == 0) return;
    if (!axyne_explorer_is_safe_child_name([name UTF8String])) {
        [self showWorkspaceMessage:@"Use one valid file or folder name without separators, . or .."];
        return;
    }
    NSString *parent = [nodePath stringByDeletingLastPathComponent];
    AxyneError error;
    AxyneStatus status = axyne_fs_rename_at([parent UTF8String],
        [nodeName UTF8String], [name UTF8String], &error);
    if (status != AXYNE_STATUS_OK)
        [self showWorkspaceError:@"Rename failed" error:&error];
    else if ([self refreshExplorer]) _hasExplorerSelection = NO;
}

- (void)removeExplorerItem:(id)sender
{
    (void)sender;
    AxyneExplorerNode *node = [self selectedExplorerNode];
    if (node == NULL) {
        [self showWorkspaceMessage:@"Select a file or folder in the explorer first."];
        return;
    }
    if (_explorer.root != NULL && strcmp(node->path, _explorer.root) == 0) {
        [self showWorkspaceMessage:@"The workspace root cannot be renamed or deleted."];
        return;
    }
    NSString *nodePath = [NSString stringWithUTF8String:node->path];
    NSString *nodeName = [NSString stringWithUTF8String:node->name];
    BOOL isDirectory = node->kind == AXYNE_FILE_KIND_DIRECTORY;
    if (nodePath == nil || nodeName == nil) return;
    NSAlert *confirm = [[[NSAlert alloc] init] autorelease];
    [confirm setAlertStyle:NSAlertStyleWarning];
    [confirm setMessageText:[NSString stringWithFormat:@"Delete \"%@\"?", nodeName]];
    [confirm setInformativeText:isDirectory
        ? @"The folder and everything inside it will be deleted. This cannot be undone."
        : @"The file will be deleted. This cannot be undone."];
    [confirm addButtonWithTitle:@"Delete"];
    [confirm addButtonWithTitle:@"Cancel"];
    if ([confirm runModal] != NSAlertFirstButtonReturn) return;
    NSString *parent = [nodePath stringByDeletingLastPathComponent];
    AxyneError error;
    AxyneStatus status = axyne_fs_remove_at([parent UTF8String],
        [nodeName UTF8String], &error);
    if (status != AXYNE_STATUS_OK)
        [self showWorkspaceError:@"Delete failed" error:&error];
    else if ([self refreshExplorer]) _hasExplorerSelection = NO;
}

- (void)performExplorerOperation:(AxyneFileKind)kind
{
    if (_explorer.root == NULL) {
        [self showWorkspaceMessage:@"Open a workspace folder before creating files or folders."];
        return;
    }
    NSString *parent = [NSString stringWithUTF8String:_explorer.root];
    AxyneExplorerNode *node = [self selectedExplorerNode];
    if (node != NULL) {
        NSString *nodePath = [NSString stringWithUTF8String:node->path];
        if (nodePath != nil)
            parent = node->kind == AXYNE_FILE_KIND_DIRECTORY ? nodePath :
                [nodePath stringByDeletingLastPathComponent];
    }
    if ([parent length] == 0) {
        [self showWorkspaceMessage:@"The target folder could not be determined."];
        return;
    }
    NSString *name = [self askForText:kind == AXYNE_FILE_KIND_FILE
        ? @"New File" : @"New Folder" label:@"Name"];
    if ([name length] == 0) return;
    if (!axyne_explorer_is_safe_child_name([name UTF8String])) {
        [self showWorkspaceMessage:@"Use one valid file or folder name without separators, . or .."];
        return;
    }
    AxyneError error;
    AxyneStatus status = kind == AXYNE_FILE_KIND_FILE
        ? axyne_fs_create_file_at([parent UTF8String], [name UTF8String], &error)
        : axyne_fs_create_directory_at([parent UTF8String], [name UTF8String], &error);
    if (status != AXYNE_STATUS_OK)
        [self showWorkspaceError:@"Create failed" error:&error];
    else [self refreshExplorer];
}

- (void)editCut:(id)sender
{
    NSResponder *external = [self externalTextResponder];
    if (external != nil) { (void)[external tryToPerform:@selector(cut:) with:sender]; return; }
    (void)[self sendEditorMessage:SCI_CUT wParam:0 lParam:0];
}

- (void)editCopy:(id)sender
{
    NSResponder *external = [self externalTextResponder];
    if (external != nil) { (void)[external tryToPerform:@selector(copy:) with:sender]; return; }
    (void)[self sendEditorMessage:SCI_COPY wParam:0 lParam:0];
}

- (void)editPaste:(id)sender
{
    NSResponder *external = [self externalTextResponder];
    if (external != nil) { (void)[external tryToPerform:@selector(paste:) with:sender]; return; }
    (void)[self sendEditorMessage:SCI_PASTE wParam:0 lParam:0];
}

- (void)editSelectAll:(id)sender
{
    NSResponder *external = [self externalTextResponder];
    if (external != nil) { (void)[external tryToPerform:@selector(selectAll:) with:sender]; return; }
    (void)[self sendEditorMessage:SCI_SELECTALL wParam:0 lParam:0];
}

- (void)findInDocument:(id)sender { (void)sender; [self findOrReplace:NO]; }
- (void)replaceInDocument:(id)sender { (void)sender; [self findOrReplace:YES]; }
- (void)searchWorkspace:(id)sender { (void)sender; [self searchFolder:NO]; }

- (CGFloat)sidebarWidth { return _explorerHidden ? 0 : AXYNE_UI_SIDEBAR; }
- (CGFloat)panelHeight { return _panelHidden ? 0 : AXYNE_UI_PANEL; }

/* In-window menu bar (Figma menu frames). Items are laid out from the shared
 * metrics; painting, hit-testing and popup anchoring all use this one rect. */
- (NSRect)menuBarItemRect:(NSUInteger)index
{
    NSDictionary *attributes = @{NSFontAttributeName:[NSFont systemFontOfSize:12]};
    CGFloat x = AXYNE_UI_MENU_INSET;
    for (NSUInteger i = 0; i < AXYNE_UI_MENU_COUNT; ++i) {
        CGFloat width = ceil([axyne_macos_menu_label(i) sizeWithAttributes:attributes].width) +
            2 * AXYNE_UI_MENU_PAD;
        if (i == index) return NSMakeRect(x, 2, width, AXYNE_MENU - 4);
        x += width + AXYNE_UI_MENU_GAP;
    }
    return NSZeroRect;
}

- (NSInteger)menuBarIndexAtPoint:(NSPoint)point
{
    if (point.y < 0 || point.y >= AXYNE_MENU) return -1;
    for (NSUInteger i = 0; i < AXYNE_UI_MENU_COUNT; ++i) {
        NSRect rect = [self menuBarItemRect:i];
        if (point.x >= NSMinX(rect) && point.x < NSMaxX(rect)) return (NSInteger)i;
    }
    return -1;
}

/* The bar shows the submenus of the native main menu (item 0 is the
 * application menu), so a bar item runs exactly the actions, key equivalents
 * and validateMenuItem: rules of the matching system-menu entry. */
- (void)openMenuBarMenu:(NSUInteger)index
{
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenu *submenu;
    BOOL light = _preferences.theme.preset == AXYNE_THEME_LIGHT ||
        (_preferences.theme.preset == AXYNE_THEME_SYSTEM && !axyne_macos_prefers_dark(self));
    if (mainMenu == nil || (NSInteger)index + 1 >= [mainMenu numberOfItems]) return;
    submenu = [[mainMenu itemAtIndex:(NSInteger)index + 1] submenu];
    if (submenu == nil) return;
    [submenu setAppearance:[NSAppearance appearanceNamed:
        light ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua]];
    _activeMenuIndex = (NSInteger)index;
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
    /* Blocks until the popup is dismissed; a chosen item has already run. */
    [submenu popUpMenuPositioningItem:nil
        atLocation:NSMakePoint(NSMinX([self menuBarItemRect:index]), AXYNE_MENU)
        inView:self];
    /* The same menu also drops down from the system menu bar, so give it
     * back its system appearance. */
    [submenu setAppearance:nil];
    _activeMenuIndex = -1;
    _hoverMenuIndex = -1;
    [self setNeedsDisplay:YES];
}

- (void)setMenuHover:(NSInteger)index
{
    if (index == _hoverMenuIndex) return;
    _hoverMenuIndex = index;
    [self setNeedsDisplayInRect:NSMakeRect(0, 0, NSWidth([self bounds]), AXYNE_MENU)];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_menuTracking != nil) {
        [self removeTrackingArea:_menuTracking];
        [_menuTracking release];
        _menuTracking = nil;
    }
    _menuTracking = [[NSTrackingArea alloc]
        initWithRect:NSMakeRect(0, 0, NSWidth([self bounds]), AXYNE_MENU)
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                NSTrackingActiveInKeyWindow
        owner:self userInfo:nil];
    [self addTrackingArea:_menuTracking];
}

- (void)mouseMoved:(NSEvent *)event
{
    [self setMenuHover:[self menuBarIndexAtPoint:
        [self convertPoint:[event locationInWindow] fromView:nil]]];
}

- (void)mouseExited:(NSEvent *)event
{
    (void)event;
    [self setMenuHover:-1];
}

/* Editor commands apply only to the source editor: not while a prompt or the
 * terminal input owns the keyboard, and not without an open document. */
- (BOOL)editorActionable
{
    return _editorView != nil && [self activeDocument] != NULL &&
        [self externalTextResponder] == nil;
}

- (void)goToLine:(id)sender
{
    size_t count;
    size_t line = 0;
    NSString *answer;
    (void)sender;
    if (![self editorActionable]) return;
    count = (size_t)[self sendEditorMessage:SCI_GETLINECOUNT wParam:0 lParam:0];
    answer = [self askForText:@"Go to Line" label:[NSString stringWithFormat:
        @"Line number (1-%lu)", (unsigned long)count]];
    if (answer == nil) return;
    if (!axyne_editor_parse_line_number([answer UTF8String], count, &line)) {
        [self showWorkspaceMessage:[NSString stringWithFormat:
            @"Enter a line number between 1 and %lu.", (unsigned long)count]];
        return;
    }
    (void)axyne_editor_go_to_line(axyne_macos_editor_message, self, line);
    if ([self window] != nil)
        [[self window] makeFirstResponder:[(id)_editorView content]];
    [self setNeedsDisplay:YES];
}

- (void)selectLine:(id)sender
{
    (void)sender;
    if ([self editorActionable])
        (void)axyne_editor_select_line(axyne_macos_editor_message, self);
}

- (void)toggleLineComment:(id)sender
{
    AxyneDocument *document = [self activeDocument];
    const char *token;
    (void)sender;
    if (![self editorActionable]) return;
    token = axyne_editor_comment_token(document->path);
    if (token != NULL)
        (void)axyne_editor_toggle_line_comment(axyne_macos_editor_message, self, token);
}

- (void)duplicateLine:(id)sender
{
    (void)sender;
    if ([self editorActionable]) (void)[self sendEditorMessage:SCI_LINEDUPLICATE wParam:0 lParam:0];
}

- (void)moveLineUp:(id)sender
{
    (void)sender;
    if ([self editorActionable])
        (void)[self sendEditorMessage:SCI_MOVESELECTEDLINESUP wParam:0 lParam:0];
}

- (void)moveLineDown:(id)sender
{
    (void)sender;
    if ([self editorActionable])
        (void)[self sendEditorMessage:SCI_MOVESELECTEDLINESDOWN wParam:0 lParam:0];
}

- (void)indentSelection:(id)sender
{
    (void)sender;
    if ([self editorActionable]) (void)[self sendEditorMessage:SCI_TAB wParam:0 lParam:0];
}

- (void)outdentSelection:(id)sender
{
    (void)sender;
    if ([self editorActionable]) (void)[self sendEditorMessage:SCI_BACKTAB wParam:0 lParam:0];
}

- (void)zoomInEditor:(id)sender
{
    (void)sender;
    (void)[self sendEditorMessage:SCI_ZOOMIN wParam:0 lParam:0];
}

- (void)zoomOutEditor:(id)sender
{
    (void)sender;
    (void)[self sendEditorMessage:SCI_ZOOMOUT wParam:0 lParam:0];
}

- (void)zoomResetEditor:(id)sender
{
    (void)sender;
    (void)[self sendEditorMessage:SCI_SETZOOM wParam:0 lParam:0];
}

/* Session-only: the effective preference is flipped in memory and is not
 * written back to preferences.json. */
- (void)toggleWordWrap:(id)sender
{
    (void)sender;
    _preferences.editor.word_wrap = !_preferences.editor.word_wrap;
    (void)[self sendEditorMessage:SCI_SETWRAPMODE
        wParam:_preferences.editor.word_wrap ? 1 : 0 lParam:0];
}

- (void)toggleExplorer:(id)sender
{
    (void)sender;
    _explorerHidden = !_explorerHidden;
    [self setNeedsLayout:YES]; [self setNeedsDisplay:YES];
}

- (void)togglePanel:(id)sender
{
    (void)sender;
    _panelHidden = !_panelHidden;
    if (_panelHidden && [self window] != nil && _editorView != nil)
        [[self window] makeFirstResponder:[(id)_editorView content]];
    [self setNeedsLayout:YES]; [self setNeedsDisplay:YES];
}

- (void)cancelBuild:(id)sender
{
    if (_activeAction == 1) [self stopTerminal:sender];
}

- (void)stopDebugger:(id)sender
{
    (void)sender;
    if (axyne_debugger_is_active(&_debugger)) axyne_debugger_stop(&_debugger);
}

- (void)clearBreakpoints:(id)sender
{
    AxyneError error;
    (void)sender;
    if (axyne_debugger_clear_breakpoints(&_debugger, &error) != AXYNE_STATUS_OK)
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
}

- (BOOL)preferencesFileExists
{
    NSString *path;
    if (_globalPreferencesPath == NULL) return NO;
    path = [NSString stringWithUTF8String:_globalPreferencesPath];
    return path != nil && [[NSFileManager defaultManager] fileExistsAtPath:path];
}

/* Opens the existing global preferences document (preferences.json) as a tab. */
- (void)openPreferencesFile:(id)sender
{
    (void)sender;
    if (![self preferencesFileExists]) return;
    [self openPath:[NSString stringWithUTF8String:_globalPreferencesPath]];
}

static NSString *axyne_macos_shortcut_text(NSString *key, NSEventModifierFlags flags)
{
    NSMutableString *text = [NSMutableString string];
    unichar character;
    if ([key length] == 0) return nil;
    character = [key characterAtIndex:0];
    /* AppKit treats an upper-case letter equivalent as implying Shift. */
    if (![key isEqualToString:[key lowercaseString]]) flags |= NSEventModifierFlagShift;
    if ((flags & NSEventModifierFlagControl) != 0) [text appendString:@"⌃"];
    if ((flags & NSEventModifierFlagOption) != 0) [text appendString:@"⌥"];
    if ((flags & NSEventModifierFlagShift) != 0) [text appendString:@"⇧"];
    if ((flags & NSEventModifierFlagCommand) != 0) [text appendString:@"⌘"];
    if (character >= NSF1FunctionKey && character <= NSF12FunctionKey)
        [text appendFormat:@"F%d", (int)(character - NSF1FunctionKey) + 1];
    else if (character == NSUpArrowFunctionKey) [text appendString:@"↑"];
    else if (character == NSDownArrowFunctionKey) [text appendString:@"↓"];
    else [text appendString:[key uppercaseString]];
    return text;
}

static void axyne_macos_collect_shortcuts(NSMenu *menu, NSMutableString *out)
{
    for (NSMenuItem *item in [menu itemArray]) {
        NSString *shortcut;
        if ([item isSeparatorItem]) continue;
        if ([item submenu] != nil) { axyne_macos_collect_shortcuts([item submenu], out); continue; }
        shortcut = axyne_macos_shortcut_text([item keyEquivalent],
            [item keyEquivalentModifierMask]);
        if (shortcut == nil) continue;
        [out appendFormat:@"  %@  %@\n",
            [[item title] stringByPaddingToLength:30 withString:@" " startingAtIndex:0],
            shortcut];
    }
}

/* Lists the shortcuts the menus actually carry, plus the effective
 * Preferences bindings (which may differ from the menu defaults). */
- (void)showKeyboardShortcuts:(id)sender
{
    NSMutableString *text = [NSMutableString string];
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 460, 340)] autorelease];
    NSTextView *view = [[[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 460, 340)] autorelease];
    (void)sender;
    for (NSMenuItem *top in [[NSApp mainMenu] itemArray]) {
        NSMutableString *section = [NSMutableString string];
        if ([top submenu] == nil) continue;
        axyne_macos_collect_shortcuts([top submenu], section);
        if ([section length] != 0)
            [text appendFormat:@"%@\n%@\n", [top title], section];
    }
    [text appendString:@"Preferences bindings\n"];
    for (size_t i = 0; i < _preferences.binding_count; ++i) {
        const AxyneKeyBinding *binding = &_preferences.bindings[i];
        NSMutableString *keys = [NSMutableString string];
        if (!binding->enabled || binding->key[0] == '\0') continue;
        if ((binding->modifiers & AXYNE_KEY_MODIFIER_CONTROL) != 0) [keys appendString:@"⌃"];
        if ((binding->modifiers & AXYNE_KEY_MODIFIER_ALT) != 0) [keys appendString:@"⌥"];
        if ((binding->modifiers & AXYNE_KEY_MODIFIER_SHIFT) != 0) [keys appendString:@"⇧"];
        if ((binding->modifiers & AXYNE_KEY_MODIFIER_COMMAND) != 0) [keys appendString:@"⌘"];
        [keys appendString:[[NSString stringWithUTF8String:binding->key] uppercaseString]];
        [text appendFormat:@"  %@  %@\n",
            [[NSString stringWithUTF8String:axyne_preferences_action_name(binding->action)]
                stringByPaddingToLength:30 withString:@" " startingAtIndex:0], keys];
    }
    [view setEditable:NO];
    [view setFont:[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular]];
    [view setString:text];
    [view setVerticallyResizable:YES]; [view setHorizontallyResizable:NO];
    [view setAutoresizingMask:NSViewWidthSizable];
    [[view textContainer] setWidthTracksTextView:YES];
    [scroll setDocumentView:view];
    [scroll setHasVerticalScroller:YES];
    [alert setMessageText:@"Keyboard Shortcuts"];
    [alert setAccessoryView:scroll];
    [alert addButtonWithTitle:@"Close"];
    [alert runModal];
}

- (void)reportIssue:(id)sender
{
    (void)sender;
    NSURL *url = [NSURL URLWithString:@"https://github.com/team-native/Axyne/issues/new"];
    if (url == nil || ![[NSWorkspace sharedWorkspace] openURL:url])
        [self showWorkspaceMessage:@"Could not open the Axyne issue tracker."];
}

- (void)openHelp:(id)sender
{
    (void)sender;
    NSURL *url = [NSURL URLWithString:@"https://github.com/team-native/Axyne#readme"];
    if (url == nil || ![[NSWorkspace sharedWorkspace] openURL:url])
        [self showWorkspaceMessage:@"Could not open the Axyne documentation."];
}

- (void)showSettingsFolder:(id)sender
{
    (void)sender;
    if (_globalPreferencesPath == NULL) {
        [self showWorkspaceMessage:@"The settings location is not available."];
        return;
    }
    NSString *file = [NSString stringWithUTF8String:_globalPreferencesPath];
    NSString *folder = [file stringByDeletingLastPathComponent];
    BOOL isDirectory = NO;
    if ([folder length] == 0 || ![[NSFileManager defaultManager]
            fileExistsAtPath:folder isDirectory:&isDirectory] || !isDirectory) {
        [self showWorkspaceMessage:@"The settings folder has not been created yet."];
        return;
    }
    (void)[[NSWorkspace sharedWorkspace] openURL:
        [NSURL fileURLWithPath:folder isDirectory:YES]];
}

- (void)saveDocument:(id)sender
{
    (void)sender;
    (void)[self saveActive];
}

- (void)saveDocumentAs:(id)sender
{
    (void)sender;
    if ([self activeDocument] == NULL || ![self editorReadyForSave]) return;
    NSSavePanel *panel = [NSSavePanel savePanel];
    if ([panel runModal] == NSModalResponseOK)
        (void)[self saveActiveToPath:[[panel URL] path]];
}

- (void)openRecent:(id)sender
{
    NSString *path = [sender representedObject];
    [self openPath:path];
}

- (void)setRecentMenu:(NSMenu *)menu
{
    [_recentMenu release];
    _recentMenu = [menu retain];
    [self refreshRecentMenu];
}

- (void)refreshRecentMenu
{
    if (_recentMenu == nil) return;
    [_recentMenu removeAllItems];
    if (_documents.recent_count == 0) {
        NSMenuItem *empty = [[NSMenuItem alloc] initWithTitle:@"최근 항목 없음"
            action:nil keyEquivalent:@""];
        [empty setEnabled:NO];
        [_recentMenu addItem:empty];
        [empty release];
        axyne_macos_style_menu(_recentMenu);
        return;
    }
    for (size_t i = 0; i < _documents.recent_count; ++i) {
        NSString *path = [NSString stringWithUTF8String:_documents.recent_paths[i]];
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:
            path != nil ? path : @"(Invalid path)"
            action:@selector(openRecent:) keyEquivalent:@""];
        [item setTarget:self];
        [item setRepresentedObject:path];
        [_recentMenu addItem:item];
        [item release];
    }
    axyne_macos_style_menu(_recentMenu);
}

- (BOOL)confirmCloseDocumentAtIndex:(size_t)index
{
    AxyneDocument *doc = &_documents.documents[index];
    if (!doc->is_dirty) return YES;
    NSString *name = [NSString stringWithUTF8String:
        doc->title != NULL ? doc->title : "Untitled"];
    if (name == nil) name = @"Untitled";
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:[NSString stringWithFormat:@"Save changes to %@?", name]];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Discard"];
    [alert addButtonWithTitle:@"Cancel"];
    NSInteger result = [alert runModal];
    if (result == NSAlertFirstButtonReturn) {
        if (![self captureEditor]) return NO;
        if (![self selectDocumentAtIndex:index]) return NO;
        return [self saveActive];
    }
    return result == NSAlertSecondButtonReturn;
}

- (void)closeDocument:(id)sender
{
    (void)sender;
    [self closeDocumentAtIndex:_documents.active_index];
}

- (void)closeDocumentAtIndex:(size_t)index
{
    if (index >= _documents.count) return;
    if (![self captureEditor]) return;
    /* A hidden placeholder has no tab to close; closing it would only swap
     * one empty buffer for another or yank the user to another document. */
    if (axyne_document_tab_hidden(&_documents.documents[index])) return;
    if (![self confirmCloseDocumentAtIndex:index]) return;
    /* Prepare the replacement before dropping the last tab. Allocation or
     * native initialization failure must leave the current document open. */
    if (_documents.count == 1) {
        size_t replacement;
        if (axyne_documents_new_placeholder(&_documents, &replacement, NULL) !=
            AXYNE_STATUS_OK)
            return;
        if (![self loadActiveDocument]) {
            (void)axyne_documents_close(&_documents, replacement, NULL);
            (void)axyne_documents_set_active(&_documents, index, NULL);
            return;
        }
    } else if (_documents.active_index == index) {
        size_t successor = axyne_macos_successor_index(&_documents, index);
        if (![self selectDocumentAtIndex:successor]) return;
    }
    AxyneDocument *doc = &_documents.documents[index];
    if (_lsp != NULL) (void)axyne_lsp_did_close(_lsp, doc, NULL);
    if (doc->owns_native_editor_document)
        (void)[self sendEditorMessage:SCI_RELEASEDOCUMENT wParam:0
            lParam:(intptr_t)doc->native_editor_document];
    (void)axyne_documents_close(&_documents, index, NULL);
    (void)axyne_documents_set_active(&_documents, _documents.active_index, NULL);
    [self loadActiveDocument];
}

- (BOOL)confirmCloseAll
{
    if (![self captureEditor]) return NO;
    for (size_t i = 0; i < _documents.count; ++i)
        if (![self confirmCloseDocumentAtIndex:i]) return NO;
    return YES;
}

- (BOOL)isFlipped
{
    return YES;
}

- (NSArray *)runtimeSearchDirectories
{
    NSMutableArray *directories = [NSMutableArray array];
    NSBundle *main = [NSBundle mainBundle];
    const char *override = getenv("AXYNE_FRAMEWORKS_DIR");
    NSString *executableDir = [[main executablePath] stringByDeletingLastPathComponent];
    NSMutableArray *candidates = [NSMutableArray array];
    if (override != NULL && override[0] != '\0')
        [candidates addObject:[NSString stringWithUTF8String:override]];
    if ([main privateFrameworksPath] != nil)
        [candidates addObject:[main privateFrameworksPath]];
    if ([main bundlePath] != nil)
        [candidates addObject:[[main bundlePath]
            stringByAppendingPathComponent:@"Contents/Frameworks"]];
    if (executableDir != nil) {
        NSString *bundleContents = [executableDir stringByDeletingLastPathComponent];
        NSString *buildDir = [[bundleContents stringByDeletingLastPathComponent]
            stringByDeletingLastPathComponent];
        [candidates addObject:[bundleContents stringByAppendingPathComponent:@"Frameworks"]];
        [candidates addObject:executableDir];
        [candidates addObject:[executableDir stringByAppendingPathComponent:@"Frameworks"]];
        /* Dev build tree: <build>/Axyne.app/Contents/MacOS with the
         * framework in <build>/scintilla and Lexilla.dylib in <build>. */
        [candidates addObject:[executableDir stringByAppendingPathComponent:@"scintilla"]];
        [candidates addObject:[buildDir stringByAppendingPathComponent:@"scintilla"]];
        [candidates addObject:buildDir];
    }
    for (NSString *candidate in candidates) {
        NSString *clean = [candidate stringByStandardizingPath];
        if ([clean length] > 0 && ![directories containsObject:clean])
            [directories addObject:clean];
    }
    return directories;
}

- (void)reportEditorLoadFailure:(NSString *)detail
{
    static BOOL alerted = NO;
    NSString *message = detail != nil ? detail : @"The Scintilla editor could not be loaded.";
    [_editorLoadError release];
    _editorLoadError = [message copy];
    NSLog(@"Axyne: %@", message);
    if (alerted) return;
    alerted = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setAlertStyle:NSAlertStyleCritical];
        [alert setMessageText:@"The code editor could not be loaded"];
        [alert setInformativeText:[message stringByAppendingString:
            @"\n\nFile, edit and save commands are unavailable until Scintilla.framework "
            @"is found in the app's Contents/Frameworks folder (or set AXYNE_FRAMEWORKS_DIR)."]];
        [alert runModal];
    });
}

- (void)loadScintillaView
{
    if (_editorView != nil) {
        return;
    }
    NSArray *directories = [self runtimeSearchDirectories];
    NSMutableString *diagnostics = [NSMutableString string];
    NSFileManager *manager = [NSFileManager defaultManager];
    NSString *frameworksPath = nil;
    if (_scintillaBundle == nil) {
        for (NSString *directory in directories) {
            NSString *frameworkPath = [directory
                stringByAppendingPathComponent:@"Scintilla.framework"];
            if (![manager fileExistsAtPath:frameworkPath]) {
                [diagnostics appendFormat:@"Not found: %@\n", frameworkPath];
                continue;
            }
            NSBundle *bundle = [NSBundle bundleWithPath:frameworkPath];
            NSError *loadError = nil;
            if (bundle == nil) {
                [diagnostics appendFormat:@"Not a valid bundle: %@\n", frameworkPath];
                continue;
            }
            if (![bundle loadAndReturnError:&loadError]) {
                const char *dlerr = NULL;
                if (dlopen([[bundle executablePath] fileSystemRepresentation],
                           RTLD_NOW | RTLD_LOCAL) == NULL) dlerr = dlerror();
                [diagnostics appendFormat:@"Failed to load %@: %@ %s\n", frameworkPath,
                    loadError != nil ? [loadError localizedDescription] : @"unknown error",
                    dlerr != NULL ? dlerr : ""];
                continue;
            }
            _scintillaBundle = [bundle retain];
            frameworksPath = directory;
            break;
        }
        if (_scintillaBundle == nil) {
            [self reportEditorLoadFailure:[@"Scintilla.framework could not be loaded.\n"
                stringByAppendingString:diagnostics]];
            return;
        }
    }

    Class scintillaClass = NSClassFromString(@"ScintillaView");
    if (scintillaClass == Nil) {
        [self reportEditorLoadFailure:
            @"Scintilla.framework loaded but the ScintillaView class is missing."];
        return;
    }
    {
        _editorView = [[scintillaClass alloc] initWithFrame:NSZeroRect];
        [(id)_editorView setDelegate:self];
        [_editorView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [_editorView setAccessibilityElement:YES];
        [_editorView setAccessibilityRole:NSAccessibilityTextAreaRole];
        [_editorView setAccessibilityLabel:@"Source editor"];
        [_editorView setAccessibilityRoleDescription:@"source editor"];
        [_editorView setFocusRingType:NSFocusRingTypeExterior];
        (void)[self sendEditorMessage:SCI_SETMARGINTYPEN wParam:0
                                 lParam:SC_MARGIN_NUMBER];
        (void)[self sendEditorMessage:SCI_SETMARGINMASKN wParam:0 lParam:0];
        (void)[self sendEditorMessage:SCI_SETMARGINSENSITIVEN wParam:0 lParam:0];
        (void)[self sendEditorMessage:SCI_STYLECLEARALL wParam:0 lParam:0];
        (void)[self sendEditorMessage:SCI_SETINDENTATIONGUIDES
                                 wParam:SC_IV_LOOKBOTH lParam:0];
        (void)[self sendEditorMessage:SCI_SETBACKSPACEUNINDENTS wParam:1 lParam:0];
        (void)[self sendEditorMessage:SCI_SETTABINDENTS wParam:1 lParam:0];
        [self addSubview:_editorView];
        /* NSScrollView resets its scroller style to the system's whenever the
         * preferred style changes (mouse/trackpad, system setting). */
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(applyEditorScrollers)
            name:NSPreferredScrollerStyleDidChangeNotification object:nil];
        [self setNeedsLayout:YES];
        [_editorLoadError release];
        _editorLoadError = nil;

        /* Lexilla is optional (syntax highlighting only): look next to the
         * framework first, then in every other runtime directory. */
        NSMutableArray *lexillaDirs = [NSMutableArray array];
        if (frameworksPath != nil) [lexillaDirs addObject:frameworksPath];
        [lexillaDirs addObjectsFromArray:directories];
        for (NSString *directory in lexillaDirs) {
            NSString *lexillaPath = [directory
                stringByAppendingPathComponent:@"Lexilla.dylib"];
            if (![manager fileExistsAtPath:lexillaPath]) continue;
            _lexillaModule = dlopen([lexillaPath fileSystemRepresentation],
                                    RTLD_NOW | RTLD_LOCAL);
            if (_lexillaModule == NULL) {
                const char *reason = dlerror();
                NSLog(@"Axyne: dlopen(%@) failed: %s", lexillaPath,
                      reason != NULL ? reason : "unknown error");
                continue;
            }
            _createLexer = (void *(*)(const char *))dlsym(
                _lexillaModule, "CreateLexer");
            if (_createLexer != NULL) break;
            NSLog(@"Axyne: %@ has no CreateLexer symbol", lexillaPath);
            dlclose(_lexillaModule);
            _lexillaModule = NULL;
        }
        if (_createLexer == NULL)
            NSLog(@"Axyne: Lexilla.dylib not found; syntax highlighting is disabled");
    }
}

- (BOOL)requireEditorFor:(NSString *)action
{
    [self loadScintillaView];
    if (_editorView != nil) return YES;
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:[NSString stringWithFormat:@"Could not %@", action]];
    [alert setInformativeText:_editorLoadError != nil ? _editorLoadError :
        @"The code editor is not available."];
    [alert runModal];
    return NO;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self loadScintillaView];
    [self applyPreferences];
    [self loadActiveDocument];
}

- (void)mouseDown:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (point.y < AXYNE_MENU) {
        NSInteger menuIndex = [self menuBarIndexAtPoint:point];
        if (menuIndex >= 0) [self openMenuBarMenu:(NSUInteger)menuIndex];
        return;
    }
    if (point.y >= AXYNE_CONTENT_TOP && point.y < AXYNE_CONTENT_TOP + AXYNE_TABS &&
        point.x >= [self sidebarWidth]) {
        for (size_t index = 0; index < _documents.count; ++index) {
            if (axyne_document_tab_hidden(&_documents.documents[index])) continue;
            NSRect tab = [self tabFrameAtIndex:index];
            if (!NSPointInRect(point, tab)) continue;
            if (![self captureEditor]) return;
            if (point.x >= NSMaxX(tab) - 24) {
                [self closeDocumentAtIndex:index];
            } else {
                [self selectDocumentAtIndex:index];
            }
            return;
        }
    }
    if (point.x < [self sidebarWidth] && point.y >= AXYNE_CONTENT_TOP + AXYNE_TABS &&
        point.y < NSHeight([self bounds]) - AXYNE_STATUS - [self panelHeight]) {
        NSInteger row = [self explorerNodeAtPoint:point];
        if (row != NSNotFound) {
            _explorerSelection = row;
            _hasExplorerSelection = YES;
            AxyneExplorerNode *node = &_explorer.nodes[(size_t)row];
            if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
                /* Only the first click of a double-click toggles, so the
                 * second click does not collapse the folder again. */
                if ([event clickCount] == 1 &&
                    axyne_explorer_toggle(&_explorer, (size_t)row, NULL) != AXYNE_STATUS_OK)
                    [self showWorkspaceError:@"Unable to read workspace folder" error:NULL];
                [self setNeedsDisplay:YES];
            } else if ([event clickCount] == 1) {
                /* A single click on a file opens (or reuses) the preview tab;
                 * the second click of a double-click does nothing extra. */
                NSString *filePath = [NSString stringWithUTF8String:node->path];
                [self setNeedsDisplay:YES];
                if (filePath != nil) [self openPath:filePath asPreview:YES];
                return;
            } else {
                [self setNeedsDisplay:YES];
            }
        } else if (_explorer.root == NULL) {
            [self openWorkspace:nil];
        }
        return;
    }
    [super mouseDown:event];
}

- (void)rightMouseDown:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    NSInteger row = [self explorerNodeAtPoint:point];
    if (point.x >= [self sidebarWidth] || point.y < AXYNE_CONTENT_TOP + AXYNE_TABS ||
        point.y >= NSHeight([self bounds]) - AXYNE_STATUS - [self panelHeight]) {
        [super rightMouseDown:event];
        return;
    }
    if (row != NSNotFound) {
        _explorerSelection = row;
        _hasExplorerSelection = YES;
    }
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@"Workspace"] autorelease];
    NSMenuItem *workspace = [menu addItemWithTitle:@"Open Workspace Folder..."
        action:@selector(openWorkspace:) keyEquivalent:@""];
    [workspace setTarget:self];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *file = [menu addItemWithTitle:@"New File"
        action:@selector(newExplorerFile:) keyEquivalent:@""];
    NSMenuItem *folder = [menu addItemWithTitle:@"New Folder"
        action:@selector(newExplorerFolder:) keyEquivalent:@""];
    [file setTarget:self]; [folder setTarget:self];
    if (row != NSNotFound) {
        NSMenuItem *rename = [menu addItemWithTitle:@"Rename"
            action:@selector(renameExplorerItem:) keyEquivalent:@""];
        NSMenuItem *remove = [menu addItemWithTitle:@"Delete"
            action:@selector(removeExplorerItem:) keyEquivalent:@""];
        [rename setTarget:self]; [remove setTarget:self];
    }
    [menu popUpMenuPositioningItem:nil atLocation:point inView:self];
    [self setNeedsDisplay:YES];
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    NSString *key = [event charactersIgnoringModifiers];
    if (axyne_macos_palette_shift_matches(&_preferences, key, event)) { [self openPaletteWithInput:@">"]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_SEARCH_WORKSPACE, key, event)) { [self searchFolder:NO]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_FIND, key, event)) { [self findOrReplace:NO]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_REPLACE, key, event)) { [self findOrReplace:YES]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_QUICK_FILE, key, event)) { [self openPaletteWithInput:@""]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_NEW, key, event)) { [self newDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_OPEN, key, event)) { [self openDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_SAVE, key, event)) { [self saveDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_CLOSE, key, event)) { [self closeDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_BUILD, key, event)) { [self buildDocument:nil]; return YES; }
    if (axyne_macos_binding_matches(&_preferences, AXYNE_ACTION_RUN, key, event)) { [self runDocument:nil]; return YES; }
    return [super performKeyEquivalent:event];
}

- (NSString *)askForText:(NSString *)title label:(NSString *)label
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:title];
    NSTextField *field = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)] autorelease];
    [field setPlaceholderString:label];
    [alert setAccessoryView:field];
    [alert addButtonWithTitle:@"Continue"]; [alert addButtonWithTitle:@"Cancel"];
    return [alert runModal] == NSAlertFirstButtonReturn ? [field stringValue] : nil;
}

static int axyne_macos_preferences_save_hook(void *context, AxynePreferences *edited)
{
    AxyneMacPreferencesContext *info = (AxyneMacPreferencesContext *)context;
    return [info->view savePreferences:edited workspace:info->workspace] ? 1 : 0;
}

/* Persists the profile produced by the preferences window and applies it.
 * Returns NO (after reporting the problem) when saving failed. */
- (BOOL)savePreferences:(AxynePreferences *)edited workspace:(BOOL)workspace
{
    AxyneError error;
    AxyneStatus status;
    const char *path = workspace ? _workspacePreferencesPath : _globalPreferencesPath;
    if (path == NULL) { [self showWorkspaceMessage:@"The preference path is unavailable."]; return NO; }
    status = workspace ? axyne_preferences_save_workspace(edited, path, &error)
                       : axyne_preferences_save_global(edited, path, &error);
    if (status != AXYNE_STATUS_OK) { [self showWorkspaceError:@"Unable to save preferences" error:&error]; return NO; }
    _preferences = *edited;
    if (workspace)
        memcpy(_workspaceBindingPresent, edited->binding_present,
               sizeof(_workspaceBindingPresent));
    if (!workspace) {
        _globalPreferences = *edited;
        if (_workspacePreferencesPath != NULL) {
            AxynePreferences workspacePreferences;
            AxyneStatus workspaceStatus = axyne_preferences_load_workspace(
                _workspacePreferencesPath, &workspacePreferences, &error);
            if (workspaceStatus == AXYNE_STATUS_OK)
                axyne_preferences_apply_workspace(&_preferences, &workspacePreferences);
            else if (workspaceStatus != AXYNE_STATUS_NOT_FOUND)
                [self showWorkspaceError:@"Unable to reload workspace preferences" error:&error];
        }
    }
    [self applyPreferences];
    return YES;
}

/* Opens the Figma preferences window. The "settings.json 열기" link closes it
 * and opens the profile's file as an editor document; a profile that was
 * never saved is created first so there is something to open. */
- (BOOL)showPreferences:(BOOL)workspace
{
    AxynePreferences initial = workspace ? _preferences : _globalPreferences;
    AxyneMacPreferencesContext context = {self, workspace};
    AxynePreferencesWindowHooks hooks = {&context, axyne_macos_preferences_save_hook};
    const char *path = workspace ? _workspacePreferencesPath : _globalPreferencesPath;
    AxyneError error;
    if (path == NULL) { [self showWorkspaceMessage:@"The preference path is unavailable."]; return NO; }
    if (workspace) {
        /* Only fields the workspace file already overrides stay present. */
        AxynePreferences stored;
        memset(initial.binding_present, 0, sizeof(initial.binding_present));
        initial.present_fields = 0;
        if (axyne_preferences_load(path, &stored, &error) == AXYNE_STATUS_OK) {
            initial.present_fields = stored.present_fields;
            memcpy(initial.binding_present, stored.binding_present,
                   sizeof(initial.binding_present));
        }
    }
    if (!axyne_preferences_window_show([self window], workspace ? 1 : 0, &initial, &hooks))
        return YES;
    if (access(path, F_OK) != 0) {
        AxynePreferences created = workspace ? _preferences : _globalPreferences;
        AxyneStatus status;
        if (workspace) { created.present_fields = 0; memset(created.binding_present, 0, sizeof(created.binding_present)); }
        status = workspace ? axyne_preferences_save_workspace(&created, path, &error)
                           : axyne_preferences_save_global(&created, path, &error);
        if (status != AXYNE_STATUS_OK) { [self showWorkspaceError:@"Unable to create settings file" error:&error]; return NO; }
    }
    [self openPath:[NSString stringWithUTF8String:path]];
    return YES;
}

- (void)showGlobalPreferences:(id)sender
{
    (void)sender;
    (void)[self showPreferences:NO];
}

- (void)showWorkspacePreferences:(id)sender
{
    (void)sender;
    if (_workspacePreferencesPath == NULL) {
        [self showWorkspaceMessage:@"Open a workspace folder before editing workspace settings."];
        return;
    }
    (void)[self showPreferences:YES];
}

static void axyne_macos_git_output(AxyneProcess *process,
                                   AxyneProcessStream stream,
                                   const char *bytes, size_t length,
                                   void *user_data)
{
    AxyneMacGitRun *run = (AxyneMacGitRun *)user_data;
    if (run != NULL && bytes != NULL &&
        !axyne_git_capture_append(&run->capture, stream, bytes, length))
        (void)axyne_process_terminate(process, NULL);
}

static void axyne_macos_git_free(AxyneMacGitRun *run)
{
    if (run == NULL) return;
    axyne_git_capture_free(&run->capture);
    (void)pthread_mutex_destroy(&run->lock);
    free(run);
}

static void axyne_macos_git_release(AxyneMacGitRun *run)
{
    int free_run = 0;
    if (run == NULL) return;
    (void)pthread_mutex_lock(&run->lock);
    if (--run->references == 0) free_run = 1;
    (void)pthread_mutex_unlock(&run->lock);
    if (free_run) axyne_macos_git_free(run);
}

/* The process handle is released exactly once, by whichever path (completion
 * on the main thread, or view teardown) claims it first. */
static AxyneProcess *axyne_macos_git_take_process(AxyneMacGitRun *run)
{
    AxyneProcess *process = NULL;
    if (run == NULL) return NULL;
    (void)pthread_mutex_lock(&run->lock);
    if (!run->process_released) {
        run->process_released = 1;
        process = run->process;
    }
    (void)pthread_mutex_unlock(&run->lock);
    return process;
}

static void axyne_macos_git_cleanup(AxyneMacGitRun *run)
{
    AxyneProcess *process;
    if (run == NULL) return;
    process = axyne_macos_git_take_process(run);
    if (process != NULL) axyne_process_release(process);
    axyne_macos_git_release(run);
}

static void axyne_macos_git_exit(AxyneProcess *process, int exit_code,
                                 void *user_data)
{
    AxyneMacGitRun *run = (AxyneMacGitRun *)user_data;
    AxyneMacGitCompletion *completion;
    AxyneWorkspaceView *view = nil;
    if (run == NULL) return;

    (void)pthread_mutex_lock(&run->lock);
    if (!run->cancelled && run->view != nil) {
        view = run->view;
        ++run->references;
        [view retain];
    }
    (void)pthread_mutex_unlock(&run->lock);
    /* A cancelled run (view teardown) is released by -dealloc's cleanup. */
    if (view == nil) return;

    /* Every non-cancelled path reaches the main thread, even when the
     * completion record cannot be allocated, so _gitRun/_gitProcess are
     * always cleared and the process and run references are always dropped. */
    completion = (AxyneMacGitCompletion *)calloc(1, sizeof(*completion));
    if (completion != NULL) {
        completion->view = view;
        completion->process = process;
        completion->report = axyne_git_format_report(run->arguments,
            run->argument_count, &run->capture, exit_code, run->empty_message);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        [view completeGitOperation:completion run:run];
    });
}

- (void)startGitOperationWithEmptyMessage:(const char *)empty_message
                                arguments:(const char *const *)arguments
                                   count:(size_t)argument_count
{
    static const char *const utf8_environment[] = {
        "LANG=en_US.UTF-8", "LC_ALL=en_US.UTF-8"
    };
    AxyneMacGitRun *run;
    AxyneProcessSpec spec;
    AxyneError error;
    AxyneStatus status;
    if (_explorer.root == NULL) {
        [self showWorkspaceMessage:@"Open a workspace folder before using Git."];
        return;
    }
    if (_gitProcess != NULL) {
        [self showWorkspaceMessage:@"A Git operation is already running."];
        return;
    }
    run = (AxyneMacGitRun *)calloc(1, sizeof(*run));
    if (run == NULL || pthread_mutex_init(&run->lock, NULL) != 0) {
        free(run);
        [self showWorkspaceMessage:@"Unable to allocate Git operation."];
        return;
    }
    run->references = 1;
    run->view = self;
    run->arguments = arguments;
    run->argument_count = argument_count;
    run->empty_message = empty_message;
    axyne_git_capture_init(&run->capture, 1);
    memset(&spec, 0, sizeof(spec));
    spec.executable = "git";
    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = _explorer.root;
    spec.environment = utf8_environment;
    spec.environment_count = 2;
    spec.on_output = axyne_macos_git_output;
    spec.on_exit = axyne_macos_git_exit;
    spec.user_data = run;
    status = axyne_process_start(&spec, &run->process, &error);
    if (status != AXYNE_STATUS_OK) {
        NSString *message = [NSString stringWithUTF8String:
            axyne_git_describe_start_failure(status, error.message)];
        axyne_macos_git_free(run);
        [self showWorkspaceMessage:message != nil
            ? message : @"Unable to start Git operation."];
        return;
    }
    _gitProcess = run->process;
    _gitRun = run;
    /* Output and Problems share the same area; show the Git output as soon
     * as the operation starts. */
    [self selectOutputPanel];
    [_terminalOutput setString:@""];
    {
        /* Use a private empty capture: run->capture belongs to the worker. */
        AxyneGitCapture pending;
        char *header;
        axyne_git_capture_init(&pending, 0);
        header = axyne_git_format_report(arguments, argument_count,
            &pending, 0, "Running...");
        if (header != NULL) {
            [self terminalAppend:header length:strlen(header)
                          stream:AXYNE_PROCESS_STDOUT];
            axyne_git_string_free(header);
        }
    }
}

- (void)selectOutputPanel
{
    _panelMode = 0;
    [_problemSummary setStringValue:_lspStatus != nil ? _lspStatus : @"LSP 진단 없음"];
    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];
}

- (void)completeGitOperation:(AxyneMacGitCompletion *)completion
                         run:(AxyneMacGitRun *)run
{
    const char *text;
    AxyneProcess *process;
    if (run == NULL) {
        if (completion != NULL) {
            free(completion->report);
            free(completion);
        }
        [self release];
        return;
    }
    text = completion != NULL && completion->report != NULL
        ? completion->report : "Unable to allocate Git output.\n";
    /* The Problems or Terminal panel may have been selected while Git ran. */
    [self selectOutputPanel];
    [_terminalOutput setString:@""];
    [self terminalAppend:text length:strlen(text) stream:AXYNE_PROCESS_STDOUT];
    [_terminalOutput scrollRangeToVisible:NSMakeRange(0, 0)];
    if (_gitRun == run) {
        _gitRun = NULL;
        _gitProcess = NULL;
    }
    process = axyne_macos_git_take_process(run);
    if (process != NULL) axyne_process_release(process);
    /* Drop the completion reference taken in the exit callback and the
     * owner reference taken when the run was created. */
    axyne_macos_git_release(run);
    axyne_macos_git_release(run);
    if (completion != NULL) {
        free(completion->report);
        free(completion);
    }
    [self setNeedsDisplay:YES];
    [self release];
}

- (void)showGitStatus:(id)sender
{
    (void)sender;
    static const char *const arguments[] = {
        "--no-pager", "status", "--short", "--branch"
    };
    [self startGitOperationWithEmptyMessage:"No Git status output."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
}

- (void)showGitDiff:(id)sender
{
    (void)sender;
    static const char *const arguments[] = {
        "--no-pager", "diff", "--no-color"
    };
    [self startGitOperationWithEmptyMessage:"No Git differences."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
}

- (void)stageAllGitChanges:(id)sender
{
    (void)sender;
    static const char *const arguments[] = { "add", "--all" };
    [self startGitOperationWithEmptyMessage:"All workspace changes staged."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
}

- (void)unstageAllGitChanges:(id)sender
{
    (void)sender;
    static const char *const arguments[] = { "reset", "--mixed" };
    [self startGitOperationWithEmptyMessage:"All changes unstaged."
                                  arguments:arguments
                                     count:sizeof(arguments) / sizeof(arguments[0])];
}

- (void)findOrReplace:(BOOL)replace
{
    NSString *q = [self askForText:replace ? @"Replace" : @"Find" label:@"Find text"];
    if ([q length] == 0 || ![self captureEditor]) return;
    NSString *r = replace ? [self askForText:@"Replace" label:@"Replace with"] : nil;
    if (replace && r == nil) return;
    BOOL replaceAll = NO;
    if (replace) {
        NSAlert *choice = [[[NSAlert alloc] init] autorelease];
        [choice setMessageText:@"Replace all occurrences?"];
        [choice addButtonWithTitle:@"Replace All"];
        [choice addButtonWithTitle:@"Replace Next"];
        [choice addButtonWithTitle:@"Cancel"];
        NSInteger answer = [choice runModal];
        if (answer == NSAlertThirdButtonReturn) return;
        replaceAll = answer == NSAlertFirstButtonReturn;
    }
    size_t length = (size_t)[self sendEditorMessage:SCI_GETTEXTLENGTH wParam:0 lParam:0];
    char *text = malloc(length + 1);
    if (text == NULL) return;
    (void)[self sendEditorMessage:SCI_GETTEXT wParam:length + 1 lParam:(intptr_t)text];
    const char *query = [q UTF8String]; size_t at;
    if (replaceAll) {
        char *output = NULL; size_t outputLength = 0, count = 0;
        if (axyne_search_replace_all(text, length, query, strlen(query),
                [r UTF8String], strlen([r UTF8String]), 0, &output,
                &outputLength, &count, NULL) == AXYNE_STATUS_OK) {
            if (count > 0) {
                (void)[self sendEditorMessage:SCI_BEGINUNDOACTION wParam:0 lParam:0];
                size_t queryLength = strlen(query), replacementLength = strlen([r UTF8String]);
                size_t searchStart = 0, previousSourceEnd = 0, previousLiveEnd = 0;
                size_t match = 0; BOOL first = YES;
                while (axyne_search_find(text, length, query, queryLength,
                                         searchStart, 0, &match) && match >= searchStart) {
                    size_t liveAt = first ? match : previousLiveEnd +
                        (match - previousSourceEnd);
                    (void)[self sendEditorMessage:SCI_SETSEL wParam:liveAt
                        lParam:liveAt + queryLength];
                    (void)[self sendEditorMessage:SCI_REPLACESEL wParam:0
                        lParam:(intptr_t)[r UTF8String]];
                    previousSourceEnd = match + queryLength;
                    previousLiveEnd = liveAt + replacementLength;
                    searchStart = previousSourceEnd;
                    first = NO;
                }
                (void)[self sendEditorMessage:SCI_ENDUNDOACTION wParam:0 lParam:0];
            }
            free(output);
        }
        free(text); return;
    }
    size_t start = (size_t)[self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    if (axyne_search_find(text, length, query, strlen(query), start, 0, &at)) {
        (void)[self sendEditorMessage:SCI_SETSEL wParam:at lParam:at + strlen(query)];
        if (replace) (void)[self sendEditorMessage:SCI_REPLACESEL wParam:0 lParam:(intptr_t)[r UTF8String]];
    } else {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"No match found"]; [alert runModal];
    }
    free(text);
}

- (void)searchFolder:(BOOL)quickFile
{
    /* Quick file lookup lives in the command palette; this method keeps the
     * text search over a chosen folder. */
    if (quickFile) { [self openPaletteWithInput:@""]; return; }
    NSOpenPanel *folder = [NSOpenPanel openPanel];
    [folder setCanChooseDirectories:YES]; [folder setCanChooseFiles:NO];
    [folder setAllowsMultipleSelection:NO];
    if ([folder runModal] != NSModalResponseOK) return;
    NSString *query = [self askForText:@"Search Folder" label:@"Search text"];
    if ([query length] == 0) return;
    NSPopUpButton *choices = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 480, 28)
                                                        pullsDown:NO] autorelease];
    size_t selectedLine = 0;
    {
        AxyneSearchResults results = {0};
        if (axyne_search_workspace([[[folder URL] path] UTF8String], [query UTF8String],
                                   0, &results, NULL) == AXYNE_STATUS_OK) {
            for (size_t i = 0; i < results.count; ++i) {
                NSString *path = [NSString stringWithUTF8String:results.items[i].path];
                NSString *preview = [NSString stringWithUTF8String:results.items[i].preview];
                if (path == nil) path = @"";
                if (preview == nil) preview = @"";
                [choices addItemWithTitle:[NSString stringWithFormat:@"%@:%zu  %@", path,
                    results.items[i].line, preview]];
                NSDictionary *match = @{
                    @"path": path,
                    @"line": [NSNumber numberWithUnsignedLong:results.items[i].line]
                };
                [[choices itemAtIndex:(NSInteger)i] setRepresentedObject:match];
            }
            if (results.count > 0) {
                NSAlert *pick = [[[NSAlert alloc] init] autorelease];
                [pick setMessageText:[NSString stringWithFormat:@"%zu matches", results.count]];
                [pick setAccessoryView:choices]; [pick addButtonWithTitle:@"Open Match"];
                [pick addButtonWithTitle:@"Cancel"];
                if ([pick runModal] == NSAlertFirstButtonReturn) {
                    NSDictionary *match = [[choices selectedItem] representedObject];
                    NSString *path = [match objectForKey:@"path"];
                    selectedLine = (size_t)[[match objectForKey:@"line"] unsignedLongValue];
                    if (path != nil && selectedLine > 0) {
                        [self openPath:path];
                        AxyneDocument *opened = [self activeDocument];
                        if (opened != NULL && opened->path != NULL &&
                            strcmp(opened->path, [path UTF8String]) == 0) {
                            NSInteger pos = [self sendEditorMessage:SCI_POSITIONFROMLINE
                                wParam:selectedLine > 0 ? selectedLine - 1 : 0 lParam:0];
                            (void)[self sendEditorMessage:SCI_GOTOPOS wParam:(uintptr_t)pos lParam:0];
                        }
                    }
                }
            }
            axyne_search_results_destroy(&results);
        }
    }
}

- (void)terminalAppend:(const char *)bytes length:(size_t)length
                stream:(AxyneProcessStream)stream
{
    if (_terminalOutput == nil || bytes == NULL || length == 0) return;
    NSString *text = [[[NSString alloc] initWithBytes:bytes length:length
                                             encoding:NSUTF8StringEncoding] autorelease];
    if (text == nil) text = @"(invalid UTF-8 output)";
    if (stream == AXYNE_PROCESS_STDERR) text = [@"[stderr] " stringByAppendingString:text];
    NSTextStorage *storage = [_terminalOutput textStorage];
    /* Figma output rows are 12px text on an 18px line. */
    NSFont *outputFont = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    NSMutableParagraphStyle *lineStyle = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [lineStyle setLineSpacing:MAX(0, 18 - ceil([outputFont ascender] -
        [outputFont descender] + [outputFont leading]))];
    uint32_t outputColor = axyne_macos_reference_surfaces(&_preferences.theme)
        ? 0xa9aeb6 : _preferences.theme.text;
    [storage appendAttributedString:[[[NSAttributedString alloc]
        initWithString:text attributes:@{ NSFontAttributeName:outputFont,
            NSParagraphStyleAttributeName:lineStyle,
            NSForegroundColorAttributeName:axyne_preference_color(
                stream == AXYNE_PROCESS_STDERR ? 0xe5a445 : outputColor) }]
        autorelease]];
    if ([storage length] > 1024 * 1024)
        [storage deleteCharactersInRange:NSMakeRange(0, [storage length] - 1024 * 1024)];
    [_terminalOutput scrollRangeToVisible:NSMakeRange([storage length], 0)];
}

- (void)terminalExited:(AxyneProcess *)process exitCode:(int)exitCode
{
    char message[96];
    _lastExitCode = exitCode;
    _lastExitFailed = exitCode != 0;
    _hasExitStatus = YES;
    (void)snprintf(message, sizeof(message),
        _lastExitFailed ? "[failed: exit %d]\n" : "[exit %d]\n", exitCode);
    [self terminalAppend:message length:strlen(message)
                   stream:AXYNE_PROCESS_STDOUT];
    if (_terminalProcess == process) {
        axyne_process_release(_terminalProcess);
        _terminalProcess = NULL;
    }
    axyne_debugger_release(&_debugger);
    _activeAction = 0;
    [_terminalStart setEnabled:YES];
    [_terminalStop setEnabled:NO];
    [_debugStart setEnabled:YES];
    [_debugPause setEnabled:NO];
    [_debugContinue setEnabled:NO];
    [_debugNext setEnabled:NO];
    [_debugBreakpoint setEnabled:NO];
    [self refreshActionControls];
    [self setNeedsDisplay:YES];
}

- (void)startDebugger:(id)sender
{
    AxyneDocument *document;
    AxyneError error;
    (void)sender;
    if (axyne_debugger_is_active(&_debugger) || ![self captureEditor]) return;
    if (_terminalProcess != NULL) {
        const char *message = "Debugger is unavailable while a terminal session is active. Stop the terminal first.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    document = [self activeDocument];
    if (document == NULL || document->is_untitled || document->path == NULL ||
        document->is_dirty) {
        const char *message =
            "The debugger requires a saved, clean, non-untitled document.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    if (axyne_debugger_start(&_debugger, document,
            axyne_macos_terminal_output, axyne_macos_terminal_exit, self,
            &error) != AXYNE_STATUS_OK) {
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    [self terminalAppend:"[debugger]\n" length:12 stream:AXYNE_PROCESS_STDOUT];
    [_debugStart setEnabled:NO]; [_debugPause setEnabled:YES];
    [_debugContinue setEnabled:YES]; [_debugNext setEnabled:YES];
    [_debugBreakpoint setEnabled:YES]; _activeAction = 4;
    [self refreshActionControls];
}

- (void)debugCommand:(id)sender
{
    AxyneDebuggerCommand command = (AxyneDebuggerCommand)[sender tag];
    AxyneError error;
    if (axyne_debugger_command(&_debugger, command, &error) != AXYNE_STATUS_OK)
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
}

- (void)toggleBreakpoint:(id)sender
{
    AxyneDocument *document = [self activeDocument];
    AxyneError error;
    size_t position;
    size_t line;
    (void)sender;
    if (document == NULL || document->path == NULL) return;
    position = (size_t)[self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    line = (size_t)[self sendEditorMessage:2166 wParam:position lParam:0] + 1;
    if (axyne_debugger_toggle_breakpoint(&_debugger, document->path, line,
                                         &error) != AXYNE_STATUS_OK)
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
}

- (void)startTerminal:(id)sender
{
    AxyneProcessSpec spec;
    AxyneError error;
    AxyneStatus status;
    (void)sender;
    if (_terminalProcess != NULL || axyne_debugger_is_active(&_debugger)) {
        const char *message = "Terminal is unavailable while the debugger session is active. Stop the debugger first.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    status = axyne_runner_process_spec(&_terminalRunner,
        axyne_macos_terminal_output, axyne_macos_terminal_exit, self,
        &spec, &error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_process_start(&spec, &_terminalProcess, &error);
    if (status != AXYNE_STATUS_OK) {
        _lastExitFailed = NO;
        _hasExitStatus = NO;
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
        [self setNeedsDisplay:YES];
        return;
    }
    [_terminalStart setEnabled:NO];
    [_terminalStop setEnabled:YES];
    _activeAction = 3;
    _lastExitFailed = NO;
    [self refreshActionControls];
}

- (void)stopTerminal:(id)sender
{
    (void)sender;
    if (_terminalProcess != NULL) (void)axyne_process_terminate(_terminalProcess, NULL);
}

- (void)sendTerminal:(id)sender
{
    const char *value;
    NSMutableData *data;
    AxyneError error;
    (void)sender;
    if (_terminalProcess == NULL || [[_terminalInput stringValue] length] == 0) return;
    value = [[_terminalInput stringValue] UTF8String];
    data = [NSMutableData dataWithBytes:value length:strlen(value)];
    [data appendBytes:"\n" length:1];
    if (axyne_process_write(_terminalProcess, [data bytes], [data length], &error) != AXYNE_STATUS_OK)
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
else [_terminalInput setStringValue:@""];
}

- (BOOL)configureRunner
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    NSView *accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 460, 280)];
    NSTextField *executable = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 246, 460, 24)];
    NSTextField *workingDirectory = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 201, 460, 24)];
    NSTextView *arguments = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 440, 70)];
    NSTextView *environment = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 440, 70)];
    NSScrollView *argumentsScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 122, 460, 70)];
    NSScrollView *environmentScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 32, 460, 70)];
    NSString *initialExecutable = _actionRunner.executable != NULL
        ? [NSString stringWithUTF8String:_actionRunner.executable] : @"";
    NSString *initialWorkingDirectory = _actionRunner.working_directory != NULL
        ? [NSString stringWithUTF8String:_actionRunner.working_directory] : @"";
    [executable setStringValue:initialExecutable != nil ? initialExecutable : @""];
    [workingDirectory setStringValue:initialWorkingDirectory != nil
        ? initialWorkingDirectory : @""];
    [arguments setString:axyne_macos_runner_lines(_actionRunner.arguments,
                                                   _actionRunner.argument_count)];
    [environment setString:axyne_macos_runner_lines(_actionRunner.environment,
                                                     _actionRunner.environment_count)];
    [arguments setFont:[NSFont userFixedPitchFontOfSize:11]];
    [environment setFont:[NSFont userFixedPitchFontOfSize:11]];
    [argumentsScroll setHasVerticalScroller:YES];
    [argumentsScroll setDocumentView:arguments];
    [environmentScroll setHasVerticalScroller:YES];
    [environmentScroll setDocumentView:environment];
    [accessory addSubview:axyne_macos_label(@"Executable", 224)];
    [accessory addSubview:executable];
    [accessory addSubview:axyne_macos_label(@"Arguments (one per line)", 194)];
    [accessory addSubview:argumentsScroll];
    [accessory addSubview:axyne_macos_label(@"Working directory (optional)", 179)];
    [accessory addSubview:workingDirectory];
    [accessory addSubview:axyne_macos_label(
        @"Environment overrides (NAME=VALUE per line)", 104)];
    [accessory addSubview:environmentScroll];
    [alert setMessageText:@"Configure Build/Run Runner"];
    [alert setInformativeText:@"Arguments are passed directly to the executable; no shell is used."];
    [alert setAccessoryView:accessory];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];
    NSInteger response = [alert runModal];
    BOOL accepted = NO;
    if (response == NSAlertFirstButtonReturn) {
        const char *executableText = [[executable stringValue] UTF8String];
        const char *argumentsText = [[arguments string] UTF8String];
        const char *workingDirectoryText = [[workingDirectory stringValue] UTF8String];
        const char *environmentText = [[environment string] UTF8String];
        char *executableUTF8 = strdup(executableText != NULL ? executableText : "");
        char *argumentsUTF8 = strdup(argumentsText != NULL ? argumentsText : "");
        char *workingDirectoryUTF8 = strdup(
            workingDirectoryText != NULL ? workingDirectoryText : "");
        char *environmentUTF8 = strdup(environmentText != NULL ? environmentText : "");
        char **argumentValues = NULL;
        char **environmentValues = NULL;
        size_t argumentCount = 0;
        size_t environmentCount = 0;
        AxyneRunnerSpec spec = {0};
        AxyneError error = {0};
        AxyneStatus status = AXYNE_STATUS_OK;
        if (executableUTF8 == NULL || executableUTF8[0] == '\0' ||
            argumentsUTF8 == NULL || workingDirectoryUTF8 == NULL ||
            environmentUTF8 == NULL ||
            !axyne_macos_runner_split_lines(argumentsUTF8, &argumentValues,
                                            &argumentCount) ||
            !axyne_macos_runner_split_lines(environmentUTF8, &environmentValues,
                                            &environmentCount)) {
            status = AXYNE_STATUS_OUT_OF_MEMORY;
            (void)snprintf(error.message, sizeof(error.message),
                           "Unable to read runner configuration.");
        } else {
            spec.executable = executableUTF8;
            spec.arguments = (const char *const *)argumentValues;
            spec.argument_count = argumentCount;
            spec.working_directory = workingDirectoryUTF8[0] != '\0'
                ? workingDirectoryUTF8 : NULL;
            spec.environment = (const char *const *)environmentValues;
            spec.environment_count = environmentCount;
            status = axyne_runner_configure(&_actionRunner, &spec, &error);
        }
        free(executableUTF8);
        free(argumentsUTF8);
        free(workingDirectoryUTF8);
        free(environmentUTF8);
        axyne_macos_runner_values_free(argumentValues, argumentCount);
        axyne_macos_runner_values_free(environmentValues, environmentCount);
        if (status == AXYNE_STATUS_OK) {
            accepted = YES;
        } else {
            NSAlert *errorAlert = [[[NSAlert alloc] init] autorelease];
            [errorAlert setMessageText:@"Invalid runner configuration"];
            NSString *detail = [NSString stringWithUTF8String:
                error.message[0] != '\0' ? error.message :
                "Unable to configure runner."];
            [errorAlert setInformativeText:detail != nil ? detail : @""];
            [errorAlert addButtonWithTitle:@"OK"];
            [errorAlert runModal];
        }
    }
    [executable release];
    [workingDirectory release];
    [arguments release];
    [environment release];
    [argumentsScroll release];
    [environmentScroll release];
    [accessory release];
    return accepted;
}

- (void)startAction:(BOOL)run
{
    AxyneDocument *doc = [self activeDocument];
    AxyneProcessSpec processSpec;
    AxyneError error;
    AxyneStatus status;
    if (_terminalProcess != NULL || axyne_debugger_is_active(&_debugger)) {
        const char *message = "Build or run is unavailable while a terminal or debugger session is active. Stop it first.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    if (![self captureEditor]) return;
    if (doc == NULL || doc->is_untitled || doc->path == NULL || doc->is_dirty) {
        if (![self saveActive]) {
            const char *message = "Save the active document before building or running.\n";
            [self terminalAppend:message length:strlen(message)
                           stream:AXYNE_PROCESS_STDERR];
            return;
        }
        doc = [self activeDocument];
        if (doc == NULL || doc->is_untitled || doc->path == NULL || doc->is_dirty)
            return;
    }
    if (_actionRunner.executable == NULL) {
        const char *message = "Configure the Build/Run Runner before building or running.\n";
        [self terminalAppend:message length:strlen(message)
                       stream:AXYNE_PROCESS_STDERR];
        return;
    }
    status = axyne_runner_process_spec(&_actionRunner,
            axyne_macos_terminal_output, axyne_macos_terminal_exit, self,
            &processSpec, &error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_process_start(&processSpec, &_terminalProcess, &error);
    if (status != AXYNE_STATUS_OK) {
        _lastExitFailed = NO;
        _hasExitStatus = NO;
        [self terminalAppend:error.message length:strlen(error.message)
                       stream:AXYNE_PROCESS_STDERR];
        [self setNeedsDisplay:YES];
    } else {
        [_terminalOutput setString:(run ? @"[run]\n" : @"[build]\n")];
        _activeAction = run ? 2 : 1;
        _lastExitFailed = NO;
        [_terminalStart setEnabled:NO];
        [_terminalStop setEnabled:YES];
        [self refreshActionControls];
        [self setNeedsDisplay:YES];
    }
}

- (void)buildDocument:(id)sender
{
    (void)sender;
    [self startAction:NO];
}

- (void)runDocument:(id)sender
{
    (void)sender;
    [self startAction:YES];
}

/* Tab badges hug their text (Figma: badge, 8px gap, name) instead of sitting
 * in a fixed slot; untitled files reserve the width of the outline icon. */
static CGFloat axyne_macos_tab_badge_width(const char *title)
{
    AxyneFileBadge badge = axyne_ui_file_badge(title);
    if (badge.label[0] == '\0') return 8;
    NSFont *font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightBold];
    NSString *label = [NSString stringWithUTF8String:badge.label];
    return ceil([label sizeWithAttributes:@{NSFontAttributeName:font}].width);
}

/* Tab title attributes shared by measurement and drawing so an italic
 * preview title never clips or leaves a gap. Preview titles use the system
 * font's italic face; if it has none, the upright face is skewed with
 * NSObliqueness. Returns an autoreleased dictionary; `color` may be nil. */
static NSDictionary *axyne_macos_tab_title_attributes(BOOL preview, NSColor *color)
{
    NSFont *font = [NSFont systemFontOfSize:12];
    NSMutableDictionary *attributes = [NSMutableDictionary dictionary];
    if (preview) {
        NSFontManager *manager = [NSFontManager sharedFontManager];
        NSFont *italic = [manager convertFont:font toHaveTrait:NSItalicFontMask];
        if (italic != nil && ([manager traitsOfFont:italic] & NSItalicFontMask) != 0)
            font = italic;
        else
            [attributes setObject:[NSNumber numberWithDouble:0.2]
                           forKey:NSObliquenessAttributeName];
    }
    [attributes setObject:font forKey:NSFontAttributeName];
    if (color != nil) [attributes setObject:color forKey:NSForegroundColorAttributeName];
    return attributes;
}

- (NSRect)tabFrameAtIndex:(size_t)index
{
    CGFloat x = [self sidebarWidth] - _tabScroll;
    for (size_t i = 0; i < _documents.count; ++i) {
        AxyneDocument *doc = &_documents.documents[i];
        /* An untouched empty Untitled buffer has no tab and takes no width. */
        if (axyne_document_tab_hidden(doc)) continue;
        NSString *title = [NSString stringWithUTF8String:doc->title != NULL ? doc->title : "Untitled"];
        CGFloat nameWidth = [title sizeWithAttributes:
            axyne_macos_tab_title_attributes(doc->preview != 0, nil)].width;

        /* 14 padding, badge, 8 gap, name, 8 gap, close glyph, 14 padding. */
        CGFloat width = MIN(240, 14 + axyne_macos_tab_badge_width(doc->title) + 8 +
            ceil(nameWidth) + 8 + 8 + 14);
        if (i == index) return NSMakeRect(x, AXYNE_CONTENT_TOP, width, AXYNE_TABS);
        x += width;
    }
    return NSZeroRect;
}

- (void)scrollTabsBy:(CGFloat)delta
{
    NSRect last = NSZeroRect;
    for (size_t i = _documents.count; i > 0; --i) {
        if (!axyne_document_tab_hidden(&_documents.documents[i - 1])) {
            last = [self tabFrameAtIndex:i - 1];
            break;
        }
    }
    if (NSIsEmptyRect(last)) { _tabScroll = 0; return; }
    CGFloat maximum = MAX(0, NSMaxX(last) + _tabScroll - NSWidth([self bounds]));
    _tabScroll = MIN(maximum, MAX(0, _tabScroll + delta));
    /* Redraw only: layout reveals the active tab and would undo manual scroll. */
    [self setNeedsDisplay:YES];
}

- (void)layout
{
    [super layout];
    NSRect bounds = [self bounds];
    CGFloat width = NSWidth(bounds);
    CGFloat bottomTop = NSHeight(bounds) - AXYNE_STATUS - [self panelHeight];
    CGFloat editorTop = AXYNE_CONTENT_TOP + AXYNE_TABS;
    [_editorView setFrame:NSMakeRect([self sidebarWidth], editorTop,
        MAX(0, width - [self sidebarWidth]), MAX(0, bottomTop - editorTop))];
    NSInteger visibleRows = MAX(1, (NSInteger)((bottomTop - editorTop -
        AXYNE_UI_EXPLORER_HEADER) / AXYNE_UI_ROW));
    _explorerFirstRow = MIN(_explorerFirstRow, MAX(0, (NSInteger)_explorer.count - visibleRows));
    BOOL terminal = _panelMode == 2;
    CGFloat inputTop = NSHeight(bounds) - AXYNE_STATUS - 28;
    [_terminalScroll setFrame:NSMakeRect([self sidebarWidth] + 16, bottomTop + 32,
        MAX(0, width - [self sidebarWidth] - 24), terminal ? 164 : 198)];
    [_terminalScroll setHidden:_panelMode == 1];
    [_terminalInput setFrame:NSMakeRect([self sidebarWidth] + 16, inputTop,
        MAX(0, width - [self sidebarWidth] - 88), 22)];
    [_terminalSend setFrame:NSMakeRect(width - 64, inputTop, 52, 22)];
    [_terminalInput setHidden:!terminal]; [_terminalSend setHidden:!terminal];
    [_terminalInput setBezeled:NO];
    [_problemSummary setFrame:NSMakeRect([self sidebarWidth] + 16, bottomTop + 42,
        MAX(0, width - [self sidebarWidth] - 32), 22)];
    [_problemSummary setHidden:_panelMode != 1];
    [_outputTab setFrame:NSMakeRect([self sidebarWidth] + 8, bottomTop, 38, 32)];
    [_problemsTab setFrame:NSMakeRect([self sidebarWidth] + 48, bottomTop, 38, 32)];
    [_terminalTab setFrame:NSMakeRect([self sidebarWidth] + 88, bottomTop, 50, 32)];
    [_terminalStart setFrame:NSMakeRect(width - 100, bottomTop + 2, 28, 28)];
    [_clearOutput setFrame:NSMakeRect(width - 68, bottomTop + 2, 28, 28)];
    [_terminalStop setFrame:NSMakeRect(width - 36, bottomTop + 2, 28, 28)];
    CGFloat x = 8;
    size_t icon = 0;
    for (NSButton *button in @[_newButton, _openButton, _saveButton, _undoButton, _redoButton]) {
        [button setFrame:NSMakeRect(x, AXYNE_MENU + 5, 28, 28)];
        x += 30;
        if (++icon == 3) x += 2;
    }
    x += 2;
    CGFloat targetWidth = MIN(220, MAX(96, ceil([[(AxyneChromeButton *)_targetButton
        richTitle] size].width) + 20));
    [_targetButton setFrame:NSMakeRect(x, AXYNE_MENU + 6, targetWidth, 26)]; x += targetWidth + 8;
    [_buildButton setFrame:NSMakeRect(x, AXYNE_MENU + 6, 88, 26)]; x += 96;
    [_runButton setFrame:NSMakeRect(x, AXYNE_MENU + 6, 86, 26)]; x += 94;
    CGFloat searchLeft = MAX(x + 8, width - 348);
    CGFloat searchWidth = width - 8 - searchLeft;
    [_searchButton setHidden:searchWidth < 120];
    [_searchButton setFrame:NSMakeRect(searchLeft, AXYNE_MENU + 6, MAX(0, searchWidth), 26)];
    for (NSButton *button in @[_outputTab, _problemsTab, _terminalTab]) {
        [(AxyneChromeButton *)button setLabelColor:axyne_preference_color(
            [button tag] == _panelMode ? _preferences.theme.text : _preferences.theme.muted)];
        [button setNeedsDisplay:YES];
    }
    if (_documents.count != 0 && _documents.active_index < _documents.count &&
        !axyne_document_tab_hidden(&_documents.documents[_documents.active_index])) {
        NSRect active = [self tabFrameAtIndex:_documents.active_index];
        if (NSMaxX(active) > width) _tabScroll += NSMaxX(active) - width;
        else if (NSMinX(active) < [self sidebarWidth])
            _tabScroll = MAX(0, _tabScroll - [self sidebarWidth] + NSMinX(active));
    }
    /* A hidden bottom panel takes every panel control with it; the per-mode
     * visibility set above still applies when it is shown. */
    for (NSView *panelView in @[_outputTab, _problemsTab, _terminalTab,
                                _terminalStart, _clearOutput, _terminalStop])
        [panelView setHidden:_panelHidden];
    if (_panelHidden) {
        [_terminalScroll setHidden:YES]; [_terminalInput setHidden:YES];
        [_terminalSend setHidden:YES]; [_problemSummary setHidden:YES];
    }
    [self layoutPalette];
}

- (void)drawLabel:(NSString *)label at:(NSPoint)point
             size:(CGFloat)size color:(NSColor *)color family:(NSString *)family
{
    NSDictionary *attributes = @{
        NSFontAttributeName: ([NSFont fontWithName:family size:size] != nil
            ? [NSFont fontWithName:family size:size]
            : [NSFont systemFontOfSize:size]),
        NSForegroundColorAttributeName: color
    };
    [label drawAtPoint:point withAttributes:attributes];
}

- (void)drawFileBadge:(const char *)name inRect:(NSRect)rect tab:(BOOL)tab
{
    /* Chip shared with Windows: 14pt high, radius 3, badge colour at 18%
     * alpha, no border, bold 9pt label centred both ways. The label is never
     * empty (see axyne_ui_file_badge). Everything here is autoreleased. */
    AxyneFileBadge badge = axyne_ui_file_badge(name);
    NSString *label = [NSString stringWithUTF8String:badge.label];
    NSColor *accent = axyne_preference_color(badge.color);
    NSRect chip;
    NSFont *font;
    NSDictionary *attributes;
    NSSize extent;
    (void)tab;
    if (label == nil || [label length] == 0) return;
    chip = NSMakeRect(NSMinX(rect),
        NSMinY(rect) + floor((NSHeight(rect) - AXYNE_UI_BADGE_HEIGHT) / 2.0),
        NSWidth(rect), AXYNE_UI_BADGE_HEIGHT);
    [[accent colorWithAlphaComponent:AXYNE_UI_BADGE_ALPHA_PERCENT / 100.0] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:chip
        xRadius:AXYNE_UI_BADGE_RADIUS yRadius:AXYNE_UI_BADGE_RADIUS] fill];
    font = [NSFont monospacedSystemFontOfSize:AXYNE_UI_BADGE_FONT_PT
                                       weight:NSFontWeightBold];
    attributes = @{NSFontAttributeName: font,
                   NSForegroundColorAttributeName: accent};
    extent = [label sizeWithAttributes:attributes];
    [label drawAtPoint:NSMakePoint(
            NSMinX(chip) + floor((NSWidth(chip) - extent.width) / 2.0),
            NSMinY(chip) + floor((NSHeight(chip) - extent.height) / 2.0))
        withAttributes:attributes];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    CGFloat width = NSWidth(bounds), height = NSHeight(bounds);
    CGFloat bottomTop = height - AXYNE_STATUS - [self panelHeight];
    CGFloat statusTop = height - AXYNE_STATUS;
    CGFloat editorTop = AXYNE_CONTENT_TOP + AXYNE_TABS;
    BOOL light = _preferences.theme.preset == AXYNE_THEME_LIGHT ||
        (_preferences.theme.preset == AXYNE_THEME_SYSTEM && !axyne_macos_prefers_dark(self));
    NSColor *background = axyne_preference_color(_preferences.theme.background);
    NSColor *panel = axyne_preference_color(_preferences.theme.panel);
    NSColor *muted = axyne_preference_color(_preferences.theme.muted);
    NSColor *text = axyne_preference_color(_preferences.theme.text);
    NSColor *border = axyne_preference_color(_preferences.theme.border);
    NSColor *toolbar = axyne_preference_color(_preferences.theme.toolbar);
    BOOL reference = axyne_macos_reference_surfaces(&_preferences.theme);
    NSColor *tabBackground = axyne_preference_color(reference ? 0x17191c : _preferences.theme.toolbar);
    [background setFill]; NSRectFill(bounds);
    {
        /* Menu bar band: Figma colours on the default dark theme, the user's
         * theme colours otherwise. */
        BOOL figma = reference && !light;
        NSColor *menuBackground = axyne_preference_color(figma ? 0x101216 : _preferences.theme.toolbar);
        NSColor *menuBorder = axyne_preference_color(figma ? 0x25282e : _preferences.theme.border);
        NSColor *menuActive = axyne_preference_color(figma ? 0x202329 : _preferences.theme.panel);
        NSColor *menuText = axyne_preference_color(figma ? 0xd2d5db : _preferences.theme.text);
        NSColor *menuMuted = axyne_preference_color(figma ? 0x969ba5 : _preferences.theme.muted);
        NSDictionary *menuAttributes = @{NSFontAttributeName:[NSFont systemFontOfSize:12]};
        [menuBackground setFill]; NSRectFill(NSMakeRect(0, 0, width, AXYNE_MENU));
        [menuBorder setFill]; NSRectFill(NSMakeRect(0, AXYNE_MENU - 1, width, 1));
        for (NSUInteger menuIndex = 0; menuIndex < AXYNE_UI_MENU_COUNT; ++menuIndex) {
            NSString *menuLabel = axyne_macos_menu_label(menuIndex);
            NSRect itemRect = [self menuBarItemRect:menuIndex];
            BOOL lit = (NSInteger)menuIndex == _activeMenuIndex ||
                (NSInteger)menuIndex == _hoverMenuIndex;
            if (lit) {
                [menuActive setFill];
                [[NSBezierPath bezierPathWithRoundedRect:itemRect
                    xRadius:AXYNE_UI_MENU_ITEM_RADIUS yRadius:AXYNE_UI_MENU_ITEM_RADIUS] fill];
            }
            [self drawLabel:menuLabel
                at:NSMakePoint(NSMinX(itemRect) + AXYNE_UI_MENU_PAD,
                    floor(NSMidY(itemRect) - [menuLabel sizeWithAttributes:menuAttributes].height / 2))
                size:12 color:lit ? menuText : menuMuted family:@"SF Pro Text"];
        }
    }
    [toolbar setFill]; NSRectFill(NSMakeRect(0, AXYNE_MENU, width, AXYNE_TOOLBAR));
    [tabBackground setFill]; NSRectFill(NSMakeRect(0, AXYNE_CONTENT_TOP, width, AXYNE_TABS));
    [panel setFill]; NSRectFill(NSMakeRect(0, editorTop, [self sidebarWidth], statusTop - editorTop));
    [axyne_preference_color(reference ? 0x191b1f : _preferences.theme.toolbar) setFill];
    NSRectFill(NSMakeRect(0, AXYNE_CONTENT_TOP, [self sidebarWidth], AXYNE_TABS));
    if (!_panelHidden) {
        [tabBackground setFill];
        NSRectFill(NSMakeRect([self sidebarWidth], bottomTop, width - [self sidebarWidth], 32));
        [axyne_preference_color(axyne_macos_output_background(&_preferences.theme)) setFill];
        NSRectFill(NSMakeRect([self sidebarWidth], bottomTop + 32, width - [self sidebarWidth], 198));
    }
    [toolbar setFill]; NSRectFill(NSMakeRect(0, statusTop, width, AXYNE_STATUS));
    [border setFill];
    NSRectFill(NSMakeRect(0, AXYNE_CONTENT_TOP - 1, width, 1));
    if (!_explorerHidden)
        NSRectFill(NSMakeRect([self sidebarWidth] - 1, editorTop, 1, statusTop - editorTop));
    if (!_panelHidden)
        NSRectFill(NSMakeRect(0, bottomTop, width, 1));
    if (![_searchButton isHidden]) {
    [axyne_preference_color(reference ? 0x3a3d44 : _preferences.theme.border) setStroke];
        [[NSBezierPath bezierPathWithRoundedRect:[_searchButton frame] xRadius:4 yRadius:4] stroke];
    }
    if (!_panelHidden) {
        NSButton *selected = _panelMode == 0 ? _outputTab : (_panelMode == 1 ? _problemsTab : _terminalTab);
        [axyne_preference_color(reference ? 0xa66bf0 : _preferences.theme.accent) setFill];
        NSRectFill(NSMakeRect(NSMinX([selected frame]), bottomTop + 29, NSWidth([selected frame]), 3));
        NSString *shell = _terminalRunner.executable == NULL ? @"" :
            [[NSString stringWithUTF8String:_terminalRunner.executable] lastPathComponent];
        [self drawLabel:shell at:NSMakePoint([self sidebarWidth] + 148, bottomTop + 10)
            size:11 color:muted family:@"SF Pro Text"];
    }
    if (_palette.active) [self drawPaletteFieldInRect:[_searchButton frame]];

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect([self sidebarWidth], AXYNE_CONTENT_TOP, MAX(0, width - [self sidebarWidth]), AXYNE_TABS));
    for (size_t i = 0; i < _documents.count; ++i) {
        AxyneDocument *doc = &_documents.documents[i];
        if (axyne_document_tab_hidden(doc)) continue;
        NSRect frame = [self tabFrameAtIndex:i];
        if (NSMaxX(frame) <= [self sidebarWidth] || NSMinX(frame) >= width) continue;
        BOOL active = i == _documents.active_index;
        if (active) {
            [axyne_preference_color(_preferences.theme.editor_background) setFill]; NSRectFill(frame);
            [axyne_preference_color(reference ? 0xa66bf0 : _preferences.theme.accent) setFill];
            NSRectFill(NSMakeRect(NSMinX(frame), NSMinY(frame), NSWidth(frame), 2));
        }
        CGFloat badgeX = NSMinX(frame) + 14;
        CGFloat badgeWidth = axyne_macos_tab_badge_width(doc->title);
        CGFloat nameX = badgeX + badgeWidth + 8;
        [self drawFileBadge:(doc->path != NULL && doc->path[0] != '\0') ? doc->path : doc->title
            inRect:NSMakeRect(badgeX, AXYNE_CONTENT_TOP + 10, badgeWidth, 16) tab:YES];
        NSString *title = [NSString stringWithUTF8String:doc->title != NULL ? doc->title : "Untitled"];
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(NSMakeRect(nameX, AXYNE_CONTENT_TOP + 4, MAX(0, NSMaxX(frame) - 30 - nameX), 28));
        [(title != nil ? title : @"Untitled")
            drawAtPoint:NSMakePoint(nameX, AXYNE_CONTENT_TOP + 10)
            withAttributes:axyne_macos_tab_title_attributes(doc->preview != 0,
                active ? axyne_preference_color(light ? 0x24272d : 0xe6e7ea) : muted)];
        [NSGraphicsContext restoreGraphicsState];
        [self drawLabel:doc->is_dirty ? @"●" : @"×"
            at:NSMakePoint(NSMaxX(frame) - (doc->is_dirty ? 19 : 22),
                           AXYNE_CONTENT_TOP + (doc->is_dirty ? 13 : 10))
            size:doc->is_dirty ? 7 : 13
            color:doc->is_dirty && reference && !light ? axyne_preference_color(0x4f535b) : muted
            family:@"SF Pro Text"];
    }
    [NSGraphicsContext restoreGraphicsState];
    if (!_explorerHidden) {
        [self drawLabel:@"탐색기" at:NSMakePoint(12, editorTop + 8)
            size:11 color:axyne_preference_color(light ? 0x68707d : 0x8b919b) family:@"SF Pro Text"];
        CGFloat explorerY = editorTop + AXYNE_UI_EXPLORER_HEADER;
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(NSMakeRect(0, explorerY, [self sidebarWidth] - 1, MAX(0, bottomTop - explorerY)));
        if (_explorer.root == NULL) {
            [self drawLabel:@"폴더 열기…" at:NSMakePoint(16, explorerY + 3)
                size:12 color:text family:@"SF Pro Text"];
        } else {
            for (size_t i = (size_t)_explorerFirstRow; i < _explorer.count &&
                explorerY + AXYNE_UI_ROW <= bottomTop; ++i, explorerY += AXYNE_UI_ROW) {
                AxyneExplorerNode *node = &_explorer.nodes[i];
                BOOL selectedRow = _hasExplorerSelection && _explorerSelection == (NSInteger)i;
                if (selectedRow) {
                    [axyne_preference_color(reference ? 0x2f343c : _preferences.theme.border) setFill];
                    NSRectFill(NSMakeRect(0, explorerY, [self sidebarWidth], AXYNE_UI_ROW));
                }
                CGFloat x = 8 + node->depth * AXYNE_UI_INDENT;
                CGFloat nameX;
                if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
                    [self drawLabel:axyne_explorer_is_expanded(&_explorer, node->path) ? @"⌄" : @"›"
                        at:NSMakePoint(x, explorerY + 4) size:11 color:muted family:@"SF Pro Text"];
                    nameX = x + 16;
                } else {
                    [self drawFileBadge:node->name inRect:NSMakeRect(x, explorerY + 4, 20, 14) tab:NO];
                    nameX = x + 26;
                }
                NSString *name = [NSString stringWithUTF8String:node->name];
                [self drawLabel:name != nil ? name : @"(invalid name)"
                    at:NSMakePoint(nameX, explorerY + 3) size:12
                    color:selectedRow ? text : (axyne_explorer_is_dimmed(node) ? muted :
                        axyne_preference_color(light ? 0x24272d : 0xc4c8ce))
                    family:@"SF Pro Text"];
            }
        }
        [NSGraphicsContext restoreGraphicsState];
    }
    NSString *status = _lastExitFailed ? [NSString stringWithFormat:@"✗ 실행 실패 (%d)", _lastExitCode] :
        (_activeAction != 0 ? @"● 실행 중" : (_hasExitStatus ? @"✓ 실행 완료" : @"준비"));
    [self drawLabel:status at:NSMakePoint(12, statusTop + 5) size:11
        color:_lastExitFailed ? axyne_preference_color(0xe5a445) :
              (_hasExitStatus ? axyne_preference_color(0xa3c98a) : muted) family:@"SF Pro Text"];
    NSInteger caret = [self sendEditorMessage:SCI_GETCURRENTPOS wParam:0 lParam:0];
    NSInteger line = [self sendEditorMessage:SCI_LINEFROMPOSITION wParam:caret lParam:0] + 1;
    NSInteger column = [self sendEditorMessage:SCI_GETCOLUMN wParam:caret lParam:0] + 1;
    NSString *editing = [NSString stringWithFormat:@"줄 %ld, 열 %ld    %@: %u    UTF-8",
        (long)line, (long)column, _preferences.editor.insert_spaces ? @"공백" : @"탭",
        _preferences.editor.tab_width];
    [self drawLabel:editing at:NSMakePoint(MAX(180, width - 320), statusTop + 5)
        size:11 color:text family:@"SF Pro Text"];
    if (_editorView == nil)
        [self drawLabel:@"Required Scintilla framework failed to load"
            at:NSMakePoint([self sidebarWidth] + 24, editorTop + 24) size:12 color:muted family:@"Menlo"];
}
- (void)dealloc
{
    [self closePaletteRestoringFocus:NO];
    axyne_palette_ctl_destroy(&_palette);
    if (_gitRun != NULL) {
        AxyneMacGitRun *run = _gitRun;
        (void)pthread_mutex_lock(&run->lock);
        run->cancelled = 1;
        run->view = nil;
        (void)pthread_mutex_unlock(&run->lock);
        (void)axyne_process_terminate(run->process, NULL);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            axyne_macos_git_cleanup(run);
        });
        _gitRun = NULL;
        _gitProcess = NULL;
    }
    if (_lsp != NULL) {
        axyne_lsp_destroy(_lsp);
        _lsp = NULL;
    }
    if (_terminalProcess != NULL) {
        axyne_process_release(_terminalProcess);
        _terminalProcess = NULL;
    }
    axyne_runner_destroy(&_terminalRunner);
    axyne_runner_destroy(&_actionRunner);
    if (_watcher != NULL) {
        axyne_watcher_stop(_watcher);
        axyne_watcher_release(_watcher);
    }
    axyne_explorer_destroy(&_explorer);
    if (_menuTracking != nil) [self removeTrackingArea:_menuTracking];
    [_menuTracking release];
    [_recentMenu release];
    if (_editorView != nil) {
        for (size_t i = 0; i < _documents.count; ++i) {
            AxyneDocument *doc = &_documents.documents[i];
            if (doc->owns_native_editor_document)
                (void)[self sendEditorMessage:SCI_RELEASEDOCUMENT wParam:0
                    lParam:(intptr_t)doc->native_editor_document];
        }
    }
    /* Scintilla's active buffer also holds a reference to its Lexilla lexer.
     * Destroy it before unloading Lexilla, including when AppKit retains the
     * editor subview until the superclass tears down its children. */
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [(id)_editorView setDelegate:nil];
    (void)[self sendEditorMessage:SCI_SETILEXER wParam:0 lParam:0];
    axyne_documents_destroy(&_documents);
    [_editorView removeFromSuperview];
    [_editorView release];
    _editorView = nil;
    [_newButton release]; [_openButton release]; [_saveButton release];
    [_undoButton release]; [_redoButton release];
    [_buildButton release]; [_runButton release];
    [_targetButton release]; [_searchButton release];
    [_outputTab release]; [_problemsTab release]; [_terminalTab release];
    [_clearOutput release]; [_problemSummary release];
    [_terminalScroll release];
    [_terminalOutput release];
    [_terminalInput release];
    [_terminalStart release];
    [_terminalStop release];
    [_terminalSend release];
    [_debugStart release];
    [_debugPause release];
    [_debugContinue release];
    [_debugNext release];
    [_debugBreakpoint release];
    axyne_debugger_destroy(&_debugger);
    [_lspStatus release];
    if (_lexillaModule != NULL) dlclose(_lexillaModule);
    [_scintillaBundle unload];
    [_scintillaBundle release];
    [_editorLoadError release];
    free(_globalPreferencesPath);
    free(_workspacePreferencesPath);
    [super dealloc];
}

@end

/* ---- command palette controller glue ---------------------------------------
 * The shared AxynePaletteController owns mode, rows, selection and actions.
 * AxyneWorkspaceView adds a real NSTextField over the toolbar search field
 * (first responder while the palette is open) and an AxynePaletteOverlay. */

static int axyne_macos_palette_document(void *user, char **path, char **text,
                                        size_t *length, size_t *lineCount)
{
    AxyneWorkspaceView *view = (AxyneWorkspaceView *)user;
    AxyneDocument *document = view != nil ? [view activeDocument] : NULL;
    NSInteger size, lines;
    char *buffer;
    if (document == NULL) return 0;
    size = [view sendEditorMessage:SCI_GETTEXTLENGTH wParam:0 lParam:0];
    lines = [view sendEditorMessage:SCI_GETLINECOUNT wParam:0 lParam:0];
    if (size < 0) return 0;
    buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) return 0;
    buffer[0] = '\0';
    (void)[view sendEditorMessage:SCI_GETTEXT wParam:(uintptr_t)size + 1
                           lParam:(intptr_t)buffer];
    buffer[size] = '\0';
    *path = (document->is_untitled || document->path == NULL) ? NULL : strdup(document->path);
    *text = buffer;
    *length = (size_t)size;
    *lineCount = lines > 0 ? (size_t)lines : 1;
    return 1;
}

@implementation AxyneWorkspaceView (AxynePalette)

- (void)openPaletteWithInput:(NSString *)input
{
    NSString *initial = input != nil ? input : @"";
    if (_palette.active) {
        if (axyne_palette_parse_mode([initial UTF8String], NULL) != _palette.mode)
            [self paletteSetText:initial];
        [self paletteFocusField];
        return;
    }
    const char **paths = (const char **)calloc(_documents.count + 1, sizeof(*paths));
    size_t count = 0;
    if (paths == NULL) return;
    for (size_t i = 0; i < _documents.count; ++i) {
        const AxyneDocument *document = &_documents.documents[i];
        if (!document->is_untitled && document->path != NULL) paths[count++] = document->path;
    }
    AxyneStatus status = axyne_palette_ctl_open(&_palette, _explorer.root,
        (const char *const *)paths, count, [initial UTF8String]);
    free(paths);
    if (status != AXYNE_STATUS_OK) return;

    _paletteOverlay = [[AxynePaletteOverlay alloc] initWithFrame:[self bounds]];
    [_paletteOverlay setController:&_palette];
    [_paletteOverlay setOwner:self];
    [self addSubview:_paletteOverlay];

    _paletteField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [_paletteField setBordered:NO];
    [_paletteField setBezeled:NO];
    [_paletteField setDrawsBackground:NO];
    [_paletteField setFocusRingType:NSFocusRingTypeNone];
    [_paletteField setFont:[NSFont systemFontOfSize:12]];
    [_paletteField setTextColor:[NSColor whiteColor]];
    [[_paletteField cell] setUsesSingleLineMode:YES];
    [[_paletteField cell] setScrollable:YES];
    [[_paletteField cell] setWraps:NO];
    [_paletteField setAccessibilityLabel:@"파일, 명령, 기호 검색"];
    [_paletteField setStringValue:initial];
    [_paletteField setDelegate:self];
    [self addSubview:_paletteField positioned:NSWindowAbove relativeTo:_paletteOverlay];
    [_searchButton setHidden:YES];
    [self layoutPalette];
    [self paletteFocusField];
    {
        NSText *editor = [_paletteField currentEditor];
        if (editor != nil) [editor setSelectedRange:NSMakeRange([initial length], 0)];
    }
    if (axyne_palette_ctl_walk_running(&_palette))
        _paletteTimer = [NSTimer scheduledTimerWithTimeInterval:0.015 target:self
            selector:@selector(paletteTick:) userInfo:nil repeats:YES];
    [self setNeedsDisplay:YES];
}

- (void)closePaletteRestoringFocus:(BOOL)restore
{
    if (!_palette.active) return;
    [_paletteTimer invalidate];
    _paletteTimer = nil;
    /* inactive first: the field's end-editing callback then does nothing */
    axyne_palette_ctl_close(&_palette);
    [_paletteField setDelegate:nil];
    [_paletteField removeFromSuperview];
    [_paletteField autorelease];
    _paletteField = nil;
    [_paletteOverlay setController:NULL];
    [_paletteOverlay setOwner:nil];
    [_paletteOverlay removeFromSuperview];
    [_paletteOverlay autorelease];
    _paletteOverlay = nil;
    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];
    if (restore && _editorView != nil) [[self window] makeFirstResponder:_editorView];
}

- (void)layoutPalette
{
    if (!_palette.active) return;
    NSRect box = [_searchButton frame];
    [_paletteOverlay setFrame:[self bounds]];
    [_paletteOverlay setNeedsDisplay:YES];
    [_searchButton setHidden:YES];
    [_paletteField setFrame:NSMakeRect(NSMinX(box) + 32, NSMinY(box) + 4,
        MAX(0, NSWidth(box) - 32 - 40), 18)];
}

/* Toolbar field while open: accent border, dark fill, search glyph and the
 * "Esc" hint; the NSTextField supplies the text. */
- (void)drawPaletteFieldInRect:(NSRect)box
{
    if (NSWidth(box) < 60) return;
    NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(box, 0.5, 0.5)
                                                          xRadius:4 yRadius:4];
    [axyne_palette_rgb(0x131417, 1) setFill];
    [shape fill];
    [axyne_palette_rgb(0xa66bf0, 1) setStroke];
    [shape setLineWidth:1];
    [shape stroke];
    NSBezierPath *glyph = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(NSMinX(box) + 11, NSMinY(box) + 7.5, 8, 8)];
    [glyph moveToPoint:NSMakePoint(NSMinX(box) + 17.5, NSMinY(box) + 14)];
    [glyph lineToPoint:NSMakePoint(NSMinX(box) + 21, NSMinY(box) + 17.5)];
    [glyph setLineWidth:1.2];
    [axyne_palette_rgb(0x8b919b, 1) setStroke];
    [glyph stroke];
    axyne_palette_draw_text(@"Esc",
        [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular],
        axyne_palette_rgb(0x8b919b, 1),
        NSMakeRect(NSMaxX(box) - 11 - 24, NSMinY(box), 24, NSHeight(box)),
        NSTextAlignmentRight);
}

- (void)paletteSyncInput
{
    if (!_palette.active) return;
    const char *text = [[_paletteField stringValue] UTF8String];
    (void)axyne_palette_ctl_set_input(&_palette, text != NULL ? text : "");
    [_paletteOverlay setNeedsDisplay:YES];
}

- (void)paletteSetText:(NSString *)text
{
    if (!_palette.active) return;
    [_paletteField setStringValue:text];
    NSText *editor = [_paletteField currentEditor];
    if (editor != nil) [editor setSelectedRange:NSMakeRange([text length], 0)];
    [self paletteSyncInput];
}

- (void)paletteTick:(NSTimer *)timer
{
    int changed = 0;
    if (!_palette.active) { [timer invalidate]; _paletteTimer = nil; return; }
    if (!axyne_palette_ctl_walk_step(&_palette, 1500, &changed)) {
        [timer invalidate];
        _paletteTimer = nil;
    }
    if (changed) [_paletteOverlay setNeedsDisplay:YES];
}

- (void)paletteEnter
{
    [self paletteActivateRow:(size_t)-1];
}

- (void)paletteDismiss
{
    [self closePaletteRestoringFocus:YES];
}

- (void)paletteChipClicked:(NSInteger)mode
{
    if (mode < 0 || mode > (NSInteger)AXYNE_PALETTE_MODE_LINE) return;
    [self paletteSetText:[NSString stringWithUTF8String:
        axyne_palette_mode_prefix((AxynePaletteMode)mode)]];
    [self paletteFocusField];
}

- (BOOL)paletteClaimsPoint:(NSPoint)point
{
    if (!NSPointInRect(point, [_searchButton frame])) return NO;
    [self paletteFocusField];
    return YES;
}

/* The field editor's caret follows the control text color, which is dark in a
 * light appearance; the toolbar field is always dark, so force a white caret. */
- (void)paletteFocusField
{
    [[self window] makeFirstResponder:_paletteField];
    NSText *editor = [_paletteField currentEditor];
    if ([editor isKindOfClass:[NSTextView class]])
        [(NSTextView *)editor setInsertionPointColor:[NSColor whiteColor]];
}

/* NSTextFieldDelegate */
- (void)controlTextDidChange:(NSNotification *)notification
{
    if ([notification object] == _paletteField) [self paletteSyncInput];
}

- (void)controlTextDidEndEditing:(NSNotification *)notification
{
    /* focus left the field (click elsewhere, window deactivated) */
    if (_palette.active && [notification object] == _paletteField)
        [self performSelector:@selector(paletteDismissWithoutFocus) withObject:nil afterDelay:0];
}

- (void)paletteDismissWithoutFocus
{
    [self closePaletteRestoringFocus:NO];
}

- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView
    doCommandBySelector:(SEL)selector
{
    (void)textView;
    if (control != _paletteField || !_palette.active) return NO;
    if (selector == @selector(moveUp:) || selector == @selector(moveDown:)) {
        axyne_palette_ctl_move(&_palette, selector == @selector(moveUp:) ? -1 : 1);
        [_paletteOverlay setNeedsDisplay:YES];
        return YES;
    }
    if (selector == @selector(pageUp:) || selector == @selector(pageDown:) ||
        selector == @selector(scrollPageUp:) || selector == @selector(scrollPageDown:)) {
        size_t step = AXYNE_PALETTE_VISIBLE_ROWS;
        BOOL up = selector == @selector(pageUp:) || selector == @selector(scrollPageUp:);
        axyne_palette_ctl_select(&_palette, up
            ? (_palette.selection > step ? _palette.selection - step : 0)
            : _palette.selection + step);
        [_paletteOverlay setNeedsDisplay:YES];
        return YES;
    }
    if (selector == @selector(insertNewline:)) {
        [self performSelector:@selector(paletteEnter) withObject:nil afterDelay:0];
        return YES;
    }
    if (selector == @selector(cancelOperation:) || selector == @selector(complete:)) {
        [self performSelector:@selector(paletteDismiss) withObject:nil afterDelay:0];
        return YES;
    }
    if (selector == @selector(insertTab:) || selector == @selector(insertBacktab:)) return YES;
    return NO;
}

/* Enter or a click: closes the palette and runs what the controller returns. */
- (void)paletteActivateRow:(size_t)row
{
    AxynePaletteAction action;
    if (!_palette.active) return;
    if (!axyne_palette_ctl_activate(&_palette, row, &action)) return;
    if (action.kind == AXYNE_PALETTE_ACTION_SET_INPUT) {
        [self paletteSetText:[NSString stringWithUTF8String:action.text]];
        axyne_palette_action_destroy(&action);
        return;
    }
    [self closePaletteRestoringFocus:YES];
    if (action.kind == AXYNE_PALETTE_ACTION_OPEN_FILE && action.path != NULL) {
        NSString *path = [NSString stringWithUTF8String:action.path];
        if (path != nil) [self openPath:path];
    } else if (action.kind == AXYNE_PALETTE_ACTION_GOTO) {
        [self paletteGotoLine:action.line column:action.column];
    } else if (action.kind == AXYNE_PALETTE_ACTION_COMMAND) {
        [self paletteRunCommand:action.command];
    }
    axyne_palette_action_destroy(&action);
}

- (void)paletteGotoLine:(size_t)line column:(size_t)column
{
    if (_editorView == nil) return;
    NSInteger count = [self sendEditorMessage:SCI_GETLINECOUNT wParam:0 lParam:0];
    line = axyne_palette_clamp_line(line, count > 0 ? (size_t)count : 1);
    NSInteger start = [self sendEditorMessage:SCI_POSITIONFROMLINE wParam:line - 1 lParam:0];
    NSInteger end = [self sendEditorMessage:SCI_GETLINEENDPOSITION wParam:line - 1 lParam:0];
    column = axyne_palette_clamp_column(column, end > start ? (size_t)(end - start) : 0);
    (void)[self sendEditorMessage:SCI_GOTOPOS wParam:(uintptr_t)start + column - 1 lParam:0];
    (void)[self sendEditorMessage:SCI_SCROLLCARET wParam:0 lParam:0];
    [[self window] makeFirstResponder:_editorView];
}

- (void)paletteRunCommand:(AxynePaletteCommandId)command
{
    AxyneDocument *document = [self activeDocument];
    BOOL gitReady = _explorer.root != NULL && _gitProcess == NULL;
    switch (command) {
    case AXYNE_PALETTE_COMMAND_NEW_FILE: [self newDocument:nil]; break;
    case AXYNE_PALETTE_COMMAND_OPEN_FILE: [self openDocument:nil]; break;
    case AXYNE_PALETTE_COMMAND_OPEN_FOLDER: [self openWorkspace:nil]; break;
    case AXYNE_PALETTE_COMMAND_SAVE: if (document != NULL) [self saveDocument:nil]; break;
    case AXYNE_PALETTE_COMMAND_SAVE_AS: if (document != NULL) [self saveDocumentAs:nil]; break;
    case AXYNE_PALETTE_COMMAND_CLOSE_TAB: if (document != NULL) [self closeDocument:nil]; break;
    case AXYNE_PALETTE_COMMAND_FIND: [self findOrReplace:NO]; break;
    case AXYNE_PALETTE_COMMAND_REPLACE: [self findOrReplace:YES]; break;
    case AXYNE_PALETTE_COMMAND_BUILD: if ([_buildButton isEnabled]) [self buildDocument:nil]; break;
    case AXYNE_PALETTE_COMMAND_RUN: if ([_runButton isEnabled]) [self runDocument:nil]; break;
    case AXYNE_PALETTE_COMMAND_START_DEBUGGING: [self startDebugger:nil]; break;
    case AXYNE_PALETTE_COMMAND_GIT_STATUS:
    case AXYNE_PALETTE_COMMAND_GIT_DIFF:
    case AXYNE_PALETTE_COMMAND_GIT_STAGE_ALL:
    case AXYNE_PALETTE_COMMAND_GIT_UNSTAGE_ALL:
        if (!gitReady) {
            [self showWorkspaceMessage:_explorer.root == NULL
                ? @"Open a workspace folder before using Git commands."
                : @"A Git command is already running."];
        } else if (command == AXYNE_PALETTE_COMMAND_GIT_STATUS) [self showGitStatus:nil];
        else if (command == AXYNE_PALETTE_COMMAND_GIT_DIFF) [self showGitDiff:nil];
        else if (command == AXYNE_PALETTE_COMMAND_GIT_STAGE_ALL) [self stageAllGitChanges:nil];
        else [self unstageAllGitChanges:nil];
        break;
    case AXYNE_PALETTE_COMMAND_PREFERENCES: [self showGlobalPreferences:nil]; break;
    case AXYNE_PALETTE_COMMAND_WORKSPACE_SETTINGS:
        if (_workspacePreferencesPath != NULL) [self showWorkspacePreferences:nil];
        else [self showWorkspaceMessage:@"Open a workspace folder before editing workspace settings."];
        break;
    case AXYNE_PALETTE_COMMAND_PANEL_OUTPUT:
    case AXYNE_PALETTE_COMMAND_PANEL_PROBLEMS:
    case AXYNE_PALETTE_COMMAND_PANEL_TERMINAL:
        _panelMode = command == AXYNE_PALETTE_COMMAND_PANEL_OUTPUT ? 0 :
            (command == AXYNE_PALETTE_COMMAND_PANEL_PROBLEMS ? 1 : 2);
        [_problemSummary setStringValue:_lspStatus != nil ? _lspStatus : @"LSP 진단 없음"];
        [self setNeedsLayout:YES];
        [self setNeedsDisplay:YES];
        break;
    case AXYNE_PALETTE_COMMAND_CLEAR_OUTPUT: [self clearOutput:nil]; break;
    case AXYNE_PALETTE_COMMAND_NONE:
    case AXYNE_PALETTE_COMMAND_QUICK_FILE:
    case AXYNE_PALETTE_COMMAND_GO_TO_LINE:
    case AXYNE_PALETTE_COMMAND_GO_TO_SYMBOL:
        break;
    }
}

@end

@interface AxyneApplicationDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    NSWindow *_window;
    NSString *_appName;
    BOOL _terminationConfirmed;
}
- (instancetype)initWithAppName:(NSString *)appName;
- (BOOL)windowShouldClose:(NSWindow *)sender;
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender;
@end

@implementation AxyneApplicationDelegate

- (instancetype)initWithAppName:(NSString *)appName
{
    self = [super init];
    if (self != nil) {
        _appName = [appName copy];
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    NSRect frame = NSMakeRect(0, 0, 1440, 900);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:_appName != nil ? _appName : @"Axyne"];
    [_window setTitlebarAppearsTransparent:YES];
    [_window setTitleVisibility:NSWindowTitleVisible];
    [_window setMinSize:NSMakeSize(800, 560)];
    AxyneWorkspaceView *workspace = [[[AxyneWorkspaceView alloc]
        initWithFrame:frame] autorelease];
    [_window setContentView:workspace];
    [_window setDelegate:self];
    axyne_install_menu([NSApplication sharedApplication], workspace);
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    /* Without explicit activation a binary started from a terminal or a
     * fresh build keeps another app's menu bar and ignores menu clicks. */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    if (_terminationConfirmed) return YES;
    _terminationConfirmed = [(AxyneWorkspaceView *)[sender contentView]
        confirmCloseAll];
    return _terminationConfirmed;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    if (_terminationConfirmed) return NSTerminateNow;
    if (_window == nil || ![_window isVisible]) return NSTerminateNow;
    _terminationConfirmed = [(AxyneWorkspaceView *)[_window contentView]
        confirmCloseAll];
    return _terminationConfirmed ? NSTerminateNow : NSTerminateCancel;
}

- (void)dealloc
{
    [_window release];
    [_appName release];
    [super dealloc];
}

@end

/* Function keys must be given to AppKit as the private-use Unicode
 * characters (NSF5FunctionKey, ...) with an empty modifier mask. */
static void axyne_set_function_key(NSMenuItem *item, unichar key,
                                   NSEventModifierFlags modifiers)
{
    [item setKeyEquivalent:[NSString stringWithCharacters:&key length:1]];
    [item setKeyEquivalentModifierMask:modifiers];
}

static NSString *axyne_macos_special_key(unichar key)
{
    return [NSString stringWithCharacters:&key length:1];
}

/* Adds a menu item bound to the workspace view with an explicit modifier
 * mask (lower-case key equivalents, so Shift is never implied). */
static NSMenuItem *axyne_macos_add_item(NSMenu *menu, NSString *title, SEL action,
                                        id target, NSString *key,
                                        NSEventModifierFlags modifiers)
{
    NSMenuItem *item = [menu addItemWithTitle:title action:action keyEquivalent:key];
    [item setKeyEquivalentModifierMask:modifiers];
    [item setTarget:target];
    return item;
}

static void axyne_install_menu(NSApplication *application,
                               AxyneWorkspaceView *workspace)
{
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"Axyne"
        action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Axyne"];
    NSMenuItem *aboutItem = [appMenu addItemWithTitle:@"Axyne 정보"
        action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [aboutItem setTarget:application];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *preferencesItem = [appMenu addItemWithTitle:@"환경설정…"
        action:@selector(showGlobalPreferences:) keyEquivalent:@","];
    [preferencesItem setTarget:workspace];
    [appMenu addItemWithTitle:@"Axyne 종료" action:@selector(terminate:)
                 keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];
    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:axyne_macos_menu_label(0)
        action:nil keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:axyne_macos_menu_label(0)];
    NSMenuItem *newItem = [fileMenu addItemWithTitle:@"새 파일"
        action:@selector(newDocument:) keyEquivalent:@"n"];
    [newItem setTarget:workspace];
    NSMenuItem *openItem = [fileMenu addItemWithTitle:@"열기…"
        action:@selector(openDocument:) keyEquivalent:@"o"];
    [openItem setTarget:workspace];
    NSMenuItem *saveItem = [fileMenu addItemWithTitle:@"저장"
        action:@selector(saveDocument:) keyEquivalent:@"s"];
    [saveItem setTarget:workspace];
    NSMenuItem *saveAsItem = [fileMenu addItemWithTitle:@"다른 이름으로 저장…"
        action:@selector(saveDocumentAs:) keyEquivalent:@"S"];
    [saveAsItem setTarget:workspace];
    NSMenuItem *closeItem = [fileMenu addItemWithTitle:@"닫기"
        action:@selector(closeDocument:) keyEquivalent:@"w"];
    [closeItem setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *workspaceItem = [fileMenu addItemWithTitle:@"폴더 열기…"
        action:@selector(openWorkspace:) keyEquivalent:@""];
    [workspaceItem setTarget:workspace];
    NSMenuItem *workspacePreferences = [fileMenu addItemWithTitle:@"작업 영역 설정…"
        action:@selector(showWorkspacePreferences:) keyEquivalent:@""];
    [workspacePreferences setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *gitStatus = [fileMenu addItemWithTitle:@"Git 상태"
        action:@selector(showGitStatus:) keyEquivalent:@""];
    NSMenuItem *gitDiff = [fileMenu addItemWithTitle:@"Git 변경 사항"
        action:@selector(showGitDiff:) keyEquivalent:@""];
    NSMenuItem *gitStage = [fileMenu addItemWithTitle:@"모두 스테이지"
        action:@selector(stageAllGitChanges:) keyEquivalent:@""];
    NSMenuItem *gitUnstage = [fileMenu addItemWithTitle:@"모두 스테이지 해제"
        action:@selector(unstageAllGitChanges:) keyEquivalent:@""];
    [gitStatus setTarget:workspace]; [gitDiff setTarget:workspace];
    [gitStage setTarget:workspace]; [gitUnstage setTarget:workspace];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *recentItem = [[NSMenuItem alloc] initWithTitle:@"최근 항목"
        action:nil keyEquivalent:@""];
    NSMenu *recentMenu = [[NSMenu alloc] initWithTitle:@"최근 항목"];
    [recentItem setSubmenu:recentMenu];
    [fileMenu addItem:recentItem];
    [workspace setRecentMenu:recentMenu];
    [fileItem setSubmenu:fileMenu];
    [mainMenu addItem:fileItem];
    [fileItem release]; [recentItem release];
    [recentMenu release]; [fileMenu release];
    /* Bar order after File: 편집, 보기, 빌드, 디버그, 도구, 도움말. */
    for (NSUInteger menuIndex = 1; menuIndex < AXYNE_UI_MENU_COUNT; ++menuIndex) {
        NSString *title = axyne_macos_menu_label(menuIndex);
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
            action:nil keyEquivalent:@""];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:title];
        if (menuIndex == 1) {
            NSMenuItem *undo = [submenu addItemWithTitle:@"실행 취소"
                action:@selector(undo:) keyEquivalent:@"z"];
            NSMenuItem *redo = [submenu addItemWithTitle:@"다시 실행"
                action:@selector(redo:) keyEquivalent:@"Z"];
            [redo setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                          NSEventModifierFlagShift];
            [submenu addItem:[NSMenuItem separatorItem]];
            NSMenuItem *cut = [submenu addItemWithTitle:@"잘라내기"
                action:@selector(editCut:) keyEquivalent:@"x"];
            NSMenuItem *copy = [submenu addItemWithTitle:@"복사"
                action:@selector(editCopy:) keyEquivalent:@"c"];
            NSMenuItem *paste = [submenu addItemWithTitle:@"붙여넣기"
                action:@selector(editPaste:) keyEquivalent:@"v"];
            NSMenuItem *selectAll = [submenu addItemWithTitle:@"모두 선택"
                action:@selector(editSelectAll:) keyEquivalent:@"a"];
            [submenu addItem:[NSMenuItem separatorItem]];
            NSMenuItem *find = [submenu addItemWithTitle:@"찾기…"
                action:@selector(findInDocument:) keyEquivalent:@"f"];
            NSMenuItem *replace = [submenu addItemWithTitle:@"바꾸기…"
                action:@selector(replaceInDocument:) keyEquivalent:@"h"];
            NSMenuItem *findInWorkspace = [submenu addItemWithTitle:@"파일에서 찾기…"
                action:@selector(searchWorkspace:) keyEquivalent:@""];
            NSMenuItem *quickOpen = [submenu addItemWithTitle:@"파일 이동…"
                action:@selector(quickFile:) keyEquivalent:@""];
            NSArray *editItems = @[undo, redo, cut, copy, paste, selectAll,
                find, replace, findInWorkspace, quickOpen];
            for (NSMenuItem *editItem in editItems) [editItem setTarget:workspace];
            axyne_macos_add_item(submenu, @"줄로 이동…", @selector(goToLine:),
                workspace, @"l", NSEventModifierFlagCommand);
            axyne_macos_add_item(submenu, @"줄 선택", @selector(selectLine:),
                workspace, @"", 0);
            [submenu addItem:[NSMenuItem separatorItem]];
            axyne_macos_add_item(submenu, @"줄 주석 토글",
                @selector(toggleLineComment:), workspace, @"/",
                NSEventModifierFlagCommand);
            axyne_macos_add_item(submenu, @"줄 복제", @selector(duplicateLine:),
                workspace, @"d", NSEventModifierFlagCommand | NSEventModifierFlagShift);
            axyne_macos_add_item(submenu, @"줄 위로 이동", @selector(moveLineUp:),
                workspace, axyne_macos_special_key(NSUpArrowFunctionKey),
                NSEventModifierFlagOption);
            axyne_macos_add_item(submenu, @"줄 아래로 이동", @selector(moveLineDown:),
                workspace, axyne_macos_special_key(NSDownArrowFunctionKey),
                NSEventModifierFlagOption);
            [submenu addItem:[NSMenuItem separatorItem]];
            axyne_macos_add_item(submenu, @"들여쓰기", @selector(indentSelection:),
                workspace, @"]", NSEventModifierFlagCommand);
            axyne_macos_add_item(submenu, @"내어쓰기", @selector(outdentSelection:),
                workspace, @"[", NSEventModifierFlagCommand);
        } else if (menuIndex == 3) {
            NSMenuItem *build = [submenu addItemWithTitle:@"빌드"
                action:@selector(buildDocument:) keyEquivalent:@"b"];
            NSMenuItem *run = [submenu addItemWithTitle:@"실행"
                action:@selector(runDocument:) keyEquivalent:@"r"];
            NSMenuItem *configure = [submenu addItemWithTitle:@"실행 구성…"
                action:@selector(configureRunnerAction:) keyEquivalent:@""];
            [build setTarget:workspace]; [run setTarget:workspace];
            [configure setTarget:workspace];
            [submenu addItem:[NSMenuItem separatorItem]];
            axyne_macos_add_item(submenu, @"빌드 취소", @selector(cancelBuild:),
                workspace, @".", NSEventModifierFlagCommand);
        } else if (menuIndex == 4) {
            NSMenuItem *start = [submenu addItemWithTitle:@"디버깅 시작"
                action:@selector(startDebugger:) keyEquivalent:@""];
            /* F5 is the default Run binding in preferences, so Start Debugger
             * takes Shift+F5 to keep both shortcuts reachable. */
            axyne_set_function_key(start, NSF5FunctionKey, NSEventModifierFlagShift);
            [start setTarget:workspace];
            axyne_macos_add_item(submenu, @"중지", @selector(stopDebugger:),
                workspace, @".", NSEventModifierFlagCommand | NSEventModifierFlagShift);
            [submenu addItem:[NSMenuItem separatorItem]];
            NSMenuItem *pause = [submenu addItemWithTitle:@"일시 중지"
                action:@selector(debugCommand:) keyEquivalent:@""];
            axyne_set_function_key(pause, NSF6FunctionKey, 0);
            NSMenuItem *resume = [submenu addItemWithTitle:@"계속"
                action:@selector(debugCommand:) keyEquivalent:@""];
            [resume setTag:AXYNE_DEBUGGER_CONTINUE]; [resume setTarget:workspace];
            [pause setTarget:workspace]; [pause setTag:AXYNE_DEBUGGER_PAUSE];
            [submenu addItem:[NSMenuItem separatorItem]];
            /* debugCommand: carries AxyneDebuggerCommand in the item tag. */
            NSMenuItem *next = [submenu addItemWithTitle:@"프로시저 단위 실행"
                action:@selector(debugCommand:) keyEquivalent:@""];
            axyne_set_function_key(next, NSF10FunctionKey, 0);
            [next setTarget:workspace]; [next setTag:AXYNE_DEBUGGER_STEP_OVER];
            NSMenuItem *into = [submenu addItemWithTitle:@"한 단계씩 코드 실행"
                action:@selector(debugCommand:) keyEquivalent:@""];
            axyne_set_function_key(into, NSF11FunctionKey, 0);
            [into setTarget:workspace]; [into setTag:AXYNE_DEBUGGER_STEP_INTO];
            NSMenuItem *out = [submenu addItemWithTitle:@"프로시저 나가기"
                action:@selector(debugCommand:) keyEquivalent:@""];
            axyne_set_function_key(out, NSF11FunctionKey, NSEventModifierFlagShift);
            [out setTarget:workspace]; [out setTag:AXYNE_DEBUGGER_STEP_OUT];
            [submenu addItem:[NSMenuItem separatorItem]];
            NSMenuItem *toggle = [submenu addItemWithTitle:@"중단점 토글"
                action:@selector(toggleBreakpoint:) keyEquivalent:@""];
            axyne_set_function_key(toggle, NSF9FunctionKey, 0);
            [toggle setTarget:workspace];
            NSMenuItem *clearBreakpoints = [submenu addItemWithTitle:@"모든 중단점 삭제"
                action:@selector(clearBreakpoints:) keyEquivalent:@""];
            axyne_set_function_key(clearBreakpoints, NSF9FunctionKey,
                NSEventModifierFlagCommand | NSEventModifierFlagShift);
            [clearBreakpoints setTarget:workspace];
        } else if (menuIndex == 2) {
            axyne_macos_add_item(submenu, @"탐색기", @selector(toggleExplorer:),
                workspace, @"e", NSEventModifierFlagCommand | NSEventModifierFlagShift);
            axyne_macos_add_item(submenu, @"하단 패널", @selector(togglePanel:),
                workspace, @"j", NSEventModifierFlagCommand);
            [submenu addItem:[NSMenuItem separatorItem]];
            NSArray *panels = @[@"출력", @"문제", @"터미널"];
            NSArray *panelKeys = @[@"u", @"m", @"`"];
            NSEventModifierFlags panelMasks[3] = {
                NSEventModifierFlagCommand | NSEventModifierFlagShift,
                NSEventModifierFlagCommand | NSEventModifierFlagShift,
                NSEventModifierFlagControl };
            for (NSInteger i = 0; i < (NSInteger)[panels count]; ++i) {
                NSMenuItem *panelItem = axyne_macos_add_item(submenu,
                    panels[(NSUInteger)i], @selector(selectPanel:), workspace,
                    panelKeys[(NSUInteger)i], panelMasks[i]);
                [panelItem setTag:i];
            }
            [submenu addItem:[NSMenuItem separatorItem]];
            axyne_macos_add_item(submenu, @"확대", @selector(zoomInEditor:),
                workspace, @"=", NSEventModifierFlagCommand);
            axyne_macos_add_item(submenu, @"축소", @selector(zoomOutEditor:),
                workspace, @"-", NSEventModifierFlagCommand);
            axyne_macos_add_item(submenu, @"기본 크기", @selector(zoomResetEditor:),
                workspace, @"0", NSEventModifierFlagCommand);
            [submenu addItem:[NSMenuItem separatorItem]];
            /* Option+Z types a character, so Word Wrap takes Command+Option. */
            axyne_macos_add_item(submenu, @"자동 줄 바꿈", @selector(toggleWordWrap:),
                workspace, @"z", NSEventModifierFlagCommand | NSEventModifierFlagOption);
        } else if (menuIndex == 5) {
            NSMenuItem *definition = [submenu addItemWithTitle:@"정의로 이동"
                action:@selector(navigateLspReferences:) keyEquivalent:@"d"];
            NSMenuItem *references = [submenu addItemWithTitle:@"참조 찾기"
                action:@selector(navigateLspReferences:) keyEquivalent:@"r"];
            [definition setTarget:workspace];
            /* Cmd+Option+D is the system Dock hide/show shortcut, so Go to Definition adds Shift. */
            [definition setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagShift];
            [references setTarget:workspace];
            /* Cmd+R belongs to Build > Run, so References adds Shift. */
            [references setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagShift];
            [references setTag:1];
            [submenu addItem:[NSMenuItem separatorItem]];
            axyne_macos_add_item(submenu, @"preferences.json 열기",
                @selector(openPreferencesFile:), workspace, @"", 0);
        } else if (menuIndex == 6) {
            NSMenuItem *help = [submenu addItemWithTitle:@"Axyne 도움말"
                action:@selector(openHelp:) keyEquivalent:@"?"];
            NSMenuItem *folder = [submenu addItemWithTitle:@"설정 폴더 표시"
                action:@selector(showSettingsFolder:) keyEquivalent:@""];
            [help setTarget:workspace]; [folder setTarget:workspace];
            axyne_macos_add_item(submenu, @"키보드 단축키 참조",
                @selector(showKeyboardShortcuts:), workspace, @"", 0);
            axyne_macos_add_item(submenu, @"문제 보고…", @selector(reportIssue:),
                workspace, @"", 0);
            [application setHelpMenu:submenu];
        }
        [item setSubmenu:submenu];
        [submenu release];
        [mainMenu addItem:item];
        [item release];
    }
    for (NSUInteger styleIndex = 1; styleIndex < (NSUInteger)[mainMenu numberOfItems]; ++styleIndex)
        axyne_macos_style_menu([[mainMenu itemAtIndex:(NSInteger)styleIndex] submenu]);
    [application setMainMenu:mainMenu];
    [mainMenu release];
    [appMenu release];
    [appItem release];
}

int axyne_ui_run(const char *app_name)
{
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSString *title = app_name != NULL
            ? [NSString stringWithUTF8String:app_name] : @"Axyne";
        AxyneApplicationDelegate *delegate =
            [[AxyneApplicationDelegate alloc] initWithAppName:title];
        [application setDelegate:delegate];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [application activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
        [application run];
        [application setDelegate:nil];
        [delegate release];
    }
    return 0;
}
