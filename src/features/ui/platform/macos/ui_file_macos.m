/* File feature category of the macOS workspace window: D4-D6: new project, save all, auto save, close window, recent files and folders.
 * Owns _fileUi. Hook contract: ui_macos_internal.h ("feature categories").
 * Base stub: implements no command and draws nothing yet. */
#include "ui_macos_internal.h"

@implementation AxyneWorkspaceView (AxyneFileFeature)

- (NSInteger)fileValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    (void)command; (void)item;
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)filePerformCommand:(AxyneCommandId)command sender:(id)sender
{
    (void)command; (void)sender;
    return NO;
}

- (void)fileDocumentActivated:(AxyneDocument *)document { (void)document; }

- (void)fileDocumentWillClose:(AxyneDocument *)document { (void)document; }

- (void)fileDocumentSaved:(AxyneDocument *)document { (void)document; }

- (void)fileEditorNotification:(SCNotification *)notification { (void)notification; }

- (BOOL)fileDrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors
{
    (void)rect; (void)colors;
    return NO;
}

- (void)fileDrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors
{
    (void)strip; (void)colors;
}

- (BOOL)fileLayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab
{
    (void)content; (void)tab;
    return NO;
}

- (void)fileLayout {}

- (void)fileApplyPreferences {}

- (void)fileTeardown
{
    [_fileUi release];
    _fileUi = nil;
}

@end
