#ifndef AXYNE_UI_MACOS_INTERNAL_H
#define AXYNE_UI_MACOS_INTERNAL_H

/* Internal interface of the macOS workspace window, shared by ui_macos.m and
 * the per-feature category files (ui_<feature>_macos.m). Objective-C with
 * MANUAL retain/release. Everything here is private to the macOS adapter. */

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
#include "axyne/empty_state.h"
#include "axyne/search.h"
#include "axyne/explorer.h"
#include "axyne/outline.h"
#include "axyne/watcher.h"
#include "axyne/process.h"
#include "axyne/runner.h"
#include "axyne/debugger.h"
#include "axyne/preferences.h"
#include "axyne/git.h"
#include "axyne/lsp.h"
#include "axyne/problems.h"
#include "axyne/problems_feed.h"
#include "axyne/palette_controller.h"
#include "axyne/language.h"
#include "axyne/build_selector.h"
#include "axyne/ui_design.h"
#include "axyne/popup_menu_layout.h"
#include "popup_menu_macos.h"
#include "git_panel_macos.h"
#include "axyne/layout_metrics.h"
#include "axyne/syntax.h"
#include "axyne/completion.h"
#include "Scintilla.h"
#include "../../editor_document.h"
#include "../../editor_actions.h"
#include "../../debugger_actions.h"
#include "../../preferences_window.h"
#include "../../app_dialogs.h"
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

/* Top-level main-menu entries that the in-window bar mirrors carry
 * AXYNE_MACOS_MENU_TAG_BASE + their bar position as the NSMenuItem tag
 * (the application menu keeps tag 0 and has no bar item). */
enum { AXYNE_MACOS_MENU_TAG_BASE = 100 };

@class AxyneWorkspaceView;
@class AxyneEmptyEditorView;
@class AxynePaletteOverlay;
typedef struct AxyneMacGitRun AxyneMacGitRun;
typedef struct AxyneMacGitCompletion AxyneMacGitCompletion;
typedef struct AxyneMacGitBatch AxyneMacGitBatch;

/* Native buttons keep AppKit's actions and accessibility while drawing the
 * flat, precisely centered Figma toolbar rather than an OS bezel. */
@interface AxyneChromeButton : NSButton
@property(nonatomic, retain) NSColor *fillColor;
@property(nonatomic, retain) NSColor *labelColor;
@property(nonatomic, retain) NSColor *strokeColor;
@property(nonatomic, retain) NSAttributedString *richTitle;
@property(nonatomic, retain) NSAttributedString *trailingTitle;
@property(nonatomic) CGFloat cornerRadius;
@property(nonatomic) NSInteger chevronState; /* 0 none, 1 down, 2 up */
@property(nonatomic) CGFloat contentInset;
@property(nonatomic) BOOL leadingAligned;
@end

static inline NSColor *axyne_color(CGFloat red, CGFloat green, CGFloat blue)
{
    return [NSColor colorWithCalibratedRed:red / 255.0
                                     green:green / 255.0
                                      blue:blue / 255.0
                                     alpha:1.0];
}

@protocol AxynePaletteOwner <NSObject>
- (void)paletteActivateRow:(size_t)row;
- (void)paletteChipClicked:(NSInteger)mode;
- (void)paletteDismiss;
/* YES when the point belongs to the toolbar search field (not an outside click). */
- (BOOL)paletteClaimsPoint:(NSPoint)point;
@end

@class AxyneProblemsView;

@protocol AxyneProblemsViewDelegate <NSObject>
- (void)problemsView:(AxyneProblemsView *)view activateRowAtIndex:(size_t)index;
- (void)problemsViewFilterChanged:(AxyneProblemsView *)view;
- (void)problemsViewWillHitTest:(AxyneProblemsView *)view;
@end

/* The deferred discovery block must not retain the workspace: a retained view
 * would never deallocate when the main queue is not drained (the editor runtime
 * test), leaking its Scintilla documents. The block holds only this box. */
typedef struct AxyneDiscoveryBox { id target; } AxyneDiscoveryBox;

@interface AxyneWorkspaceView : NSView <NSMenuItemValidation, AxyneProblemsViewDelegate> {
    NSView *_editorView;
    AxyneEmptyEditorView *_emptyView;
    NSImageView *_imagePreview;
    BOOL _emptyShown;
    BOOL _emptyGuideDirty;
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
    /* Problems panel: the model (LSP diagnostics + build output) and the
     * list view that paints the rows built from it. */
    AxyneProblemsView *_problemsView;
    AxyneProblemList _problems;
    AxyneProblemCollapsed _problemsCollapsed;
    AxyneProblemCounts _problemCounts;
    AxyneBuildFeed _buildFeed;
    BOOL _buildFeedActive;
    NSInteger _panelMode;
    NSInteger _activeMenuIndex; /* bar item whose popup is open, or -1 */
    NSInteger _hoverMenuIndex;  /* bar item under the pointer, or -1 */
    NSTrackingArea *_menuTracking;
    AxynePopupMenu *_popup; /* open Axyne popup (menu bar, build target, context), or nil */
    NSInteger _explorerFirstRow;
    /* Outline (개요) section at the bottom of the explorer tab. Scanned only
     * while that tab is shown; otherwise marked stale and rescanned when the
     * tab appears. Edits rescan after a debounce (assign-only box). */
    AxyneOutline _outline;
    AxyneDiscoveryBox *_outlineBox;
    NSTimeInterval _outlineDue; /* reference-date seconds of the next rescan */
    BOOL _outlineStale;
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
    /* Build target selector. Lives in memory for the session; runtimes are
     * discovered lazily (first editor file, click or build), never at startup. */
    AxyneBuildTarget _buildTarget;
    AxyneRuntimeList _runtimes;
    BOOL _runtimesDiscovered;
    BOOL _runtimeDiscoveryScheduled;
    BOOL _workspaceRefreshPending; /* one coalesced explorer reload is queued */
    BOOL _buildMenuOpen; /* chevron points up while the popup is open */
    AxyneDiscoveryBox *_discoveryBox;
    AxyneDiscoveryBox *_refreshBox; /* assign-only target of the queued reload */
    AxyneDiscoveryBox *_problemsBox; /* assign-only target of the queued problems rebuild */
    /* Plan whose run step starts when its build step exits with 0. */
    AxyneLanguagePlan _pendingPlan;
    BOOL _pendingRun;
    AxyneDebugger _debugger;
    AxyneProcess *_terminalProcess;
    AxyneProcess *_gitProcess;
    AxyneMacGitRun *_gitRun;
    /* A commit, push or pull is running on a worker thread. */
    BOOL _gitBatchBusy;
    /* The running batch is a commit started from the Git panel. */
    BOOL _gitBatchFromPanel;
    /* Left sidebar tab: 0 explorer, 1 Git. The Git panel is created the first
     * time its tab is shown and freed with the view. */
    NSInteger _sidebarTab;
    AxyneGitPanelView *_gitPanel;
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
    CGFloat _sidebarSize; /* dragged explorer width; 0 = default, session only */
    CGFloat _panelSize;   /* dragged bottom panel height; 0 = default */
    NSInteger _splitterDrag; /* 0 none, 1 explorer border, 2 panel border */
    CGFloat _splitterStart;     /* pointer coordinate when the drag began */
    CGFloat _splitterStartSize; /* size when the drag began */
    NSTrackingArea *_sidebarSplitterTracking;
    NSTrackingArea *_panelSplitterTracking;
    AxynePaletteController _palette;
    AxynePaletteOverlay *_paletteOverlay;
    NSTextField *_paletteField;
    NSTimer *_paletteTimer;
}
@end

static inline NSColor *axyne_preference_color(uint32_t value)
{
    return axyne_color((CGFloat)((value >> 16) & 0xff),
                       (CGFloat)((value >> 8) & 0xff),
                       (CGFloat)(value & 0xff));
}

static inline intptr_t axyne_editor_color(uint32_t rgb)
{
    return (intptr_t)(((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff));
}

static inline BOOL axyne_macos_prefers_dark(NSView *view)
{
    NSAppearance *appearance = [view effectiveAppearance];
    NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:
        @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    return [match isEqualToString:NSAppearanceNameDarkAqua];
}

/* Perceived luminance below mid-grey: scrollers use the dark appearance. */
static inline BOOL axyne_macos_color_is_dark(uint32_t rgb)
{
    uint32_t red = (rgb >> 16) & 0xffu, green = (rgb >> 8) & 0xffu, blue = rgb & 0xffu;
    return (red * 299u + green * 587u + blue * 114u) / 1000u < 128u;
}

static inline BOOL axyne_macos_reference_surfaces(const AxyneThemePreferences *theme)
{
    return theme->background == 0x16171a && theme->panel == 0x1f2126 &&
        theme->toolbar == 0x1c1e22;
}

/* Popup menu colours: the Figma menu frames on the reference theme (the same
 * values as the Windows popup), the theme's own colours otherwise. */
static inline AxynePopupMenuColors axyne_macos_popup_colors(const AxyneThemePreferences *theme)
{
    AxynePopupMenuColors colors;
    if (axyne_macos_reference_surfaces(theme)) {
        colors.background = 0x202329; colors.border = 0x2b2e35;
        colors.hover = 0x402d5c; colors.hoverText = 0xf4edf9;
        colors.text = 0xd2d5db; colors.muted = 0x969ba5;
        colors.disabled = 0x666c76; colors.separator = 0x2b2e35;
        colors.check = 0xa667e8;
    } else {
        colors.background = theme->panel; colors.border = theme->border;
        colors.hover = theme->border; colors.hoverText = theme->text;
        colors.text = theme->text; colors.muted = theme->muted;
        colors.disabled = theme->muted; colors.separator = theme->border;
        colors.check = theme->accent;
    }
    return colors;
}

static inline uint32_t axyne_macos_output_background(const AxyneThemePreferences *theme)
{
    return axyne_macos_reference_surfaces(theme) ? 0x1d1f23 : theme->background;
}

/* Methods implemented in ui_macos.m (@implementation AxyneWorkspaceView). */
@interface AxyneWorkspaceView (AxyneActions) <AxynePopupMenuDelegate, AxyneGitPanelDelegate>
- (AxyneDocument *)activeDocument;
- (BOOL)imagePreviewShown;
- (AxyneLanguageId)activeLanguage;
- (NSResponder *)externalTextResponder;
- (BOOL)captureEditorSnapshot;
- (void)updateWindowTitle;
- (BOOL)saveActive;
- (BOOL)saveActiveToPath:(NSString *)path;
- (void)openPath:(NSString *)path;
- (NSString *)label:(NSString *)label fittingWidth:(CGFloat)width
               size:(CGFloat)size family:(NSString *)family;
- (void)drawLabel:(NSString *)label at:(NSPoint)point
             size:(CGFloat)size color:(NSColor *)color family:(NSString *)family;
- (void)drawFileBadge:(const char *)name inRect:(NSRect)rect tab:(BOOL)tab;
- (void)startGitBatch:(int)kind message:(char *)message stageAll:(int)stageAll;
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
- (BOOL)isEmptyState;
- (void)updateEmptyState;
- (void)applyEmptyGuideTheme;
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
- (NSInteger)splitterAtPoint:(NSPoint)point;
- (NSRect)splitterRect:(NSInteger)which;
- (void)refreshSplitterTracking;
- (NSRect)menuBarItemRect:(NSUInteger)index;
- (NSInteger)menuBarIndexAtPoint:(NSPoint)point;
- (void)openMenuBarMenu:(NSUInteger)index;
- (void)openMenuBarMenu:(NSUInteger)index selectFirst:(BOOL)selectFirst;
- (BOOL)showPopupMenu:(NSMenu *)menu belowScreenRect:(NSRect)anchor gap:(CGFloat)gap
          selectFirst:(BOOL)selectFirst;
- (void)closePopupMenu;
- (void)setMenuHover:(NSInteger)index;
- (BOOL)editorActionable;
- (BOOL)activeIsImage;
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
- (void)showGitPanel:(id)sender;
- (void)selectSidebarTab:(NSInteger)tab;
- (NSRect)sidebarTabRect:(NSInteger)tab;
- (NSInteger)sidebarTabAtPoint:(NSPoint)point;
- (AxyneGitPanelTheme)gitPanelTheme;
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
- (BOOL)explorerPinnedAtPoint:(NSPoint)point;
- (NSUInteger)explorerPinnedRows:(size_t *)out;
- (NSInteger)explorerVisibleRows;
- (NSRect)tabFrameAtIndex:(size_t)index;
- (void)selectPanel:(id)sender;
- (void)clearOutput:(id)sender;
- (void)quickFile:(id)sender;
- (void)configureRunnerAction:(id)sender;
- (void)showBuildTargetMenu:(id)sender;
- (void)pickBuildTarget:(id)sender;
- (void)discoverRuntimes;
- (void)scheduleRuntimeDiscovery;
- (NSString *)buildTargetLabel;
- (void)clearPendingRun;
- (BOOL)startStep:(const AxyneLanguageStep *)step plan:(const AxyneLanguagePlan *)plan
           action:(int)action clearOutput:(BOOL)clear;
- (void)startAction:(BOOL)run;
- (void)performExplorerOperation:(AxyneFileKind)kind;
- (NSString *)askForText:(NSString *)title label:(NSString *)label;
- (void)startTerminal:(id)sender;
- (void)stopTerminal:(id)sender;
- (void)sendTerminal:(id)sender;
- (void)terminalAppend:(const char *)bytes length:(size_t)length
                stream:(AxyneProcessStream)stream;
- (void)terminalExited:(AxyneProcess *)process exitCode:(int)exitCode;
- (BOOL)configureRunner;
- (BOOL)applyRunnerValues:(const AxyneRunnerDialogValues *)values
                    error:(char *)message capacity:(size_t)capacity;
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
- (void)showCompletion;
- (BOOL)showPreferences:(BOOL)workspace;
- (BOOL)savePreferences:(AxynePreferences *)edited workspace:(BOOL)workspace;
- (void)showGitStatus:(id)sender;
- (void)showGitDiff:(id)sender;
- (void)stageAllGitChanges:(id)sender;
- (void)unstageAllGitChanges:(id)sender;
- (void)commitGitChanges:(id)sender;
- (void)pushGitChanges:(id)sender;
- (void)pullGitChanges:(id)sender;
- (void)showGitLog:(id)sender;
- (void)completeGitBatch:(AxyneMacGitBatch *)batch;
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
- (void)applyProblemsTheme;
- (void)cancelProblemsRefresh;
- (void)refreshProblems;
- (void)scheduleProblemsRefresh;
- (void)flushProblemsRefresh;
- (const AxyneProblemCounts *)problemCounts;
- (void)applyLspDiagnostics:(const AxyneLspDiagnostic *)diagnostics
                      count:(size_t)count path:(const char *)path;
- (void)clearLspProblemsForPath:(const char *)path;
- (void)beginBuildProblemsInDirectory:(const char *)directory clear:(BOOL)clear;
- (void)beginBuildProblems;
- (void)feedBuildProblems:(const char *)bytes length:(size_t)length
                   stream:(AxyneProcessStream)stream;
- (void)finishBuildProblems;
- (void)showProblemAtPath:(NSString *)path line:(size_t)line column:(size_t)column
                    utf16:(BOOL)utf16;
- (void)moveCaretToProblemLine:(size_t)line column:(size_t)column utf16:(BOOL)utf16;
- (void)afterSaveAsFromPath:(const char *)oldPath;
- (BOOL)outlineVisible;
- (CGFloat)outlineHeight;
- (CGFloat)explorerTreeBottom;
- (BOOL)pointIsInOutline:(NSPoint)point;
- (NSInteger)outlineSymbolAtPoint:(NSPoint)point;
- (void)cancelOutlineRefresh;
- (void)queueOutlineRefreshAfter:(NSTimeInterval)delay;
- (void)scheduleOutlineRefresh;
- (void)outlineRefreshDue;
- (void)refreshOutline;
- (void)jumpToOutlineSymbol:(NSInteger)index;
- (void)drawOutlineAtTop:(CGFloat)top light:(BOOL)light reference:(BOOL)reference;
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

#endif
