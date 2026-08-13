# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-08-13 (canvas spike closed — PASS)

## Workfront — markoff-canvas spike: CLOSED, PASS

Projection view leaf on `QTextLayout`, candidate replacement for the
widget-composition leaves. All ten exit criteria (E1–E10) passed with
falsification proof; constitution (C1–C4) intact end to end, confirmed
by both the grep gate and T11's manual line-by-line read of every
canvas source file. Verdict and recommendation to the D5 design:
[`specs/2026-08-13-markoff-canvas-spike-design.md`](specs/2026-08-13-markoff-canvas-spike-design.md)
§10. Full task history/findings: same spec §9; task checklist + SHAs:
[`plans/2026-08-13-markoff-canvas-spike.md`](plans/2026-08-13-markoff-canvas-spike.md).

**Next:** the D5 design (candidate architecture + contingent
retirement of markoff-live/styled, decision record §5.3) is a new,
not-yet-opened arc — a user decision, not an implementer default.
Until D5 opens, treat the whole tree (including `libs/markoff-canvas/`)
as bug-fix-only.

Post-spike hands-on use of `libs/markoff-canvas/app` raised four
findings (spec §9 "Post-spike", queue **#18**). Two of them —
delimiter reflow via layout-string omission, and selection across
tables — are **D5 design inputs** and should be priced into that arc
rather than patched in as bug fixes.

## Test baseline

**288/288 (100%)** on the full `scripts/run-tests.sh` (277 standstill
+ 11 canvas), verified 2026-08-13 at spike close. Any failure is a new
regression (classify before fixing).

## Dormant items (not to be worked until D5 opens)

- Queue **#18** — canvas findings from first hands-on use (2026-08-13,
  post-verdict): delimiter hiding is colour-only and must become true
  reflow via layout-string omission + a per-`Entry` projection map;
  selection cannot cross a table; typed headings always render H1
  (`level` attr never set outside parse time); the Shift+Enter setext
  path works but is untested. Detail in spec §9 "Post-spike". The
  first two are D5 design inputs, not bug fixes.
- Queue **#13** — source cursor/selection translation rewrite.
- Styled tables — in-grid cell edit, row/col ops (deferred).
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
2026-08-13 pre-canvas snapshots of CLAUDE.md and queue.md).
