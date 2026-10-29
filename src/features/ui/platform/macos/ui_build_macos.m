/* Build feature category of the macOS workspace window: CMake, build configurations, targets, tasks.json (D10, D11).
 * Owns _buildUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneBuildFeature)

- (NSInteger)buildValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)buildPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)buildDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)buildDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)buildDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)buildEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)buildDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)buildDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)buildLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)buildLayout {}

- (void)buildApplyPreferences {}

- (void)buildTeardown
{
    [_buildUi release];
    _buildUi = nil;
}

@end
