#import <AppKit/AppKit.h>

#include <stdlib.h>
#include <string.h>

#include "../../extensions_window.h"
#include "../../preferences_window.h"

/* 확장 window as an AppKit modal panel in the preferences window frame. The
 * content view paints every element and resolves clicks through the hit list
 * of its last draw, like the Windows adapter. */

typedef enum EwAction {
    EW_HIT_NONE = 0,
    EW_HIT_CLOSE,
    EW_HIT_NAV,
    EW_HIT_INSTALL,
    EW_HIT_TOGGLE,
    EW_HIT_REMOVE,
    EW_HIT_OPEN_FOLDER
} EwAction;

enum {
    EW_PAGE_Y = AXYNE_PW_TITLE_HEIGHT,
    EW_FOOTER_Y = AXYNE_PW_HEIGHT - AXYNE_PW_FOOTER_HEIGHT,
    EW_LEFT = AXYNE_PW_SIDEBAR_WIDTH + AXYNE_PW_CONTENT_LEFT,
    EW_RIGHT = AXYNE_PW_WIDTH - AXYNE_PW_CONTENT_LEFT,
    EW_LIST_Y = EW_PAGE_Y + AXYNE_EW_HEADER_HEIGHT + 8,
    EW_MAX_HITS = 128
};

typedef struct EwHit {
    NSRect rect;
    EwAction action;
    int index;
} EwHit;

static NSColor *ew_color(uint32_t rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0 green:((rgb >> 8) & 0xff) / 255.0
                                blue:(rgb & 0xff) / 255.0 alpha:1.0];
}

static NSString *ew_string(const char *utf8)
{
    NSString *text = utf8 != NULL ? [NSString stringWithUTF8String:utf8] : nil;
    return text != nil ? text : @"";
}

static int ew_list_height(void) { return EW_FOOTER_Y - EW_LIST_Y - 8; }

/* Borderless panels refuse key status by default; Esc needs it. */
@interface AxyneExtensionsPanel : NSPanel
@end
@implementation AxyneExtensionsPanel
- (BOOL)canBecomeKeyWindow { return YES; }
@end

@interface AxyneExtensionsView : NSView {
@public
    AxynePluginSet *_plugins;
    const char *_ideVersion;
    int _page;
    CGFloat _scroll;
    BOOL _done;
    EwHit _hits[EW_MAX_HITS];
    int _hitCount;
}
@end

@implementation AxyneExtensionsView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)addHit:(NSRect)rect action:(EwAction)action index:(int)index
{
    if (_hitCount >= EW_MAX_HITS) return;
    _hits[_hitCount].rect = rect;
    _hits[_hitCount].action = action;
    _hits[_hitCount].index = index;
    ++_hitCount;
}

- (void)text:(NSString *)text rect:(NSRect)rect font:(NSFont *)font color:(uint32_t)color
       align:(NSTextAlignment)align
{
    NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    NSDictionary *attributes;
    NSSize size;
    [style setAlignment:align];
    [style setLineBreakMode:NSLineBreakByTruncatingTail];
    attributes = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: ew_color(color),
                    NSParagraphStyleAttributeName: style };
    size = [text sizeWithAttributes:attributes];
    rect.origin.y += floor((rect.size.height - size.height) / 2); /* vertical centre */
    rect.size.height = size.height;
    [text drawInRect:rect withAttributes:attributes];
}

- (void)button:(NSRect)rect label:(NSString *)label accent:(BOOL)accent
        action:(EwAction)action index:(int)index
{
    uint32_t fill = accent ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_BUTTON;
    [ew_color(fill) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:6 yRadius:6] fill];
    [self text:label rect:rect font:[NSFont systemFontOfSize:12 weight:NSFontWeightSemibold]
         color:accent ? 0xffffffu : AXYNE_PW_COLOR_TEXT align:NSTextAlignmentCenter];
    [self addHit:rect action:action index:index];
}

- (CGFloat)maxScroll
{
    CGFloat content = (CGFloat)_plugins->count * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP);
    return content > ew_list_height() ? content - ew_list_height() : 0;
}

- (void)drawInstalled
{
    NSFont *small = [NSFont systemFontOfSize:11];
    size_t i;
    [self text:@"설치된 플러그인" rect:NSMakeRect(EW_LEFT, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP, 300, 22)
          font:[NSFont systemFontOfSize:16 weight:NSFontWeightSemibold] color:AXYNE_PW_COLOR_TEXT
         align:NSTextAlignmentLeft];
    [self button:NSMakeRect(EW_RIGHT - 128, EW_PAGE_Y + 16, 128, 30) label:@"패키지 설치…" accent:NO
          action:EW_HIT_INSTALL index:0];
    if (_plugins->count == 0) {
        [self text:@"설치된 플러그인이 없습니다." rect:NSMakeRect(EW_LEFT, EW_LIST_Y + 40, EW_RIGHT - EW_LEFT, 20)
              font:[NSFont systemFontOfSize:12] color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentCenter];
        [self text:@"마켓플레이스에서 받거나 플러그인 패키지(.zip)를 설치하세요."
              rect:NSMakeRect(EW_LEFT, EW_LIST_Y + 64, EW_RIGHT - EW_LEFT, 20) font:small
             color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentCenter];
        return;
    }
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(EW_LEFT, EW_LIST_Y, EW_RIGHT - EW_LEFT, ew_list_height()));
    for (i = 0; i < _plugins->count; ++i) {
        const AxynePlugin *plugin = &_plugins->items[i];
        const AxynePluginRequirement *missing = plugin->compatible ? axyne_plugin_missing_requirement(plugin) : NULL;
        CGFloat top = EW_LIST_Y + (CGFloat)i * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP) - _scroll;
        NSRect row = NSMakeRect(EW_LEFT, top, EW_RIGHT - EW_LEFT, AXYNE_EW_ROW_HEIGHT);
        NSString *detail;
        uint32_t detail_color = AXYNE_PW_COLOR_MUTED;
        if (NSMaxY(row) < EW_LIST_Y || top > EW_LIST_Y + ew_list_height()) continue;
        [ew_color(AXYNE_PW_COLOR_FIELD) setFill];
        [ew_color(AXYNE_PW_COLOR_BORDER) setStroke];
        {
            NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(row, 0.5, 0.5) xRadius:8 yRadius:8];
            [path fill];
            [path stroke];
        }
        [self text:[NSString stringWithFormat:@"%@  v%@", ew_string(plugin->name), ew_string(plugin->version)]
              rect:NSMakeRect(row.origin.x + 14, top + 10, row.size.width - 204, 18)
              font:[NSFont systemFontOfSize:12 weight:NSFontWeightSemibold]
             color:plugin->enabled ? AXYNE_PW_COLOR_TEXT : AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
        [self text:ew_string(plugin->description) rect:NSMakeRect(row.origin.x + 14, top + 30, row.size.width - 204, 16)
              font:small color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
        if (!plugin->compatible) {
            detail = @"이 IDE 버전 또는 운영체제에서는 사용할 수 없습니다";
            detail_color = 0xd9b36cu;
        } else if (missing != NULL) {
            detail = [NSString stringWithFormat:@"필요: %@%@%@", ew_string(missing->program),
                      missing->hint[0] ? @" — " : @"", ew_string(missing->hint)];
            detail_color = 0xd9b36cu;
        } else {
            NSString *author = ew_string(plugin->author), *category = ew_string(plugin->category);
            detail = [NSString stringWithFormat:@"%@%@%@ · 명령 %zu개", author,
                      [author length] && [category length] ? @" · " : @"", category, plugin->command_count];
        }
        [self text:detail rect:NSMakeRect(row.origin.x + 14, top + 50, row.size.width - 204, 16) font:small
             color:detail_color align:NSTextAlignmentLeft];
        [self button:NSMakeRect(NSMaxX(row) - 176, top + 23, 92, 30)
               label:plugin->enabled ? @"사용 중" : @"사용 안 함" accent:plugin->enabled && plugin->compatible
              action:EW_HIT_TOGGLE index:(int)i];
        [self button:NSMakeRect(NSMaxX(row) - 76, top + 23, 64, 30) label:@"삭제" accent:NO
              action:EW_HIT_REMOVE index:(int)i];
    }
    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawRect:(NSRect)dirty
{
    static NSString *const pages[AXYNE_EXTENSIONS_PAGE_COUNT] = { @"설치됨" };
    NSRect close = NSMakeRect(AXYNE_PW_WIDTH - AXYNE_PW_TITLE_HEIGHT, 0, AXYNE_PW_TITLE_HEIGHT, AXYNE_PW_TITLE_HEIGHT);
    int i;
    (void)dirty;
    _hitCount = 0;
    [ew_color(AXYNE_PW_COLOR_CONTENT) setFill];
    NSRectFill([self bounds]);
    [ew_color(AXYNE_PW_COLOR_CHROME) setFill];
    NSRectFill(NSMakeRect(0, 0, AXYNE_PW_WIDTH, AXYNE_PW_TITLE_HEIGHT));
    [self text:@"확장" rect:NSMakeRect(10, 0, 300, AXYNE_PW_TITLE_HEIGHT) font:[NSFont systemFontOfSize:12]
         color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
    [self text:@"✕" rect:close font:[NSFont systemFontOfSize:14] color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentCenter];
    [self addHit:close action:EW_HIT_CLOSE index:0];
    [ew_color(AXYNE_PW_COLOR_SIDEBAR) setFill];
    NSRectFill(NSMakeRect(0, EW_PAGE_Y, AXYNE_PW_SIDEBAR_WIDTH, EW_FOOTER_Y - EW_PAGE_Y));
    for (i = 0; i < AXYNE_EXTENSIONS_PAGE_COUNT; ++i) {
        NSRect item = NSMakeRect(0, EW_PAGE_Y + i * AXYNE_PW_ITEM_HEIGHT, AXYNE_PW_SIDEBAR_WIDTH, AXYNE_PW_ITEM_HEIGHT);
        if (i == _page) { [ew_color(AXYNE_PW_COLOR_SELECTED) setFill]; NSRectFill(item); }
        [self text:pages[i] rect:NSMakeRect(12, item.origin.y, item.size.width - 12, item.size.height)
              font:i == _page ? [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold] : [NSFont systemFontOfSize:12]
             color:i == _page ? AXYNE_PW_COLOR_TEXT : AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
        [self addHit:item action:EW_HIT_NAV index:i];
    }
    if (_page == AXYNE_EXTENSIONS_INSTALLED) [self drawInstalled];
    [ew_color(AXYNE_PW_COLOR_CHROME) setFill];
    NSRectFill(NSMakeRect(0, EW_FOOTER_Y, AXYNE_PW_WIDTH, AXYNE_PW_FOOTER_HEIGHT));
    [ew_color(AXYNE_PW_COLOR_CHROME_LINE) setFill];
    NSRectFill(NSMakeRect(0, EW_FOOTER_Y, AXYNE_PW_WIDTH, 1));
    [self text:@"플러그인 폴더 열기" rect:NSMakeRect(12, EW_FOOTER_Y, 200, AXYNE_PW_FOOTER_HEIGHT)
          font:[NSFont systemFontOfSize:12] color:AXYNE_PW_COLOR_ACCENT align:NSTextAlignmentLeft];
    [self addHit:NSMakeRect(12, EW_FOOTER_Y + 28, 120, AXYNE_PW_FOOTER_HEIGHT - 56) action:EW_HIT_OPEN_FOLDER index:0];
    [self button:NSMakeRect(AXYNE_PW_WIDTH - 20 - AXYNE_PW_BUTTON_WIDTH, EW_FOOTER_Y + 23,
                            AXYNE_PW_BUTTON_WIDTH, AXYNE_PW_BUTTON_HEIGHT)
           label:@"닫기" accent:YES action:EW_HIT_CLOSE index:0];
}

- (void)message:(NSString *)text detail:(const char *)detail
{
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:text];
    [alert setInformativeText:ew_string(detail)];
    [alert runModal];
}

- (void)install
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    AxyneError error;
    char *installed = NULL;
    [panel setAllowedFileTypes:@[@"zip"]];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] != NSModalResponseOK) return;
    if (axyne_plugins_install_zip(_plugins, [[[panel URL] path] UTF8String], _ideVersion, &installed, &error)
        != AXYNE_STATUS_OK) {
        [self message:@"플러그인을 설치하지 못했습니다." detail:error.message];
    } else {
        AxynePlugin *plugin = axyne_plugins_find(_plugins, installed);
        [self message:@"플러그인을 설치했습니다." detail:plugin != NULL ? plugin->name : installed];
    }
    free(installed);
    _scroll = 0;
}

- (void)activate:(EwHit)hit
{
    AxyneError error;
    AxynePlugin *plugin = hit.index >= 0 && (size_t)hit.index < _plugins->count ? &_plugins->items[hit.index] : NULL;
    switch (hit.action) {
    case EW_HIT_CLOSE: _done = YES; [NSApp stopModal]; break;
    case EW_HIT_NAV: _page = hit.index; _scroll = 0; break;
    case EW_HIT_INSTALL: [self install]; break;
    case EW_HIT_TOGGLE:
        if (plugin != NULL && axyne_plugins_set_enabled(_plugins, plugin->id, !plugin->enabled, &error) != AXYNE_STATUS_OK)
            [self message:@"설정을 저장하지 못했습니다." detail:error.message];
        break;
    case EW_HIT_REMOVE:
        if (plugin != NULL) {
            NSAlert *alert = [[[NSAlert alloc] init] autorelease];
            [alert setMessageText:[NSString stringWithFormat:@"%@ 플러그인을 삭제할까요?", ew_string(plugin->name)]];
            [alert addButtonWithTitle:@"삭제"];
            [alert addButtonWithTitle:@"취소"];
            if ([alert runModal] == NSAlertFirstButtonReturn) {
                char id[AXYNE_PLUGIN_ID_MAX + 1];
                strncpy(id, plugin->id, sizeof(id) - 1);
                id[sizeof(id) - 1] = '\0';
                if (axyne_plugins_uninstall(_plugins, id, &error) != AXYNE_STATUS_OK)
                    [self message:@"플러그인을 삭제하지 못했습니다." detail:error.message];
                if (_scroll > [self maxScroll]) _scroll = [self maxScroll];
            }
        }
        break;
    case EW_HIT_OPEN_FOLDER: {
        NSString *folder = ew_string(_plugins->plugins_dir);
        [[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES
                                                   attributes:nil error:NULL];
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:folder isDirectory:YES]];
        break;
    }
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    int i;
    for (i = _hitCount - 1; i >= 0; --i) {
        BOOL inList = _hits[i].action == EW_HIT_TOGGLE || _hits[i].action == EW_HIT_REMOVE;
        if (inList && (point.y < EW_LIST_Y || point.y >= EW_LIST_Y + ew_list_height())) continue;
        if (NSPointInRect(point, _hits[i].rect)) { [self activate:_hits[i]]; return; }
    }
}

- (void)mouseDragged:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (point.y < AXYNE_PW_TITLE_HEIGHT) [[self window] performWindowDragWithEvent:event];
}

- (void)scrollWheel:(NSEvent *)event
{
    CGFloat next = _scroll - [event scrollingDeltaY];
    if (next < 0) next = 0;
    if (next > [self maxScroll]) next = [self maxScroll];
    if (next != _scroll) { _scroll = next; [self setNeedsDisplay:YES]; }
}

- (void)keyDown:(NSEvent *)event
{
    if ([event keyCode] == 53) { _done = YES; [NSApp stopModal]; return; } /* Esc */
    [super keyDown:event];
}

@end

void axyne_extensions_window_show(void *native_owner, AxynePluginSet *plugins,
                                  const char *ide_version, AxyneExtensionsPage page)
{
    NSWindow *owner = (NSWindow *)native_owner;
    NSPanel *panel;
    AxyneExtensionsView *view;
    NSRect frame = NSMakeRect(0, 0, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT);
    if (plugins == NULL) return;
    panel = [[AxyneExtensionsPanel alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless
                                         backing:NSBackingStoreBuffered defer:NO];
    [panel setReleasedWhenClosed:NO];
    [panel setHasShadow:YES];
    view = [[[AxyneExtensionsView alloc] initWithFrame:frame] autorelease];
    view->_plugins = plugins;
    view->_ideVersion = ide_version;
    view->_page = page < AXYNE_EXTENSIONS_PAGE_COUNT ? (int)page : 0;
    [panel setContentView:view];
    if (owner != nil) {
        NSRect bounds = [owner frame];
        [panel setFrameOrigin:NSMakePoint(NSMidX(bounds) - AXYNE_PW_WIDTH / 2, NSMidY(bounds) - AXYNE_PW_HEIGHT / 2)];
    } else {
        [panel center];
    }
    [panel makeFirstResponder:view];
    [NSApp runModalForWindow:panel];
    [panel orderOut:nil];
    [panel release];
}
