#import "git_panel_macos.h"

#include <dispatch/dispatch.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/git_panel.h"
#include "axyne/git_graph_geometry.h"
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
static const CGFloat kGpGraphRow = 38;    /* two text lines per commit */
static const CGFloat kGpLane = 10;        /* width of one graph lane cell */
static const CGFloat kGpTextGap = 6;      /* graph mark to row text */
static const int kGpGraphStep = 200;      /* commits added per "더 불러오기" */
static const unsigned kGpRefreshDelayMs = 500;

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

/* Growable byte buffer for the text sent to the output panel. */
typedef struct GpBuf {
    char *data;
    size_t length, capacity;
    int failed;
} GpBuf;

static void gp_buf_append(GpBuf *buf, const char *text, size_t length)
{
    if (buf->failed || length == 0) return;
    if (buf->length + length + 1 > buf->capacity) {
        size_t wanted = buf->capacity == 0 ? 1024 : buf->capacity;
        char *grown;
        while (wanted < buf->length + length + 1) wanted *= 2;
        grown = (char *)realloc(buf->data, wanted);
        if (grown == NULL) { buf->failed = 1; return; }
        buf->data = grown;
        buf->capacity = wanted;
    }
    memcpy(buf->data + buf->length, text, length);
    buf->length += length;
    buf->data[buf->length] = '\0';
}

static void gp_buf_puts(GpBuf *buf, const char *text)
{
    if (text != NULL) gp_buf_append(buf, text, strlen(text));
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
    AxyneGitGraph graph;
    int graphCount;           /* commits to read (set on the main thread) */
    AxyneGitGraphMode graphMode;
    int graphOk;              /* the graph could be read (zero commits is fine) */
    char graphMessage[160];   /* graph failures are independent of changes */
} AxyneGitPanelLoad;

static void gp_load_free(AxyneGitPanelLoad *load)
{
    if (load == NULL) return;
    axyne_git_changes_free(&load->changes);
    axyne_git_graph_free(&load->graph);
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
    if (axyne_git_graph_with_mode(load->workspace, load->graphCount, load->graphMode,
                        &load->graph, &error) == AXYNE_STATUS_OK)
        load->graphOk = 1;
    else {
        gp_first_line(error.message, load->graphMessage, sizeof(load->graphMessage));
        axyne_git_graph_free(&load->graph);
    }
}

/* One short Git action started by a click. */
typedef enum AxyneGitPanelTaskKind {
    GP_TASK_STAGE = 0,
    GP_TASK_UNSTAGE,
    GP_TASK_FILE_DIFF,
    GP_TASK_COMMIT_DIFF,   /* one file of a commit */
    GP_TASK_COMMIT_FILES   /* the files a commit changed (inline list) */
} AxyneGitPanelTaskKind;

typedef struct AxyneGitPanelTask {
    int kind;
    unsigned long sequence;   /* diff requests: newest one wins */
    unsigned long expandSequence; /* file-list requests: newest one wins */
    unsigned long generation; /* workspace generation when it was started */
    char *workspace;
    char **paths;             /* stage / unstage */
    size_t pathCount;
    char *path, *origPath;    /* file diff / commit file diff */
    int staged;
    char *hash;               /* commit file diff / commit file list */
    char *title;              /* diff: title of the editor tab */
    AxyneGitChanges files;    /* commit file list result */
    int filesOk;
    char message[160];        /* first line of Git's error for the file list */
    char *text;               /* result: diff text (valid UTF-8), stage error or NULL */
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
    free(task->path); free(task->origPath);
    free(task->hash); free(task->title);
    axyne_git_changes_free(&task->files);
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

static void gp_task_finish_text(AxyneGitPanelTask *task, GpBuf *buf)
{
    if (!buf->failed && buf->data != NULL)
        task->text = gp_utf8_clean(buf->data, buf->length);
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

/* Unified diff text of a file (working tree / index) or of one file of a
 * commit, for the read-only editor tab. A failure becomes the text so the
 * user sees Git's own message. */
static void gp_task_run_diff(AxyneGitPanelTask *task)
{
    AxyneGitDiff diff;
    AxyneError error;
    AxyneStatus status;
    GpBuf buf;
    memset(&diff, 0, sizeof(diff));
    memset(&error, 0, sizeof(error));
    memset(&buf, 0, sizeof(buf));
    status = task->kind == GP_TASK_FILE_DIFF
        ? axyne_git_file_diff(task->workspace, task->path, task->origPath,
                              task->staged, &diff, &error)
        : axyne_git_commit_diff(task->workspace, task->hash, task->path, &diff, &error);
    if (status != AXYNE_STATUS_OK) {
        gp_buf_puts(&buf, error.message[0] != '\0' ? error.message : "Git diff 실행 실패");
        gp_buf_puts(&buf, "\n");
    } else if (diff.length == 0 || diff.text == NULL) {
        gp_buf_puts(&buf, "(변경 내용 없음)\n");
    } else {
        gp_buf_append(&buf, diff.text, diff.length);
        if (diff.text[diff.length - 1] != '\n') gp_buf_puts(&buf, "\n");
        if (diff.truncated) gp_buf_puts(&buf, "\n[diff가 1 MiB에서 잘렸습니다]\n");
    }
    axyne_git_diff_free(&diff);
    gp_task_finish_text(task, &buf);
}

static void gp_task_run_commit_files(AxyneGitPanelTask *task)
{
    AxyneError error;
    memset(&error, 0, sizeof(error));
    if (axyne_git_commit_files(task->workspace, task->hash, &task->files, &error) ==
        AXYNE_STATUS_OK)
        task->filesOk = 1;
    else
        gp_first_line(error.message, task->message, sizeof(task->message));
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
    if (task->kind == GP_TASK_FILE_DIFF || task->kind == GP_TASK_COMMIT_DIFF)
        gp_task_run_diff(task);
    else if (task->kind == GP_TASK_COMMIT_FILES)
        gp_task_run_commit_files(task);
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
    NSString *text;
    if (utf8 == NULL) return @"";
    text = [NSString stringWithUTF8String:utf8];
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

/* Lane colours, indexed by AxyneGitGraphLane.color (0..7). */
static const uint32_t kGpLanePalette[AXYNE_GIT_GRAPH_PALETTE] = {
    0x7db5e3, 0xc79ad9, 0xa3c98a, 0xd9b36c, 0xd98e73, 0x8cc7c0, 0xb48ae0, 0xe5a445
};

static uint32_t gp_ref_color(const AxyneGitRef *ref)
{
    switch (ref->kind) {
    case AXYNE_GIT_REF_LOCAL_BRANCH: return 0x7db5e3;
    case AXYNE_GIT_REF_REMOTE_BRANCH: return 0xc79ad9;
    case AXYNE_GIT_REF_TAG: return 0xd9b36c;
    case AXYNE_GIT_REF_HEAD: return 0xe5a445;
    default: return 0x8b919b;
    }
}

/* Rectangles of the panel's parts, from the bounds and the change count. */
typedef struct GpGeometry {
    NSRect refresh, changes;
    NSRect message, commit, push;
    NSRect graphHeader, graph;
} GpGeometry;

/* The graph list is a sequence of virtual items: each commit is one item
 * (kGpGraphRow tall); the expanded commit is followed by one item per changed
 * file, or a single note item while loading, on failure or when the commit
 * changed no files (kGpRow tall). While more history may exist the last item
 * is the "더 불러오기" row (kGpRow tall). Scrolling (in pixels), hit testing
 * and drawing all walk this sequence. */
typedef enum GpItemKind { GP_ITEM_COMMIT = 0, GP_ITEM_FILE, GP_ITEM_NOTE,
                          GP_ITEM_MORE } GpItemKind;

typedef struct GpItem {
    int kind;
    size_t commit; /* index into the graph (the owning commit for sub items) */
    size_t file;   /* index into the expanded file list for GP_ITEM_FILE */
} GpItem;

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
    AxyneGitGraph _graph;
    char _graphError[160];
    CGFloat _graphScroll;
    int _graphLimit;              /* commits requested by reloads; 0 = default */
    AxyneGitGraphMode _graphMode;
    NSSegmentedControl *_graphSelector;
    int _graphLoadedLimit;        /* the request the shown graph answered (0 = none) */
    BOOL _graphLoadingMore;       /* a larger graph was requested and has not arrived */
    char *_selectedHash;
    unsigned long _outputSequence;
    /* The commit whose changed files are listed inline below its row (only
     * one at a time). _expandedState: 0 none, 1 loading, 2 ready, 3 failed. */
    char *_expandedHash;
    size_t _expandedRow;
    AxyneGitChanges _expandedFiles;
    int _expandedState;
    char _expandedError[160];
    unsigned long _expandSequence;
    char *_selectedCommitFile;    /* selected file row under the expanded commit */
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
        _graphMode = AXYNE_GIT_GRAPH_COMPACT;
        _graphSelector = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
        [_graphSelector setSegmentCount:2];
        [_graphSelector setLabel:@"Compact" forSegment:0];
        [_graphSelector setLabel:@"Full" forSegment:1];
        [_graphSelector setSelectedSegment:0];
        [_graphSelector setControlSize:NSControlSizeSmall];
        [_graphSelector setTarget:self];
        [_graphSelector setAction:@selector(graphModeChanged:)];
        [_graphSelector setAccessibilityLabel:@"Commit history: Compact (HEAD first-parent) / Full (all branches)"];
        [_graphSelector setToolTip:@"Compact: HEAD first-parent history. Full: all branches and merge edges."];
        [self addSubview:_graphSelector];
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(applicationActivated:)
            name:NSApplicationDidBecomeActiveNotification object:nil];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_graphSelector release];
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
    axyne_git_graph_free(&_graph);
    free(_selectedHash);
    free(_selectedPath);
    free(_selectedCommitFile);
    free(_expandedHash);
    axyne_git_changes_free(&_expandedFiles);
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
    fixed += kGpHeader + 18;
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
    g.graphHeader = NSMakeRect(0, y, width, kGpHeader + 18);
    g.graph = NSMakeRect(0, NSMaxY(g.graphHeader), width,
                         MAX(0, height - NSMaxY(g.graphHeader)));
    return g;
}

/* Keeps scroll offsets inside the lists after the data or the size changed. */
- (void)clampScrolls
{
    GpGeometry g = [self geometry];
    _changesScroll = gp_clamp(_changesScroll, 0,
        MAX(0, (CGFloat)_changes.count * kGpRow - NSHeight(g.changes)));
    _graphScroll = gp_clamp(_graphScroll, 0,
        MAX(0, [self graphContentHeight] - NSHeight(g.graph)));
}

/* ---- graph items (commits and the expanded commit's files) ---------------------- */

- (size_t)expandedExtra
{
    if (_expandedHash == NULL || _expandedRow >= _graph.count) return 0;
    if (_expandedState == 2 && _expandedFiles.count > 0) return _expandedFiles.count;
    return 1;
}

/* The "더 불러오기" row: the shown graph filled its whole request (fewer rows
 * means the history is complete) and the cap is not reached; it stays, dimmed,
 * while loading. */
- (BOOL)moreVisible
{
    if (_graph.count == 0) return NO;
    if (_graphLoadingMore) return YES;
    return _graphLoadedLimit > 0 && _graph.count >= (size_t)_graphLoadedLimit &&
           _graphLoadedLimit < AXYNE_GIT_GRAPH_MAX_COUNT;
}

- (int)graphRequest
{
    return _graphLimit > 0 ? _graphLimit : AXYNE_GIT_GRAPH_DEFAULT_COUNT;
}

- (size_t)itemCount
{
    return _graph.count + [self expandedExtra] + ([self moreVisible] ? 1 : 0);
}

- (GpItem)itemAtIndex:(size_t)i
{
    GpItem item;
    size_t extra = [self expandedExtra];
    memset(&item, 0, sizeof(item));
    if (i >= _graph.count + extra) {
        item.kind = GP_ITEM_MORE;
        item.commit = _graph.count > 0 ? _graph.count - 1 : 0;
    } else if (extra == 0 || i <= _expandedRow) {
        item.kind = GP_ITEM_COMMIT;
        item.commit = i;
    } else if (i <= _expandedRow + extra) {
        item.commit = _expandedRow;
        if (_expandedState == 2 && _expandedFiles.count > 0) {
            item.kind = GP_ITEM_FILE;
            item.file = i - _expandedRow - 1;
        } else {
            item.kind = GP_ITEM_NOTE;
        }
    } else {
        item.kind = GP_ITEM_COMMIT;
        item.commit = i - extra;
    }
    return item;
}

- (CGFloat)heightOfItem:(const GpItem *)item
{
    return item->kind == GP_ITEM_COMMIT ? kGpGraphRow : kGpRow;
}

- (CGFloat)graphContentHeight
{
    return (CGFloat)_graph.count * kGpGraphRow + (CGFloat)[self expandedExtra] * kGpRow +
           ([self moreVisible] ? kGpRow : 0);
}

/* The item under `offset` pixels from the top of the (unscrolled) graph
 * content; NO past the last item. */
- (BOOL)itemAtOffset:(CGFloat)offset item:(GpItem *)out
{
    size_t count = [self itemCount], i;
    CGFloat top = 0;
    if (offset < 0) return NO;
    for (i = 0; i < count; ++i) {
        GpItem item = [self itemAtIndex:i];
        CGFloat height = [self heightOfItem:&item];
        if (offset < top + height) { *out = item; return YES; }
        top += height;
    }
    return NO;
}

/* Collapses the inline file list and invalidates a file-list request that is
 * still running. */
- (void)clearExpansion
{
    free(_expandedHash);
    _expandedHash = NULL;
    axyne_git_changes_free(&_expandedFiles);
    _expandedState = 0;
    _expandedError[0] = '\0';
    _expandedRow = 0;
    free(_selectedCommitFile);
    _selectedCommitFile = NULL;
    ++_expandSequence;
}

/* Re-finds the expanded commit in a freshly loaded graph; collapses when it
 * is gone. */
- (void)syncExpansion
{
    size_t i;
    if (_expandedHash == NULL) return;
    for (i = 0; i < _graph.count; ++i)
        if (_graph.rows[i].hash != NULL && strcmp(_graph.rows[i].hash, _expandedHash) == 0) {
            _expandedRow = i;
            return;
        }
    [self clearExpansion];
}

- (void)layoutParts
{
    [self clampScrolls];
    {
        GpGeometry g = [self geometry];
        BOOL show = _loaded && !_noWorkspace && _notice == nil;
        [_messageScroll setFrame:NSInsetRect(g.message, 1, 1)];
        [_messageScroll setHidden:!show];
        CGFloat selectorWidth = MIN(134, MAX(0, NSWidth([self bounds]) - 16));
        [_graphSelector setFrame:NSMakeRect(MAX(0, NSWidth([self bounds]) - selectorWidth - 8),
                                            NSMinY(g.graphHeader) + 2, selectorWidth, 22)];
        [_graphSelector setHidden:!show];
    }
}

/* ---- loading ------------------------------------------------------------------ */

- (void)applicationActivated:(NSNotification *)notification
{
    (void)notification;
    if (![self isHidden]) [self scheduleRefresh];
}

- (void)graphModeChanged:(id)sender
{
    (void)sender;
    AxyneGitGraphMode mode = [_graphSelector selectedSegment] == 0
        ? AXYNE_GIT_GRAPH_COMPACT : AXYNE_GIT_GRAPH_FULL;
    if (mode == _graphMode) return;
    _graphMode = mode;
    ++_generation;
    [self clearResults];
    [self clearExpansion];
    _graphLimit = 0;
    _graphScroll = 0;
    gp_set_string(&_selectedHash, NULL);
    [self refresh];
    [self setNeedsDisplay:YES];
}

/* "더 불러오기": raises the requested commit count by one step and reloads on
 * the background queue; new rows only append below the old ones, so the scroll
 * position and the expanded commit are kept. */
- (void)loadMoreGraph
{
    int current = [self graphRequest];
    int next = current + kGpGraphStep;
    if (_graphLoadingMore || ![self moreVisible]) return;
    if (next > AXYNE_GIT_GRAPH_MAX_COUNT) next = AXYNE_GIT_GRAPH_MAX_COUNT;
    if (next <= current) return;
    _graphLimit = next;
    _graphLoadingMore = YES;
    [self refresh];
    if (!_loading && !_refreshAgain) { /* no reload could be started */
        _graphLimit = current;
        _graphLoadingMore = NO;
    }
    [self setNeedsDisplay:YES];
}

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
    axyne_git_graph_free(&_graph);
    _graphLoadedLimit = 0;
    _graphLoadingMore = NO;
    _graphError[0] = '\0';
}

/* Scroll positions survive reloads; they restart only for another folder, an
 * unloaded panel or an empty state. */
- (void)resetScrolls
{
    _changesScroll = 0;
    _graphScroll = 0;
}

- (void)refresh
{
    const char *workspace;
    AxyneGitPanelLoad *load;
    AxyneGitPanelLife *life = _life;
    if (life == NULL || [self isHidden]) return;
    if (_loading) { _refreshAgain = YES; return; }
    workspace = [_delegate gitPanelWorkspace:self];
    if (workspace == NULL || workspace[0] == '\0') {
        [self clearResults];
        [self clearExpansion];
        [self resetScrolls];
        _graphLimit = 0;
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
    load->graphCount = [self graphRequest];
    load->graphMode = _graphMode;
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
    BOOL current = load->generation == _generation &&
                   load->graphMode == _graphMode && ![self isHidden];
    _loading = NO;
    if (current) {
        BOOL requested = load->graphCount == [self graphRequest];
        axyne_git_changes_free(&_changes);
        _hasStaged = 0;
        if (load->changesStatus != AXYNE_STATUS_OK) {
            axyne_git_graph_free(&_graph);
            _graphLoadedLimit = 0;
            _graphLoadingMore = NO;
        }
        [_notice release];
        _notice = nil;
        if (load->changesStatus == AXYNE_STATUS_OK) {
            _changes = load->changes;
            memset(&load->changes, 0, sizeof(load->changes));
            _hasStaged = load->hasStaged;
            if (!load->graphOk && _graphLoadingMore && _graph.count > 0) {
                /* Loading more failed: keep the graph that is shown. */
                if (requested) _graphLimit = _graphLoadedLimit;
            } else {
                axyne_git_graph_free(&_graph);
                _graph = load->graph; /* moved, never copied */
                memset(&load->graph, 0, sizeof(load->graph));
                _graphLoadedLimit = load->graphOk ? load->graphCount : 0;
                snprintf(_graphError, sizeof(_graphError), "%s",
                         load->graphOk ? "" : load->graphMessage);
            }
            if (requested && !load->graphOk)
                [self deliverText:load->graphMessage[0] ? load->graphMessage
                                                       : "Unable to load commit graph."];
            /* An older load finishing while a larger request is queued keeps
             * the loading state until that request arrives. */
            if (requested) _graphLoadingMore = NO;
            [self syncExpansion];
        } else if (load->changesStatus == AXYNE_STATUS_IO_ERROR) {
            [self clearExpansion];
            _notice = [@"Git 저장소가 아닙니다." retain];
        } else if (load->changesStatus == AXYNE_STATUS_NOT_FOUND) {
            [self clearExpansion];
            _notice = [@"Git을 찾을 수 없습니다." retain];
        } else {
            [self clearExpansion];
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
    [self clearExpansion];
    [self resetScrolls];
    _graphLimit = 0; /* a new workspace starts at the default count again */
    gp_set_string(&_selectedPath, NULL);
    gp_set_string(&_selectedHash, NULL);
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
    [self clearExpansion];
    [self resetScrolls];
    [_notice release];
    _notice = nil;
    _loaded = NO;
    _noWorkspace = NO;
    [self layoutParts]; /* hides the commit message box */
    gp_set_string(&_selectedPath, NULL);
    gp_set_string(&_selectedHash, NULL);
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

/* Opens the diff as a read-only tab in the editor area. */
- (void)deliverDiffTitle:(const char *)title text:(const char *)text
{
    NSString *titleString = title != NULL ? [NSString stringWithUTF8String:title] : nil;
    NSString *textString = text != NULL ? [NSString stringWithUTF8String:text] : nil;
    if (titleString != nil && textString != nil)
        [_delegate gitPanel:self openDiffTitle:titleString text:textString];
}

/* Main thread; takes ownership of `task`. */
- (void)finishTask:(AxyneGitPanelTask *)task
{
    if (task->kind == GP_TASK_STAGE || task->kind == GP_TASK_UNSTAGE) {
        _stageBusy = NO;
        [self deliverText:task->text];
        if (![self isHidden]) [self refresh];
    } else if (task->kind == GP_TASK_COMMIT_FILES) {
        if (task->generation == _generation && task->expandSequence == _expandSequence &&
            _expandedHash != NULL && task->hash != NULL &&
            strcmp(_expandedHash, task->hash) == 0) {
            axyne_git_changes_free(&_expandedFiles);
            if (task->filesOk) {
                _expandedFiles = task->files;
                memset(&task->files, 0, sizeof(task->files));
                _expandedState = 2;
            } else {
                _expandedState = 3;
                (void)snprintf(_expandedError, sizeof(_expandedError), "%s",
                    task->message[0] != '\0' ? task->message : "Git 실행 실패");
            }
            [self clampScrolls];
            [self setNeedsDisplay:YES];
        }
    } else if (task->sequence == _outputSequence && task->generation == _generation) {
        [self deliverDiffTitle:task->title text:task->text];
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


/* "<prefix><file name>" for the title of a diff tab (malloc'ed or NULL). */
static char *gp_diff_title(const char *prefix, const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash != NULL ? slash + 1 : path;
    size_t size = strlen(prefix) + strlen(base) + 1;
    char *title = (char *)malloc(size);
    if (title != NULL) (void)snprintf(title, size, "%s%s", prefix, base);
    return title;
}

/* A clicked changed file opens its diff in the editor area. */
- (void)showDiffForChange:(const AxyneGitChange *)change
{
    const char *workspace = [_delegate gitPanelWorkspace:self];
    AxyneGitPanelTask *task;
    if (workspace == NULL) return;
    task = gp_task_create(GP_TASK_FILE_DIFF, workspace);
    if (task == NULL) return;
    task->path = strdup(change->path);
    task->origPath = change->orig_path != NULL ? strdup(change->orig_path) : NULL;
    task->staged = change->staged;
    task->title = gp_diff_title("변경: ", change->path);
    if (task->path == NULL || task->title == NULL ||
        (change->orig_path != NULL && task->origPath == NULL)) {
        gp_task_free(task);
        return;
    }
    task->sequence = ++_outputSequence;
    task->generation = _generation;
    [self startTask:task];
}

/* A clicked file under the expanded commit opens that file's commit diff. */
- (void)showCommitFileDiff:(const AxyneGitChange *)file
{
    const char *workspace = [_delegate gitPanelWorkspace:self];
    AxyneGitPanelTask *task;
    char prefix[16];
    if (workspace == NULL || _expandedHash == NULL) return;
    task = gp_task_create(GP_TASK_COMMIT_DIFF, workspace);
    if (task == NULL) return;
    (void)snprintf(prefix, sizeof(prefix), "%.7s: ", _expandedHash);
    task->hash = strdup(_expandedHash);
    task->path = strdup(file->path);
    task->title = gp_diff_title(prefix, file->path);
    if (task->hash == NULL || task->path == NULL || task->title == NULL) {
        gp_task_free(task);
        return;
    }
    task->sequence = ++_outputSequence;
    task->generation = _generation;
    [self startTask:task];
}

/* A clicked commit expands the list of files it changed right below its row
 * (one commit at a time); clicking the expanded commit collapses it. */
- (void)toggleCommitAtRow:(size_t)row
{
    const char *hash;
    const char *workspace = [_delegate gitPanelWorkspace:self];
    BOOL collapse;
    if (row >= _graph.count || _graph.rows[row].hash == NULL) return;
    hash = _graph.rows[row].hash;
    collapse = _expandedHash != NULL && strcmp(_expandedHash, hash) == 0;
    gp_set_string(&_selectedPath, NULL);
    gp_set_string(&_selectedHash, hash);
    [self clearExpansion];
    if (!collapse && workspace != NULL) {
        AxyneGitPanelTask *task = gp_task_create(GP_TASK_COMMIT_FILES, workspace);
        _expandedHash = strdup(hash);
        _expandedRow = row;
        _expandedState = 1;
        if (task != NULL && _expandedHash != NULL) {
            task->hash = strdup(hash);
            task->expandSequence = _expandSequence;
            task->generation = _generation;
            if (task->hash != NULL) {
                [self startTask:task];
            } else {
                gp_task_free(task);
                _expandedState = 3;
                (void)snprintf(_expandedError, sizeof(_expandedError), "%s", "Git 실행 실패");
            }
        } else {
            gp_task_free(task);
            [self clearExpansion];
        }
    }
    [self clampScrolls];
    [self setNeedsDisplay:YES];
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
        gp_set_string(&_selectedHash, NULL);
        gp_set_string(&_selectedCommitFile, NULL);
        [self setNeedsDisplay:YES];
        [self showDiffForChange:&_changes.items[row]];
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
    if (NSPointInRect(point, g.graph)) {
        GpItem item;
        if (![self itemAtOffset:point.y - NSMinY(g.graph) + _graphScroll item:&item]) return;
        if (item.kind == GP_ITEM_COMMIT) {
            [self toggleCommitAtRow:item.commit];
        } else if (item.kind == GP_ITEM_MORE) {
            [self loadMoreGraph];
        } else if (item.kind == GP_ITEM_FILE) {
            gp_set_string(&_selectedPath, NULL);
            gp_set_string(&_selectedHash, NULL);
            gp_set_string(&_selectedCommitFile, _expandedFiles.items[item.file].path);
            [self setNeedsDisplay:YES];
            [self showCommitFileDiff:&_expandedFiles.items[item.file]];
        }
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
    } else if (NSPointInRect(point, g.graph)) {
        _graphScroll -= delta;
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
    [gp_color(_theme.border) setFill];
    NSRectFill(NSMakeRect(0, NSMinY(g.graphHeader), NSWidth(bounds), 1));
    gp_draw_text(@"그래프", NSMakeRect(12, NSMinY(g.graphHeader),
                                      MAX(0, NSMinX([_graphSelector frame]) - 16), kGpHeader),
                 small, sectionColor, _leftStyle);
    gp_draw_text(_graphMode == AXYNE_GIT_GRAPH_COMPACT
                     ? @"HEAD first-parent only · side history omitted"
                     : @"All branches · full merge ancestry",
                 NSMakeRect(12, NSMinY(g.graphHeader) + kGpHeader, NSWidth(bounds) - 24, 18),
                 [NSFont systemFontOfSize:10], sectionColor, _leftStyle);
    [self drawGraphInRect:g.graph];
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

- (void)drawGraphInRect:(NSRect)rect
{
    size_t count, i;
    CGFloat y;
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(rect);
    if (_graph.count == 0) {
        gp_draw_text(_graphError[0] ? gp_string(_graphError) : @"커밋이 없습니다",
                     NSMakeRect(16, NSMinY(rect), NSWidth(rect) - 24, kGpRow),
                     [NSFont systemFontOfSize:12], gp_color(_theme.muted), _leftStyle);
    } else {
        count = [self itemCount];
        y = NSMinY(rect) - _graphScroll;
        for (i = 0; i < count; ++i) {
            GpItem item = [self itemAtIndex:i];
            CGFloat height = [self heightOfItem:&item];
            if (y >= NSMaxY(rect)) break;
            if (y + height > NSMinY(rect)) {
                NSRect row = NSMakeRect(0, y, NSWidth(rect), height);
                if (item.kind == GP_ITEM_COMMIT)
                    [self drawGraphRow:&_graph.rows[item.commit] inRect:row];
                else if (item.kind == GP_ITEM_MORE)
                    gp_draw_text(_graphLoadingMore ? @"더 불러오는 중…" : @"더 불러오기",
                        NSMakeRect(10 + MAX(_graph.max_lanes, 1) * axyne_git_graph_lane_width(_graph.max_lanes, kGpLane, 100) + 8,
                                   y, MAX(0, NSWidth(rect) - 16 -
                                       (10 + MAX(_graph.max_lanes, 1) * axyne_git_graph_lane_width(_graph.max_lanes, kGpLane, 100) + 8)),
                                   kGpRow),
                        [NSFont systemFontOfSize:11],
                        gp_color(_graphLoadingMore ? _theme.muted : _theme.accent), _leftStyle);
                else
                    [self drawCommitFileItem:&item inRect:row];
            }
            y += height;
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}

/* A file row (kind chip, name, dim directory) or a note row (loading, failure,
 * no files) under the expanded commit. The lanes of that commit that continue
 * below it run through the row as plain vertical lines, so the lane strip
 * stays continuous: a lane continues when its cell leaves through the bottom
 * edge (DOWN, or the FORK connector that ends there). */
- (void)drawCommitFileItem:(const GpItem *)item inRect:(NSRect)rect
{
    const AxyneGitGraphRow *commit = &_graph.rows[item->commit];
    const AxyneGitChange *change = item->kind == GP_ITEM_FILE
        ? &_expandedFiles.items[item->file] : NULL;
    BOOL selected = change != NULL && _selectedCommitFile != NULL &&
        strcmp(_selectedCommitFile, change->path) == 0;
    int cells = MAX(_graph.max_lanes, 1);
    CGFloat laneWidth = axyne_git_graph_lane_width(cells, kGpLane, 100);
    CGFloat left = 10;
    /* Text follows the rightmost lane continuing through this row. */
    CGFloat textX = axyne_git_graph_text_x(left, laneWidth,
        axyne_git_graph_last_cell(commit, AXYNE_GIT_LANE_DOWN | AXYNE_GIT_LANE_FORK, 0),
        0.75, kGpTextGap);
    CGFloat textRight = NSMaxX(rect) - 8;
    int i;
    if (selected) {
        [gp_color(_theme.reference ? 0x2f343c : _theme.border) setFill];
        NSRectFill(rect);
    }
    for (i = 0; i < commit->lane_count && i < cells; ++i) {
        unsigned flags = commit->lanes[i].flags;
        CGFloat cx = left + i * laneWidth + laneWidth / 2 + 0.5;
        NSBezierPath *path;
        if ((flags & (AXYNE_GIT_LANE_DOWN | AXYNE_GIT_LANE_FORK)) == 0) continue;
        path = [NSBezierPath bezierPath];
        [path setLineWidth:1.5];
        [path moveToPoint:NSMakePoint(cx, NSMinY(rect))];
        [path lineToPoint:NSMakePoint(cx, NSMaxY(rect))];
        [gp_color(kGpLanePalette[commit->lanes[i].color % AXYNE_GIT_GRAPH_PALETTE]) setStroke];
        [path stroke];
    }
    if (change == NULL) {
        NSString *note = _expandedState == 1 ? @"불러오는 중…"
            : (_expandedState == 3 ? gp_string(_expandedError) : @"변경된 파일 없음");
        gp_draw_text(note, NSMakeRect(textX, NSMinY(rect), MAX(0, textRight - textX), kGpRow),
                     [NSFont systemFontOfSize:11], gp_color(_theme.muted), _leftStyle);
        return;
    }
    {
        NSRect chip = NSMakeRect(textX, NSMinY(rect) + floor((kGpRow - 14) / 2), 14, 14);
        char letter = change->kind != '\0' ? change->kind : '?';
        NSString *kind = [NSString stringWithFormat:@"%c", letter];
        uint32_t kindRgb = gp_kind_color(letter);
        const char *slash = strrchr(change->path, '/');
        NSString *name = gp_string(slash != NULL ? slash + 1 : change->path);
        NSFont *nameFont = [NSFont systemFontOfSize:12];
        NSFont *directoryFont = [NSFont systemFontOfSize:11];
        CGFloat nameX = textX + 20;
        CGFloat available = MAX(0, textRight - nameX);
        CGFloat nameWidth;
        gp_draw_chip(kind, chip, [gp_color(kindRgb) colorWithAlphaComponent:
            AXYNE_UI_BADGE_ALPHA_PERCENT / 100.0], gp_color(kindRgb),
            [NSFont monospacedSystemFontOfSize:AXYNE_UI_BADGE_FONT_PT weight:NSFontWeightBold]);
        nameWidth = ceil([name sizeWithAttributes:@{NSFontAttributeName: nameFont}].width);
        gp_draw_text(name, NSMakeRect(nameX, NSMinY(rect), MIN(available, nameWidth), kGpRow),
                     nameFont, selected ? gp_color(_theme.text)
                         : gp_color(_theme.light ? 0x24272d : 0xc4c8ce), _leftStyle);
        if (slash != NULL && available - nameWidth > 24) {
            char *directoryText = (char *)malloc((size_t)(slash - change->path) + 1);
            if (directoryText != NULL) {
                memcpy(directoryText, change->path, (size_t)(slash - change->path));
                directoryText[slash - change->path] = '\0';
                gp_draw_text(gp_string(directoryText),
                             NSMakeRect(nameX + nameWidth + 6, NSMinY(rect),
                                        available - nameWidth - 6, kGpRow),
                             directoryFont, gp_color(_theme.muted), _leftStyle);
                free(directoryText);
            }
        }
    }
}

/* Lane cells (vertical lines, join/fork curves, the commit dot) followed by
 * the ref chips, subject and the dim "author · date" line. */
- (void)drawGraphRow:(const AxyneGitGraphRow *)row inRect:(NSRect)rect
{
    BOOL selected = _selectedHash != NULL && strcmp(_selectedHash, row->hash) == 0;
    int cells = MAX(_graph.max_lanes, 1);
    CGFloat laneWidth = axyne_git_graph_lane_width(cells, kGpLane, 100);
    CGFloat left = 10;
    CGFloat top = NSMinY(rect), bottom = NSMaxY(rect), middle = floor(top + NSHeight(rect) / 2) + 0.5;
    CGFloat textX;
    CGFloat textRight = NSMaxX(rect) - 8;
    CGFloat dotX;
    BOOL current = NO;
    int last = axyne_git_graph_last_cell(row, ~0u, 1);
    NSFont *subjectFont = [NSFont systemFontOfSize:12];
    NSFont *detailFont = [NSFont systemFontOfSize:11];
    NSColor *textColor = selected ? gp_color(_theme.text)
        : gp_color(_theme.light ? 0x24272d : 0xc4c8ce);
    CGFloat chipX;
    size_t shown = 0, r;
    int i;
    if (selected) {
        [gp_color(_theme.reference ? 0x2f343c : _theme.border) setFill];
        NSRectFill(rect);
    }
    dotX = left + row->column * laneWidth + laneWidth / 2 + 0.5;
    for (i = 0; i < row->lane_count && i < cells; ++i) {
        unsigned flags = row->lanes[i].flags;
        CGFloat cx = left + i * laneWidth + laneWidth / 2 + 0.5;
        NSColor *color = gp_color(kGpLanePalette[row->lanes[i].color % AXYNE_GIT_GRAPH_PALETTE]);
        BOOL up, down;
        NSBezierPath *path;
        if (flags == 0) continue;
        up = (flags & AXYNE_GIT_LANE_UP) != 0 && (flags & AXYNE_GIT_LANE_JOIN) == 0;
        down = (flags & AXYNE_GIT_LANE_DOWN) != 0 &&
            ((flags & AXYNE_GIT_LANE_FORK) == 0 || (flags & AXYNE_GIT_LANE_UP) != 0);
        path = [NSBezierPath bezierPath];
        [path setLineWidth:1.5];
        if (up) { [path moveToPoint:NSMakePoint(cx, top)]; [path lineToPoint:NSMakePoint(cx, middle)]; }
        if (down) { [path moveToPoint:NSMakePoint(cx, middle)]; [path lineToPoint:NSMakePoint(cx, bottom)]; }
        if ((flags & AXYNE_GIT_LANE_JOIN) != 0) {
            [path moveToPoint:NSMakePoint(cx, top)];
            [path curveToPoint:NSMakePoint(dotX, middle)
                 controlPoint1:NSMakePoint(cx, middle) controlPoint2:NSMakePoint(cx, middle)];
        }
        if ((flags & AXYNE_GIT_LANE_FORK) != 0) {
            [path moveToPoint:NSMakePoint(dotX, middle)];
            [path curveToPoint:NSMakePoint(cx, bottom)
                 controlPoint1:NSMakePoint(cx, middle) controlPoint2:NSMakePoint(cx, middle)];
        }
        [color setStroke];
        [path stroke];
    }
    for (r = 0; r < row->ref_count; ++r)
        if (row->refs[r].is_current) current = YES;
    /* Text follows this row's rightmost dot or stroke: the dot (ring on HEAD)
     * reaches 3.5 (5.75) pt past its centre, a 1.5pt stroke 0.75 pt. */
    textX = axyne_git_graph_text_x(left, laneWidth, last,
                                   last == row->column ? (current ? 5.75 : 3.5) : 0.75,
                                   kGpTextGap);
    {
        NSColor *dotColor = gp_color(kGpLanePalette[row->color % AXYNE_GIT_GRAPH_PALETTE]);
        if (current) {
            /* HEAD: ring with a smaller solid centre. */
            NSBezierPath *ring = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(dotX - 5, middle - 5, 10, 10)];
            [ring setLineWidth:1.5];
            [dotColor setStroke];
            [ring stroke];
            [dotColor setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(dotX - 2.5, middle - 2.5, 5, 5)] fill];
        } else {
            [dotColor setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(dotX - 3.5, middle - 3.5, 7, 7)] fill];
        }
    }
    /* Line 1: ref chips (the current branch solid), then the subject. */
    chipX = textX;
    for (r = 0; r < row->ref_count; ++r) {
        const AxyneGitRef *ref = &row->refs[r];
        NSString *label = gp_string(ref->name);
        NSFont *chipFont = [NSFont systemFontOfSize:10 weight:NSFontWeightSemibold];
        CGFloat width = ceil([label sizeWithAttributes:@{NSFontAttributeName: chipFont}].width) + 10;
        uint32_t rgb = gp_ref_color(ref);
        NSRect chip;
        if (shown == 3 || chipX + width > textRight - 40) {
            if (chipX + 20 <= textRight - 20) {
                NSString *more = [NSString stringWithFormat:@"+%zu", row->ref_count - r];
                gp_draw_text(more, NSMakeRect(chipX, top + 3, 24, 16), chipFont,
                             gp_color(_theme.muted), _leftStyle);
                chipX += 26;
            }
            break;
        }
        chip = NSMakeRect(chipX, top + 5, width, 14);
        gp_draw_chip(label, chip, ref->is_current ? gp_color(_theme.accent)
                         : [gp_color(rgb) colorWithAlphaComponent:0.2],
                     ref->is_current ? gp_color(_theme.background) : gp_color(rgb), chipFont);
        chipX += width + 4;
        ++shown;
    }
    gp_draw_text(gp_string(row->subject), NSMakeRect(chipX, top + 3, MAX(0, textRight - chipX), 16),
                 subjectFont, textColor, _leftStyle);
    /* Line 2: author and date, dim. */
    gp_draw_text([NSString stringWithFormat:@"%@  ·  %@", gp_string(row->author),
                     gp_string(row->date)],
                 NSMakeRect(textX, top + 20, MAX(0, textRight - textX), 14),
                 detailFont, gp_color(_theme.muted), _leftStyle);
}

@end
