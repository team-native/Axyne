# Release UI — issue 114 / PR 115

## Requirements and decisions
- Native themed release history/detail/filter/footer and nonblocking update notification. source: USER + Figma 57:3552/57:3696; status: CONFIRMED.
- Check real server using version/os/arch/channel, default stable. Reuse axyne_server_client; camelCase and legacy envelopes accepted. source: USER + Notion API; status: CONFIRMED.
- History includes publication dates when supplied. Missing dates remain explicitly unavailable, never inferred from creation time. source: IMPLEMENTATION; status: CONFIRMED.
- Filter categories feature/improvement/fix/notice; parse categorized Markdown headings/prefixes and preserve unclassified text under notice. source: IMPLEMENTATION; status: CONFIRMED.
- Specific-version suppression persists in release-state.json under config directory; reset via public API, isolated from settings JSON to avoid concurrent saves. source: IMPLEMENTATION; status: CONFIRMED.
- Settings worker owns settings controls. Release worker supplies reset API and integration contract only. source: USER; status: CONFIRMED.
- Installer wizard excluded. No unrelated account/plugin commits. No push before independent review. source: USER; status: CONFIRMED.

## Implementation units / task list
- [x] Merge reviewed shared network foundation (dc1e892).
- [ ] Shared release client/model, persistence, protocol and focused tests.
- [ ] Native Windows release browser and nonblocking notification; safe async ownership.
- [ ] Native macOS release browser and nonblocking notification; safe async ownership.
- [ ] Validate build/tests and visual UI; independent ephemeral review, fixes, coherent Korean commits.
- [ ] Final audit and settings handoff.

## Integration contract
`include/axyne/release_notes.h`: public C model and settings suppression reset API. All fetch and persistence calls are blocking: invoke from background workers, never native event callbacks. Returned snapshots are owned by caller and freed after views stop referencing them. Settings must call reset asynchronously and display its status; no changes to settings worker files here.
