# Markoff

Qt6/C++ markdown editor widget family: a CRDT-backed foundation
(`markoff-core` + `markoff-parser`) plus view leaves. Primary consumer:
Corbomite (submodules this repo at `libs/markoff-family`).

## Current workfront — 2026-08-19: G1 accessibility (canvas)

The canvas production arc (D5 part 1) is **CLOSED** — full history in
[`docs/plans/2026-08-13-canvas-production-plan.md`](docs/plans/2026-08-13-canvas-production-plan.md)
and [`docs/STATUS.md`](docs/STATUS.md). All three of its gates are now
decided: **G1** was deferred 2026-08-14, then reopened and chosen as
the next workfront 2026-08-19 (below). **G2** (Corbomite adoption) is
done — Corbomite's Cluster K Phase 5 (2026-08-18) made
`Markoff::Canvas::EditorWidget` its sole LivePreview engine. **G3**
(retirement) was decided 2026-08-19, scoped to `markoff-live` only:
retired, zero downstream consumers left (`MARKOFF_BUILD_LIVE` now
defaults OFF; tag `archive/markoff-live-final`); `markoff-styled` is
untouched by that decision. Full context on both gates:
[`docs/handoff/2026-08-19-to-markoff-retire-live-close-e-arc-regroup.md`](docs/handoff/2026-08-19-to-markoff-retire-live-close-e-arc-regroup.md)
(Corbomite's handoff that prompted recording them).

**Current workfront: G1 accessibility for `libs/markoff-canvas`.**
Gate decided 2026-08-19; spec drafted:
[`docs/specs/2026-08-19-g1-canvas-accessibility-design.md`](docs/specs/2026-08-19-g1-canvas-accessibility-design.md).
**Shape: a per-block accessibility tree** — `View` is a
`QAccessible::Document` container, each block a child implementing
`QAccessibleTextInterface` over its own buffer. This was chosen over
the monolithic flat `QAccessibleTextInterface` the old §8 framing
implied, because that interface's whole-document offset space is
exactly what **C4 forbids** — it would have needed a constitutional
amendment, not just more weeks. Acceptance: in-process offscreen
tests (the ratchet) plus one manual Orca pass at arc close (needs
`--direct` permission). Tables and theme-side a11y are decided out
(spec §6, §1). Plan:
[`docs/plans/2026-08-19-g1-canvas-accessibility.md`](docs/plans/2026-08-19-g1-canvas-accessibility.md)
— phases A1–A5, gate A-G1. **Start at A1.0** (throwaway probe: does
`QAccessible::Attribute::Level` survive Qt's AT-SPI bridge?).

**Standstill:** with the canvas production arc closed, `markoff-core`
and `libs/markoff-canvas/` are open again for ordinary work (not
gated to plan-named seams anymore — that restriction was specific to
the closed arc). `markoff-styled` stays bug-fix-only (backs
Corbomite's Reading mode); `markoff-source` stays untouched,
permanently; `markoff-live` is retired (see G3 above). Test baseline:
**315/315** (full `scripts/run-tests.sh`) at arc close — any drop is
a regression.

**Status (2026-08-15, arc close):** all 7 F1 CodeMirror-parity gaps
closed (P7.2a–g), including a real document-convergence regression
found and fixed mid-arc (`623ed6ca`) and one user-approved behavior
change (`StructuralKeyHandler` Backspace-at-list-start, `fc7ea6fe`).

## Building

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 4     # never more than -j 4 (machine-wide rule)
```

## Testing

```bash
scripts/run-tests.sh                    # full suite, offscreen (default)
scripts/run-tests.sh -R canvas          # canvas leaf only
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'   # fast inner loop
```

Offscreen is the daily driver — no windows, no stolen focus. Never
use `--direct` without explicit per-task user permission; reserve
`--nested` for visual verification. When a test fails, **classify
before fixing** (contract drift vs bug); tests define expected
behavior — fix the code, not the test, unless the contract is being
explicitly changed.

## Conventions

- C++20, Qt6.8+, CMake 3.19+. `tr()` for user-visible strings,
  `QIcon::fromTheme()` for icons, SPDX `GPL-3.0-or-later` header on
  every source file.
- Specs → `docs/specs/`, plans → `docs/plans/`, dated
  `YYYY-MM-DD-<slug>.md`. Retired paper → `docs/archive/` (move,
  never delete).
- Commit style: `canvas(T<n>): …` for spike tasks; `fix(<area>): …`
  for standstill bug fixes.

## Layout (one line each)

- `libs/markoff-canvas` — **ACTIVE:** projection view spike. Own
  `CLAUDE.md`.
- `libs/markoff-core` — foundation: `MarkoffDocument` (per-block CRDT
  buffers + `IdList` order + LWW maps), `Cmd::*`, `UndoLog`,
  `StructuralKeyHandler` (pure), `FindController`, `Theme`,
  `MarkdownView` base. Own `CLAUDE.md`.
- `libs/markoff-parser` — tree-sitter Markdown; load-time +
  per-block on-demand only (`inlineSpansFor`, cached).
- `libs/markoff-live` — QML live-preview leaf, **retired 2026-08-19**
  (G3): zero downstream consumers, `MARKOFF_BUILD_LIVE` defaults OFF.
  Source stays in-tree build-fix-only; full deletion is a future
  decision. Tag `archive/markoff-live-final` for the last normal state.
- `libs/markoff-source` — QPlainTextEdit source leaf (permanent;
  bug-fix only).
- `libs/markoff-styled` — QTextEdit styled leaf (frozen; bug-fix
  only).
- `libs/collabtext` — CRDT engine, **git submodule**.
  `libs/rapidyaml`, `libs/jkqtmathtext` — vendored.

## Working on the OLD leaves (standstill bug fixes only)

The live/styled/source seam has its own discipline: read
[`docs/INVARIANTS.md`](docs/INVARIANTS.md) (eight rules),
[`docs/VIEW-IMPLEMENTORS-GUIDE.md`](docs/VIEW-IMPLEMENTORS-GUIDE.md),
and the per-lib `libs/*/CLAUDE.md` before touching it, and log smells in
`docs/queue.md` § Discipline Log. The canvas leaf is instead governed
by its constitution (spec §6) — the invariants' file-scope list does
not cover it.

## Status tracking

- [`docs/STATUS.md`](docs/STATUS.md) — live board (sparse; update
  when the baseline or workfront changes).
- [`docs/queue.md`](docs/queue.md) — dormant items + Discipline Log.
- History: `docs/STATUS-LOG.md` (dated banners),
  `docs/archive/` (retired arcs, pre-canvas snapshots of this file,
  the queue, and the closed d-arc board and e-arc board — the latter
  formally closed 2026-08-19, `docs/archive/e-arc/`).
