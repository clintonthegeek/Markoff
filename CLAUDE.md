# Markoff

Qt6/C++ markdown editor widget family: a CRDT-backed foundation
(`markoff-core` + `markoff-parser`) plus view leaves. Primary consumer:
Corbomite (submodules this repo at `libs/markoff-family`).

Core reading: [Shared Editing Manifesto](docs/EDITING-MANIFESTO.md).
Use it to guide CRDT integration, ownership boundaries, development priorities,
and the evidence required for editing behavior.

## Current workfront

Corbomite's consolidated asks (2026-10-18) are the new workfront. The
verdicts and the seven-wave order are on the
[Corbomite open-asks board](docs/plans/2026-10-19-corbomite-open-asks-triage.md);
no wave has started. Read [`docs/STATUS.md`](docs/STATUS.md) first.

The CollabText vNext adoption board is closed. The readiness signal is
[`docs/handoff/2026-10-17-to-corbomite-ready-for-collabtext-transition.md`](docs/handoff/2026-10-17-to-corbomite-ready-for-collabtext-transition.md).
Corbomite may begin its transition. Host storage, lease integration,
application wiring, and production acceptance stay in Corbomite. Do not
edit that checkout from this repository.

For a later change here, follow the session routine in
[`AGENTS.md`](AGENTS.md): one task, test it, update the status note, commit.
Every build requires the installed candidate
`collabtext-markoff-core-2026-10-13-alpha5`
(`e8a8e7d1a95dd50e23f38d6727921b47b814e44b`, 0.2.0-alpha.5, manifest v17).
The prefix used for the acceptance runs is `build-collabtext-alpha5/install`.

Keep D2 per-block streams, Markoff-owned actions, and history with physical
collection disabled. Design rationale:
[adoption plan](docs/plans/2026-10-11-collabtext-vnext-adoption.md).
Progress diary through 2026-10-16:
[`docs/archive/collabtext-adoption/`](docs/archive/collabtext-adoption/).

Canvas production, G1 accessibility, and the Hologram title-hide arc are
closed. The manual Orca pass stays deferred until the user asks.
`markoff-styled` is bug-fix-only outside this migration.
`markoff-source` stays untouched. The live leaf and the vendored CollabText
submodule were removed in A13. A historical test count is not a current result.

## Building

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build-collabtext-alpha5/install"
cmake --build build-dev -j 4     # never more than -j 4 (machine-wide rule)
```

The prefix is the installed alpha.5 package from `docs/STATUS.md`. Recreate
it from the candidate's `docs/USING_VNEXT.md` when that directory is missing.

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
- Commit style: `collabtext(A<nn>): …` for the adoption board;
  `fix(<area>): …` for a bug outside that board.

## Layout (one line each)

- `libs/markoff-canvas` — **ACTIVE:** the primary editing leaf
  (production arc closed 2026-08-15; G1
  accessibility closed 2026-09-30, no active arc). Own `CLAUDE.md`.
- `libs/markoff-core` — foundation: `MarkoffDocument` (per-block CRDT
  buffers + `IdList` order + LWW maps), `Cmd::*`,
  `StructuralKeyHandler` (pure), `FindController`, `Theme`,
  `MarkdownView` base. Own `CLAUDE.md`.
- `libs/markoff-parser` — tree-sitter Markdown; load-time +
  per-block on-demand only (`inlineSpansFor`, cached).
- `libs/markoff-source` — QPlainTextEdit source leaf (permanent;
  bug-fix only).
- `libs/markoff-styled` — QTextEdit styled leaf (frozen; bug-fix
  only).
- `libs/rapidyaml`, `libs/jkqtmathtext` — vendored third-party libraries.

## Working on the OLD leaves (standstill bug fixes only)

The styled/source seam has its own discipline: read
[`docs/INVARIANTS.md`](docs/INVARIANTS.md) (eight rules),
[`docs/VIEW-IMPLEMENTORS-GUIDE.md`](docs/VIEW-IMPLEMENTORS-GUIDE.md),
and the per-lib `libs/*/CLAUDE.md` before touching it, and log smells in
`docs/queue.md` § Discipline Log. The canvas leaf is instead governed
by its constitution (spec §6) — the invariants' file-scope list does
not cover it.

## Status tracking

- [`docs/STATUS.md`](docs/STATUS.md) — one-screen live board. Update it
  when the current task or the latest test result changes.
- [`docs/plans/2026-10-12-collabtext-adoption-execution.md`](docs/plans/2026-10-12-collabtext-adoption-execution.md)
  — the task list. Update the active task in the same change as the code.
- [`docs/queue.md`](docs/queue.md) — dormant items. Not the workfront.
- History: `docs/STATUS-LOG.md`, `docs/archive/` (including
  `docs/archive/collabtext-adoption/` and the closed e-arc).
