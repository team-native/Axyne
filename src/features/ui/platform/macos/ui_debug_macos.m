/* Debug feature category of the macOS workspace window: debugger, run configuration, external debuggers, debug console (D1, D12, D13).
 * Owns _debugUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneDebugFeature)

- (NSInteger)debugValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)debugPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)debugDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)debugDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)debugDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)debugEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)debugDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)debugDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)debugLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)debugLayout {}

- (void)debugApplyPreferences {}

- (void)debugTeardown
{
    [_debugUi release];
    _debugUi = nil;
}

@end
