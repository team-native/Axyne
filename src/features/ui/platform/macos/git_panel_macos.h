#ifndef AXYNE_GIT_PANEL_MACOS_H
#define AXYNE_GIT_PANEL_MACOS_H

#import <AppKit/AppKit.h>

#include <stdint.h>

/* Git side panel of the macOS sidebar (the "Git" tab next to the explorer).
 * One flipped NSView that draws the changed-file list, the commit message
 * area, the commit/push buttons and the commit graph itself (only the visible
 * rows), plus one NSTextView for the multi-line commit message. All Git work
 * runs on background queues through the shared core (axyne/git_panel.h); the
 * view never blocks the main thread. Manual retain/release, like the rest of
 * the macOS UI. The view holds no strong reference to its delegate and its
 * background blocks never capture the view (see AxyneGitPanelLife in the .m),
 * so releasing the owner always deallocates it. */

typedef struct AxyneGitPanelTheme {
    uint32_t background, panel, toolbar, border, text, muted, accent; /* 0xRRGGBB */
    int light;     /* nonzero for a light appearance */
    int reference; /* nonzero for the reference (Figma) dark surfaces */
} AxyneGitPanelTheme;

@class AxyneGitPanelView;

@protocol AxyneGitPanelDelegate <NSObject>
/* Workspace folder (UTF-8) or NULL when none is open. Valid only for the
 * duration of the call; the panel copies it. */
- (const char *)gitPanelWorkspace:(AxyneGitPanelView *)panel;
/* A commit, push, pull or other Git menu operation is running. */
- (BOOL)gitPanelBusy:(AxyneGitPanelView *)panel;
/* Shows text in the output panel (diffs, commit details, errors). */
- (void)gitPanel:(AxyneGitPanelView *)panel showText:(NSString *)text;
/* The "folder open" empty state was clicked. */
- (void)gitPanelOpenWorkspace:(AxyneGitPanelView *)panel;
@end

@interface AxyneGitPanelView : NSView
@property(nonatomic, assign) id<AxyneGitPanelDelegate> delegate;

- (void)setTheme:(const AxyneGitPanelTheme *)theme;
/* Reloads changes and graph in the background now. A request made while a
 * load is running reruns once when it finishes. */
- (void)refresh;
/* Same, coalesced: bursts of calls (file-watch events) trigger one load
 * shortly after the last call. Uses an assign-only box with dispatch_after. */
- (void)scheduleRefresh;
/* The workspace folder changed: drops everything shown and reloads when the
 * panel is visible. */
- (void)workspaceChanged;
/* Frees the loaded results (the panel was hidden). Safe to call repeatedly. */
- (void)unload;
@end

#endif
