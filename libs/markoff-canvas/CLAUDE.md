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

**Status (2026-08-19):** the production arc is **CLOSED** — P1–P7
done, all three gates decided (G2 done 2026-08-18, G3 retired
`markoff-live` 2026-08-19). Full suite 315/315, perf budgets held,
constitution clean.

**Current workfront: G1 accessibility.** Gate reopened and decided
2026-08-19. Spec (normative for this arc):
[`docs/specs/2026-08-19-g1-canvas-accessibility-design.md`](../../docs/specs/2026-08-19-g1-canvas-accessibility-design.md)
— per-block a11y tree, not a flat `QAccessibleTextInterface` (the
flat one's whole-document offset space violates **C4**; see spec §2).
Plan (do the topmost unchecked task):
[`docs/plans/2026-08-19-g1-canvas-accessibility.md`](../../docs/plans/2026-08-19-g1-canvas-accessibility.md).
The four hard rules below still govern — and a11y work needs **no**
exception to any of them. If a task seems to, you are doing it wrong:
stop and log.

**Phase A1 (tree, roles, registration) CLOSED 2026-08-19** (A1.4
phase close, exempt from falsification). Landed:
`src/Accessibility.{h,cpp}` — `CanvasAccessible : QAccessibleWidget`
(container, role `Document`, child list backed by `View::blockCount`/
`blockIdAt`/`blockIndexOf`/`blockRect`, factory installed once per
`View` ctor) and `CanvasBlockAccessible` (per-block role/state per
spec §4.2, `Attribute::Level` on `Heading` blocks via
`QAccessibleAttributesInterface` — confirmed reaching AT-SPI by
A1.0's probe, so no description-text fallback needed). `View`/
`EditorWidget` gained `accessibleDocumentName` with container `Name`
resolution `accessibleDocumentName()` → `inlineTitle()` →
`tr("Markdown document")`. Two roles remain unreachable from Qt
(`BlockQuote`→`Section`, `Math`→`StaticText`) — logged at the mapping
site, not fixed. No `markoff-core` change needed anywhere in A1
(spec §8 held); constitution clean throughout. Full suite **208/208**
at close. **Next: Phase A2 (text interface), start at A2.1**
(`QAccessibleTextInterface` core — text/characterCount/offsets,
per-block UTF-8 byte↔QChar via `coords::`, never cross-block per C4).

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
