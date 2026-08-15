# markoff-canvas — projection view leaf (production arc)

Custom `QAbstractScrollArea` widget rendering `MarkoffDocument`
directly: one `QTextLayout` per block, own input pipeline. **No
`QTextDocument`, no QML, no second document model, ever.** The
2026-08-13 spike proved the premise (PASS, E1–E10); this leaf is now
being built out to feature parity as Markoff's candidate primary
editing leaf.

Everything you need is in two files — read them in this order:

1. **Plan (do the topmost unchecked task in the current phase):**
   [`docs/plans/2026-08-13-canvas-production-plan.md`](../../docs/plans/2026-08-13-canvas-production-plan.md)
   — session protocol, cheat sheet, phase/task checklist P1–P7,
   findings log.
2. **Spec (normative):**
   [`docs/specs/2026-08-13-canvas-production-design.md`](../../docs/specs/2026-08-13-canvas-production-design.md)
   — authority model (§2), constitution (§3), architecture deltas
   (§4), parity contract (§5), user gates (§8).

Spike record (verdict + findings the plan cites):
[`docs/specs/2026-08-13-markoff-canvas-spike-design.md`](../../docs/specs/2026-08-13-markoff-canvas-spike-design.md).

**Status (2026-08-15):** Phases P1–P6 closed; Phase 7 closing at
P7.3 (arc close) — G1 (accessibility) deferred by user decision, all
7 F1 CodeMirror-parity gaps (P7.2a–g) closed. Full suite 315/315,
perf budgets held, constitution clean. G2 (Corbomite adoption) is the
next open question, for the user.

## The four hard rules (constitution — now permanent law, spec §3)

- **C1** no re-entrance guards (`m_applying*`, `isApplying*`, …).
- **C2** no `singleShot(0)` / `Qt.callLater` / queued-connection
  deferrals in this leaf.
- **C3** no `QTextDocument`, `QTextEdit`, `QPlainTextEdit`, or Quick
  text types.
- **C4** one document coordinate space: per-block UTF-8 byte offsets.
  No `applyFlatEdit`, no `flatView()`, no cross-block byte sums. The
  **projection map** (layout QChar ↔ buffer byte, per realized entry)
  is the one sanctioned layout-local index space — it lives only in
  `ProjectionMap` and never crosses the layout boundary. The byte↔QChar
  helpers themselves are core's since P1.2
  (`<markoff/core/TextUnits.h>`, aliased here as `coords::`).

`tests/check-constitution.sh` gates all four; run it before every
commit. If a task seems to *require* violating a rule — stop, log it
in the plan's findings log, report. That has been the correct move
every time so far.

**C3 does NOT block inline objects** (math glyphs, inline images,
future video-frame/pill spans): from **Qt 6.12**, standalone
`QTextLayout` sizes a U+FFFC char via a `QTextImageFormat` in
`setFormats()` — no `QTextDocument` involved. Do not re-conclude
"impossible without a document" (that was true pre-6.12 and is
logged as obsolete). Mechanism: spec §4.5; steps: plan gated task
G-Q612. Below 6.12: keep the styled-text fallback; never build a
shim.

## Build / test

```bash
cmake --build build-dev -j 4          # never more than -j 4
scripts/run-tests.sh -R canvas        # offscreen; never --direct
```

Manual eyeball loop: the demo app takes a file argument and
`MARKOFF_CANVAS_GRAB=<path.png>` renders offscreen to a file.
