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

**G1 accessibility arc CLOSED 2026-09-30. Canvas has no active arc.**
Spec (normative): [`docs/specs/2026-08-19-g1-canvas-accessibility-design.md`](../../docs/specs/2026-08-19-g1-canvas-accessibility-design.md);
plan + findings log: [`docs/plans/2026-08-19-g1-canvas-accessibility.md`](../../docs/plans/2026-08-19-g1-canvas-accessibility.md).
The four hard rules below governed it and needed no exception.
- **Shape:** per-block tree in `src/Accessibility.{h,cpp}` — `CanvasAccessible`
  (`QAccessible::Document` container over `View`'s block list) with a
  `CanvasBlockAccessible` child per block. Chosen over a flat
  `QAccessibleTextInterface` because that interface's whole-document
  offset space violates **C4**; every offset here is per-block via `coords::`.
- **Interfaces:** roles/states per spec §4.2 (BlockQuote -> `QAccessible::BlockQuote`
  on Qt>=6.9; `Attribute::Level` on headings reaches AT-SPI);
  `QAccessibleTextInterface` on text-bearing blocks (bounded realization via
  `View::ensureBlockRealized`, asserted by a test); `QAccessibleEditableTextInterface`
  via View's IME-commit route; fold state + one "Toggle fold" action;
  `accessibleDocumentName` on `View`/`EditorWidget`.
- **Ownership/eviction:** block interfaces are owned by Qt's cache and released
  via `deleteAccessibleInterface`; eviction and `ObjectCreated`/`ObjectDestroyed`
  come from one `onDocumentChanged` hook (`notifyDocumentChanged`).
- **Events:** caret/selection/focus, text insert/remove, fold and read-only
  state changes, all from hooks at existing View chokepoints (no new View API,
  synchronous, `isActive()`-gated).
- **No `markoff-core` change** (spec §8 held). Counts at close: full suite
  **213/213**, canvas `-R canvas` 41/41, `tst_canvas_accessibility` 90 cases,
  constitution clean (81 files), perf held.
- **Outstanding: manual Orca pass (A5.3), deferred by the user 2026-09-30.**
  To run it: `sudo pacman -S orca`, get explicit user permission for a
  `--direct` run, then follow the checklist in plan A5.3 (document navigation,
  heading + level, caret/selection, task checked state, fold, table).
  Findings become follow-ups, not retroactive failures.
- **Known limitations / follow-ups:** `docs/queue.md` "Canvas a11y limitations"
  (no table interface, Math/footnote roles unreachable, etc.) and the
  pre-existing stale-layout-on-remote-edit bug (queue.md).

**H arc CLOSED 2026-08-30 (`9b0138f8`, Hologram feature):** opt-in
`setHideMatchingFirstHeadingAsTitle(bool)` (+ `EditorWidget` pass-through)
hides the first block when it is a level-1 Heading matching
`inlineTitle()` — reuse of the fold-hidden projection (union fed to
`BlockLayoutCache::setFoldedBlocks`), title stays in the block-index
space (D2), never foldable, never a caret target. Plan:
`docs/plans/2026-08-30-hide-first-heading-as-title.md` (CLOSED). No core
change; constitution clean. Measured default-config full-suite baseline
at this HEAD is **213/213** (canvas-scoped 41/41), not the handoff's
stale 208/208 — see the H plan's findings log entry for the count
reconciliation.

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
