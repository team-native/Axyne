/* View feature category of the macOS workspace window: View toggles, themes and full screen (D7, D9, D18).
 * Owns _viewUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneViewFeature)

- (NSInteger)viewValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)viewPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)viewDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)viewDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)viewDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)viewEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)viewDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)viewDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)viewLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)viewLayout {}

- (void)viewApplyPreferences {}

- (void)viewTeardown
{
    [_viewUi release];
    _viewUi = nil;
}

@end
