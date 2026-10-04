#import "popup_menu_macos.h"

#include <math.h>
#include <stdlib.h>

#include "axyne/popup_menu_layout.h"

static NSColor *axyne_popup_color(uint32_t rgb)
{
    return [NSColor colorWithSRGBRed:(CGFloat)((rgb >> 16) & 0xff) / 255.0
                               green:(CGFloat)((rgb >> 8) & 0xff) / 255.0
                                blue:(CGFloat)(rgb & 0xff) / 255.0
                               alpha:1.0];
}

static unsigned axyne_popup_modifiers(NSEventModifierFlags flags)
{
    unsigned mods = 0;
    if ((flags & NSEventModifierFlagControl) != 0) mods |= AXYNE_POPUP_MOD_CONTROL;
    if ((flags & NSEventModifierFlagOption) != 0) mods |= AXYNE_POPUP_MOD_OPTION;
    if ((flags & NSEventModifierFlagShift) != 0) mods |= AXYNE_POPUP_MOD_SHIFT;
    if ((flags & NSEventModifierFlagCommand) != 0) mods |= AXYNE_POPUP_MOD_COMMAND;
    return mods;
}

/* Whether a row can be highlighted and chosen. A submenu parent counts as
 * enabled when the menu auto-enables items, as AppKit does. */
static BOOL axyne_popup_item_enabled(NSMenuItem *item)
{
    if ([item isSeparatorItem]) return NO;
    if ([item isEnabled]) return YES;
    return [item submenu] != nil && [item action] == NULL &&
        [[item menu] autoenablesItems];
}

/* ------------------------------------------------------------------ view */

@interface AxynePopupMenuView : NSView {
    NSMutableArray *_items;      /* visible NSMenuItem rows */
    NSMutableArray *_accels;     /* NSString or NSNull per row */
    uint8_t *_kinds;
    uint8_t *_selectable;
    int *_tops;
    int *_heights;
    NSInteger _count;
    NSInteger _selected;
    NSSize _size;
    NSColor *_background, *_border, *_hover, *_hoverText, *_text, *_muted,
            *_disabled, *_separator, *_check;
    NSFont *_font, *_accelFont, *_checkFont, *_arrowFont;
}
- (instancetype)initWithMenu:(NSMenu *)menu colors:(const AxynePopupMenuColors *)colors;
- (NSSize)contentSize;
- (NSInteger)rowCount;
- (NSMenuItem *)itemAtRow:(NSInteger)row;
- (BOOL)rowSelectable:(NSInteger)row;
- (NSInteger)rowAtViewPoint:(NSPoint)point;
- (int)rowTop:(NSInteger)row;
- (NSInteger)selectedIndex;
- (void)setSelectedIndex:(NSInteger)index;
- (void)moveSelection:(int)request;
@end

@implementation AxynePopupMenuView

- (instancetype)initWithMenu:(NSMenu *)menu colors:(const AxynePopupMenuColors *)colors
{
    self = [super initWithFrame:NSZeroRect];
    if (self == nil) return nil;
    NSDictionary *labelAttributes;
    NSDictionary *accelAttributes;
    NSDictionary *arrowAttributes;
    CGFloat labelWidth = 0, trailing = 0;
    _selected = -1;
    _background = [axyne_popup_color(colors->background) retain];
    _border = [axyne_popup_color(colors->border) retain];
    _hover = [axyne_popup_color(colors->hover) retain];
    _hoverText = [axyne_popup_color(colors->hoverText) retain];
    _text = [axyne_popup_color(colors->text) retain];
    _muted = [axyne_popup_color(colors->muted) retain];
    _disabled = [axyne_popup_color(colors->disabled) retain];
    _separator = [axyne_popup_color(colors->separator) retain];
    _check = [axyne_popup_color(colors->check) retain];
    _font = [[NSFont systemFontOfSize:12] retain];
    _accelFont = [[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular] retain];
    _checkFont = [[NSFont systemFontOfSize:12 weight:NSFontWeightBold] retain];
    _arrowFont = [[NSFont systemFontOfSize:13] retain];
    labelAttributes = @{NSFontAttributeName: _font};
    accelAttributes = @{NSFontAttributeName: _accelFont};
    arrowAttributes = @{NSFontAttributeName: _arrowFont};

    _items = [[NSMutableArray alloc] init];
    _accels = [[NSMutableArray alloc] init];
    for (NSMenuItem *item in [menu itemArray]) {
        if ([item isHidden]) continue;
        [_items addObject:item];
    }
    _count = (NSInteger)[_items count];
    if (_count > 0) {
        _kinds = (uint8_t *)calloc((size_t)_count, sizeof(uint8_t));
        _selectable = (uint8_t *)calloc((size_t)_count, sizeof(uint8_t));
        _tops = (int *)calloc((size_t)_count, sizeof(int));
        _heights = (int *)calloc((size_t)_count, sizeof(int));
        if (_kinds == NULL || _selectable == NULL || _tops == NULL || _heights == NULL) {
            [self release];
            return nil;
        }
    }
    for (NSInteger i = 0; i < _count; ++i) {
        NSMenuItem *item = [_items objectAtIndex:(NSUInteger)i];
        _kinds[i] = [item isSeparatorItem] ? AXYNE_POPUP_KIND_SEPARATOR
                                           : AXYNE_POPUP_KIND_ITEM;
        _selectable[i] = axyne_popup_item_enabled(item) ? 1 : 0;
        id accel = [NSNull null];
        if (_kinds[i] == AXYNE_POPUP_KIND_ITEM) {
            NSString *title = [item title];
            if (title != nil) {
                CGFloat width = ceil([title sizeWithAttributes:labelAttributes].width);
                if (width > labelWidth) labelWidth = width;
            }
            if ([item submenu] != nil) {
                CGFloat width = ceil([@"›" sizeWithAttributes:arrowAttributes].width);
                if (width > trailing) trailing = width;
            } else {
                NSString *equivalent = [item keyEquivalent];
                if ([equivalent length] > 0) {
                    char text[40];
                    uint32_t key = (uint32_t)[equivalent characterAtIndex:0];
                    if (axyne_popup_accelerator(text, sizeof(text), key,
                            axyne_popup_modifiers([item keyEquivalentModifierMask])) > 0) {
                        NSString *shown = [NSString stringWithUTF8String:text];
                        if (shown != nil) {
                            CGFloat width = ceil([shown sizeWithAttributes:accelAttributes].width);
                            if (width > trailing) trailing = width;
                            accel = shown;
                        }
                    }
                }
            }
        }
        [_accels addObject:accel];
    }
    {
        int height = axyne_popup_layout(_kinds, (size_t)_count, _tops, _heights);
        int width = axyne_popup_width((int)labelWidth, (int)trailing);
        _size = NSMakeSize(width, height);
    }
    [self setFrame:NSMakeRect(0, 0, _size.width, _size.height)];
    /* A non-key panel only receives mouse-moved events through a tracking
     * area; the popup's event monitor reads them. */
    {
        NSTrackingArea *area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
            options:NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect
            owner:self userInfo:nil];
        [self addTrackingArea:area];
        [area release];
    }
    return self;
}

- (void)dealloc
{
    [_items release]; [_accels release];
    free(_kinds); free(_selectable); free(_tops); free(_heights);
    [_background release]; [_border release]; [_hover release]; [_hoverText release];
    [_text release]; [_muted release]; [_disabled release]; [_separator release];
    [_check release];
    [_font release]; [_accelFont release]; [_checkFont release]; [_arrowFont release];
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }

- (NSSize)contentSize { return _size; }
- (NSInteger)rowCount { return _count; }

- (NSMenuItem *)itemAtRow:(NSInteger)row
{
    return row >= 0 && row < _count ? [_items objectAtIndex:(NSUInteger)row] : nil;
}

- (BOOL)rowSelectable:(NSInteger)row
{
    return row >= 0 && row < _count && _selectable[row] != 0;
}

- (NSInteger)rowAtViewPoint:(NSPoint)point
{
    if (_count == 0) return -1;
    return axyne_popup_hit(_tops, _heights, (size_t)_count, (int)floor(point.y));
}

- (int)rowTop:(NSInteger)row
{
    return row >= 0 && row < _count ? _tops[row] : 0;
}

- (NSInteger)selectedIndex { return _selected; }

- (void)setSelectedIndex:(NSInteger)index
{
    if (index < 0 || index >= _count || !_selectable[index]) index = -1;
    if (index == _selected) return;
    _selected = index;
    [self setNeedsDisplay:YES];
}

- (void)moveSelection:(int)request
{
    [self setSelectedIndex:axyne_popup_nav(_selectable, (size_t)_count,
        (int)_selected, request)];
}

- (void)drawString:(NSString *)string font:(NSFont *)font color:(NSColor *)color
                 x:(CGFloat)x width:(CGFloat)width top:(CGFloat)top
         align:(NSTextAlignment)alignment truncating:(BOOL)truncating
{
    NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    NSDictionary *attributes;
    NSSize size;
    CGFloat y;
    [style setLineBreakMode:truncating ? NSLineBreakByTruncatingTail : NSLineBreakByClipping];
    [style setAlignment:alignment];
    attributes = @{NSFontAttributeName: font, NSForegroundColorAttributeName: color,
                   NSParagraphStyleAttributeName: style};
    size = [string sizeWithAttributes:attributes];
    y = top + floor((AXYNE_POPUP_ROW - size.height) / 2.0);
    [string drawInRect:NSMakeRect(x, y, width, ceil(size.height)) withAttributes:attributes];
}

- (void)drawRect:(NSRect)dirty
{
    NSRect bounds = [self bounds];
    NSBezierPath *frame = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5)
        xRadius:AXYNE_POPUP_RADIUS yRadius:AXYNE_POPUP_RADIUS];
    CGFloat rowX = AXYNE_POPUP_BORDER + AXYNE_POPUP_PAD;
    CGFloat rowWidth = NSWidth(bounds) - 2 * rowX;
    (void)dirty;
    [_background setFill];
    [frame fill];
    [_border setStroke];
    [frame setLineWidth:1.0];
    [frame stroke];
    for (NSInteger i = 0; i < _count; ++i) {
        NSMenuItem *item = [_items objectAtIndex:(NSUInteger)i];
        CGFloat top = _tops[i];
        BOOL enabled = _selectable[i] != 0;
        BOOL hot = enabled && i == _selected;
        NSColor *textColor;
        CGFloat left, right;
        id accel;
        if (_kinds[i] == AXYNE_POPUP_KIND_SEPARATOR) {
            [_separator setFill];
            NSRectFill(NSMakeRect(rowX + 8, top + 4, rowWidth - 16, 1));
            continue;
        }
        if (hot) {
            NSBezierPath *fill = [NSBezierPath bezierPathWithRoundedRect:
                NSMakeRect(rowX, top, rowWidth, AXYNE_POPUP_ROW)
                xRadius:AXYNE_POPUP_ROW_RADIUS yRadius:AXYNE_POPUP_ROW_RADIUS];
            [_hover setFill];
            [fill fill];
        }
        textColor = !enabled ? _disabled : (hot ? _hoverText : _text);
        left = rowX + AXYNE_POPUP_ROW_INSET;
        right = rowX + rowWidth - AXYNE_POPUP_ROW_INSET;
        if ([item state] == NSControlStateValueOn) {
            [self drawString:@"✓" font:_checkFont color:_check x:left
                       width:AXYNE_POPUP_STATUS top:top align:NSTextAlignmentCenter
               truncating:NO];
        }
        left += AXYNE_POPUP_STATUS + AXYNE_POPUP_GAP;
        accel = [_accels objectAtIndex:(NSUInteger)i];
        if ([item submenu] != nil) {
            NSDictionary *arrowAttributes = @{NSFontAttributeName: _arrowFont};
            CGFloat arrow = ceil([@"›" sizeWithAttributes:arrowAttributes].width);
            [self drawString:@"›" font:_arrowFont color:_muted x:right - arrow
                       width:arrow top:top align:NSTextAlignmentRight truncating:NO];
            right -= arrow + 12;
        } else if ([accel isKindOfClass:[NSString class]]) {
            NSDictionary *accelAttributes = @{NSFontAttributeName: _accelFont};
            CGFloat width = ceil([(NSString *)accel sizeWithAttributes:accelAttributes].width);
            [self drawString:(NSString *)accel font:_accelFont
                       color:enabled ? _muted : _disabled x:right - width
                       width:width top:top align:NSTextAlignmentRight truncating:NO];
            right -= width + 12;
        }
        if (right > left && [item title] != nil)
            [self drawString:[item title] font:_font color:textColor x:left
                       width:right - left top:top align:NSTextAlignmentLeft truncating:YES];
    }
}

@end

/* ------------------------------------------------------------ controller */

@interface AxynePopupMenu () {
    NSMenu *_menu;
    AxynePopupMenuColors _colors;
    AxynePopupMenuView *_view;
    NSPanel *_panel;
    AxynePopupMenu *_child;    /* retained submenu level */
    AxynePopupMenu *_parent;   /* not retained */
    NSInteger _childRow;
    id _localMonitor;
    id _globalMonitor;
    AxynePopupMenu *_pressedMenu; /* retained; level of the pending click */
    NSInteger _pressedRow;
    id<AxynePopupMenuDelegate> _delegate;
    BOOL _open;
}
@end

@implementation AxynePopupMenu
@synthesize delegate = _delegate;
@synthesize open = _open;

- (instancetype)initWithMenu:(NSMenu *)menu colors:(const AxynePopupMenuColors *)colors
{
    self = [super init];
    if (self == nil) return nil;
    _menu = [menu retain];
    _colors = *colors;
    _childRow = -1;
    /* Same validation as the native path: autoenabled items ask their target
     * (or the responder chain) through validateMenuItem:. */
    [_menu update];
    _view = [[AxynePopupMenuView alloc] initWithMenu:_menu colors:colors];
    if (_view == nil) { [self release]; return nil; }
    return self;
}

- (void)dealloc
{
    [self removeMonitorsAndObservers];
    [_pressedMenu release];
    [_child release];
    [_panel orderOut:nil];
    [_panel release];
    [_view release];
    [_menu release];
    [super dealloc];
}

- (void)removeMonitorsAndObservers
{
    if (_localMonitor != nil) {
        [NSEvent removeMonitor:_localMonitor];
        [_localMonitor release];
        _localMonitor = nil;
    }
    if (_globalMonitor != nil) {
        [NSEvent removeMonitor:_globalMonitor];
        [_globalMonitor release];
        _globalMonitor = nil;
    }
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (AxynePopupMenu *)root
{
    AxynePopupMenu *root = self;
    while (root->_parent != nil) root = root->_parent;
    return root;
}

- (AxynePopupMenu *)deepest
{
    AxynePopupMenu *level = self;
    while (level->_child != nil) level = level->_child;
    return level;
}

- (NSInteger)rowCount { return [_view rowCount]; }
- (NSMenuItem *)itemAtRow:(NSInteger)row { return [_view itemAtRow:row]; }
- (BOOL)rowEnabled:(NSInteger)row { return [_view rowSelectable:row]; }
- (NSInteger)selectedRow { return [_view selectedIndex]; }

/* ---- geometry */

+ (NSRect)visibleFrameNearPoint:(NSPoint)point window:(NSWindow *)window
{
    NSScreen *match = nil;
    for (NSScreen *screen in [NSScreen screens])
        if (NSPointInRect(point, [screen frame])) { match = screen; break; }
    if (match == nil) match = [window screen];
    if (match == nil) match = [NSScreen mainScreen];
    return match != nil ? [match visibleFrame] : NSMakeRect(0, 0, 1440, 900);
}

static AxynePopupRect axyne_popup_rect(NSRect rect)
{
    AxynePopupRect out = { rect.origin.x, rect.origin.y, rect.size.width, rect.size.height };
    return out;
}

- (void)showPanelAtOrigin:(NSPoint)origin
{
    NSSize size = [_view contentSize];
    NSRect frame = NSMakeRect(origin.x, origin.y, size.width, size.height);
    _panel = [[NSPanel alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:NO];
    [_panel setReleasedWhenClosed:NO];
    [_panel setOpaque:NO];
    [_panel setBackgroundColor:[NSColor clearColor]];
    [_panel setHasShadow:YES];
    [_panel setLevel:NSPopUpMenuWindowLevel];
    [_panel setHidesOnDeactivate:NO];
    [_panel setAcceptsMouseMovedEvents:YES];
    [_panel setBecomesKeyOnlyIfNeeded:YES];
    [_panel setCollectionBehavior:NSWindowCollectionBehaviorTransient |
        NSWindowCollectionBehaviorIgnoresCycle |
        NSWindowCollectionBehaviorFullScreenAuxiliary];
    [_panel setContentView:_view];
    [_panel orderFrontRegardless];
    [_panel displayIfNeeded]; /* the shadow follows the drawn rounded shape */
    [_panel invalidateShadow];
}

- (BOOL)presentBelowScreenRect:(NSRect)anchor gap:(CGFloat)gap
                   ownerWindow:(NSWindow *)window selectFirst:(BOOL)selectFirst
{
    NSSize size = [_view contentSize];
    NSRect visible;
    AxynePopupRect placed;
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    __block AxynePopupMenu *blockSelf = self; /* MRC: no retain, no cycle */
    if (_open || _parent != nil || [_view rowCount] == 0) return NO;
    visible = [AxynePopupMenu visibleFrameNearPoint:
        NSMakePoint(NSMidX(anchor), NSMidY(anchor)) window:window];
    placed = axyne_popup_place_below(axyne_popup_rect(anchor), size.width, size.height,
        axyne_popup_rect(visible), gap);
    [self showPanelAtOrigin:NSMakePoint(placed.x, placed.y)];
    if (selectFirst) [_view moveSelection:AXYNE_POPUP_NAV_FIRST];
    _open = YES;
    _localMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskMouseMoved | NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp |
        NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown | NSEventMaskKeyDown
        handler:^NSEvent *(NSEvent *event) {
            return [blockSelf handleLocalEvent:event];
        }] retain];
    _globalMonitor = [[NSEvent addGlobalMonitorForEventsMatchingMask:
        NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown
        handler:^(NSEvent *event) {
            (void)event;
            [blockSelf close];
        }] retain];
    [center addObserver:self selector:@selector(environmentChanged:)
        name:NSApplicationDidResignActiveNotification object:nil];
    if (window != nil) {
        [center addObserver:self selector:@selector(environmentChanged:)
            name:NSWindowDidResignKeyNotification object:window];
        [center addObserver:self selector:@selector(environmentChanged:)
            name:NSWindowDidMoveNotification object:window];
        [center addObserver:self selector:@selector(environmentChanged:)
            name:NSWindowDidResizeNotification object:window];
        [center addObserver:self selector:@selector(environmentChanged:)
            name:NSWindowWillCloseNotification object:window];
    }
    return YES;
}

- (void)environmentChanged:(NSNotification *)note
{
    (void)note;
    [self close];
}

/* ---- submenu levels */

- (void)closeChild
{
    if (_child == nil) return;
    if (_pressedMenu == _child) { [_pressedMenu release]; _pressedMenu = nil; }
    [_child closeChild];
    [_child->_panel orderOut:nil];
    [_child release];
    _child = nil;
    _childRow = -1;
}

- (void)openChildForRow:(NSInteger)row selectFirst:(BOOL)selectFirst
{
    NSMenuItem *item = [_view itemAtRow:row];
    NSMenu *submenu = [item submenu];
    AxynePopupMenu *child;
    NSRect parentFrame;
    NSRect visible;
    NSSize size;
    AxynePopupRect placed;
    if (submenu == nil || ![_view rowSelectable:row] || _panel == nil) return;
    [self closeChild];
    child = [[AxynePopupMenu alloc] initWithMenu:submenu colors:&_colors];
    if (child == nil) return;
    if ([child rowCount] == 0) { [child release]; return; }
    child->_parent = self;
    parentFrame = [_panel frame];
    size = [child->_view contentSize];
    visible = [AxynePopupMenu visibleFrameNearPoint:
        NSMakePoint(NSMidX(parentFrame), NSMidY(parentFrame)) window:nil];
    placed = axyne_popup_place_side(axyne_popup_rect(parentFrame),
        NSMaxY(parentFrame) - [_view rowTop:row], size.width, size.height,
        axyne_popup_rect(visible), AXYNE_POPUP_SUBMENU_OVERLAP);
    [child showPanelAtOrigin:NSMakePoint(placed.x, placed.y)];
    if (selectFirst) [child->_view moveSelection:AXYNE_POPUP_NAV_FIRST];
    _child = child;
    _childRow = row;
    [_view setSelectedIndex:row];
}

/* ---- closing and activation */

- (void)close
{
    AxynePopupMenu *root = [self root];
    id<AxynePopupMenuDelegate> delegate;
    if (root != self) { [root close]; return; }
    if (!_open) return;
    [[self retain] autorelease]; /* the delegate may drop its reference */
    _open = NO;
    [self removeMonitorsAndObservers];
    [_pressedMenu release];
    _pressedMenu = nil;
    [self closeChild];
    [_panel orderOut:nil];
    [_panel setContentView:nil];
    [_panel release];
    _panel = nil;
    delegate = _delegate;
    [delegate popupMenuDidClose:self];
}

/* Activates a row of this level (the whole popup closes first). Used by the
 * hooks and tests; events go through activateLevel:row:. */
- (BOOL)activateRow:(NSInteger)row
{
    NSMenuItem *item = [_view itemAtRow:row];
    if (item == nil || ![_view rowSelectable:row]) return NO;
    if ([item submenu] != nil) {
        [self openChildForRow:row selectFirst:YES];
        return YES;
    }
    [[self root] activateLevel:self row:row];
    return YES;
}

/* ---- events */

- (AxynePopupMenu *)levelAtScreenPoint:(NSPoint)point
{
    for (AxynePopupMenu *level = [self deepest]; level != nil; level = level->_parent)
        if (level->_panel != nil && NSPointInRect(point, [level->_panel frame])) return level;
    return nil;
}

- (NSInteger)rowAtScreenPoint:(NSPoint)point
{
    NSRect frame = [_panel frame];
    return [_view rowAtViewPoint:NSMakePoint(point.x - NSMinX(frame),
                                             NSMaxY(frame) - point.y)];
}

- (void)hoverRow:(NSInteger)row
{
    [_view setSelectedIndex:[_view rowSelectable:row] ? row : -1];
    if (_child != nil && row != _childRow) [self closeChild];
    if (_child == nil && [_view rowSelectable:row] && [[_view itemAtRow:row] submenu] != nil)
        [self openChildForRow:row selectFirst:NO];
}

- (void)handlePointerMoved
{
    NSPoint point = [NSEvent mouseLocation];
    AxynePopupMenu *level = [self levelAtScreenPoint:point];
    NSInteger row;
    if (level == nil) {
        id<AxynePopupMenuDelegate> delegate = _delegate;
        if ([delegate respondsToSelector:@selector(popupMenu:pointerMovedOutsideToScreenPoint:)])
            [delegate popupMenu:self pointerMovedOutsideToScreenPoint:point];
        return;
    }
    row = [level rowAtScreenPoint:point];
    if (row >= 0) [level hoverRow:row];
}

- (NSEvent *)handleMouseDown
{
    NSPoint point = [NSEvent mouseLocation];
    AxynePopupMenu *level = [self levelAtScreenPoint:point];
    NSInteger row;
    if (level == nil) { [self close]; return nil; } /* outside: dismiss, swallow */
    row = [level rowAtScreenPoint:point];
    [_pressedMenu release];
    _pressedMenu = [level retain];
    _pressedRow = row;
    if (row >= 0) [level hoverRow:row];
    return nil;
}

- (NSEvent *)handleMouseUp:(NSEvent *)event
{
    NSPoint point;
    AxynePopupMenu *pressed = _pressedMenu;
    NSInteger row = _pressedRow;
    if (pressed == nil) return event; /* the release of the opening click */
    point = [NSEvent mouseLocation];
    _pressedMenu = nil;
    [pressed autorelease];
    if ([self levelAtScreenPoint:point] == pressed &&
        [pressed rowAtScreenPoint:point] == row && row >= 0 &&
        [pressed->_view rowSelectable:row])
        [pressed activateFromLevelRow:row];
    return nil;
}

/* Activation entry for a specific level (root receives the events). */
- (void)activateFromLevelRow:(NSInteger)row
{
    NSMenuItem *item = [_view itemAtRow:row];
    if ([item submenu] != nil) { [self openChildForRow:row selectFirst:NO]; return; }
    [[self root] activateLevel:self row:row];
}

/* Closes the popup, then sends the item's action. The item and target are
 * kept alive across the close (the delegate may release the whole popup). */
- (void)activateLevel:(AxynePopupMenu *)level row:(NSInteger)row
{
    NSMenuItem *item = [level->_view itemAtRow:row];
    SEL action;
    id target;
    if (item == nil) return;
    action = [item action];
    target = [item target];
    [[item retain] autorelease];
    [[target retain] autorelease];
    [[self retain] autorelease];
    [self close];
    if (action != NULL) [NSApp sendAction:action to:target from:item];
}

- (void)requestNeighbor:(NSInteger)direction
{
    id<AxynePopupMenuDelegate> delegate = _delegate;
    if ([delegate respondsToSelector:@selector(popupMenu:requestsNeighbor:)])
        [delegate popupMenu:self requestsNeighbor:direction];
}

- (NSEvent *)handleKeyDown:(NSEvent *)event
{
    NSEventModifierFlags flags = [event modifierFlags];
    AxynePopupMenu *active = [self deepest];
    NSInteger selected = [active->_view selectedIndex];
    /* Command/Control chords belong to the key equivalents: close and let
     * the event through to the native menu. */
    if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0) {
        [self close];
        return event;
    }
    switch ([event keyCode]) {
    case 126: [active->_view moveSelection:AXYNE_POPUP_NAV_PREVIOUS]; break;
    case 125: [active->_view moveSelection:AXYNE_POPUP_NAV_NEXT]; break;
    case 115: [active->_view moveSelection:AXYNE_POPUP_NAV_FIRST]; break;
    case 119: [active->_view moveSelection:AXYNE_POPUP_NAV_LAST]; break;
    case 36: case 76: /* Return, keypad Enter */
        if (selected >= 0) {
            NSMenuItem *item = [active->_view itemAtRow:selected];
            if ([item submenu] != nil) [active openChildForRow:selected selectFirst:YES];
            else [self activateLevel:active row:selected];
        }
        break;
    case 53: /* Escape closes one level, then the popup */
        if (active->_parent != nil) [active->_parent closeChild];
        else [self close];
        break;
    case 124: /* Right */
        if (selected >= 0 && [[active->_view itemAtRow:selected] submenu] != nil &&
            [active->_view rowSelectable:selected])
            [active openChildForRow:selected selectFirst:YES];
        else [self requestNeighbor:1];
        break;
    case 123: /* Left */
        if (active->_parent != nil) [active->_parent closeChild];
        else [self requestNeighbor:-1];
        break;
    default: break;
    }
    return nil;
}

- (NSEvent *)handleLocalEvent:(NSEvent *)event
{
    if (!_open) return event;
    [[self retain] autorelease]; /* a switch may release this popup mid-event */
    switch ([event type]) {
    case NSEventTypeMouseMoved:
        [self handlePointerMoved];
        return event;
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
    case NSEventTypeOtherMouseDown:
        return [self handleMouseDown];
    case NSEventTypeLeftMouseUp:
        return [self handleMouseUp:event];
    case NSEventTypeKeyDown:
        return [self handleKeyDown:event];
    default:
        return event;
    }
}

@end
