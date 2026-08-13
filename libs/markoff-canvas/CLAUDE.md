# markoff-canvas — projection view leaf (SPIKE)

Custom `QAbstractScrollArea` widget rendering `MarkoffDocument`
directly: one `QTextLayout` per block, own input pipeline. **No
`QTextDocument`, no QML, no second document model, ever.**

This leaf is a time-boxed spike. Everything you need is in two
files — read them in this order, then start:

1. **Plan (do the topmost unchecked task):**
   [`docs/plans/2026-08-13-markoff-canvas-spike.md`](../../docs/plans/2026-08-13-markoff-canvas-spike.md)
   — session protocol, API cheat sheet, task checklist T0–T11.
2. **Spec (normative):**
   [`docs/specs/2026-08-13-markoff-canvas-spike-design.md`](../../docs/specs/2026-08-13-markoff-canvas-spike-design.md)
   — authority model (§2), constitution C1–C4 (§6), exit criteria
   E1–E10 (§7), findings log (§9).

## The four hard rules (constitution — violating one ends the spike)

- **C1** no re-entrance guards (`m_applying*`, `isApplying*`, …).
- **C2** no `singleShot(0)` / `Qt.callLater` / queued-connection
  deferrals in this leaf.
- **C3** no `QTextDocument`, `QTextEdit`, `QPlainTextEdit`, or Quick
  text types.
- **C4** one coordinate space: per-block UTF-8 byte offsets. No
  `applyFlatEdit`, no `flatView()`, no cross-block byte sums.

`tests/check-constitution.sh` gates all four; run it before every
commit. If an exit criterion seems to *require* violating a rule,
that is the spike's answer — stop, log it in spec §9, report.

## Build / test

```bash
cmake --build build-dev -j 4          # never more than -j 4
scripts/run-tests.sh -R canvas        # offscreen; never --direct
```
