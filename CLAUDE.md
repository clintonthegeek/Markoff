# Markoff

Qt6/C++ markdown editor widget family: a CRDT-backed foundation
(`markoff-core` + `markoff-parser`) plus view leaves. Primary consumer:
Corbomite (submodules this repo at `libs/markoff-family`).

Core reading: [Shared Editing Manifesto](docs/EDITING-MANIFESTO.md).
Use it to guide CRDT integration, ownership boundaries, development priorities,
and the evidence required for editing behavior.

## Current workfront — Collabtext vNext adoption planning (opened 2026-10-11)

Start with the authoritative [execution roadmap and A01–A16 task board](docs/plans/2026-10-12-collabtext-adoption-execution.md).
Milestone summaries and historical findings: [adoption plan](docs/plans/2026-10-11-collabtext-vnext-adoption.md).
**User coordination rule:** Corbomite development stays paused until final
task A16 / milestone M12 delivers the readiness signal to begin its transition.
Implement and verify Markoff using reference providers and host-shaped fixtures.
Do not depend on paused Corbomite code or send an earlier readiness signal.
Current Core candidate: `collabtext-markoff-core-2026-10-13-alpha5` at
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b` (0.2.0-alpha.5, manifest v17);
the alpha.4 tag remains immutable. Markoff now requires the installed candidate
for standalone and embedded builds; the pinned engine is no longer a build
choice and the retired Live leaf is excluded from the root build.
Core supplies ordered list identities, scoped gap application, durable CTLO
readers and selective local-history groups for text/IdList. The Corbomite host
design directions are accepted; Markoff's reference provider and isolated
provider-backed adapters pass their current consumer checks. A02 is complete at
the reference-provider boundary. The provider-backed `MarkoffDocument`, its
active Source/Styled/Canvas views and installed consumers now use one engine.
Continue closing the remaining production routing, history-retirement,
responsiveness and transition-evidence work on the A01–A16 board.
Preserve D2 per-block ownership, retain history with collection disabled, and
require A01–A15 evidence before A16. Corbomite production acceptance follows its
transition; Wave Q06/global merge and publication gates remain separate release
obligations.

**Also landed 2026-08-30 — H arc (Hologram, new consumer):** opt-in
`View::setHideMatchingFirstHeadingAsTitle(bool)` (+ `EditorWidget`
pass-through) hides the first body block when it is a level-1 Heading
whose plain text equals `inlineTitle()` — the title band stops
duplicating the body heading. Hidden via the existing fold-hidden
projection; **stays in the block-index space** (D2) and in the
document. Plan: `docs/plans/2026-08-30-hide-first-heading-as-title.md`
(CLOSED, `9b0138f8`); consumer answer note in `docs/handoff/`.

The canvas production arc (D5 part 1) is **CLOSED** — full history in
[`docs/plans/2026-08-13-canvas-production-plan.md`](docs/plans/2026-08-13-canvas-production-plan.md)
and [`docs/STATUS.md`](docs/STATUS.md). All three of its gates are now
decided: **G1** was deferred 2026-08-14, then reopened and chosen as
the next workfront 2026-08-19 (below). **G2** (Corbomite adoption) is
done — Corbomite's Cluster K Phase 5 (2026-08-18) made
`Markoff::Canvas::EditorWidget` its sole LivePreview engine. **G3**
(retirement) was decided 2026-08-19, scoped to `markoff-live` only:
retired with zero downstream consumers and excluded from the supported root
build (tag `archive/markoff-live-final`); `markoff-styled` is untouched by
that decision. Full context on both gates:
[`docs/handoff/2026-08-19-to-markoff-retire-live-close-e-arc-regroup.md`](docs/handoff/2026-08-19-to-markoff-retire-live-close-e-arc-regroup.md)
(Corbomite's handoff that prompted recording them).

**G1 accessibility arc CLOSED 2026-09-30.** The current workfront is the
CollabText adoption roadmap above.
Gate A-G1 decided 2026-09-30: the manual Orca pass (A5.3) is **deferred**
by the user to a later dogfood session (needs `sudo pacman -S orca` and
explicit permission for a `--direct` run; checklist in the plan's A5.3).
It is the one outstanding acceptance item. Shape: a per-block
accessibility tree — `View` is a `QAccessible::Document` container, each
block a child with `QAccessibleTextInterface` (and editable text, fold
state/action, events) over its own buffer; chosen over a flat interface
because that whole-document offset space is what **C4 forbids**. Phases
A1–A5 all closed; no `markoff-core` change; constitution clean; perf held;
`tst_canvas_accessibility` 90 cases. Spec:
[`docs/specs/2026-08-19-g1-canvas-accessibility-design.md`](docs/specs/2026-08-19-g1-canvas-accessibility-design.md),
plan + findings log:
[`docs/plans/2026-08-19-g1-canvas-accessibility.md`](docs/plans/2026-08-19-g1-canvas-accessibility.md),
limitations/follow-ups: `docs/queue.md` "Canvas a11y limitations" (also
holds the pre-existing stale-layout-on-remote-edit bug found in A3.4).

**Standstill:** with the canvas production arc closed, `markoff-core`
and `libs/markoff-canvas/` are open again for ordinary work (not
gated to plan-named seams anymore — that restriction was specific to
the closed arc). `markoff-styled` stays bug-fix-only (backs
Corbomite's Reading mode); `markoff-source` stays untouched,
permanently; `markoff-live` is retired (see G3 above). Historical baseline:
**213/213** (full `scripts/run-tests.sh`) at the G1 close. Record current
migration evidence and any explained test-inventory changes in the execution
roadmap; a historical count is not a current passing result.

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

- `libs/markoff-canvas` — **ACTIVE:** the primary editing leaf
  (production arc closed 2026-08-15; G1
  accessibility closed 2026-09-30, no active arc). Own `CLAUDE.md`.
- `libs/markoff-core` — foundation: `MarkoffDocument` (per-block CRDT
  buffers + `IdList` order + LWW maps), `Cmd::*`, `UndoLog`,
  `StructuralKeyHandler` (pure), `FindController`, `Theme`,
  `MarkdownView` base. Own `CLAUDE.md`.
- `libs/markoff-parser` — tree-sitter Markdown; load-time +
  per-block on-demand only (`inlineSpansFor`, cached).
- `libs/markoff-live` — QML live-preview leaf, **retired 2026-08-19**
  (G3): zero downstream consumers and excluded from the supported root build.
  Its source remains only for repository cleanup; it is not a Markoff consumer.
  Tag `archive/markoff-live-final` records its last supported state.
- `libs/markoff-source` — QPlainTextEdit source leaf (permanent;
  bug-fix only).
- `libs/markoff-styled` — QTextEdit styled leaf (frozen; bug-fix
  only).
- `libs/rapidyaml`, `libs/jkqtmathtext` — vendored third-party libraries.

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
