/* Help feature category of the macOS workspace window: Help menu: getting started, log folder, updates, licenses (D22, D27, D28).
 * Owns _helpUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneHelpFeature)

- (NSInteger)helpValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)helpPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)helpDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)helpDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)helpDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)helpEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)helpDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)helpDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)helpLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)helpLayout {}

- (void)helpApplyPreferences {}

- (void)helpTeardown
{
    [_helpUi release];
    _helpUi = nil;
}

@end
