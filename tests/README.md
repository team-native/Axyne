# Headless regression fixtures

The CTest cases create per-process fixture directories below the build tree's
`tests/fixtures` directory. They use the filesystem APIs under test and remove
their files and directories when the case completes.

The Git case requires the `git` executable to be available on `PATH`, matching
the public Git service contract. On Windows, the test resolves `git.exe` with
`SearchPathW` before passing the absolute path to the process API. It
initializes only its private fixture directory and supplies a temporary commit
identity; it does not modify the checkout containing the tests.

The LSP case does not require an installed language server. It covers the
public UTF-8/UTF-16 helper and inactive-client configuration validation, so the
suite remains deterministic and headless on macOS and Windows.
