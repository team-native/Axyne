/* Tools feature category: installed plugins and their external commands. */
#include "ui_macos_internal.h"

#include "axyne/app_paths.h"
#include "axyne/build_config.h"

@implementation AxyneWorkspaceView (AxyneToolsFeature)

- (void)ensurePlugins
{
    char *config, *pluginsDir, *statePath;
    if (_pluginsLoaded) return;
    _pluginsLoaded = YES;
    config = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR);
    if (config == NULL) return;
    pluginsDir = axyne_app_path_join(config, "plugins");
    statePath = axyne_app_path_join(config, "plugins.json");
    if (pluginsDir != NULL)
        (void)axyne_plugins_load(&_plugins, pluginsDir, statePath,
                                 AXYNE_CONFIG_VERSION, NULL);
    axyne_app_path_free(pluginsDir);
    axyne_app_path_free(statePath);
    axyne_app_path_free(config);
}

- (void)toolsAppendPluginMenu:(NSMenu *)menu
{
    NSMenuItem *pluginsItem;
    NSMenu *pluginsMenu;
    NSMenuItem *extensionsItem;
    if (menu == nil) return;
    if ([menu numberOfItems] != 0) [menu addItem:[NSMenuItem separatorItem]];
    pluginsItem = [[NSMenuItem alloc] initWithTitle:@"플러그인" action:nil keyEquivalent:@""];
    pluginsMenu = [[NSMenu alloc] initWithTitle:@"플러그인"];
    [pluginsMenu setDelegate:(id<NSMenuDelegate>)self];
    [pluginsMenu setAutoenablesItems:NO];
    [_pluginsMenu setDelegate:nil];
    [_pluginsMenu release];
    _pluginsMenu = [pluginsMenu retain];
    [pluginsItem setSubmenu:pluginsMenu];
    [menu addItem:pluginsItem];
    [pluginsMenu release];
    [pluginsItem release];

    extensionsItem = [[NSMenuItem alloc] initWithTitle:@"확장..."
        action:@selector(toolsShowExtensions:) keyEquivalent:@""];
    [extensionsItem setTarget:self];
    [menu addItem:extensionsItem];
    [extensionsItem release];
}

- (void)menuNeedsUpdate:(NSMenu *)menu
{
    AxyneDocument *document;
    const char *path;
    NSInteger target = 0;
    size_t i, j;
    if (menu != _pluginsMenu) return;
    [self ensurePlugins];
    document = [self activeDocument];
    path = document != NULL && !document->is_untitled ? document->path : NULL;
    [menu removeAllItems];
    for (i = 0; i < _plugins.count; ++i) {
        const AxynePlugin *plugin = &_plugins.items[i];
        BOOL requirementsMet;
        if (!plugin->enabled || !plugin->compatible) continue;
        requirementsMet = axyne_plugin_missing_requirement(plugin) == NULL;
        for (j = 0; j < plugin->command_count; ++j, ++target) {
            NSString *name = [NSString stringWithUTF8String:plugin->name];
            NSString *title = [NSString stringWithUTF8String:plugin->commands[j].title];
            NSString *label = [NSString stringWithFormat:@"%@: %@",
                               name != nil ? name : @"",
                               title != nil ? title : @""];
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:label
                action:@selector(toolsRunPluginCommand:) keyEquivalent:@""];
            [item setTarget:self];
            [item setTag:target];
            [item setEnabled:requirementsMet &&
                axyne_plugin_command_applies(&plugin->commands[j], path)];
            [menu addItem:item];
            [item release];
        }
    }
    if (target == 0) {
        NSMenuItem *empty = [[NSMenuItem alloc] initWithTitle:@"설치된 플러그인 명령 없음"
            action:nil keyEquivalent:@""];
        [empty setEnabled:NO];
        [menu addItem:empty];
        [empty release];
    }
}

- (void)toolsRunPluginCommand:(id)sender
{
    NSInteger target = [sender tag], index = 0;
    AxynePlugin *plugin = NULL;
    AxynePluginCommand *command = NULL;
    AxynePluginInvocation invocation;
    AxynePluginContext context;
    AxyneProcessSpec spec;
    AxyneDocument *document;
    AxyneError error;
    char header[320];
    size_t i, j;

    [self ensurePlugins];
    for (i = 0; i < _plugins.count && command == NULL; ++i) {
        AxynePlugin *candidate = &_plugins.items[i];
        if (!candidate->enabled || !candidate->compatible) continue;
        for (j = 0; j < candidate->command_count; ++j, ++index) {
            if (index == target) {
                plugin = candidate;
                command = &candidate->commands[j];
                break;
            }
        }
    }
    if (plugin == NULL || command == NULL) return;
    [self selectOutputPanel];
    if (_actionProcess != NULL || axyne_debugger_is_active(&_debugger)) {
        const char *busy = "[plugin] Another action is running. Stop it first.\n";
        [self terminalAppend:busy length:strlen(busy) stream:AXYNE_PROCESS_STDERR];
        return;
    }
    document = [self activeDocument];
    if (document != NULL && document->is_dirty && !document->is_untitled)
        if (![self saveActive]) {
            const char *cancelled = "[plugin] Save cancelled; command was not started.\n";
            [self terminalAppend:cancelled length:strlen(cancelled)
                           stream:AXYNE_PROCESS_STDERR];
            return;
        }
    context.file = document != NULL && !document->is_untitled ? document->path : NULL;
    context.workspace_folder = _explorer.root;
    [_terminalOutput setString:@""];
    memset(&invocation, 0, sizeof(invocation));
    if (axyne_plugin_command_prepare(plugin, command, &context,
                                     &invocation, &error) != AXYNE_STATUS_OK) {
        (void)snprintf(header, sizeof(header), "[plugin] %s: %s\n",
                       plugin->name, error.message);
        [self terminalAppend:header length:strlen(header) stream:AXYNE_PROCESS_STDERR];
        axyne_plugin_invocation_clear(&invocation);
        return;
    }
    memset(&spec, 0, sizeof(spec));
    spec.executable = invocation.executable;
    spec.arguments = (const char *const *)invocation.arguments;
    spec.argument_count = invocation.argument_count;
    spec.working_directory = invocation.working_directory;
    spec.on_output = axyne_macos_terminal_output;
    spec.on_exit = axyne_macos_terminal_exit;
    spec.user_data = self;
    if (axyne_process_start(&spec, &_actionProcess, &error) != AXYNE_STATUS_OK) {
        (void)snprintf(header, sizeof(header), "[plugin] %s: %s\n",
                       plugin->name, error.message);
        [self terminalAppend:header length:strlen(header) stream:AXYNE_PROCESS_STDERR];
    } else {
        (void)snprintf(header, sizeof(header), "[plugin] %s: %s\n",
                       plugin->name, command->title);
        [self terminalAppend:header length:strlen(header) stream:AXYNE_PROCESS_STDOUT];
        _activeAction = 3;
        _lastExitFailed = NO;
        [_terminalStart setEnabled:NO];
        [_terminalStop setEnabled:YES];
        [self refreshActionControls];
    }
    axyne_plugin_invocation_clear(&invocation);
    [self setNeedsDisplay:YES];
}

- (void)toolsShowExtensions:(id)sender
{
    (void)sender;
    [self ensurePlugins];
    axyne_extensions_window_show([self window], &_plugins,
                                 AXYNE_CONFIG_VERSION, AXYNE_EXTENSIONS_INSTALLED);
}

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
    [_pluginsMenu setDelegate:nil];
    [_pluginsMenu release];
    _pluginsMenu = nil;
    axyne_plugins_clear(&_plugins);
    [_toolsUi release];
    _toolsUi = nil;
}

@end
