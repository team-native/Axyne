# Axyne 0.1.0

This release is a native desktop application for Windows and macOS. It
includes the initial editor shell, Scintilla-based editing surface, workspace
file browsing, document editing and search, local process-backed runner and
terminal actions, and local Git/LSP integration points that are only activated
when invoked.

The application is designed to work offline. This release does not include
server communication, accounts, plugin sessions or a plugin marketplace.
Language runtimes and other external tools are discovered from the local
machine when requested and are not bundled.

The packaged application includes this file and `version.json` so the installed
version and release notes remain available without network access.
