# Markoff

Qt6/C++ markdown editor widget family: a CRDT-backed foundation
(`markoff-core` + `markoff-parser`) plus view leaves. Primary consumer:
Corbomite (submodules this repo at `libs/markoff-family`).

## Current workfront — 2026-08-13: canvas production arc (D5 part 1)

The canvas spike closed PASS on 2026-08-13 (verdict: spike spec §10),
and the user opened the production arc the same day: build
`libs/markoff-canvas/` out to feature parity — Corbomite contract v2,
old-leaf parity, Obsidian Live Preview benchmark, and the collab
rendering surface the projection architecture was chosen for.

- **Plan (do the topmost unchecked task in the current phase):**
  [`docs/plans/2026-08-13-canvas-production-plan.md`](docs/plans/2026-08-13-canvas-production-plan.md)
- **Spec (normative):**
  [`docs/specs/2026-08-13-canvas-production-design.md`](docs/specs/2026-08-13-canvas-production-design.md)
  — constitution C1–C4 now permanent (§3), architecture deltas (§4),
  parity contract (§5), user gates G1–G3 (§8).
- Spike record (findings referenced throughout):
  [`docs/specs/2026-08-13-markoff-canvas-spike-design.md`](docs/specs/2026-08-13-markoff-canvas-spike-design.md);
  direction rationale:
  [`docs/specs/2026-08-13-view-authority-direction-decision.md`](docs/specs/2026-08-13-view-authority-direction-decision.md).

**Standstill (spec §7):** canvas is the active workfront;
`markoff-core` is open **only** for plan-named seams (P1 promotions
and later named APIs); markoff-live/styled are **bug-fix-only** until
gate G3 (retirement — a user decision); markoff-source is untouched,
permanently. Test baseline: **288/288** (full `scripts/run-tests.sh`)
at arc open; the plan ratchets it upward — any drop is a regression.

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
- `libs/markoff-live` — QML live-preview leaf (frozen; bug-fix only).
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
  the queue, and the closed d-arc/e-arc boards).
