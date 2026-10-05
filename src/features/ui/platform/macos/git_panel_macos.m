#import "git_panel_macos.h"

#include <dispatch/dispatch.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/git_panel.h"
#include "axyne/ui_design.h"

/* Layout metrics, logical pixels. The sidebar column is flipped (y grows
 * downwards) like AxyneWorkspaceView, so rows are laid out top to bottom. */
static const CGFloat kGpHeader = 26;      /* section title row */
static const CGFloat kGpRow = AXYNE_UI_ROW;
static const unsigned kGpRefreshDelayMs = 300;

/* ---- lifetime box ----------------------------------------------------------
 * Background blocks must never keep the view alive (the editor runtime test
 * releases the workspace without draining the main queue and then asserts that
 * every Scintilla document was released). They therefore capture only this
 * heap box, whose `panel` is assigned, never retained, and cleared in -dealloc.
 * The box is counted by the view and by each queued block; every count change
 * happens on the main thread, so no locking is needed. */
typedef struct AxyneGitPanelLife {
    id panel;
    unsigned references;
} AxyneGitPanelLife;

static AxyneGitPanelLife *gp_life_create(id panel)
{
    AxyneGitPanelLife *life = (AxyneGitPanelLife *)calloc(1, sizeof(*life));
    if (life != NULL) { life->panel = panel; life->references = 1; }
    return life;
}

static void gp_life_release(AxyneGitPanelLife *life)
{
    if (life != NULL && --life->references == 0) free(life);
}

/* ---- small C helpers ------------------------------------------------------- */


static CGFloat gp_clamp(CGFloat value, CGFloat low, CGFloat high)
{
    return value < low ? low : (value > high ? high : value);
}

/* First line of `text` into `out`, truncated to `capacity`. */
static void gp_first_line(const char *text, char *out, size_t capacity)
{
    size_t i = 0;
    if (capacity == 0) return;
    while (text != NULL && text[i] != '\0' && text[i] != '\n' && text[i] != '\r' &&
           i + 1 < capacity) { out[i] = text[i]; ++i; }
    out[i] = '\0';
}



/* ---- background work ------------------------------------------------------- */

/* Result of one reload. Allocated on the main thread, filled by the worker,
 * consumed (or freed) on the main thread. */
typedef struct AxyneGitPanelLoad {
    unsigned long generation;
    char *workspace;
    AxyneGitChanges changes;
    AxyneStatus changesStatus;
    char message[160];        /* first line of Git's error, when it failed */
    int hasStaged;
} AxyneGitPanelLoad;

static void gp_load_free(AxyneGitPanelLoad *load)
{
    if (load == NULL) return;
    axyne_git_changes_free(&load->changes);
    free(load->workspace);
    free(load);
}

static void gp_load_run(AxyneGitPanelLoad *load)
{
    AxyneError error;
    memset(&error, 0, sizeof(error));
    load->changesStatus = axyne_git_changes(load->workspace, &load->changes, &error);
    if (load->changesStatus != AXYNE_STATUS_OK) {
        gp_first_line(error.message, load->message, sizeof(load->message));
        return;
    }
    if (axyne_git_has_staged(load->workspace, &load->hasStaged, &error) != AXYNE_STATUS_OK)
        load->hasStaged = 0;
}


/* ---- drawing helpers ------------------------------------------------------- */

static NSColor *gp_color(uint32_t rgb)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:1.0];
}

static NSString *gp_string(const char *utf8)
{
    NSString *text = utf8 != NULL ? [NSString stringWithUTF8String:utf8] : nil;
    return text != nil ? text : @"(이름 오류)";
}

/* One line of text vertically centred in `rect`; a paragraph style with tail
 * truncation shortens it to the rect's width. */
static void gp_draw_text(NSString *text, NSRect rect, NSFont *font, NSColor *color,
                         NSParagraphStyle *style)
{
    NSDictionary *attributes;
    NSSize size;
    if (text == nil || NSWidth(rect) <= 0) return;
    attributes = @{NSFontAttributeName: font, NSForegroundColorAttributeName: color,
                   NSParagraphStyleAttributeName: style};
    size = [text sizeWithAttributes:attributes];
    [text drawInRect:NSMakeRect(NSMinX(rect),
            NSMinY(rect) + floor((NSHeight(rect) - size.height) / 2),
            NSWidth(rect), size.height)
        withAttributes:attributes];
}



/* Rectangles of the panel's parts, from the bounds and the change count. */
typedef struct GpGeometry {
    NSRect refresh, changes;
} GpGeometry;

@interface AxyneGitPanelView () <NSTextViewDelegate>
{
    AxyneGitPanelTheme _theme;
    BOOL _hasTheme;
    AxyneGitPanelLife *_life;
    BOOL _loading;
    BOOL _loaded;
    BOOL _refreshAgain;
    BOOL _refreshScheduled;
    BOOL _noWorkspace;
    unsigned long _generation;
    AxyneGitChanges _changes;
    int _hasStaged;
    NSString *_notice;            /* why there is nothing to show, or nil */
    NSMutableParagraphStyle *_leftStyle;
    NSMutableParagraphStyle *_centerStyle;
    CGFloat _changesScroll;
}
@end

@implementation AxyneGitPanelView

@synthesize delegate = _delegate;

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) {
        _life = gp_life_create(self);
        if (_life == NULL) { [self release]; return nil; }
        _leftStyle = [[NSMutableParagraphStyle alloc] init];
        [_leftStyle setLineBreakMode:NSLineBreakByTruncatingTail];
        _centerStyle = [[NSMutableParagraphStyle alloc] init];
        [_centerStyle setLineBreakMode:NSLineBreakByTruncatingTail];
        [_centerStyle setAlignment:NSTextAlignmentCenter];
        [self setAccessibilityElement:YES];
        [self setAccessibilityRole:NSAccessibilityGroupRole];
        [self setAccessibilityLabel:@"Git 패널"];
    }
    return self;
}

- (void)dealloc
{
    if (_life != NULL) {
        _life->panel = nil;
        gp_life_release(_life);
        _life = NULL;
    }
    axyne_git_changes_free(&_changes);
    [_notice release];
    [_leftStyle release];
    [_centerStyle release];
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }
- (NSFocusRingType)focusRingType { return NSFocusRingTypeNone; }
/* Typing on the lists does nothing (no beep); menu key equivalents are
 * resolved before keyDown: and keep working. */
- (void)keyDown:(NSEvent *)event { (void)event; }
/* The explorer's context menu must not appear over the Git tab. */
- (void)rightMouseDown:(NSEvent *)event { (void)event; }

- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    [self layoutParts];
}

/* ---- theme ------------------------------------------------------------------ */

- (void)setTheme:(const AxyneGitPanelTheme *)theme
{
    if (theme == NULL) return;
    if (_hasTheme && memcmp(&_theme, theme, sizeof(_theme)) == 0) return;
    _theme = *theme;
    _hasTheme = YES;
    [self setNeedsDisplay:YES];
}

/* ---- geometry ----------------------------------------------------------------- */

- (GpGeometry)geometry
{
    GpGeometry g;
    NSRect bounds = [self bounds];
    CGFloat width = NSWidth(bounds), height = NSHeight(bounds);
    CGFloat fixed = kGpHeader;
    CGFloat available, wanted, limit, listHeight;
    memset(&g, 0, sizeof(g));
    available = MAX(0, height - fixed);
    wanted = (CGFloat)_changes.count * kGpRow;
    /* The change list takes what it needs up to half of the free height, but
     * always room for the empty-state line; the graph gets the rest. */
    limit = MAX(2 * kGpRow, floor(available * 0.5 / kGpRow) * kGpRow);
    listHeight = MIN(MAX(wanted, 2 * kGpRow), MIN(limit, available));
    g.refresh = NSMakeRect(width - 30, 2, 24, 22);
    g.changes = NSMakeRect(0, kGpHeader, width, listHeight);
    return g;
}

/* Keeps scroll offsets inside the lists after the data or the size changed. */
- (void)clampScrolls
{
    GpGeometry g = [self geometry];
    _changesScroll = gp_clamp(_changesScroll, 0,
        MAX(0, (CGFloat)_changes.count * kGpRow - NSHeight(g.changes)));
}

- (void)layoutParts
{
    [self clampScrolls];
}

/* ---- loading ------------------------------------------------------------------ */

- (void)scheduleRefresh
{
    AxyneGitPanelLife *life = _life;
    if (_refreshScheduled || life == NULL) return;
    _refreshScheduled = YES;
    ++life->references;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 (int64_t)kGpRefreshDelayMs * (int64_t)NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
        AxyneGitPanelView *panel = life->panel;
        if (panel != nil) [panel scheduledRefreshFired];
        gp_life_release(life);
    });
}

- (void)scheduledRefreshFired
{
    _refreshScheduled = NO;
    if (![self isHidden]) [self refresh];
}

- (void)clearResults
{
    axyne_git_changes_free(&_changes);
    _hasStaged = 0;
}

/* Scroll positions survive reloads; they restart only for another folder, an
 * unloaded panel or an empty state. */
- (void)resetScrolls
{
    _changesScroll = 0;
}

- (void)refresh
{
    const char *workspace;
    AxyneGitPanelLoad *load;
    AxyneGitPanelLife *life = _life;
    if (life == NULL) return;
    if (_loading) { _refreshAgain = YES; return; }
    workspace = [_delegate gitPanelWorkspace:self];
    if (workspace == NULL || workspace[0] == '\0') {
        [self clearResults];
        [self resetScrolls];
        [_notice release];
        _notice = nil;
        _noWorkspace = YES;
        _loaded = YES;
        [self layoutParts];
        [self setNeedsDisplay:YES];
        return;
    }
    load = (AxyneGitPanelLoad *)calloc(1, sizeof(*load));
    if (load != NULL) load->workspace = strdup(workspace);
    if (load == NULL || load->workspace == NULL) { gp_load_free(load); return; }
    load->generation = _generation;
    _noWorkspace = NO;
    _loading = YES;
    ++life->references;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        gp_load_run(load);
        dispatch_async(dispatch_get_main_queue(), ^{
            AxyneGitPanelView *panel = life->panel;
            if (panel != nil) [panel finishLoad:load];
            else gp_load_free(load);
            gp_life_release(life);
        });
    });
}

/* Main thread; takes ownership of `load`. */
- (void)finishLoad:(AxyneGitPanelLoad *)load
{
    BOOL current = load->generation == _generation;
    _loading = NO;
    if (current) {
        [self clearResults];
        [_notice release];
        _notice = nil;
        if (load->changesStatus == AXYNE_STATUS_OK) {
            _changes = load->changes;
            memset(&load->changes, 0, sizeof(load->changes));
            _hasStaged = load->hasStaged;
        } else if (load->changesStatus == AXYNE_STATUS_IO_ERROR) {
            _notice = [@"Git 저장소가 아닙니다." retain];
        } else if (load->changesStatus == AXYNE_STATUS_NOT_FOUND) {
            _notice = [@"Git을 찾을 수 없습니다." retain];
        } else {
            NSString *detail = load->message[0] != '\0'
                ? [NSString stringWithUTF8String:load->message] : nil;
            _notice = [(detail != nil ? detail : @"Git을 실행할 수 없습니다.") retain];
        }
        _loaded = YES;
    }
    gp_load_free(load);
    if (current) {
        [self layoutParts];
        [self setNeedsDisplay:YES];
    }
    if ((_refreshAgain || !current) && ![self isHidden]) {
        _refreshAgain = NO;
        [self refresh];
    } else {
        _refreshAgain = NO;
    }
}

- (void)workspaceChanged
{
    ++_generation;
    [self clearResults];
    [self resetScrolls];
    [_notice release];
    _notice = nil;
    _loaded = NO;
    [self layoutParts];
    [self setNeedsDisplay:YES];
    if (![self isHidden]) [self refresh];
}

- (void)unload
{
    if (!_loaded && !_loading) return;
    ++_generation;
    [self clearResults];
    [self resetScrolls];
    [_notice release];
    _notice = nil;
    _loaded = NO;
    _noWorkspace = NO;
}


/* ---- actions --------------------------------------------------------------------- */




/* ---- mouse ------------------------------------------------------------------------- */

- (void)mouseDown:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    GpGeometry g;
    [[self window] makeFirstResponder:self];
    if (_noWorkspace) { [_delegate gitPanelOpenWorkspace:self]; return; }
    if (!_loaded) return;
    g = [self geometry];
    if (NSPointInRect(point, g.refresh)) { [self refresh]; return; }
    if (_notice != nil) return;
}

- (void)scrollWheel:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    GpGeometry g = [self geometry];
    CGFloat delta = [event scrollingDeltaY];
    if (![event hasPreciseScrollingDeltas]) delta *= kGpRow;
    if (NSPointInRect(point, g.changes)) {
        _changesScroll -= delta;
    } else {
        [super scrollWheel:event];
        return;
    }
    [self clampScrolls];
    [self setNeedsDisplay:YES];
}

/* ---- drawing ------------------------------------------------------------------------- */

- (void)drawRect:(NSRect)dirtyRect
{
    NSRect bounds = [self bounds];
    NSColor *muted = gp_color(_theme.muted);
    NSColor *sectionColor = gp_color(_theme.light ? 0x68707d : 0x8b919b);
    NSFont *small = [NSFont systemFontOfSize:11];
    NSFont *normal = [NSFont systemFontOfSize:12];
    GpGeometry g;
    (void)dirtyRect;
    if (!_hasTheme) return;
    [gp_color(_theme.panel) setFill];
    NSRectFill(bounds);
    if (_noWorkspace) {
        /* Same wording and place as the explorer's empty state. */
        gp_draw_text(@"폴더 열기…", NSMakeRect(16, 4, NSWidth(bounds) - 24, kGpRow),
                     normal, gp_color(_theme.text), _leftStyle);
        return;
    }
    if (!_loaded) return;
    g = [self geometry];
    /* refresh glyph */
    gp_draw_text(@"↻", g.refresh, [NSFont systemFontOfSize:14], muted, _centerStyle);
    if (_notice != nil) {
        gp_draw_text(_notice, NSMakeRect(16, kGpHeader + 4, NSWidth(bounds) - 24, kGpRow),
                     normal, muted, _leftStyle);
        return;
    }
    gp_draw_text([NSString stringWithFormat:@"변경 사항  %zu", _changes.count],
                 NSMakeRect(12, 0, MAX(0, NSMinX(g.refresh) - 12), kGpHeader),
                 small, sectionColor, _leftStyle);
}




@end
