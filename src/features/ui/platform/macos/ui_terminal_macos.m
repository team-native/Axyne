/* Terminal feature category of the macOS workspace window: terminal profiles and sessions (D14).
 * Owns _terminalUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneTerminalFeature)

- (NSInteger)terminalValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)terminalPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)terminalDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)terminalDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)terminalDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)terminalEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)terminalDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)terminalDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)terminalLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)terminalLayout {}

- (void)terminalApplyPreferences {}

- (void)terminalTeardown
{
    [_terminalUi release];
    _terminalUi = nil;
}

@end
