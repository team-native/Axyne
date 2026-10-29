/* LspNav feature category of the macOS workspace window: go to definition, references, workspace symbols (D20).
 * Owns _lspNavUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneLspNavFeature)

- (NSInteger)lspNavValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)lspNavPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)lspNavDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)lspNavDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)lspNavDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)lspNavEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)lspNavDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)lspNavDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)lspNavLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)lspNavLayout {}

- (void)lspNavApplyPreferences {}

- (void)lspNavTeardown
{
    [_lspNavUi release];
    _lspNavUi = nil;
}

@end
