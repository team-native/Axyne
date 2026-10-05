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
static const CGFloat kGpGap = 6;
static const CGFloat kGpPad = 8;
static const CGFloat kGpMessage = 56;     /* commit message box */
static const CGFloat kGpButton = 24;
static const CGFloat kGpPushWidth = 56;
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

static void gp_set_string(char **slot, const char *value)
{
    char *copy = value != NULL ? strdup(value) : NULL;
    free(*slot);
    *slot = copy;
}

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


/* Copy of `in` with every byte that is not part of valid UTF-8 (and every NUL)
 * replaced by '?', so one bad byte in a diff cannot blank the whole output. */
static char *gp_utf8_clean(const char *in, size_t length)
{
    char *out = (char *)malloc(length + 1);
    size_t i = 0, o = 0;
    if (out == NULL) return NULL;
    while (i < length) {
        unsigned char c = (unsigned char)in[i];
        size_t need = 0, k;
        int valid = 0;
        if (c < 0x80) { out[o++] = c != 0 ? (char)c : '?'; ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) need = 2;
        else if (c >= 0xF0 && c <= 0xF4) need = 3;
        if (need != 0 && i + need < length) {
            unsigned char second = (unsigned char)in[i + 1];
            valid = 1;
            for (k = 1; k <= need; ++k)
                if (((unsigned char)in[i + k] & 0xC0) != 0x80) valid = 0;
            if (c == 0xE0 && second < 0xA0) valid = 0;
            if (c == 0xED && second > 0x9F) valid = 0;
            if (c == 0xF0 && second < 0x90) valid = 0;
            if (c == 0xF4 && second > 0x8F) valid = 0;
        }
        if (valid) {
            memcpy(out + o, in + i, need + 1);
            o += need + 1;
            i += need + 1;
        } else {
            out[o++] = '?';
            ++i;
        }
    }
    out[o] = '\0';
    return out;
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

/* One short Git action started by a click. */
typedef enum AxyneGitPanelTaskKind {
    GP_TASK_STAGE = 0,
    GP_TASK_UNSTAGE,
    GP_TASK_FILE_DIFF,
    GP_TASK_COMMIT_DIFF
} AxyneGitPanelTaskKind;

typedef struct AxyneGitPanelTask {
    int kind;
    unsigned long sequence;   /* output requests: newest one wins */
    char *workspace;
    char **paths;             /* stage / unstage */
    size_t pathCount;
    char *text;               /* result for the output panel (valid UTF-8) or NULL */
} AxyneGitPanelTask;

static AxyneGitPanelTask *gp_task_create(int kind, const char *workspace)
{
    AxyneGitPanelTask *task = (AxyneGitPanelTask *)calloc(1, sizeof(*task));
    if (task == NULL) return NULL;
    task->kind = kind;
    task->workspace = strdup(workspace);
    if (task->workspace == NULL) { free(task); return NULL; }
    return task;
}

static void gp_task_free(AxyneGitPanelTask *task)
{
    size_t i;
    if (task == NULL) return;
    for (i = 0; i < task->pathCount; ++i) free(task->paths[i]);
    free(task->paths);
    free(task->text);
    free(task->workspace);
    free(task);
}

/* "<title>: <Git message>\n" for a failed action. */
static char *gp_error_text(const char *title, const AxyneError *error)
{
    char line[768];
    (void)snprintf(line, sizeof(line), "%s: %s\n", title,
                   error->message[0] != '\0' ? error->message : "Git 실행 실패");
    return gp_utf8_clean(line, strlen(line));
}


static void gp_task_run(AxyneGitPanelTask *task)
{
    AxyneError error;
    AxyneStatus status;
    memset(&error, 0, sizeof(error));
    if (task->kind == GP_TASK_STAGE || task->kind == GP_TASK_UNSTAGE) {
        status = task->kind == GP_TASK_STAGE
            ? axyne_git_stage_paths(task->workspace, (const char *const *)task->paths,
                                    task->pathCount, &error)
            : axyne_git_unstage_paths(task->workspace, (const char *const *)task->paths,
                                      task->pathCount, &error);
        if (status != AXYNE_STATUS_OK)
            task->text = gp_error_text(task->kind == GP_TASK_STAGE
                ? "스테이지 실패" : "스테이지 해제 실패", &error);
        return;
    }
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

/* Chip shared with the explorer badges: rounded rect in `rgb` at low alpha (or
 * solid) with a bold label centred in it. */
static void gp_draw_chip(NSString *label, NSRect chip, NSColor *fill, NSColor *textColor,
                         NSFont *font)
{
    NSDictionary *attributes = @{NSFontAttributeName: font,
                                 NSForegroundColorAttributeName: textColor};
    NSSize size = [label sizeWithAttributes:attributes];
    [fill setFill];
    [[NSBezierPath bezierPathWithRoundedRect:chip xRadius:AXYNE_UI_BADGE_RADIUS
                                     yRadius:AXYNE_UI_BADGE_RADIUS] fill];
    [label drawAtPoint:NSMakePoint(NSMinX(chip) + floor((NSWidth(chip) - size.width) / 2),
                                   NSMinY(chip) + floor((NSHeight(chip) - size.height) / 2))
        withAttributes:attributes];
}

/* Badge colour of the Git kind letter (see AxyneGitChange.kind). */
static uint32_t gp_kind_color(char kind)
{
    switch (kind) {
    case 'A': return 0xa3c98a;
    case 'D': return 0xe0707a;
    case 'R': case 'C': return 0x7db5e3;
    case 'U': return 0xe5a445;
    case '?': return 0x8b919b;
    default: return 0xd9b36c; /* M */
    }
}


/* Rectangles of the panel's parts, from the bounds and the change count. */
typedef struct GpGeometry {
    NSRect refresh, changes;
    NSRect message, commit, push;
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
    char *_selectedPath;
    BOOL _stageBusy;
    NSScrollView *_messageScroll;
    NSTextView *_messageView;
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
        _messageView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 100, kGpMessage)];
        [_messageView setMinSize:NSMakeSize(0, kGpMessage)];
        [_messageView setMaxSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
        [_messageView setVerticallyResizable:YES];
        [_messageView setHorizontallyResizable:NO];
        [_messageView setAutoresizingMask:NSViewWidthSizable];
        [[_messageView textContainer] setContainerSize:NSMakeSize(100, CGFLOAT_MAX)];
        [[_messageView textContainer] setWidthTracksTextView:YES];
        [_messageView setRichText:NO];
        [_messageView setAllowsUndo:YES];
        [_messageView setDrawsBackground:NO];
        [_messageView setAutomaticQuoteSubstitutionEnabled:NO];
        [_messageView setAutomaticDashSubstitutionEnabled:NO];
        [_messageView setAutomaticTextReplacementEnabled:NO];
        [_messageView setAutomaticSpellingCorrectionEnabled:NO];
        [_messageView setTextContainerInset:NSMakeSize(3, 4)];
        [_messageView setFont:[NSFont systemFontOfSize:12]];
        [_messageView setDelegate:self];
        [_messageView setAccessibilityLabel:@"커밋 메시지"];
        _messageScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        [_messageScroll setBorderType:NSNoBorder];
        [_messageScroll setHasVerticalScroller:YES];
        [_messageScroll setAutohidesScrollers:YES];
        [_messageScroll setDrawsBackground:NO];
        [_messageScroll setDocumentView:_messageView];
        [_messageScroll setHidden:YES];
        [self addSubview:_messageScroll];
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
    [_messageView setDelegate:nil];
    [_messageScroll removeFromSuperview];
    [_messageView release];
    [_messageScroll release];
    axyne_git_changes_free(&_changes);
    free(_selectedPath);
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
    {
        NSColor *text = gp_color(_theme.text);
        [_messageView setTextColor:text];
        [_messageView setInsertionPointColor:text];
        [_messageView setTypingAttributes:@{NSFontAttributeName: [NSFont systemFontOfSize:12],
                                            NSForegroundColorAttributeName: text}];
    }
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
    CGFloat y;
    memset(&g, 0, sizeof(g));
    fixed += kGpGap + kGpMessage + kGpGap + kGpButton + kGpGap;
    available = MAX(0, height - fixed);
    wanted = (CGFloat)_changes.count * kGpRow;
    /* The change list takes what it needs up to half of the free height, but
     * always room for the empty-state line; the graph gets the rest. */
    limit = MAX(2 * kGpRow, floor(available * 0.5 / kGpRow) * kGpRow);
    listHeight = MIN(MAX(wanted, 2 * kGpRow), MIN(limit, available));
    g.refresh = NSMakeRect(width - 30, 2, 24, 22);
    g.changes = NSMakeRect(0, kGpHeader, width, listHeight);
    y = NSMaxY(g.changes);
    {
        CGFloat inner = MAX(0, width - 2 * kGpPad);
        CGFloat commitWidth = MAX(0, inner - kGpGap - kGpPushWidth);
        g.message = NSMakeRect(kGpPad, y + kGpGap, inner, kGpMessage);
        y = NSMaxY(g.message) + kGpGap;
        g.commit = NSMakeRect(kGpPad, y, commitWidth, kGpButton);
        g.push = NSMakeRect(kGpPad + commitWidth + kGpGap, y, MIN(kGpPushWidth, inner), kGpButton);
        y = NSMaxY(g.commit) + kGpGap;
    }
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
    {
        GpGeometry g = [self geometry];
        BOOL show = _loaded && !_noWorkspace && _notice == nil;
        [_messageScroll setFrame:NSInsetRect(g.message, 1, 1)];
        [_messageScroll setHidden:!show];
    }
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
    gp_set_string(&_selectedPath, NULL);
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
    gp_set_string(&_selectedPath, NULL);
}

- (void)operationFinishedWithSuccessfulCommit:(BOOL)commitSucceeded
{
    if (commitSucceeded) [_messageView setString:@""];
    [self setNeedsDisplay:YES];
    if (![self isHidden]) [self refresh];
}

/* ---- actions --------------------------------------------------------------------- */

/* Runs `task` on a background queue and hands it back to -finishTask:. Takes
 * ownership of `task`. */
- (void)startTask:(AxyneGitPanelTask *)task
{
    AxyneGitPanelLife *life = _life;
    if (life == NULL) { gp_task_free(task); return; }
    ++life->references;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        gp_task_run(task);
        dispatch_async(dispatch_get_main_queue(), ^{
            AxyneGitPanelView *panel = life->panel;
            if (panel != nil) [panel finishTask:task];
            else gp_task_free(task);
            gp_life_release(life);
        });
    });
}

- (void)deliverText:(const char *)text
{
    NSString *string = text != NULL ? [NSString stringWithUTF8String:text] : nil;
    if (string != nil) [_delegate gitPanel:self showText:string];
}

/* Main thread; takes ownership of `task`. */
- (void)finishTask:(AxyneGitPanelTask *)task
{
    if (task->kind == GP_TASK_STAGE || task->kind == GP_TASK_UNSTAGE) {
        _stageBusy = NO;
        [self deliverText:task->text];
        [self refresh];
    }
    gp_task_free(task);
}

- (void)toggleStageForChange:(const AxyneGitChange *)change
{
    BOOL unstage = change->staged != 0;
    const char *workspace;
    AxyneGitPanelTask *task;
    if (_stageBusy || [_delegate gitPanelBusy:self]) return;
    workspace = [_delegate gitPanelWorkspace:self];
    if (workspace == NULL) return;
    task = gp_task_create(unstage ? GP_TASK_UNSTAGE : GP_TASK_STAGE, workspace);
    if (task == NULL) return;
    task->paths = (char **)calloc(2, sizeof(char *));
    if (task->paths != NULL) {
        task->paths[0] = strdup(change->path);
        if (task->paths[0] != NULL) task->pathCount = 1;
        /* A staged rename is unstaged completely with both of its paths. */
        if (task->pathCount == 1 && unstage && change->orig_path != NULL) {
            task->paths[1] = strdup(change->orig_path);
            if (task->paths[1] != NULL) task->pathCount = 2;
        }
    }
    if (task->pathCount == 0) { gp_task_free(task); return; }
    _stageBusy = YES;
    [self startTask:task];
}



- (BOOL)canCommit
{
    NSString *trimmed = [[_messageView string] stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return _hasStaged != 0 && [trimmed length] > 0 && ![_delegate gitPanelBusy:self];
}

- (void)textDidChange:(NSNotification *)notification
{
    (void)notification;
    [self setNeedsDisplay:YES];
}

/* ---- mouse ------------------------------------------------------------------------- */

- (void)mouseDown:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    GpGeometry g;
    NSInteger row;
    [[self window] makeFirstResponder:self];
    if (_noWorkspace) { [_delegate gitPanelOpenWorkspace:self]; return; }
    if (!_loaded) return;
    g = [self geometry];
    if (NSPointInRect(point, g.refresh)) { [self refresh]; return; }
    if (_notice != nil) return;
    if (NSPointInRect(point, g.changes)) {
        row = (NSInteger)floor((point.y - NSMinY(g.changes) + _changesScroll) / kGpRow);
        if (row < 0 || (size_t)row >= _changes.count) return;
        if (point.x < 28) { [self toggleStageForChange:&_changes.items[row]]; return; }
        gp_set_string(&_selectedPath, _changes.items[row].path);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, g.commit)) {
        if ([self canCommit]) [_delegate gitPanel:self commitMessage:[_messageView string]];
        return;
    }
    if (NSPointInRect(point, g.push)) {
        if (![_delegate gitPanelBusy:self]) [_delegate gitPanelPush:self];
        return;
    }
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
    [self drawChangesInRect:g.changes];
    [self drawMessageAreaWithGeometry:&g];
}

- (void)drawChangesInRect:(NSRect)rect
{
    size_t first, i;
    CGFloat y;
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(rect);
    if (_changes.count == 0) {
        gp_draw_text(@"변경 사항 없음", NSMakeRect(16, NSMinY(rect), NSWidth(rect) - 24, kGpRow),
                     [NSFont systemFontOfSize:12], gp_color(_theme.muted), _leftStyle);
    } else {
        first = (size_t)floor(_changesScroll / kGpRow);
        for (i = first; i < _changes.count; ++i) {
            y = NSMinY(rect) + (CGFloat)i * kGpRow - _changesScroll;
            if (y >= NSMaxY(rect)) break;
            [self drawChange:&_changes.items[i]
                      inRect:NSMakeRect(0, y, NSWidth(rect), kGpRow)];
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}

/* Checkbox (staged / indeterminate / empty), kind chip, file name and the dim
 * directory of one changed file. */
- (void)drawChange:(const AxyneGitChange *)change inRect:(NSRect)row
{
    BOOL selected = _selectedPath != NULL && strcmp(_selectedPath, change->path) == 0;
    NSRect box = NSMakeRect(NSMinX(row) + 10, NSMinY(row) + floor((kGpRow - 12) / 2), 12, 12);
    NSBezierPath *outline = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(box, 0.5, 0.5)
                                                             xRadius:3 yRadius:3];
    NSRect chip = NSMakeRect(NSMinX(row) + 28, NSMinY(row) + floor((kGpRow - 14) / 2), 14, 14);
    NSString *kind = [NSString stringWithFormat:@"%c", change->kind];
    uint32_t kindRgb = gp_kind_color(change->kind);
    const char *slash = strrchr(change->path, '/');
    NSString *name = gp_string(slash != NULL ? slash + 1 : change->path);
    NSFont *nameFont = [NSFont systemFontOfSize:12];
    NSFont *directoryFont = [NSFont systemFontOfSize:11];
    CGFloat nameX = NSMinX(row) + 48;
    CGFloat available = MAX(0, NSWidth(row) - nameX - 8);
    CGFloat nameWidth;
    if (selected) {
        [gp_color(_theme.reference ? 0x2f343c : _theme.border) setFill];
        NSRectFill(row);
    }
    if (change->staged || change->partially) {
        [gp_color(_theme.accent) setFill];
        [outline fill];
        [gp_color(_theme.background) setStroke];
        {
            NSBezierPath *mark = [NSBezierPath bezierPath];
            if (change->staged) {
                [mark moveToPoint:NSMakePoint(NSMinX(box) + 3, NSMinY(box) + 6.5)];
                [mark lineToPoint:NSMakePoint(NSMinX(box) + 5.2, NSMinY(box) + 8.7)];
                [mark lineToPoint:NSMakePoint(NSMinX(box) + 9, NSMinY(box) + 3.8)];
            } else {
                [mark moveToPoint:NSMakePoint(NSMinX(box) + 3, NSMidY(box))];
                [mark lineToPoint:NSMakePoint(NSMaxX(box) - 3, NSMidY(box))];
            }
            [mark setLineWidth:1.6];
            [mark setLineCapStyle:NSLineCapStyleRound];
            [mark setLineJoinStyle:NSLineJoinStyleRound];
            [mark stroke];
        }
    } else {
        [[gp_color(_theme.muted) colorWithAlphaComponent:0.8] setStroke];
        [outline setLineWidth:1];
        [outline stroke];
    }
    gp_draw_chip(kind, chip, [gp_color(kindRgb) colorWithAlphaComponent:
        AXYNE_UI_BADGE_ALPHA_PERCENT / 100.0], gp_color(kindRgb),
        [NSFont monospacedSystemFontOfSize:AXYNE_UI_BADGE_FONT_PT weight:NSFontWeightBold]);
    nameWidth = ceil([name sizeWithAttributes:@{NSFontAttributeName: nameFont}].width);
    gp_draw_text(name, NSMakeRect(nameX, NSMinY(row), MIN(available, nameWidth), kGpRow),
                 nameFont, selected ? gp_color(_theme.text)
                     : gp_color(_theme.light ? 0x24272d : 0xc4c8ce), _leftStyle);
    if (slash != NULL && available - nameWidth > 24) {
        char *directoryText = (char *)malloc((size_t)(slash - change->path) + 1);
        if (directoryText != NULL) {
            memcpy(directoryText, change->path, (size_t)(slash - change->path));
            directoryText[slash - change->path] = '\0';
            gp_draw_text(gp_string(directoryText),
                         NSMakeRect(nameX + nameWidth + 6, NSMinY(row),
                                    available - nameWidth - 6, kGpRow),
                         directoryFont, gp_color(_theme.muted), _leftStyle);
            free(directoryText);
        }
    }
}


/* Message box border, placeholder and the two buttons. The text view itself
 * is a subview drawn over the box. */
- (void)drawMessageAreaWithGeometry:(const GpGeometry *)g
{
    NSBezierPath *box = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(g->message, 0.5, 0.5)
                                                         xRadius:4 yRadius:4];
    [gp_color(_theme.background) setFill];
    [box fill];
    [gp_color(_theme.reference ? 0x3a3d44 : _theme.border) setStroke];
    [box setLineWidth:1];
    [box stroke];
    if ([[_messageView string] length] == 0)
        gp_draw_text(@"메시지 (스테이지된 변경 사항을 커밋)",
                     NSMakeRect(NSMinX(g->message) + 8, NSMinY(g->message) + 4,
                                NSWidth(g->message) - 16, 16),
                     [NSFont systemFontOfSize:12], gp_color(_theme.muted), _leftStyle);
    [self drawButton:g->commit title:@"커밋" enabled:[self canCommit] primary:YES];
    [self drawButton:g->push title:@"푸시" enabled:![_delegate gitPanelBusy:self] primary:NO];
}

- (void)drawButton:(NSRect)rect title:(NSString *)title enabled:(BOOL)enabled
           primary:(BOOL)primary
{
    NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(rect, 0.5, 0.5)
                                                           xRadius:4 yRadius:4];
    NSColor *fill, *label;
    if (primary) {
        fill = gp_color(enabled ? _theme.accent : _theme.border);
        label = gp_color(enabled ? _theme.background : _theme.muted);
    } else {
        fill = gp_color(_theme.toolbar);
        label = gp_color(enabled ? _theme.text : _theme.muted);
    }
    [fill setFill];
    [shape fill];
    if (!primary) {
        [gp_color(_theme.reference ? 0x3a3d44 : _theme.border) setStroke];
        [shape setLineWidth:1];
        [shape stroke];
    }
    gp_draw_text(title, rect, primary ? [NSFont boldSystemFontOfSize:12]
                                      : [NSFont systemFontOfSize:12],
                 label, _centerStyle);
}


@end
