/* Diag feature category of the macOS workspace window: inline diagnostics and explorer badges (D17, D19).
 * Owns _diagUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneDiagFeature)

- (NSInteger)diagValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)diagPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)diagDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)diagDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)diagDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)diagEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)diagDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)diagDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)diagLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)diagLayout {}

- (void)diagApplyPreferences {}

- (void)diagTeardown
{
    [_diagUi release];
    _diagUi = nil;
}

@end
