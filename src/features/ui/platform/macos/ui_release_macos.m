/* Release feature category of the macOS workspace window: release notes window and banner (D21).
 * Owns _releaseUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneReleaseFeature)

- (NSInteger)releaseValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)releasePerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)releaseDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)releaseDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)releaseDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)releaseEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)releaseDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)releaseDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)releaseLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)releaseLayout {}

- (void)releaseApplyPreferences {}

- (void)releaseTeardown
{
    [_releaseUi release];
    _releaseUi = nil;
}

@end
