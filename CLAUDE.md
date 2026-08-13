# Markoff

Qt6/C++ markdown editor widget family: a CRDT-backed foundation
(`markoff-core` + `markoff-parser`) plus view leaves. Primary consumer:
Corbomite (submodules this repo at `libs/markoff-family`).

## Current workfront — 2026-08-13: none. The canvas spike is CLOSED (PASS)

The canvas spike — a projection view leaf (`libs/markoff-canvas/`)
rendering `MarkoffDocument` directly via per-block `QTextLayout`, no
second document model — **finished on 2026-08-13 with a PASS**. All
ten exit criteria met with falsification proof; constitution (C1–C4)
intact end to end. Task list T0–T11 is complete; there is no
"topmost unchecked task" left to pick up.

- Verdict + recommendation:
  [`docs/specs/2026-08-13-markoff-canvas-spike-design.md`](docs/specs/2026-08-13-markoff-canvas-spike-design.md)
  §10; findings (incl. post-spike) §9.
- Completed checklist + commit/falsification SHAs:
  [`docs/plans/2026-08-13-markoff-canvas-spike.md`](docs/plans/2026-08-13-markoff-canvas-spike.md).
- Direction rationale:
  [`docs/specs/2026-08-13-view-authority-direction-decision.md`](docs/specs/2026-08-13-view-authority-direction-decision.md).

**Next arc is D5** (candidate architecture + contingent retirement of
markoff-live/styled). Per the decision record §6 that is a **user
decision, not an implementer default** — do not open it unprompted.

**Standstill, now tree-wide:** every leaf *including
`libs/markoff-canvas/`* and `markoff-core` is **bug-fix-only** until
D5 opens. No new features, no refactors, no opportunistic cleanup.
Test baseline: **288/288** (full `scripts/run-tests.sh`, 277
standstill + 11 canvas); any failure is a regression. Post-spike
findings are queued (**#18**), not open work.

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
