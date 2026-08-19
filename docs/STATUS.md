# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-08-19 — **new workfront: G1 canvas
accessibility**, spec + plan both written, ready to start at task
A1.0. The canvas production arc is closed (all three gates decided)
and its board body moved to [`STATUS-LOG.md`](STATUS-LOG.md); the
E-arc is formally closed. No code has been written for the a11y arc
yet; baseline stands at 315/315.

## Workfront — G1 canvas accessibility

User-opened 2026-08-19. G1 (deferred 2026-08-14) reopened and decided.
Goal: make `Markoff::Canvas::View` usable with a screen reader.

- **Spec (normative):**
  [`specs/2026-08-19-g1-canvas-accessibility-design.md`](specs/2026-08-19-g1-canvas-accessibility-design.md)
- **Plan (do the topmost unchecked task):**
  [`plans/2026-08-19-g1-canvas-accessibility.md`](plans/2026-08-19-g1-canvas-accessibility.md)
  — phases A1 (tree/roles/registration), A2 (text interface),
  A3 (notifications), A4 (folding/actions), A5 (acceptance); user
  gate A-G1 = when to run the Orca pass. First task is **A1.0**, a
  throwaway probe settling whether `QAccessible::Attribute::Level`
  survives Qt's AT-SPI bridge.
- **Platform notes (spec §4.6):** the AT-SPI bridge lives inside
  `libQt6Gui` (no plugin to install); Orca is not installed yet
  (`extra/orca 50.2-1`); `ROLE_BLOCK_QUOTE` and `ROLE_MATH` are
  unreachable from Qt — stated limitations.
- **Scope decision (user, 2026-08-19):** per-block accessibility tree
  (`View` as `QAccessible::Document` container, one text-interface
  child per block over its own buffer) — chosen over a monolithic
  flat `QAccessibleTextInterface`, which would have required a **C4
  amendment** for a whole-document offset space. Acceptance is
  in-process offscreen tests plus one manual Orca pass at arc close.
  Driver is Markoff's own roadmap, not a Corbomite deadline.
- **Motivating gap:** canvas has zero a11y; source/styled inherit
  Qt's for free, so G2 (canvas as Corbomite's sole LivePreview
  engine) regressed Corbomite's editing surface.
- **Decided out:** `QAccessibleTableInterface` (spec §6), theme-side
  a11y — contrast/motion/font preferences (spec §1).
- **Baseline:** 315/315, unchanged. Core is expected to need nothing.

## Previous workfront (CLOSED) — canvas production arc (D5 part 1)

Opened 2026-08-13, closed 2026-08-15 at P7.3; all three gates now
decided (G1 reopened as the current workfront above, G2 done
2026-08-18, G3 retired `markoff-live` 2026-08-19). Arc-close baseline
**315/315**.

- Spec: [`specs/2026-08-13-canvas-production-design.md`](specs/2026-08-13-canvas-production-design.md)
- Plan (phase-by-phase SHAs + findings log):
  [`plans/2026-08-13-canvas-production-plan.md`](plans/2026-08-13-canvas-production-plan.md)
- **Full board body** (phase-by-phase narrative, all gate records)
  moved verbatim to [`STATUS-LOG.md`](STATUS-LOG.md), 2026-08-19
  banner — read it there if you need the history.

Standstill from that arc is **lifted**: `markoff-core` and
`markoff-canvas` are open for ordinary work, not restricted to
plan-named seams. `markoff-styled` stays bug-fix-only (backs
Corbomite's Reading mode); `markoff-source` stays untouched,
permanently; `markoff-live` is retired. Queue **#18** was absorbed
into the closed plan.

## Test baseline

> **Current baseline: 315/315 (100%)**, last verified 2026-08-15 at
> the canvas production arc close (P7.2g). `check-constitution.sh`
> clean. **This is the number to beat — any drop is a regression.**
> The entries below are the chronological accretion that got us here
> (oldest first); read them for history, not for the current figure.

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

P7.2f (2026-08-15): **full suite 315/315** (up from 314/314 at P7.2e)
— required by the plan's tier rule since this task added
`Theme::Slot::BracketMatchBackground` (a new, additive core `Theme.h`
enumerator + light/dark default colors). Canvas-scoped suite **37/37**
(up from 36/36), one new executable, `tst_canvas_p72f` (9 cases across
4 falsification pairs: scroll-past-end room-to-scroll, placeholder
focus-independence + typing-clears-it, bracket-match nesting depth,
drop-cursor drag-move tracking). `check-constitution.sh` clean
(C1–C4, 74 files).

P7.2g (2026-08-15): **full suite 315/315** (unchanged count — one new
`Theme::Slot::InvisibleCharBox` enumerator, required the full-suite
tier rule same as P7.2e/P7.2f, but no new test executable — extended
`tst_canvas_projection` with 2 cases). Canvas-scoped suite still
37/37. `check-constitution.sh` clean (C1–C4, 74 files). All 7 F1
gap-closure sub-tasks (P7.2a–g) now complete.

**Baseline reset 2026-08-19 (G3, `markoff-live` retired):**
`MARKOFF_BUILD_LIVE` now defaults OFF, so the default
`scripts/run-tests.sh` run no longer builds/registers the
`markoff-live` test executables. Re-verified same day: **207/207
(100%)** on a from-scratch `cmake -S . -B build-dev` + full build +
full suite, default config. The 315/315 figure above remains accurate
for a `-DMARKOFF_BUILD_LIVE=ON` build (source unchanged, build-fix-
only) but is no longer the default-config baseline; future baseline
figures in this doc are the default-config (LIVE-OFF) count unless
stated otherwise.

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
- E-arc — CLOSED 2026-08-19 (formally retired, not merely dormant;
  scope shipped under canvas Phase 5 — `docs/archive/e-arc/`).
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
