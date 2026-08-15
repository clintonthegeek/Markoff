# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-08-14 (canvas production arc — Phase 6 opened, P6.0 landed)

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
  opened 2026-08-14 with P6.0 (core anchor seam + fold retro-wire to
  Session, reduced scope — see plan findings log); next is P6.1.

Standstill after this opening (spec §7): canvas active; `markoff-core`
open **only** for plan-named seams; live/styled bug-fix-only until G3;
source untouched. Queue **#18** is absorbed into the plan (P1.1,
P2.1–P2.3 done).

## Test baseline

**307/307** on the full `scripts/run-tests.sh` (one new executable,
`tst_d2_block_crdt_anchor`, registered at P6.0), verified 2026-08-14 —
up from the 306/306 baseline at Phase 5 close via six new canvas
executables registered that phase (`tst_canvas_table_wrap_nav` P5.1,
`tst_canvas_table_ops` P5.2, `tst_canvas_math` P5.3,
`tst_canvas_media_seams` P5.4, `tst_canvas_side_content` P5.5,
`tst_canvas_folding` P5.6). `check-constitution.sh` clean (C1–C4) over
66 files. Perf re-baseline
(`build-perf`, `tst_canvas_perf_500` + `tst_canvas_perf_formatted`)
held with folding + table wrap/ops in the y-position walk: load→paint
188 ms/500 ms, p95 keystroke 1.38 ms/16 ms, scroll-realize
11.6 %/30 %, RSS delta 0 KB/100 MB. The plan ratchets this up per
task; any drop is a regression (classify before fixing).

## Dormant items

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
