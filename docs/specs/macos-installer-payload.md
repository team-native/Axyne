# macOS installer payload repair

Category: macOS packaging. Branch: `fix/macos-installer-payload`.

| Decision | Value | Source | Status |
|---|---|---|---|
| Install behavior | Install the full existing application and uninstall command under `/Applications`; retain native Installer | IMPLEMENTATION | CONFIRMED existing behavior |
| Scope | Repair packaging and CI validation only; no new features | IMPLEMENTATION | CONFIRMED implementation boundary |
| Component selection | Explicitly include the existing `Unspecified` component | IMPLEMENTATION | CONFIRMED by experiment |
| Generator staging | DragNDrop `/`, productbuild `/Applications` | IMPLEMENTATION | CONFIRMED |
| Installer choice | Display existing required component as visible `Axyne` | DELEGATED | ASSUMED |
| Archive validation | Expand the final CPack productbuild archive with `pkgutil`; resolve Distribution choices and component references, verify effective destination and every staged file, directory, symlink, file digest, and executable permission | IMPLEMENTATION | CONFIRMED |

## Implementation state

| Unit | Responsibility | Dependencies | Status |
|---|---|---|---|
| Packaging | Include payload component and separate generator prefixes | Existing Release install rules | COMPLETE |
| Validation | Validate final archive and use actual CPack productbuild in existing CI | Packaging, staged install tree | COMPLETE |
| Verification | Reproduce original failure, validate repaired archive, separate review | Both units | COMPLETE |

## Experimental evidence

Original CPack productbuild produces a 2.1 KB package with an empty
`choices-outline`, although its staging tree contains the app. Setting
`CPACK_MONOLITHIC_INSTALL=ON` still produces the empty archive. Setting
`CPACK_COMPONENTS_ALL=Unspecified` creates a required, hidden, selected choice
and a component payload containing `Applications/Axyne.app`. PackageInfo uses
install-location `/`, so the effective application destination is `/Applications`.

Repaired deliverables are under `artifacts/macos-installer-fixed-final-20261011`.
Existing user artifacts are untouched.

Final checks: repaired productbuild archive passes the complete staged-tree
comparison; original empty archive fails on empty choices-outline; DragNDrop
succeeds without a prefix override; Debug packaging rejects with Release-only;
final Distribution shows visible, selected, disabled (required) `Axyne` choice;
`git diff --check` passes. Independent final review found no actionable issues.
Installer acceptance was checked with `installer -showChoicesXML`; an actual
privileged installation was not performed.
