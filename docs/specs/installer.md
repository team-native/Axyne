# Windows installer and uninstaller wizard

Scope: `packaging/windows/` (custom Win32 wizard `installer_ui.c`, uninstaller `uninstaller_ui.c`,
NSIS backend `Axyne-Installer.nsi.in`, `generate-installer-config.cmake`) and the one-line
MSVC runtime hunk in the root `CMakeLists.txt` packaging section.
Reference: GRANT spec D26; Figma file `7J8SYhLpybJgpxD3qFqL5u`, frames `60:3` (설치 마법사) and
`68:8` (제거 마법사).

## Decisions

| Decision | Value | Source | Status |
|---|---|---|---|
| Install scope | 설치 위치 page offers 현재 사용자만 (권장) = `%LOCALAPPDATA%\Programs\Axyne` + HKCU, no elevation, and 모든 사용자 = `%ProgramFiles%\Axyne` + HKLM (64-bit view) + all-users Start menu/desktop | DELEGATED (D26) | ASSUMED |
| Elevation | Only when 모든 사용자 is chosen and 설치 is pressed: the wizard relaunches itself with `runas` and waits hidden; the backend keeps `RequestExecutionLevel user` (2da74dd), so the current-user path never shows UAC. UAC declined → error in the footer, wizard stays on 구성 요소 | DELEGATED (D26) / AGENT | ASSUMED |
| Launch after elevated install | The elevated instance exits with code 10 when "지금 Axyne 실행" is checked; the unelevated parent starts `axyne.exe`. When the wizard itself was started elevated, Axyne is started through the desktop shell (`IShellDispatch2::ShellExecute` of explorer), so it runs unelevated; if that fails the launch is skipped and the footer says "관리자 권한으로 실행 중이라 Axyne를 일반 권한으로 시작하지 못했습니다. 시작 메뉴에서 실행하세요." | AGENT (review) | ASSUMED |
| Scope switch and folder | Switching scope swaps the folder only while it is still the other scope's default; a folder picked with 찾아보기 stays | AGENT | ASSUMED |
| Browsed folder | 찾아보기 on a folder that is not empty and has no `axyne.exe` installs into `<folder>\Axyne` (shown in the path box, note "선택한 폴더에 다른 파일이 있어 그 안의 Axyne 폴더에 설치합니다."), because install/uninstall own every top-level file of the install folder | AGENT (review) | ASSUMED |
| Existing install | If an uninstall entry exists in either hive, its scope and InstallLocation are preselected and locked (other scope card and 찾아보기 disabled, note "이미 설치된 Axyne를 같은 범위와 위치에 업그레이드합니다. 다른 범위나 위치에 설치하려면 먼저 Axyne를 제거하세요."). If both hives have an entry (older installers), the one whose folder has `axyne.exe` wins, HKLM first | AGENT (review) | ASSUMED |
| Running Axyne during install | 구성 요소 page shows "Axyne가 실행 중입니다. 저장한 뒤 닫아 주세요." with "Axyne 닫기" (posts WM_CLOSE, same as the uninstaller) and disables 설치 while any `axyne.exe` runs; re-checked every 0.7 s and again before installing | AGENT (review) | ASSUMED |
| Disk space | Required = payload bytes measured at build time (`AXYNE_UI_INSTALL_BYTES`) + 1 MiB (uninstaller backend + journal); available = `GetDiskFreeSpaceExW` of the nearest existing ancestor of the target folder. Shown as "필요한 공간 X MB · 사용 가능 Y GB"; when insufficient or unknown, 다음 is disabled with "대상 드라이브의 공간이 부족합니다…", and it is checked again before installing | DELEGATED (D26) | ASSUMED |
| Progress | Real: the backend writes a rollback journal (below) before each step and reports the payload size (`SectionGetSize`, KiB); the UI polls every 100 ms and sums the current sizes of the journaled target files (the file being extracted grows), capped at 99 % until the backend exits 0 | DELEGATED (D26) | ASSUMED |
| Remaining time | "남은 시간 약 N초/분" = elapsed × (1 − p) / p from measured progress; "남은 시간 계산 중..." until 3 % and 0.7 s | DELEGATED (D26) | ASSUMED |
| Install log box | Last 7 journaled steps (✓ done, → current), e.g. "axyne.exe 복사", "시작 메뉴 바로 가기 만들기", "프로그램 등록 정보 기록" | AGENT (Figma 57:3386) | ASSUMED |
| Cancel during install | 취소, the title-bar × and Alt+F4 stop the backend (TerminateProcess) and roll back immediately, without a confirmation prompt; result page "설치를 취소했습니다" + 닫기 | DELEGATED (D26) | ASSUMED |
| Rollback | Reverse journal order: new files deleted; confirmed backups (`B`) moved back over the replaced file; an unconfirmed move (`M` without `B`) moved back only when the original is missing; directories created by the backend or the UI removed if empty; uninstall key deleted if this run created it, otherwise overwritten values restored. Each move back is retried 10 × 100 ms. A failed backend (non-zero exit) is rolled back the same way ("설치하지 못했습니다") | DELEGATED (D26) | ASSUMED |
| Incomplete restore | If any backup cannot be moved back, `.axyne-backup` is kept with `restore-list.txt` (UTF-8, "<backup> -> <original>" per line) and the result page says "일부 기존 파일을 원래 위치로 복원하지 못했습니다…" with the backup folder path and "폴더 열기" | AGENT (review) | ASSUMED |
| Backups | Existing files are moved (same volume rename) into `.axyne-backup` before being replaced and deleted only after a successful install. A stale backup file from an earlier run is deleted first; if it cannot be deleted, or the move fails, the install aborts before that file is touched | AGENT | ASSUMED |
| Components | The previously painted-but-inert checkboxes now work: 시작 메뉴 바로 가기 (default on) and 바탕 화면 바로 가기 (default off, as in Figma) map to backend `/NOSTARTMENU` `/NODESKTOP`; Axyne 편집기 (필수) is shown disabled. Figma's 언어 지원 / 파일 연결 / 탐색기 메뉴 / PATH rows are not shown because the backend has no such feature (file associations are D24, another category) | DELEGATED (D26) | ASSUMED |
| Navigation | `< 이전` on 사용권 계약, 설치 위치, 구성 요소 (Figma footer); 완료 page has only 마침; "지금 Axyne 실행" checked by default (Figma) | AGENT (Figma) | ASSUMED |
| Uninstall user data | "사용자 설정과 캐시도 함께 제거" (kept from the existing UI; Figma shows the inverse "설정과 테마 유지") removes `%APPDATA%\Axyne` and `%LOCALAPPDATA%\Axyne` (logs, cache) after the backend succeeds, done by the unelevated UI so it is the signed-in user's data. Default unchecked (= keep). Other users' data and project `.axyne` folders are never touched | DELEGATED (D26) | ASSUMED |
| Uninstall execution | The uninstaller UI relaunches from `%TEMP%`; it copies `Uninstall-Backend.exe` to `%TEMP%` and runs it `/S [/ALLUSERS] _?=<install dir>` so the whole install folder (UI included) can be removed and the real exit code is awaited; `runas` only for an all-users install. The UI switches its working directory to `%TEMP%` at start and both launches use `%TEMP%` as working directory (a process started from Explorer would otherwise keep the install folder open). The temp copies delete themselves on exit | AGENT | ASSUMED |
| Uninstall scope detection | `UninstallString` is `"<dir>\Uninstall.exe" /ALLUSERS` (HKLM entry) or `/CURRENTUSER` (HKCU entry), forwarded to the temp relaunch. The hinted hive is used when its InstallLocation matches the UI's own folder; otherwise the only matching hive, and HKCU when both match without a hint | AGENT (review) | ASSUMED |
| Uninstall progress | Items = top-level files of the install folder, existing shortcuts, the uninstall key, the user-data folders when checked; percentage = removed / total, log box shows ✓ done and → current; footer "잠시만 기다려 주세요" + disabled "제거 중..." (Figma 57:3826). Uninstall cannot be cancelled once started | AGENT | ASSUMED |
| Running Axyne | Warning box with "Axyne 닫기" (Figma); it now only posts WM_CLOSE so Axyne can still ask to save — the old forced TerminateProcess after 2 s was removed. 제거 is disabled while Axyne runs | AGENT | ASSUMED |
| Completion page | "Axyne가 제거되었습니다" (or "제거를 완료하지 못했습니다") and the leftover list: 설정 `%APPDATA%\Axyne`, 로그 `%LOCALAPPDATA%\Axyne\logs`, 캐시 `%LOCALAPPDATA%\Axyne` (if it holds more than logs), 설치 폴더 (if anything is left; an empty install folder the backend could not remove is removed by the UI or listed as 빈 폴더), each with "폴더 열기" (Explorer); "남은 파일이 없습니다." when empty | DELEGATED (D26) | ASSUMED |
| Feedback survey | Success page only: "제거하는 이유를 알려주시겠어요? (선택)" with single-select chips 다른 IDE를 사용 / 필요한 기능이 없음 / 성능·메모리 / 다시 설치할 예정 / 기타 and an optional one-line comment (max 300 chars). 보내기 (enabled once a chip or text is given) opens `https://github.com/team-native/Axyne/issues/new?title=…&body=…` (UTF-8 percent-encoded; body = reason, comment, Axyne version) with ShellExecuteW and closes; 건너뛰기 closes. The uninstaller makes no network call | DELEGATED (D26) | ASSUMED |

## Backend command lines and journal

Installer backend (embedded NSIS, run by `installer_ui.c`):

```
backend.exe /S [/ALLUSERS] [/NOSTARTMENU] [/NODESKTOP] /JOURNAL="<temp>\axyXXXX.tmp.exe.journal" /D=<dir>
```

`/D=` stays last and unquoted (NSIS rule; the old UI quoted it, which broke paths with spaces).
Without `/JOURNAL` the backend behaves as before (no backups, no journal); without the shortcut
flags both shortcuts are created as before.

Journal (UTF-16LE, one CR LF terminated line per step, written *before* the step; a journal
write failure aborts the install):

| Line | Meaning |
|---|---|
| `T<TAB>kib` | payload size for progress |
| `N<TAB>path` | new file about to be written |
| `M<TAB>path<TAB>backup` | about to move the existing file to `backup` (intent, written after any stale backup was deleted) |
| `B<TAB>path<TAB>backup` | the move succeeded (confirmed); `path` is replaced next |
| `D<TAB>dir` | directory created by the backend (Start menu folder) |
| `K` | uninstall key created by this run |
| `V<TAB>name<TAB>old` | previous value of an uninstall-key value (empty = absent) |

The uninstall entry's `UninstallString` is quoted and carries the hive: `"<dir>\Uninstall.exe" /ALLUSERS` or
`/CURRENTUSER`.

Uninstaller backend: `/S [/ALLUSERS] _?=<install dir>`; `un.onInit` switches to the all-users
shell folders and `SHCTX` = HKLM for `/ALLUSERS`.

NSIS changes (`Axyne-Installer.nsi.in`): `.onInit`/`un.onInit` parse the options with
`FileFunc.nsh`, `SetRegView 64`; `File` lines go through the `AxyneFile` macro (journal/backup
then `File /oname=`); registry writes use `SHCTX` through `AxyneRegValue` (journals the old value);
shortcuts are conditional; the uninstall section also removes `.axyne-backup` and uses `SHCTX`.
The root `CMakeLists.txt` emits the MSVC runtime DLLs as `!insertmacro AxyneFile` lines so they are
journaled too. `generate-installer-config.cmake` adds `AXYNE_UI_INSTALL_BYTES` and counts the
license texts and the uninstaller in the payload size.

## Verification

- `wincheck.sh` (zig, MinGW headers) clean for `packaging/windows/installer_ui.c` and
  `packaging/windows/uninstaller_ui.c` (with a stub `installer_ui_config.h`).
- `makensis` 3.09 (Linux) compiles the configured script without warnings.
- Under Wine 9 (Xvfb) with a fake payload: fresh per-user install; upgrade (B entries, backups
  removed on success); cancel mid-upgrade restored the replaced files and left no backups/temp
  files; cancel after the registry step of a fresh install removed files, the Start menu folder
  and the uninstall key and restored the existing desktop shortcut; all-users install wrote HKLM
  (64-bit) and all-users shortcuts; uninstall per-user with and without user-data removal,
  all-users uninstall, running-Axyne warning + Axyne 닫기, survey URL decoded back to the
  expected title/body.
- After the review fixes, also under Wine: a stale `1.bak` was replaced by the real backup and
  cancel restored the original files; a file held open (no sharing) during cancel left
  `.axyne-backup` + `restore-list.txt` and showed the incomplete-restore page; running-Axyne
  warning on 구성 요소 with Axyne 닫기; scope lock with an existing HKCU install; quoted
  `UninstallString ... /CURRENTUSER`; uninstaller hint with duplicate HKLM/HKCU entries
  (`/ALLUSERS` → 모든 사용자, `/CURRENTUSER` → 현재 사용자); uninstall started with the working
  directory inside the install folder removed the folder; launch after install went through the
  desktop shell dispatch (Wine reports the wizard as elevated); 찾아보기 on a non-empty folder
  appended `\Axyne`, on an empty one did not.
- Not verified: a real Windows UAC prompt/elevated relaunch (Wine reports the process as
  elevated, so that branch did not run), the Windows-only working-directory lock (Wine does not
  lock a process's current directory, so the cwd fix could not be shown to matter there), the
  shell-dispatch failure note, MSVC `/W4` build, real payload sizes.
