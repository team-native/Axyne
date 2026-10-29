/* Status feature category of the macOS workspace window: status bar segments and memory gauge (D15, D16).
 * Owns _statusUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneStatusFeature)

- (NSInteger)statusValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)statusPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)statusDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)statusDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)statusDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)statusEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)statusDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)statusDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)statusLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)statusLayout {}

- (void)statusApplyPreferences {}

- (void)statusTeardown
{
    [_statusUi release];
    _statusUi = nil;
}

@end
