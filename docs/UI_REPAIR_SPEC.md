# Native IDE UI repair

Reference: Figma `7J8SYhLpybJgpxD3qFqL5u`, main window `6:399`.

| Decision | Value | Source | Status |
|---|---|---|---|
| Goal | Files open inside Scintilla; toolbar, file badges, terminal and chrome match the referenced design | USER | CONFIRMED |
| Native title bar | macOS keeps AppKit traffic lights and system menu | USER | CONFIRMED |
| Geometry | Toolbar 38, tabs 34, explorer 248, rows 22, output panel 230 with header 32, status 24 logical pixels | USER (Figma) | CONFIRMED |
| Badges | Separate centered 20px labels, 9px explorer text, per-language reference colors; tabs use 11px monospaced badges | USER (Figma) | CONFIRMED |
| Dynamic values | Show real file names, target, shell and editor position; do not show sample build success/diagnostic/RAM numbers as actual state | IMPLEMENTATION | CONFIRMED |
| Output controls | Compact native interactive controls in the panel header; debugger actions remain available in the menu | DELEGATED | ASSUMED |
| Terminal input | Separate input appears when the terminal panel is selected; Enter sends input to the existing process | DELEGATED | ASSUMED |
| Exclusions | No server/account/plugin/marketplace functionality and no installer changes | USER | CONFIRMED |

## Implementation units

- Document opening / framework loading: independent `codex/document-opening-repair` branch with runtime regression checks.
- Native visual fidelity: `codex/native-workspace-fidelity`, shared badge/geometry contract, disjoint Windows and macOS adapters.
- Integration: build both category changes together, native file-open and toolbar/terminal checks, delegated review, Korean template PRs targeting `dev`.
