# Markoff TODO

## 2026-04-28 — Phase 13 (Foundation library Part 2) acceptance passed

All 25 `tst_foundation_*` targets pass on a fresh build. The `markoff_foundation`
library is feature-complete per spec §12 (foundation-library.md + Part 2 plan).
Baseline captured in `docs/2026-04-28-foundation-tests-baseline.log` (145/147
suite-wide; only `tst_markoff_undo_grouping` and `tst_markoff_table_operations`
fail — both pre-existing master-side failures, not regressions).

---

## 2026-04-28 — markoff-view-qml POC complete (Phase 1)

The QML POC view library landed: 24-task plan + 7 follow-up `fix:`/`perf:` commits = 31 commits on top of foundation acceptance, 30 on top of `ca5ff52`. All architectural goals validated:

- Foundation API consumes cleanly from a non-current-Live toolkit (QML).
- Cycle-guarded bidirectional edit bridge works end-to-end (T12/T13/T14).
- Source-mode editor renders markdown source with KSyntaxHighlighting coloring.
- Selection round-trips through `Session::primarySelection()` always (Phase-2 cross-block selection inherits this scaffold).
- `parseUpdatedAt(parsed, version)` signal version-tagged; AST inspector pane in test app exercises the seam day-one.
- The Phase-2 growth seam (`MarkoffEditor.qml` outer shell) is in place; Live mode is a sibling-of-SourceEditor away.

**6/6 view-qml test executables green; 25/25 foundation tests still green** after the foundation prework (T0 widened `parseUpdated` signal signature).

**Deferred to perf phase:**
- Typing in long documents (~16KB) freezes the UI for tens of seconds. Allocation-free UTF-8/16 conversion (commit `5b116be`) wasn't enough; further investigation needed. See `docs/handoff/2026-04-28-post-poc-perf-SESSION-BRIEF.md`.

**Deferred to Phase 2 (live preview):**
- Live-formatted rendering (`**bold**` displayed as bold inline).
- Inline image / table / math / mermaid rendering.
- DelegateChooser per-AST-block delegates.
- Cross-block selection.
- `EditorHighlighter` (the second `QSyntaxHighlighter` for code-block content overlay).
- File save / save-as / file picker.
- Multi-cursor UI / replace UI / link Ctrl-click activation.

Tag: `exploration/foundation-poc-2026-04-28` (will be created after this commit lands).
