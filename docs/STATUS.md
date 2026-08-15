# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-08-15 (canvas production arc — Phase 7, P7.2e landed)

## Workfront — canvas production arc (D5 part 1)

User-opened 2026-08-13, same day the spike closed PASS. Goal: take
`libs/markoff-canvas` to feature parity (Corbomite contract v2, old-leaf
parity, Obsidian Live Preview benchmark, collab rendering surface).

- **Spec (normative):**
  [`specs/2026-08-13-canvas-production-design.md`](specs/2026-08-13-canvas-production-design.md)
- **Plan (do the topmost unchecked task):**
  [`plans/2026-08-13-canvas-production-plan.md`](plans/2026-08-13-canvas-production-plan.md)
  — phases P1–P7, user gates G1 (a11y scope), G2 (Corbomite adoption),
  G3 (retirement decision). Phase 1 (core promotions) closed
  2026-08-13 at P1.5; Phase 2 (projection map) closed 2026-08-14 at
  P2.4 (perf re-baseline, all E9 budgets held); Phase 3 (MarkdownView
  contract v2) closed 2026-08-14 at P3.7; Phase 4 (inline/text parity)
  closed 2026-08-14 at P4.8; Phase 5 (block parity) closed 2026-08-14
  at P5.7 (perf re-baseline held). Phase 6 (collaboration surface)
  opened and closed 2026-08-14: P6.0 (core anchor seam + fold retro-
  wire to Session, reduced scope — see plan findings log), P6.1
  (Session caret authority closure), P6.2 (remote presence rendering —
  caret bar, name flag, selection tint), P6.3 (IME-vs-concurrent-
  remote-edit + seeded gremlin fuzz convergence test — no C1/C2
  workaround needed, both tests passed on the first run), P6.4 (⏸
  phase close, full suite 310/310). **G1 decided 2026-08-14: user
  deferred accessibility** — P7.1 skipped for this arc, canvas ships
  with no a11y support this arc (explicit, logged gap, not an
  oversight). P7.2 (drag-drop + middle-click paste) landed the same
  day: text drag out (Copy while read-only, Copy+Move once editable —
  self-drop Move is treated as a copy, logged decision, see plan
  findings log), text/file drag in (text routes through the same
  `insertText()` `paste()` uses; file drops emit
  `EditorWidget::fileDropped(urls, viewportPos)`, Corbomite decides
  embed-vs-link), and X11 primary-selection middle-click paste
  (no-op under this leaf's offscreen test environment, which has no
  platform clipboard integration — real behavior can't be exercised
  here, only the guard path). User then directed F1 gap closure
  (P7.2a-g, 2026-08-14): **P7.2a (undo-coalescing defect, F1 #3)**
  landed same day — `View::insertPrintable` now routes each typed
  character through `Cmd::insertCharacter`, so printable-only,
  same-block runs within 1000ms coalesce into one undo entry
  (`UndoLog::maybeCoalesceOrTransaction`) instead of one entry per
  keystroke; surrogate-pair codepoints (emoji) fall back to a direct
  `maybeCoalesceOrTransaction` call with `isPrintable=false` to avoid
  the QChar-only signature's data-loss trap (see plan findings log).
  No core change needed. **P7.2b (editing-command floor, F1 #1)**
  landed same day: word-wise motion + selection (Ctrl+Left/Right,
  Ctrl+Shift+Left/Right — `QTextBoundaryFinder`, same idiom
  `markoff-live` already uses, not hand-rolled), word-wise delete
  (Ctrl+Backspace/Delete), document start/end (Ctrl+Home/End, now
  moving the caret in addition to the pre-existing scroll-to-extreme),
  delete-line (Ctrl+Shift+K — clears the block's content), move-line
  up/down (Alt+Up/Alt+Down — a content swap between adjacent
  `BlockId`s, since core's `StructuralOp`/`IdList` has no reorder
  primitive; logged, not a core change), select-line (Alt+L), and Esc
  simplify-selection. New test `tst_canvas_editing_command_floor` (4
  falsification-backed scenario groups). P7.2b's agent found
  `tst_canvas_concurrency`'s gremlin-fuzz convergence test failing
  deterministically and mis-logged it as pre-existing; independently
  re-bisected and **fixed same day**: a latent `UndoLog` bug (coalesced
  transactions never fired `onCommit`, silently dropping ops to collab
  peers after a run's first keystroke) made reachable for the first
  time by P7.2a's routing change (commit `623ed6ca`; see Dormant items
  and the plan findings log's "Regression fix" entry for the full
  writeup). **P7.2c (auto-pairing/wrap-selection, F1 #4)** landed
  2026-08-15: 5 named pairs, view-local freshness tracking
  (`m_autoPairedClose`), insertion routed through the same
  `insertPrintable`/`Cmd::insertCharacter` machinery the regression
  fix lives in (deliberately, to not reintroduce that bug class) — see
  plan findings log. **P7.2d (Enter/Backspace semantics checklist,
  F1 #5)** landed 2026-08-15, test-only: diffed `StructuralKeyHandler`
  against CodeMirror lang-markdown's `insertNewlineContinueMarkup`/
  `deleteMarkupBackward` case by case. 3 of 4 documented cases already
  correct — 2 had no direct regression test (ordered-list renumber on
  mid-split; empty-nested-item outdent) and got one each; the 3rd
  (blockquote continuation losing quote depth) is a pre-existing,
  already-logged follow-up (`docs/plans/2026-05-29-styled-structural-
  key-authority.md:670`), re-confirmed not re-fixed. The 4th case is a
  **real, found gap left unfixed on purpose**: CM's Backspace at an
  indent-0 list item's content-start de-lists the line without
  touching the previous block; ours instead merges into the previous
  block via `Cmd::backspaceMerge` — existing, deliberately-tested
  behavior (`listitem_backspace_at_start_indent0_merges`), not a fresh
  regression, and changing it would flip a shared core handler's
  documented behavior against its own test's name — logged as a
  dormant item (below) rather than changed unilaterally. No core
  source change landed. **User decided same day: switch to CM's
  de-list-in-place semantics** — implemented and closed, see Dormant
  items below. **P7.2e (highlight selection occurrences, F1 #7)**
  landed 2026-08-15: a non-trivial (min length 2), non-whitespace-only
  selection gets every OTHER exact-text occurrence in the realized
  entries painted with a new `Theme::Slot::SelectionOccurrenceBackground`
  (green, distinct from both the active selection's blue and find's
  orange), via `View::recomputeOccurrenceHighlights()` hooked into the
  existing `pushSelectionToSession()` chokepoint — same draw-time
  `QTextLayout::FormatRange` mechanism `setFindHighlights` established.
  Realized-entries-only scope (matches every other paint-time
  highlight feature in this leaf); case-sensitive, no whole-word
  requirement (CM `highlightSelectionMatches` defaults). Phase 7
  continues at P7.2f (scroll-past-end + placeholder + bracket-match +
  drop-cursor) through P7.2g, then P7.3 (⏸ arc close: Obsidian parity
  audit + full audit).

Standstill after this opening (spec §7): canvas active; `markoff-core`
open **only** for plan-named seams; live/styled bug-fix-only until G3;
source untouched. Queue **#18** is absorbed into the plan (P1.1,
P2.1–P2.3 done).

## Test baseline

**Full suite: 310/310 (100%)**, re-verified 2026-08-14 at Phase 6
close (P6.4) — up from the 307/307 baseline at P6.0 via three new
canvas executables registered this phase (`tst_canvas_session_selection`
P6.1, `tst_canvas_remote_presence` P6.2, `tst_canvas_concurrency` P6.3).
`check-constitution.sh` clean (C1–C4) over 69 files. Honest re-read of
the phase's diff for C1/C2 smells the script doesn't literally
string-match: clean (plan findings log, P6.4 entry). The previously
reported "pre-existing build break" in `tst_view_contract_live_caret_rect`
(P6.1 finding) did **not** reproduce at P6.4's clean whole-tree
rebuild — stale build-directory artifact, not a real defect; dormant
item withdrawn. Perf re-baseline not re-run this phase (P6.0–P6.3 are
caret/selection/fold/presence bookkeeping + paint-time-only additions,
none of which touch the y-position walk E9 tracks — same judgment
P3.7/P4.8 made for their non-layout phases); last-held figures (P5.7):
load→paint 188 ms/500 ms, p95 keystroke 1.38 ms/16 ms, scroll-realize
11.6 %/30 %, RSS delta 0 KB/100 MB. The plan ratchets this up per
task; any drop is a regression (classify before fixing).

**Canvas-scoped suite (`scripts/run-tests.sh -R canvas`): 33/33**,
verified 2026-08-14 at P7.2 — up from 32/32 via one new executable,
`tst_canvas_drag_drop` (drag-out MIME shape, drop-position accuracy,
file-drop signal shape, middle-click no-op-when-unsupported).
`check-constitution.sh` clean (C1–C4). Full suite not re-run per the
plan's tier rule (P7.2's diff stays inside `libs/markoff-canvas/`, no
core seam touched); the 310/310 full-suite figure above is still the
last verified whole-tree number, from P6.4.

P7.2a (2026-08-14) re-verified 33/33 on the same canvas-scoped suite —
no new executable, `tst_canvas_undo` extended with
`consecutive_printable_keys_coalesce_into_one_undo_entry`.
`check-constitution.sh` clean. Full suite not re-run (diff stays
inside `libs/markoff-canvas/`; the only core header touched,
`Cmd/D2.h`, is pre-existing public API, no core seam changed).

P7.2b (2026-08-14): canvas-scoped suite **34/34** — up from 33/33 via
one new executable, `tst_canvas_editing_command_floor` (word motion/
delete, doc start/end, delete/move/select-line, Esc-simplify; 4
falsification-backed groups). `check-constitution.sh` clean (C1–C4,
71 files). **Correction:** this task's agent flagged
`tst_canvas_concurrency`'s gremlin-fuzz failure as "pre-existing" —
independently re-bisected by the orchestrator and found to be a real
regression introduced by P7.2a, one commit prior; fixed same day
(`623ed6ca`, see Dormant items below and the plan findings log).

Regression fix (2026-08-15): full suite **312/312** (up from 311/311
at P7.2) — the only run since P6.4 that re-verified the whole tree,
since a core header (`UndoLog.h`) was touched. `check-constitution.sh`
clean.

P7.2c (2026-08-15): canvas-scoped suite **35/35** — up from 34/34 via
one new executable, `tst_canvas_auto_pair` (8 cases: auto-pair,
type-through, wrap-selection, bold two-star completion, fresh-pair
Backspace, manually-typed-pair non-interference). `check-constitution.sh`
clean (C1–C4, 72 files). `tst_canvas_concurrency` (incl. the gremlin
fuzz) explicitly re-run 3x to confirm no regression in the shared
typing path. Full suite not re-run per the plan's tier rule (no core
seam touched this task).

P7.2d (2026-08-15): **full suite 313/313** (up from 312/312 at the
regression fix) — required by the plan's own tier rule since this
task's diff touches `libs/markoff-core/tests/` even though it's
test-only, not a source change. `tst_structural_key_handler` 27/27
(up from 25/25): two new regression tests
(`listitem_enter_mid_split_ordered_list_renumbers`,
`listitem_enter_empty_at_indent_gt0_outdents_not_exits`). No canvas
files touched, so `check-constitution.sh` not applicable this task.

P7.2e (2026-08-15): **full suite 314/314** (up from 313/313 at P7.2d)
— required by the plan's tier rule since this task added
`Theme::Slot::SelectionOccurrenceBackground` (a new, additive core
`Theme.h` enumerator + light/dark default colors). One new executable,
`tst_canvas_selection_matches` (3 cases: selecting a repeated word
highlights its other occurrences, not its own span; sub-minimum-length
and whitespace-only selections highlight nothing; collapsing the
selection clears all highlights). `check-constitution.sh` clean
(C1–C4, 73 files).

## Dormant items

- **CLOSED 2026-08-15 (commit `fc7ea6fe`):** `StructuralKeyHandler`'s
  indent-0 ListItem Backspace previously merged into the previous
  block (P7.2d finding); user decided to switch to CodeMirror's
  de-list-in-place semantics instead. `listItemBackspace`'s indent==0
  branch now converts the block to `Paragraph` + clears `MarkerStyle`
  (mirrors `listItemEnter`'s exit-list branch), never touching the
  previous block. No renumbering needed (de-listing only ever splits a
  run, never merges two). Cross-leaf check caught a `markoff-styled`
  test also asserting the old behavior (`markoff-live`/`markoff-source`
  had none) — renamed and rewritten to match. Full suite 313/313. Full
  writeup: plan findings log, "P7.2d addendum" entry.
- **CLOSED 2026-08-15 (commit `623ed6ca`):** the P7.2a-introduced
  gremlin-fuzz convergence regression (fixed seed `3237998146`,
  bisected to exactly one commit — see the plan findings log's
  "Regression fix" entry, inserted between P7.2a and P7.2b, for the
  full root-cause writeup). Root cause: `UndoLog::
  maybeCoalesceOrTransaction`'s coalesce-extend branch never fired
  `onCommit` for ops after the first keystroke of a coalescing run, so
  they were applied locally but silently never sent to collab peers —
  a latent bug in `UndoLog` made reachable for the first time by
  P7.2a's `insertPrintable` → `Cmd::insertCharacter` routing. Fixed by
  firing `onCommit` explicitly for the coalesced-in ops. Gremlin fuzz
  now passes 5/5 consecutive runs; P7.2a's own coalescing test still
  passes — both properties hold simultaneously. Full suite 312/312.
- **Self-drop text-drag Move does not delete the source selection**
  (P7.2, 2026-08-14) — dragging a selection and dropping it back
  inside the SAME `View` duplicates the text instead of moving it
  (the source selection is left intact even though the drop reports
  `Qt::MoveAction`). Deliberate scope cut, not a bug: deleting the
  source after the drop's own insert has already run in the same
  document would need re-deriving the source range's now-stale byte
  offsets, which this leaf's raw per-block-byte `CanvasCursor` model
  (C4) has no cursor-adjustment machinery for. Cross-view/cross-app
  Move (drop target ≠ this view's own viewport) works correctly. Full
  writeup: plan findings log, P7.2 entry.
- **`BlockLayoutCache::m_preeditByte` goes stale across a remote edit
  mid-composition** (P6.3 finding, 2026-08-14) — cosmetic only,
  bounded to the paint between a remote edit landing and the next real
  IME event. `View::onDocumentChanged()` re-resolves the CARET's byte
  offset from the Session anchor correctly (P6.1) and that IS what
  commit-time positioning uses, so no functional bug results — but the
  cache's own preedit splice point (`BlockLayoutCache::setPreedit`'s
  stored offset) is never re-issued from `onDocumentChanged()`, so a
  remote edit before the caret during composition visually splices the
  preedit at the wrong position for one paint. Fix sketch: in
  `onDocumentChanged()`, after caret resolution, re-call
  `m_cache->setPreedit(...)` with the fresh `m_caret` when
  `isComposing()`. Not fixed this task (production `View.cpp` change,
  outside a tests-only task's scope, and no public inspection surface
  exists to test it directly). Full writeup: plan findings log, P6.3
  entry.
- **Remote presence name flag has no fade** (P6.2, 2026-08-14) —
  `Selection::cursorVersion` is an opaque monotonic counter, not a
  timestamp, so a real "fade out N seconds since last move" needs
  either a core timestamp field (out of P6.2's closed-core scope) or
  view-local per-participant timer state this leaf's draw-time-only
  design otherwise avoids. Painted at full, constant opacity. Full
  writeup: plan findings log, P6.2 entry.
- **`MarkoffDocument::blockAt(const TextAnchor &)` always returns
  `nullopt`** (P6.1 finding, 2026-08-14) — its implementation reads
  `d->latestBlockRanges`/`d->latestBlockAnchors`
  (`MarkoffDocumentPrivate.h`), which are never assigned anywhere in
  `markoff-core/src` (confirmed by a whole-tree grep) — not "stale
  after D2 edits" as a neighboring comment on `offsetInBlock()`
  suggests, but permanently empty on every document, D2 or legacy.
  `blockAnchorAt(int)` and `blockByteRange(BlockAnchor)` share the same
  dead-data dependency and are equally non-functional. P6.1 needed
  `blockAt()` for its Session-anchor resolve step and worked around it
  (`TextAnchor::block()` + the confirmed-D2-safe
  `offsetInBlock(BlockAnchor, TextAnchor)`) rather than fix it —
  `markoff-core/` was closed for that task. Worth a real core session:
  either populate `latestBlockRanges`/`latestBlockAnchors` for real, or
  retire the three accessors and redirect any other silent callers to
  the D2-safe alternatives. Full writeup: plan findings log, P6.1 entry.
- **CLOSED 2026-08-14 (P6.0, commit `f2e705d5`):** the D2-safe core
  accessor now exists (`MarkoffDocument::blockCrdtAnchorAt`/
  `resolveBlockCrdtAnchor`) and `View::toggleFold()` writes every fold
  through to `Session::foldedRegions`/`FoldRef`. **New dormant item
  opened in its place:** the reverse direction (rebuilding
  `View::m_foldedHeads` FROM cold/restored `Session` state) is
  unsound as designed — per-block CRDT buffers share one Lamport
  clock seed (`Buffer(d->replicaId)`, no per-block offset), so
  different foldable blocks' byte-0 anchors routinely collide and
  `FoldRef::start` alone can't disambiguate which block a collided
  anchor names. `m_foldedHeads` stays the View's own write authority;
  Session is a write-through mirror only. Closing generally needs
  `FoldRef` to carry a block identity (core schema change). Full
  writeup: plan findings log, P6.0 entry.
- **Inline math (`$...$`) renders as styled monospace, not real
  glyphs** (P5.3 finding) — **now a scheduled arrival, not a Qt
  limitation** (2026-08-14 investigation): standalone `QTextLayout`
  gains a C3-clean inline-object path in **Qt 6.12** (qtbase
  `be73ca50a34`; U+FFFC + `QTextImageFormat` via `setFormats()`).
  Build machine is on 6.11.1, so the styled-text fallback stands
  until then. Mechanism: spec §4.5; implementation steps: plan gated
  task **G-Q612**; full record: plan findings log 2026-08-14. No
  pre-6.12 shim (rejected, same entry).
- **Parser gap: `latex_span`/`latex_block` delimiter spans never get
  `parentCharStart`/`parentCharEnd`** (P5.3 finding) —
  `collectParentRanges` checks for a `latex_span` node type
  markoff-parser's grammar never emits (both `$...$` and `$$...$$`
  parse as `latex_block`). Worked around with per-block (not per-span)
  math reveal; revisit if per-span granularity is ever needed.
- **Table/code horizontal-pan-within-own-rect** (P4.5 carried scope):
  tables and code blocks currently take the (possibly narrowed)
  readable-line-width column like any other block instead of
  overflowing it with their own horizontal scroll (Obsidian's actual
  behavior). Needs per-rect horizontal scroll-offset state that
  doesn't exist yet in `BlockLayoutCache`/`View`. Should land before
  the Obsidian-parity audit (P7.3) if still open then.
- **`Theme` default palette has no colors for the 16 `Code*` token
  slots** (P4.6 finding) — `Kf6SyntaxHighlightService` wiring is
  correct end-to-end but renders no token differentiation under
  `defaultLight()`/`defaultDark()` until those slots are defined.
  Small, cleanly-scoped core follow-up.
- Queue **#13** — source cursor/selection translation rewrite.
- Styled tables — in-grid cell edit, row/col ops (moot if G3 retires
  styled; frozen until then).
- Release scaffolding — install/export rules + header tiering for a
  non-submodule consumer.
- E-arc — dormant since 2026-05-25 (`docs/e-arc/`, closed board).
- Discipline-Log open smells — `docs/queue.md`.

## Corbomite pin

Corbomite tracks Markoff at `libs/markoff-family`. As of 2026-08-12
(Corbomite `fde31e82`) pinned at Markoff `b349f122`. Re-pin guidance:
jump to current master when next touched; **never** pin into
`8c13c5d..079ac1f` (list-after-table SIGSEGV window, fixed `b1b238f`).

## History

Arc dispositions, closed queues, and dated banners:
[`STATUS-LOG.md`](STATUS-LOG.md), `docs/archive/` (incl. the
2026-08-13 pre-canvas snapshots of CLAUDE.md and queue.md; spike
verdict: `specs/2026-08-13-markoff-canvas-spike-design.md` §10).
