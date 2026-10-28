#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/plugins.h"

static const char *const manifest =
    "{\"id\":\"demo-preview\",\"name\":\"Demo Preview\",\"version\":\"1.2.0\","
    "\"description\":\"d\",\"author\":\"Native\",\"platforms\":[\"windows\",\"macos\"],"
    "\"minIdeVersion\":\"0.1.0\","
    "\"requires\":[{\"program\":\"surely-not-installed-tool\",\"hint\":\"install it\"}],"
    "\"commands\":[{\"id\":\"open\",\"title\":\"Open\",\"program\":\"surely-not-installed-tool\","
    "\"args\":[\"--file\",\"${file}\",\"${fileBasenameNoExtension}\"],\"extensions\":[\"SLINT\"]},"
    "{\"id\":\"local\",\"title\":\"Local\",\"program\":\"./bin/tool\",\"args\":[\"${pluginDir}\"],"
    "\"cwd\":\"${fileDirname}\"},"
    "{\"title\":\"\",\"program\":\"x\"},"
    "{\"id\":\"os\",\"title\":\"Per OS\",\"program\":{\"windows\":\"w.cmd\",\"macos\":\"m.sh\"}},"
    "{\"id\":\"none\",\"title\":\"Other OS\",\"program\":{\"linux\":\"l\"}}]}";

static int parse_checks(void)
{
    AxyneError error;
    AxynePlugin *plugin = axyne_plugin_parse(manifest, strlen(manifest), "/p/demo-preview", "0.1.0", &error);
    AXYNE_TEST_CHECK(plugin != NULL);
    AXYNE_TEST_STREQ(plugin->id, "demo-preview");
    AXYNE_TEST_EQ_INT(plugin->compatible, 1);
    /* the untitled command and the one without a program for this OS are dropped */
    AXYNE_TEST_EQ_INT(plugin->command_count, 3);
#ifdef _WIN32
    AXYNE_TEST_STREQ(plugin->commands[2].program, "w.cmd");
#else
    AXYNE_TEST_STREQ(plugin->commands[2].program, "m.sh");
#endif
    AXYNE_TEST_EQ_INT(plugin->requirement_count, 1);
    AXYNE_TEST_CHECK(axyne_plugin_command_applies(&plugin->commands[0], "C:\\a\\ui.slint"));
    AXYNE_TEST_CHECK(axyne_plugin_command_applies(&plugin->commands[0], "/a/UI.SLINT"));
    AXYNE_TEST_CHECK(!axyne_plugin_command_applies(&plugin->commands[0], "/a.slint/readme"));
    AXYNE_TEST_CHECK(!axyne_plugin_command_applies(&plugin->commands[0], NULL));
    AXYNE_TEST_CHECK(axyne_plugin_command_applies(&plugin->commands[1], NULL));
    AXYNE_TEST_CHECK(axyne_plugin_missing_requirement(plugin) != NULL);
    axyne_plugin_free(plugin);

    plugin = axyne_plugin_parse(manifest, strlen(manifest), "/p", "0.0.9", &error);
    AXYNE_TEST_CHECK(plugin != NULL && plugin->compatible == 0); /* below minIdeVersion */
    axyne_plugin_free(plugin);
    AXYNE_TEST_CHECK(axyne_plugin_parse("{\"id\":\"Bad\",\"name\":\"x\",\"version\":\"1.0.0\"}", 41, "/p", "", &error) == NULL);
    AXYNE_TEST_CHECK(axyne_plugin_parse("{\"id\":\"ok\",\"name\":\"x\",\"version\":\"1.0\"}", 39, "/p", "", &error) == NULL);
    AXYNE_TEST_CHECK(axyne_plugin_parse("not json", 8, "/p", "", &error) == NULL);
    AXYNE_TEST_EQ_INT(axyne_plugin_version_compare("0.10.0", "0.9.9"), 1);
    AXYNE_TEST_EQ_INT(axyne_plugin_version_compare("1.0.0", "1.0"), 0);
    return 1;
}

static int prepare_checks(const char *root)
{
    AxyneError error;
    AxynePluginInvocation invocation;
    AxynePluginContext context = {0};
    char directory[1024], bin[1024], tool[1024], file[1024];
    AxynePlugin *plugin;
    AXYNE_TEST_CHECK(axyne_test_path(directory, sizeof(directory), root, "demo-preview"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(directory));
    AXYNE_TEST_CHECK(axyne_test_path(bin, sizeof(bin), directory, "bin"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(bin));
    AXYNE_TEST_CHECK(axyne_test_path(tool, sizeof(tool), bin, "tool"));
    AXYNE_TEST_CHECK(axyne_test_write(tool, "x"));
    AXYNE_TEST_CHECK(axyne_test_path(file, sizeof(file), root, "main.slint"));
    plugin = axyne_plugin_parse(manifest, strlen(manifest), directory, "0.1.0", &error);
    AXYNE_TEST_CHECK(plugin != NULL);

    /* PATH program missing: the requirement hint is reported. */
    context.file = file;
    AXYNE_TEST_STATUS(axyne_plugin_command_prepare(plugin, &plugin->commands[0], &context, &invocation, &error),
                      AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CONTAINS(error.message, "install it");

    /* The remaining checks exercise commands without declared prerequisites. */
    plugin->requirement_count = 0;

    /* Plugin-relative program, ${pluginDir}, ${fileDirname} as cwd. */
    AXYNE_TEST_STATUS(axyne_plugin_command_prepare(plugin, &plugin->commands[1], &context, &invocation, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(invocation.executable, "tool");
    AXYNE_TEST_EQ_INT(invocation.argument_count, 1);
    AXYNE_TEST_STREQ(invocation.arguments[0], directory);
    AXYNE_TEST_STREQ(invocation.working_directory, root);
    axyne_plugin_invocation_clear(&invocation);

    /* ${fileDirname} without a file. */
    context.file = NULL;
    AXYNE_TEST_STATUS(axyne_plugin_command_prepare(plugin, &plugin->commands[1], &context, &invocation, &error),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CONTAINS(error.message, "${file}");
    plugin->requirement_count = 1;
    axyne_plugin_free(plugin);
    return 1;
}

static int prepare_requirement_checks(const char *root)
{
    static const char *const json =
        "{\"id\":\"bundled\",\"name\":\"Bundled\",\"version\":\"1.0.0\","
        "\"requires\":[{\"program\":\""
#ifdef _WIN32
        "cmd.exe"
#else
        "sh"
#endif
        "\"},{\"program\":\"surely-not-installed-runtime\",\"hint\":\"install the runtime and add it to PATH\"}],"
        "\"commands\":[{\"id\":\"run\",\"title\":\"Run\",\"program\":\"./bundled.cmd\"}]}";
    AxyneError error;
    AxynePluginInvocation invocation;
    AxynePlugin *plugin;
    char tool[1024];
    char *hint;
    AXYNE_TEST_CHECK(axyne_test_path(tool, sizeof(tool), root, "bundled.cmd"));
#ifdef _WIN32
    AXYNE_TEST_CHECK(axyne_test_write(tool, "@exit /b 0\r\n"));
#else
    AXYNE_TEST_CHECK(axyne_test_write(tool, "#!/bin/sh\nexit 0\n"));
#endif
    plugin = axyne_plugin_parse(json, strlen(json), root, "0.1.0", &error);
    AXYNE_TEST_CHECK(plugin != NULL);

    /* Prove the bundled command prepares when its only prerequisite exists. */
    plugin->requirement_count = 1;
    AXYNE_TEST_CHECK(axyne_plugin_missing_requirement(plugin) == NULL);
    AXYNE_TEST_STATUS(axyne_plugin_command_prepare(plugin, &plugin->commands[0], NULL, &invocation, &error),
                      AXYNE_STATUS_OK);
    axyne_plugin_invocation_clear(&invocation);

    /* A later, missing declared runtime must block even this bundled command. */
    plugin->requirement_count = 2;
    AXYNE_TEST_STATUS(axyne_plugin_command_prepare(plugin, &plugin->commands[0], NULL, &invocation, &error),
                      AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_EQ_INT(error.code, AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CONTAINS(error.message, "surely-not-installed-runtime");
    AXYNE_TEST_CONTAINS(error.message, "install the runtime and add it to PATH");
    AXYNE_TEST_CHECK(invocation.executable == NULL && invocation.arguments == NULL &&
                     invocation.working_directory == NULL && invocation.argument_count == 0);

    /* A manifest without an installation hint still explains PATH lookup. */
    hint = plugin->requirements[1].hint;
    plugin->requirements[1].hint = "";
    AXYNE_TEST_STATUS(axyne_plugin_command_prepare(plugin, &plugin->commands[0], NULL, &invocation, &error),
                      AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CONTAINS(error.message, "surely-not-installed-runtime was not found on PATH");
    plugin->requirements[1].hint = hint;
    axyne_plugin_free(plugin);
    return 1;
}

static int write_manifest(const char *directory, const char *json)
{
    char path[1024];
    return axyne_test_make_directory(directory) &&
           axyne_test_path(path, sizeof(path), directory, "manifest.json") &&
           axyne_test_write(path, json);
}

static int set_checks(const char *root)
{
    AxynePluginSet set;
    AxyneError error;
    char plugins[1024], one[1024], two[1024], stray[1024], state[1024];
    memset(&set, 0, sizeof(set));
    AXYNE_TEST_CHECK(axyne_test_path(plugins, sizeof(plugins), root, "plugins"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(plugins));
    AXYNE_TEST_CHECK(axyne_test_path(one, sizeof(one), plugins, "zeta"));
    AXYNE_TEST_CHECK(axyne_test_path(two, sizeof(two), plugins, "alpha"));
    AXYNE_TEST_CHECK(axyne_test_path(stray, sizeof(stray), plugins, "renamed"));
    AXYNE_TEST_CHECK(axyne_test_path(state, sizeof(state), root, "plugins.json"));
    AXYNE_TEST_CHECK(write_manifest(one, "{\"id\":\"zeta\",\"name\":\"Zeta\",\"version\":\"1.0.0\"}"));
    AXYNE_TEST_CHECK(write_manifest(two, "{\"id\":\"alpha\",\"name\":\"Alpha\",\"version\":\"2.0.0\"}"));
    /* Folder name differs from the id: ignored. */
    AXYNE_TEST_CHECK(write_manifest(stray, "{\"id\":\"zeta\",\"name\":\"Copy\",\"version\":\"9.0.0\"}"));

    AXYNE_TEST_STATUS(axyne_plugins_load(&set, plugins, state, "0.1.0", &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(set.count, 2);
    AXYNE_TEST_STREQ(set.items[0].id, "alpha"); /* sorted by name */
    AXYNE_TEST_STATUS(axyne_plugins_set_enabled(&set, "zeta", 0, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_plugins_load(&set, plugins, state, "0.1.0", &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_plugins_find(&set, "zeta")->enabled, 0);
    AXYNE_TEST_EQ_INT(axyne_plugins_find(&set, "alpha")->enabled, 1);

    AXYNE_TEST_STATUS(axyne_plugins_uninstall(&set, "alpha", &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(set.count, 1);
    AXYNE_TEST_STATUS(axyne_plugins_load(&set, plugins, state, "0.1.0", &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(set.count, 1);
    AXYNE_TEST_CHECK(axyne_plugins_find(&set, "alpha") == NULL);

    {
        /* Install from a zip built with the same system tools. */
        char package[1024], source[1024], command[4096];
        char *installed = NULL;
        AXYNE_TEST_CHECK(axyne_test_path(source, sizeof(source), root, "package-src"));
        AXYNE_TEST_CHECK(write_manifest(source, "{\"id\":\"zeta\",\"name\":\"Zeta\",\"version\":\"1.1.0\"}"));
        AXYNE_TEST_CHECK(axyne_test_path(package, sizeof(package), root, "zeta.zip"));
#ifdef _WIN32
        (void)snprintf(command, sizeof(command), "tar -a -cf \"%s\" -C \"%s\" manifest.json", package, source);
#else
        (void)snprintf(command, sizeof(command), "cd '%s' && /usr/bin/ditto -c -k manifest.json '%s'", source, package);
#endif
        AXYNE_TEST_EQ_INT(system(command), 0);
        AXYNE_TEST_STATUS(axyne_plugins_install_zip(&set, package, "0.1.0", &installed, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STREQ(installed, "zeta");
        free(installed);
        AXYNE_TEST_STREQ(axyne_plugins_find(&set, "zeta")->version, "1.1.0");
        AXYNE_TEST_EQ_INT(axyne_plugins_find(&set, "zeta")->enabled, 0); /* kept disabled */
        AXYNE_TEST_STATUS(axyne_plugins_install_zip(&set, "missing.zip", "0.1.0", NULL, &error),
                          AXYNE_STATUS_NOT_FOUND);
    }
    axyne_plugins_clear(&set);
    return 1;
}

int axyne_test_plugins(const char *root)
{
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    return parse_checks() && prepare_requirement_checks(root) && prepare_checks(root) && set_checks(root);
}
