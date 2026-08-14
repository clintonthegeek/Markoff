# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-08-14 (canvas production arc — Phase 4 CLOSED, P4.8)

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
  closed 2026-08-14 at P4.8. Phase 5 (block parity) is next, starting
  at P5.1.

Standstill after this opening (spec §7): canvas active; `markoff-core`
open **only** for plan-named seams; live/styled bug-fix-only until G3;
source untouched. Queue **#18** is absorbed into the plan (P1.1,
P2.1–P2.3 done).

## Test baseline

**300/300 (100%)** on the full `scripts/run-tests.sh`, re-verified
2026-08-14 at Phase 4 close (P4.8) — up from the 293/293 baseline at
Phase 3 close via seven new canvas executables registered this phase
(`tst_canvas_links` P4.2, `tst_canvas_format_ops` P4.3,
`tst_canvas_context_menu` P4.4, `tst_canvas_layout_width` P4.5,
`tst_canvas_inline_title` P4.9, `tst_canvas_code_highlight` P4.6,
`tst_canvas_task_checkbox` P4.7); P4.1's inline-kind-set growth landed
in the existing `tst_canvas_inline_formatting` binary rather than a
new one; `tst_format_ops` (core) grew 8 per-block cases (P4.3).
`check-constitution.sh` clean (C1–C4) over 46 files. The plan ratchets
this up per task; any drop is a regression (classify before fixing).

## Dormant items

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
