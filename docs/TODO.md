# Markoff TODO

## 2026-04-29 — Incremental parser + old-leaf deletion (this branch)

Work landed on `exploration/new-foundation`:

- `18cfa31` — `setCoalescingIdleMs` API removed (was a dead setter; no
  callers honored it).
- `309f9ce` — old leaves deleted: `markoff-core`, `markoff-live`,
  `markoff-reading`, `markoff-source`, `tests/markoff/`. Net −309k
  lines.
- `5d8f764` — `TreeSitterParser::parseIncremental(edits, newUtf8)`
  added with `ByteEdit` peer struct in markoff-parser. 10
  fingerprint-equivalence tests prove output matches a fresh parse
  across insertion/deletion/replacement/multi-edit/typing-burst/
  cross-block-reframe.
- `7b06a4d` — `Markoff::Document` now bakes `DocumentQueryResult` at
  construction; no longer holds a parser member. Public API
  unchanged.
- `eee1505` — `IncrementalParseSession` (foundation-side) owns one
  long-lived `TreeSitterParser` on the ParsePool worker thread.
  `ParsePool::scheduleReset()` added for `MarkoffDocument::resetContent`;
  `ParsePool::schedule()` is the incremental path. The session derives
  a single `ByteEdit` per call via prefix/suffix diff of prior vs new
  body; the `MarkoffEdit` list is intentionally not threaded through
  (avoids frontmatter/footnote shift math).
- `b01f913` / `e08c512` / `bc8dca1` — Inline-tree reuse (Phase 2 of
  incremental parsing). `parseIncremental` now snapshots old inline
  ranges + tree pointers before `ts_tree_edit`, applies the same edits
  to old inline trees so their internal node positions move to the new
  frame, then matches each new inline range against unconsumed shifted-
  old ranges by exact byte equality. Matched regions reuse the old
  `TSTree *`; unmatched regions parse fresh. New public
  `TreeSitterParser::inlineTreeReuseCount()` exposes per-call reuse for
  benchmarks. 6 new tests in `tst_incremental_parse` cover the reuse
  paths (counter scaffolding, single-paragraph edit, edit-inside-region,
  edits-in-two-regions, no-edits-buffer-replace).

End-to-end tests: 78/78 green including `tst_benchmark` (381s, was
404s) and `tst_realistic` (80s, was 87s).

**Per-keystroke parse cost on a 50KB doc dropped from ~8–12ms (full
reparse) to ~2–3ms (incremental block + full inline reparse) and now
to sub-millisecond for typical typing where most inline regions are
unaffected (incremental block + inline-tree reuse).**

### Open follow-ups (priority order)

1. **Stop pre-processing inside `Document`.** The footnote
   `[^1]` → `<sup>1</sup>` substitution is a render concern, not a
   parse concern. Moving it to the rendering layer makes the body
   diff trivial (== source diff) and removes the per-call regex
   pass. Bigger blast radius — touches markoff-parser, markoff-view-qml,
   markoff-source-widget. Worth doing but not urgent.
2. **Hard benchmarks of the new pipeline vs. the old.** The cost
   estimates above are theoretical (based on tree-sitter's documented
   subtree-reuse behavior). `tst_benchmark` exists but doesn't
   directly compare incremental vs fresh — adding a per-keystroke
   benchmark would let us put numbers on the win.

---

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
