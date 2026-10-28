#include "axyne/release_notes.h"

static const char release_version[] = "0.1.0";
static const char release_text[] =
    "Axyne 0.1.0\n\n"
    "This release includes the native Windows and macOS editor shell, "
    "Scintilla editing, workspace browsing, document editing and search, "
    "local runner and terminal actions, and local Git/LSP integration.\n\n"
    "Axyne is designed to work offline. External language runtimes and "
    "tools are discovered from the local machine when requested.\n\n"
    "The packaged application includes these release notes and version "
    "information so they remain available without network access.";

const char *axyne_release_notes_version(void)
{
    return release_version;
}

const char *axyne_release_notes_text(void)
{
    return release_text;
}
