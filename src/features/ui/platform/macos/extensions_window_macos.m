#import <AppKit/AppKit.h>

#include <stdlib.h>
#include <string.h>

#include "axyne/account.h"
#include "axyne/app_paths.h"
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
    EW_HIT_OPEN_FOLDER,
    EW_HIT_MARKET_INSTALL,
    EW_HIT_SEARCH,
    EW_HIT_SUBMIT,
    EW_HIT_SWITCH_MODE,
    EW_HIT_LOGOUT
} EwAction;

/* Text fields are real NSTextField subviews; the box around each is drawn. */
typedef enum EwField {
    EW_FIELD_SEARCH = 0,
    EW_FIELD_EMAIL,
    EW_FIELD_NAME,
    EW_FIELD_PASSWORD,
    EW_FIELD_COUNT
} EwField;

enum {
    EW_ACCOUNT_Y = AXYNE_PW_TITLE_HEIGHT + 76,
    EW_ACCOUNT_ROW = 62,
    EW_ACCOUNT_WIDTH = 340
};

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
    NSTextField *_fields[EW_FIELD_COUNT];
    char *_configDir;
    char *_server;
    AxyneAccount _account;
    AxyneMarketList _market;
    BOOL _marketLoaded;
    BOOL _registerMode;
    NSString *_notice;
    BOOL _noticeError;
}
- (void)openPage:(int)page;
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

- (void)dealloc
{
    int i;
    for (i = 0; i < EW_FIELD_COUNT; ++i) [_fields[i] release];
    axyne_market_list_clear(&_market);
    axyne_account_clear(&_account);
    free(_server);
    axyne_app_path_free(_configDir);
    [_notice release];
    [super dealloc];
}

- (CGFloat)maxScroll
{
    size_t rows = _page == AXYNE_EXTENSIONS_MARKETPLACE ? _market.count
                : _page == AXYNE_EXTENSIONS_INSTALLED ? _plugins->count : 0;
    CGFloat content = (CGFloat)rows * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP);
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

- (void)setNotice:(NSString *)prefix detail:(const char *)detail error:(BOOL)isError
{
    NSString *text = detail != NULL && detail[0] ? [NSString stringWithFormat:@"%@ %@", prefix, ew_string(detail)] : prefix;
    [_notice release];
    _notice = [text retain];
    _noticeError = isError;
}

- (BOOL)fieldBox:(EwField)field rect:(NSRect *)box
{
    int row;
    if (field == EW_FIELD_SEARCH) {
        if (_page != AXYNE_EXTENSIONS_MARKETPLACE) return NO;
        *box = NSMakeRect(EW_LEFT + 140, EW_PAGE_Y + 16, EW_RIGHT - 76 - (EW_LEFT + 140), AXYNE_PW_FIELD_HEIGHT);
        return YES;
    }
    if (_page != AXYNE_EXTENSIONS_ACCOUNT || axyne_account_signed_in(&_account)) return NO;
    if (field == EW_FIELD_NAME && !_registerMode) return NO;
    row = field == EW_FIELD_EMAIL ? 0 : field == EW_FIELD_NAME ? 1 : _registerMode ? 2 : 1;
    *box = NSMakeRect(EW_LEFT, EW_ACCOUNT_Y + row * EW_ACCOUNT_ROW + 20, EW_ACCOUNT_WIDTH, AXYNE_PW_FIELD_HEIGHT);
    return YES;
}

- (void)createFields
{
    int i;
    for (i = 0; i < EW_FIELD_COUNT; ++i) {
        NSTextField *field = i == EW_FIELD_PASSWORD ? [[NSSecureTextField alloc] initWithFrame:NSZeroRect]
                                                    : [[NSTextField alloc] initWithFrame:NSZeroRect];
        [field setBezeled:NO];
        [field setBordered:NO];
        [field setDrawsBackground:NO];
        [field setFocusRingType:NSFocusRingTypeNone];
        [field setTextColor:ew_color(AXYNE_PW_COLOR_TEXT)];
        [field setFont:[NSFont systemFontOfSize:12]];
        [[field cell] setUsesSingleLineMode:YES];
        [[field cell] setScrollable:YES];
        [field setTarget:self];
        [field setAction:i == EW_FIELD_SEARCH ? @selector(searchAction:) : @selector(submitAction:)];
        [field setHidden:YES];
        [self addSubview:field];
        _fields[i] = field;
    }
    [_fields[EW_FIELD_SEARCH] setPlaceholderString:@"플러그인 검색"];
    [_fields[EW_FIELD_EMAIL] setNextKeyView:_fields[EW_FIELD_NAME]];
    [_fields[EW_FIELD_NAME] setNextKeyView:_fields[EW_FIELD_PASSWORD]];
    [_fields[EW_FIELD_PASSWORD] setNextKeyView:_fields[EW_FIELD_EMAIL]];
}

- (void)layoutFields
{
    int i;
    for (i = 0; i < EW_FIELD_COUNT; ++i) {
        NSRect box;
        if ([self fieldBox:(EwField)i rect:&box]) {
            [_fields[i] setFrame:NSMakeRect(box.origin.x + 9, box.origin.y + 7, box.size.width - 18, box.size.height - 13)];
            [_fields[i] setHidden:NO];
        } else {
            [_fields[i] setHidden:YES];
        }
    }
}

- (void)drawField:(EwField)field label:(NSString *)label
{
    NSRect box;
    NSBezierPath *path;
    if (![self fieldBox:field rect:&box]) return;
    if (label != nil)
        [self text:label rect:NSMakeRect(box.origin.x, box.origin.y - 20, box.size.width, 16)
              font:[NSFont systemFontOfSize:11] color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
    path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(box, 0.5, 0.5) xRadius:6 yRadius:6];
    [ew_color(AXYNE_PW_COLOR_FIELD) setFill];
    [ew_color(AXYNE_PW_COLOR_BORDER) setStroke];
    [path fill];
    [path stroke];
}

- (void)drawNotice:(NSRect)rect
{
    NSMutableParagraphStyle *style;
    if ([_notice length] == 0) return;
    style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [style setLineBreakMode:NSLineBreakByWordWrapping];
    [_notice drawInRect:rect withAttributes:@{
        NSFontAttributeName: [NSFont systemFontOfSize:11],
        NSForegroundColorAttributeName: ew_color(_noticeError ? 0xe57373u : AXYNE_PW_COLOR_MUTED),
        NSParagraphStyleAttributeName: style }];
}

- (void)drawMarketplace
{
    NSFont *small = [NSFont systemFontOfSize:11];
    size_t i;
    [self text:@"마켓플레이스" rect:NSMakeRect(EW_LEFT, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP, 136, 22)
          font:[NSFont systemFontOfSize:16 weight:NSFontWeightSemibold] color:AXYNE_PW_COLOR_TEXT
         align:NSTextAlignmentLeft];
    [self drawField:EW_FIELD_SEARCH label:nil];
    [self button:NSMakeRect(EW_RIGHT - 68, EW_PAGE_Y + 16, 68, AXYNE_PW_FIELD_HEIGHT) label:@"검색" accent:NO
          action:EW_HIT_SEARCH index:0];
    if (_market.count == 0) {
        NSRect rect = NSMakeRect(EW_LEFT, EW_LIST_Y + 40, EW_RIGHT - EW_LEFT, 80);
        if ([_notice length] > 0) [self drawNotice:rect];
        else [self text:_marketLoaded ? @"조건에 맞는 플러그인이 없습니다." : @"불러오는 중…"
                   rect:NSMakeRect(EW_LEFT, EW_LIST_Y + 40, EW_RIGHT - EW_LEFT, 20)
                   font:[NSFont systemFontOfSize:12] color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentCenter];
        return;
    }
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(EW_LEFT, EW_LIST_Y, EW_RIGHT - EW_LEFT, ew_list_height()));
    for (i = 0; i < _market.count; ++i) {
        const AxyneMarketItem *item = &_market.items[i];
        const AxynePlugin *installed = axyne_plugins_find(_plugins, item->slug);
        BOOL same = installed != NULL && item->version != NULL && strcmp(installed->version, item->version) == 0;
        CGFloat top = EW_LIST_Y + (CGFloat)i * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP) - _scroll;
        NSRect row = NSMakeRect(EW_LEFT, top, EW_RIGHT - EW_LEFT, AXYNE_EW_ROW_HEIGHT);
        NSString *developer = ew_string(item->developer);
        if (NSMaxY(row) < EW_LIST_Y || top > EW_LIST_Y + ew_list_height()) continue;
        [ew_color(AXYNE_PW_COLOR_FIELD) setFill];
        [ew_color(AXYNE_PW_COLOR_BORDER) setStroke];
        {
            NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(row, 0.5, 0.5) xRadius:8 yRadius:8];
            [path fill];
            [path stroke];
        }
        [self text:[NSString stringWithFormat:@"%@  v%@", ew_string(item->name),
                    item->version != NULL ? ew_string(item->version) : @"?"]
              rect:NSMakeRect(row.origin.x + 14, top + 10, row.size.width - 134, 18)
              font:[NSFont systemFontOfSize:12 weight:NSFontWeightSemibold] color:AXYNE_PW_COLOR_TEXT
             align:NSTextAlignmentLeft];
        [self text:ew_string(item->description) rect:NSMakeRect(row.origin.x + 14, top + 30, row.size.width - 134, 16)
              font:small color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
        [self text:[NSString stringWithFormat:@"%@%@다운로드 %ld회%@", developer, [developer length] ? @" · " : @"",
                    item->downloads, installed == NULL ? @"" : same ? @" · 설치됨" : @" · 새 버전 있음"]
              rect:NSMakeRect(row.origin.x + 14, top + 50, row.size.width - 134, 16) font:small
             color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
        if (same)
            [self text:@"설치됨" rect:NSMakeRect(NSMaxX(row) - 104, top + 23, 92, 30)
                  font:[NSFont systemFontOfSize:12] color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentCenter];
        else
            [self button:NSMakeRect(NSMaxX(row) - 104, top + 23, 92, 30)
                   label:installed != NULL ? @"업데이트" : @"설치" accent:YES
                  action:EW_HIT_MARKET_INSTALL index:(int)i];
    }
    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawAccount
{
    BOOL signedIn = axyne_account_signed_in(&_account);
    NSRect notice;
    [self text:signedIn ? @"Axyne 계정" : _registerMode ? @"Axyne 계정 만들기" : @"Axyne 계정 로그인"
          rect:NSMakeRect(EW_LEFT, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP, EW_RIGHT - EW_LEFT, 22)
          font:[NSFont systemFontOfSize:16 weight:NSFontWeightSemibold] color:AXYNE_PW_COLOR_TEXT
         align:NSTextAlignmentLeft];
    if (signedIn) {
        [self text:ew_string(_account.display_name) rect:NSMakeRect(EW_LEFT, EW_ACCOUNT_Y, EW_RIGHT - EW_LEFT, 22)
              font:[NSFont systemFontOfSize:16 weight:NSFontWeightSemibold] color:AXYNE_PW_COLOR_TEXT
             align:NSTextAlignmentLeft];
        [self text:ew_string(_account.email) rect:NSMakeRect(EW_LEFT, EW_ACCOUNT_Y + 28, EW_RIGHT - EW_LEFT, 18)
              font:[NSFont systemFontOfSize:12] color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
        [self button:NSMakeRect(EW_LEFT, EW_ACCOUNT_Y + 66, 110, AXYNE_PW_BUTTON_HEIGHT) label:@"로그아웃" accent:NO
              action:EW_HIT_LOGOUT index:0];
        notice = NSMakeRect(EW_LEFT, EW_ACCOUNT_Y + 116, EW_RIGHT - EW_LEFT, 60);
    } else {
        CGFloat y = EW_ACCOUNT_Y + (_registerMode ? 3 : 2) * EW_ACCOUNT_ROW + 4;
        NSRect link = NSMakeRect(EW_LEFT + 136, y, EW_ACCOUNT_WIDTH - 76, AXYNE_PW_BUTTON_HEIGHT);
        [self drawField:EW_FIELD_EMAIL label:@"이메일"];
        [self drawField:EW_FIELD_NAME label:@"이름"];
        [self drawField:EW_FIELD_PASSWORD label:@"비밀번호"];
        [self button:NSMakeRect(EW_LEFT, y, 120, AXYNE_PW_BUTTON_HEIGHT) label:_registerMode ? @"가입하기" : @"로그인"
              accent:YES action:EW_HIT_SUBMIT index:0];
        [self text:_registerMode ? @"이미 계정이 있나요? 로그인" : @"계정이 없나요? 회원가입" rect:link
              font:[NSFont systemFontOfSize:12] color:AXYNE_PW_COLOR_ACCENT align:NSTextAlignmentLeft];
        [self addHit:link action:EW_HIT_SWITCH_MODE index:0];
        notice = NSMakeRect(EW_LEFT, y + 48, EW_RIGHT - EW_LEFT, 60);
    }
    [self drawNotice:notice];
    [self text:[NSString stringWithFormat:@"서버: %@  (설정 폴더의 server.json에서 변경)", ew_string(_server)]
          rect:NSMakeRect(EW_LEFT, EW_FOOTER_Y - 30, EW_RIGHT - EW_LEFT, 18) font:[NSFont systemFontOfSize:11]
         color:AXYNE_PW_COLOR_MUTED align:NSTextAlignmentLeft];
}

- (char *)fieldText:(EwField)field
{
    const char *text = [[_fields[field] stringValue] UTF8String];
    size_t length = text != NULL ? strlen(text) : 0;
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) { if (length) memcpy(copy, text, length); copy[length] = '\0'; }
    return copy;
}

- (void)busy
{
    [self display];
}

- (void)search
{
    AxyneError error;
    char *query = [self fieldText:EW_FIELD_SEARCH];
    [_notice release];
    _notice = nil;
    [self busy];
    axyne_market_list_clear(&_market);
    if (axyne_market_list(_server, query, "macos", _ideVersion, &_market, &error) != AXYNE_STATUS_OK)
        [self setNotice:@"마켓플레이스를 불러오지 못했습니다." detail:error.message error:YES];
    _marketLoaded = YES;
    _scroll = 0;
    free(query);
    [self setNeedsDisplay:YES];
}

- (void)searchAction:(id)sender { (void)sender; [self search]; }

- (void)marketInstall:(int)index
{
    const AxyneMarketItem *item;
    NSString *zip;
    char *installed = NULL;
    AxyneError error;
    if (index < 0 || (size_t)index >= _market.count) return;
    item = &_market.items[index];
    zip = [NSTemporaryDirectory() stringByAppendingPathComponent:
           [NSString stringWithFormat:@"axyne-%@.zip", ew_string(item->slug)]];
    [self busy];
    if (axyne_market_download(_server, item->slug, "macos", _ideVersion, [zip fileSystemRepresentation], &error)
        != AXYNE_STATUS_OK)
        [self message:@"플러그인을 내려받지 못했습니다." detail:error.message];
    else if (axyne_plugins_install_zip(_plugins, [zip fileSystemRepresentation], _ideVersion, &installed, &error)
             != AXYNE_STATUS_OK)
        [self message:@"플러그인을 설치하지 못했습니다." detail:error.message];
    else
        [self setNotice:@"설치했습니다:" detail:item->name error:NO];
    [[NSFileManager defaultManager] removeItemAtPath:zip error:NULL];
    free(installed);
}

- (void)submitAccount
{
    AxyneError error;
    AxyneStatus status;
    char *email = [self fieldText:EW_FIELD_EMAIL];
    char *name = [self fieldText:EW_FIELD_NAME];
    char *password = [self fieldText:EW_FIELD_PASSWORD];
    [_notice release];
    _notice = nil;
    if (email == NULL || name == NULL || password == NULL) goto done;
    if (email[0] == '\0' || password[0] == '\0' || (_registerMode && name[0] == '\0')) {
        [self setNotice:_registerMode ? @"이메일, 이름, 비밀번호를 입력하세요." : @"이메일과 비밀번호를 입력하세요."
                 detail:NULL error:YES];
        goto done;
    }
    [self busy];
    status = _registerMode ? axyne_account_register(_server, email, name, password, &error) : AXYNE_STATUS_OK;
    if (status == AXYNE_STATUS_OK) status = axyne_account_login(_server, email, password, &_account, &error);
    if (status != AXYNE_STATUS_OK) {
        [self setNotice:_registerMode ? @"가입하지 못했습니다." : @"로그인하지 못했습니다." detail:error.message error:YES];
    } else {
        if (axyne_account_save(_configDir, &_account, &error) != AXYNE_STATUS_OK)
            [self setNotice:@"로그인했지만 계정 정보를 저장하지 못했습니다." detail:error.message error:YES];
        else
            [self setNotice:_registerMode ? @"가입하고 로그인했습니다." : @"로그인했습니다." detail:NULL error:NO];
        _registerMode = NO;
    }
    [_fields[EW_FIELD_PASSWORD] setStringValue:@""];
done:
    if (password != NULL) memset(password, 0, strlen(password));
    free(email); free(name); free(password);
    [self layoutFields];
    [self setNeedsDisplay:YES];
}

- (void)submitAction:(id)sender { (void)sender; [self submitAccount]; }

- (void)logout
{
    [self busy];
    axyne_account_logout(_server, &_account);
    (void)axyne_account_save(_configDir, &_account, NULL);
    [self setNotice:@"로그아웃했습니다." detail:NULL error:NO];
    [self layoutFields];
}

/* Page entry: the marketplace loads on first view. */
- (void)openPage:(int)page
{
    _page = page;
    _scroll = 0;
    [_notice release];
    _notice = nil;
    [self layoutFields];
    [[self window] makeFirstResponder:self];
    [self setNeedsDisplay:YES];
    if (page == AXYNE_EXTENSIONS_MARKETPLACE && !_marketLoaded) [self search];
}

- (void)drawRect:(NSRect)dirty
{
    static NSString *const pages[AXYNE_EXTENSIONS_PAGE_COUNT] = { @"설치됨", @"마켓플레이스", @"계정" };
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
    else if (_page == AXYNE_EXTENSIONS_MARKETPLACE) [self drawMarketplace];
    else [self drawAccount];
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
    case EW_HIT_NAV: [self openPage:hit.index]; break;
    case EW_HIT_SEARCH: [self search]; break;
    case EW_HIT_MARKET_INSTALL: [self marketInstall:hit.index]; break;
    case EW_HIT_SUBMIT: [self submitAccount]; break;
    case EW_HIT_LOGOUT: [self logout]; break;
    case EW_HIT_SWITCH_MODE:
        _registerMode = !_registerMode;
        [_notice release];
        _notice = nil;
        [self layoutFields];
        break;
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
        BOOL inList = _hits[i].action == EW_HIT_TOGGLE || _hits[i].action == EW_HIT_REMOVE ||
                      _hits[i].action == EW_HIT_MARKET_INSTALL;
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
    view->_configDir = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR);
    view->_server = axyne_server_base_url(view->_configDir);
    axyne_account_load(view->_configDir, &view->_account);
    [panel setContentView:view];
    [view createFields];
    if (owner != nil) {
        NSRect bounds = [owner frame];
        [panel setFrameOrigin:NSMakePoint(NSMidX(bounds) - AXYNE_PW_WIDTH / 2, NSMidY(bounds) - AXYNE_PW_HEIGHT / 2)];
    } else {
        [panel center];
    }
    [panel makeFirstResponder:view];
    [panel makeKeyAndOrderFront:nil];
    [view openPage:view->_page];
    [NSApp runModalForWindow:panel];
    [panel orderOut:nil];
    [panel release];
}
