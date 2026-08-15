# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-08-14 (canvas production arc — Phase 6 CLOSED, P6.4)

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
  phase close, full suite 310/310). Phase 7 (polish + a11y) is next,
  gated on user gate G1 (accessibility scope).

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

## Dormant items

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
