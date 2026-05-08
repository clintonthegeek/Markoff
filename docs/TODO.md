# Markoff TODO

## 2026-05-07 — ListItem Tab/Shift-Tab: no parent-existence guard

The Tab (indent) and Shift-Tab (outdent) handlers in
`LiveStructuralKeyHandler.cpp` increment/decrement `IndentLevel` by one
with only a floor-of-zero and ceiling-of-6 boundary check. There is no
check that a parent item at `newIndent - 1` actually exists in the
surrounding run. A lone item at level 2 can be tabbed to level 3 even
though no sibling at level 2 is present to parent it.

In practice this produces a structurally odd list but does not crash or
corrupt data — the item serializes and round-trips correctly, and the
delegate renders the extra indentation faithfully. It's cosmetically
weird and semantically wrong but not a regression from anything prior.

**When to fix:** D5 or as a small standalone. The fix should walk the
preceding blocks and refuse the indent if no block at `newIndent - 1`
exists within the same style/indent boundary that `renumberRunStartingAt`
would recognise as a run.

---

> **2026-05-07 — D4 complete. D5 (collab activation) is the next design target.**
> See `docs/d-arc/d-arc-status.md` for the live status board.
> D2 / D3 / D4 are all shipped; the D-arc roadmap is at
> `docs/d-arc/2026-05-04-d-arc-roadmap.md`.

> **2026-05-02 — (closed; arc retired and archived 2026-05-07) live render restoration arc.**
> All entries below this banner predate the restoration arc. The arc
> itself was closed by the D-evolution pivot 2026-05-04 and the paper
> trail was archived under `docs/archive/c-restoration-arc/` on
> 2026-05-07 (see `docs/handoff/2026-05-07-pivot-to-d5-first.md` §2.2).
> Older entries kept for historical reference; do not resume them
> without consulting the pivot doc.

## 2026-05-02 — R2 dogfood: LiveView needs a scrollbar

`ListView` in `LiveView.qml` has no attached `ScrollBar`. Without a mouse
wheel or a draggable element in the viewport, keyboard-only and
pointer-only navigation is impossible. Fix: one-liner in `LiveView.qml`:

```qml
ScrollBar.vertical: ScrollBar {}
```

Earliest natural slot: R3 (when the plan covers `LiveView.qml` anyway for
cursor/focus routing). Can land as a standalone commit if convenient before
that.

---

## 2026-04-30 — BlockAnchor foundation complete; two perf follow-ups

The 14-task BlockAnchor foundation plan
(`docs/plans/2026-04-30-block-anchor-foundation.md`) landed as commits
`7f0bcad..5bb4491` on `exploration/new-foundation`. 96/96 tests pass.
The plan's hard-precondition for the live-editing plan
(`docs/plans/2026-04-30-live-editing.md`) is satisfied — that plan can
now begin.

Two perf gaps the work surfaced are deferred to follow-up specs:

1. **`computeBlockAnchors` per-parse cost is ~32ms** on a 100-block /
   50KB doc, dominated by per-block CRDT anchor lookups (the foundation-
   side scanner alone meets the < 1ms budget). The compute test
   `tst_foundation_block_anchor_perf::anchor_compute_per_parse_regression_guardrail_50KB_doc`
   ships as a 50ms regression guardrail rather than a strict budget.
   Root cause is the same as the `Global::join` hot spot tracked in
   `docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md`. A
   follow-up spec should either tighten via batch-anchor-lookup or
   address the underlying CRDT structure.

2. **`toMarkdownUtf8()` per-parse copy in the relay lambda.** The
   parse-relay in `MarkoffDocument`'s constructor calls
   `toMarkdownUtf8()` once per parse to feed `computeBlockAnchors` —
   ≈50KB of QByteArray construction per keystroke on a 50KB doc. No
   regression guardrail yet. Possible mitigations: have
   `ParsePool::parseReady` carry the UTF-8 it parsed (so the relay
   reuses it), or expose the parsed `Document`'s source bytes via a
   `sourceUtf8()` companion accessor.

Optional follow-up cleanups (none blocking):
- Add `qHash(const Markoff::BlockAnchor &)` so `QHash<BlockAnchor, …>`
  works out-of-box for downstream consumers (e.g. AstBlockDiff move
  detection).
- Index `latestBlockAnchors` / `latestBlockRanges` via a `QHash` so the
  block-aware queries on `MarkoffDocument` (`blockByteRange`,
  `blockAt`, `offsetInBlock`, block-local `textAnchorAt`) are O(1)
  rather than O(n) linear scans.
- Sweep the remaining `Crdt::Anchor` exposures on
  `Session::topVisibleAnchor`/`scrollChanged` and `FoldRef` —
  pre-existing public-boundary surfaces this work didn't touch.

## 2026-04-30 — Revert bench small-replicaId workaround (collabtext side fixed)

`apps/bench/markoff-bench-render.cpp` (≈line 252) and
`libs/markoff-bench/src/ScenarioRunner.cpp` use a small monotonic
`replicaId` to sidestep collabtext bug 0008577 (SBO heap-promotion
crash on first heap-bound `Global::observe`). Documented in
`docs/handoff/2026-04-30-collabtext-sbo-regression-repro.md`.

Collabtext has fixed the bug. The small-replicaId workaround is now
safe to revert: switch back to a random `quint16` (or whatever the
pre-workaround code used) and re-run the render bench matrix to
confirm nothing regresses.

When reverting:
- update `docs/handoff/2026-04-30-collabtext-sbo-regression-repro.md`
  to mark the upstream as fixed (or move the file to `docs/archive/`).
- delete the inline `// We use a small monotonic replicaId rather
  than a random uint16…` comment block(s) at the workaround sites.
- re-run `markoff-bench-render --mode live` across the full corpus
  and confirm no crashes.

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
- `9bf92fe` / `955e935` / `bd9d69a` / `92e57f1` — Footnote cleanup
  (Option A of `docs/specs/2026-04-29-footnote-cleanup-design.md`).
  `Document::extract` no longer rewrites the body: the dead
  `[^N]` → `<sup>N</sup>` substitution and the definition-line strip
  are gone, so `body == source.mid(frontmatterBlockEnd)` byte-for-byte.
  New `Document::footnoteRefs()` exposes per-reference label + number +
  source offset for the future live preview. The reference scan and
  numbering pass became a single sweep with a definition-prefix skip
  rule. `Document::footnotes()` unchanged. Body diff in
  `IncrementalParseSession` now equals the source diff modulo a
  constant frontmatter offset — perf win on every footnote-related
  edit. Option B (custom tree-sitter grammar with native
  footnote_reference / footnote_definition / inline_footnote nodes,
  Obsidian inline `^[content]`, multi-paragraph defs) is captured as a
  deferred epic in the spec.

End-to-end tests: 78/78 green including `tst_benchmark` (~400s) and
`tst_realistic` (~85s).

**Per-keystroke parse cost on a 50KB doc dropped from ~8–12ms (full
reparse) to ~2–3ms (incremental block + full inline reparse) and now
to sub-millisecond for typical typing where most inline regions are
unaffected (incremental block + inline-tree reuse).** `Document::extract`
no longer regex-rewrites body on every keystroke.

### Open follow-ups (priority order)

1. **Hard benchmarks of the new pipeline vs. the old.** The cost
   estimates above are theoretical (based on tree-sitter's documented
   subtree-reuse behavior). `tst_benchmark` exists but doesn't
   directly compare incremental vs fresh — adding a per-keystroke
   benchmark would let us put numbers on the win.

---

## 2026-04-28 — Phase 13 (Foundation library Part 2) acceptance passed

All 25 `tst_foundation_*` targets pass on a fresh build. The `markoff_core`
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
