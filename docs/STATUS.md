# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-08-13 (board reset for the canvas spike)

## Workfront — markoff-canvas spike

Projection view leaf on `QTextLayout`; candidate replacement for the
widget-composition leaves. **Implementers: do the topmost unchecked
task in [`docs/plans/2026-08-13-markoff-canvas-spike.md`](plans/2026-08-13-markoff-canvas-spike.md)**
(progress is tracked in that plan's checklist, not here). Spec:
[`specs/2026-08-13-markoff-canvas-spike-design.md`](specs/2026-08-13-markoff-canvas-spike-design.md).
Decision record:
[`specs/2026-08-13-view-authority-direction-decision.md`](specs/2026-08-13-view-authority-direction-decision.md).

**Spike state:** not started (T0 next). Timebox: ~3 weeks from the T0
commit.

**Standstill:** live/styled/source + core are bug-fix-only until the
spike closes.

## Test baseline

**277/277 (100%)** on the full `scripts/run-tests.sh`, verified
2026-08-13. Any failure is a new regression (classify before fixing).

## Dormant items (not to be worked during the spike)

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
