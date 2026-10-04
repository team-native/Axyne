#ifndef AXYNE_POPUP_MENU_MACOS_H
#define AXYNE_POPUP_MENU_MACOS_H

#import <AppKit/AppKit.h>

#include <stdint.h>

/* Axyne-styled popup that lists an NSMenu (Figma 24:14189 menu frames) in a
 * borderless, non-activating panel. The NSMenu stays the single item table:
 * titles, key equivalents, state, enabled flags and submenus come from it and
 * a chosen item is sent with [NSApp sendAction:to:from:]. Manual
 * retain/release, like the rest of the macOS UI. */

typedef struct AxynePopupMenuColors {
    uint32_t background, border, hover, hoverText, text, muted, disabled,
             separator, check; /* 0xRRGGBB */
} AxynePopupMenuColors;

@class AxynePopupMenu;

@protocol AxynePopupMenuDelegate <NSObject>
/* Called exactly once when the whole popup (every level) has closed, after
 * its panels are gone and its event monitors are removed. */
- (void)popupMenuDidClose:(AxynePopupMenu *)popup;
@optional
/* The pointer moved outside every popup panel (menu-bar switching). */
- (void)popupMenu:(AxynePopupMenu *)popup
    pointerMovedOutsideToScreenPoint:(NSPoint)point;
/* Left (-1) or Right (+1) pressed with no submenu to enter or leave. */
- (void)popupMenu:(AxynePopupMenu *)popup requestsNeighbor:(NSInteger)direction;
@end

@interface AxynePopupMenu : NSObject
@property(nonatomic, assign) id<AxynePopupMenuDelegate> delegate;
@property(nonatomic, readonly, getter=isOpen) BOOL open;

/* Validates the menu ([menu update], i.e. validateMenuItem: through each
 * item's target or the responder chain) and measures its rows. */
- (instancetype)initWithMenu:(NSMenu *)menu colors:(const AxynePopupMenuColors *)colors;

/* Shows the popup under `anchor` (screen coordinates), left edges aligned
 * and `gap` apart, clamped to the visible screen area. Returns NO, showing
 * nothing, when the menu has no visible items. `window` is the owner window:
 * the popup closes when it resigns key, moves, resizes or closes. */
- (BOOL)presentBelowScreenRect:(NSRect)anchor gap:(CGFloat)gap
                   ownerWindow:(NSWindow *)window selectFirst:(BOOL)selectFirst;
- (void)close;

/* Inspection and activation hooks (also used by tests). */
- (NSInteger)rowCount;
- (NSMenuItem *)itemAtRow:(NSInteger)row;
- (BOOL)rowEnabled:(NSInteger)row;
- (NSInteger)selectedRow;
- (BOOL)activateRow:(NSInteger)row;
@end

#endif
