/* Edit feature category of the macOS workspace window: Edit menu additions: block comment, undo limit (D8).
 * Owns _editUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneEditFeature)

- (NSInteger)editValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)editPerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)editDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)editDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)editDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)editEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)editDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)editDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)editLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)editLayout {}

- (void)editApplyPreferences {}

- (void)editTeardown
{
    [_editUi release];
    _editUi = nil;
}

@end
