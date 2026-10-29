/* Tools feature category of the macOS workspace window: Tools menu: external tools, keybindings, file associations, memory dialog (D23, D24).
 * Owns _toolsUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneToolsFeature)

- (NSInteger)toolsValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)toolsPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)toolsDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)toolsDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)toolsDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)toolsEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)toolsDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)toolsDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)toolsLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)toolsLayout {}

- (void)toolsApplyPreferences {}

- (void)toolsTeardown
{
    [_toolsUi release];
    _toolsUi = nil;
}

@end
