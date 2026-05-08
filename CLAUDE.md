# Markoff (exploration/new-foundation branch)

> **2026-05-07 — Branch is on D5-first posture. v1.0 plan retired.**
> Authoritative posture for the branch is
> `docs/handoff/2026-05-07-pivot-to-d5-first.md`. Collab (D5) ships
> before any public-API freeze, before any Corbomite-facing migration
> guide, and before any further perf or facade work. Operating
> principle: one arc at a time, no side work, retirement is explicit.
>
> **2026-05-08 — Trajectory beyond D-arc.** D-arc closes with §4.5
> audit + §4.6 public-API freeze (per the pivot doc). After that, the
> next arc is **E-arc** — live-render completion as the maximalist
> Markoff prototype: inline-format styling, cursor-aware delimiter
> visibility, Obsidian affordances (wikilinks/embeds/tags/callouts),
> tables, frontmatter, footnote rendering, math/mermaid Live parity.
> E-arc bookends with a distillation phase (E6) that extracts the
> recipe for generalising Markoff into new view shapes — every future
> view is a structural subset of the live-render prototype. Authoritative
> framing: `docs/specs/2026-05-08-e-arc-framing.md`. Roadmap:
> `docs/e-arc/2026-05-08-e-arc-roadmap.md`. E-arc work does not begin
> until D-arc's bookend ships.
>
> **Fresh agent context — read in order:**
>
> 1. `docs/handoff/2026-05-07-pivot-to-d5-first.md` — **the pivot
>    doc; authoritative for the branch.**
> 2. `docs/handoff/2026-05-07-live-binding-developmental-history.md` —
>    pipeline-feature provenance; cited by operating principle 4.
>    Has 2026-05-08 erratum on §A.7 — `inlineSpansFor` is load-bearing
>    for E-arc, not dead code.
> 3. `docs/d-arc/d-arc-status.md` — live D-arc status board
>    (D4 complete; D5 active).
> 4. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
>    (with §6 pointer to E-arc).
> 5. `docs/d-arc/collabtext-scope-line.md` — six "won't do" items.
> 6. `docs/specs/2026-05-07-d5-collab-activation-design.md` —
>    **D5 substantive design (spec-approved 2026-05-08).** Companion:
>    `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md`.
>    Retired stub at `docs/archive/2026-05-04-d5-collab-activation-STUB.md`.
> 7. `docs/specs/2026-05-08-e-arc-framing.md` — **E-arc constitutional
>    framing (post-D-arc).** Live-render as maximalist prototype;
>    every other view is a structural subset.
> 8. `docs/e-arc/2026-05-08-e-arc-roadmap.md` — E-arc orientation.
> 9. `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` —
>    D4 spec (background; complete).
> 10. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` —
>    D3 spec (background; complete).
> 11. `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` —
>    D3 corrective (background; complete).
>
> The C-restoration arc and the v1.0 plan series are retired and
> archived under `docs/archive/c-restoration-arc/` and
> `docs/archive/v1.0-plan-pre-d5/` respectively. They are not
> authoritative and not to be cited in new specs except as historical
> context. R5.5 Bug 3 is cancelled, not paused.
>
> `libs/markoff-view-qml` was deleted entirely in commit `f646c90`
> (landed 2026-05-07 as part of v1.0 Part 1 prep before the v1.0 plan
> was retired). The deletion stands; the pivot doc §4.2 work-unit was
> redundant and is closed. `markoff-source` is the canonical source
> widget.
>
> All other content below describes the project at large.

---

Qt6/C++ markdown editor family, mid-rebuild. The new-foundation branch
has retired the original four leaves (`markoff-core`, `markoff-live`,
`markoff-reading`, `markoff-source`) and is rebuilding around a
foundation library + two canonical view leaves.

## Layout

- `libs/rapidyaml`             — vendored YAML parser (`ryml::ryml`).
- `libs/markoff-parser`        — tree-sitter Markdown AST + frontmatter.
                                 Public type `Markoff::Document` is a
                                 value snapshot; `TreeSitterParser`
                                 parses on demand (no incremental path
                                 after D4).
- `libs/collabtext`            — CRDT text engine, sibling-symlinked
                                 from `/home/clinton/dev/collabtext`.
- `libs/markoff-core`    — `Markoff::MarkoffDocument` (D2: per-
                                 block CRDT buffers + `IdList` for block
                                 order + sibling causal-LWW maps for
                                 kind/attrs/link-refs/footnotes/
                                 frontmatter). `Cmd::*` command set,
                                 `UndoLog`, `WatermarkCoordinator`,
                                 `applyFlatEdit` (D4: flat-text entry
                                 point for source-widget edits).
- `libs/markoff-live`   — **the active live-preview view leaf.**
                                 Built on D2's per-block CRDT buffers via
                                 `LiveListModelBinding`, `LiveBlockModel`,
                                 `LiveCursorState`, `LiveStructuralKeyHandler`,
                                 `LiveEditBinding`, `BlockKindRegistry`.
                                 Layered L0–L8 (see lib's CLAUDE.md). D3
                                 is implemented here; D3-correction (per-
                                 item ListItem blocks) is the active rework.
- `libs/markoff-source` — canonical QPlainTextEdit-based source
                                 widget (replaces the retired Qutepart-
                                 based `markoff-source`).
- `libs/jkqtmathtext`          — LaTeX math rendering. Untracked sibling
                                 wired in for D3's Math delegate.

## Building

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j
cd build-dev && ctest -j
```

141/141 fast tests pass at the tip of `exploration/new-foundation` (post-D3,
mid-D4). Use `-E "tst_realistic|tst_benchmark"` for a fast inner loop.

## Conventions

- C++20, Qt6.8+, CMake 3.19+.
- `tr()` for user-visible strings.
- `QIcon::fromTheme()` for icons.
- SPDX header `GPL-3.0-or-later` on every source file.
- Tests define expected behavior — when a test fails, fix the code,
  not the test. Exception: tests that probed behavior we're explicitly
  changing (rename test contracts to match the new shape, don't
  retrofit).

## Edit hot path (D2, current)

In D2, typing does not reparse the document. The path is:

1. QML `TextEdit::contentsChange(qtPos, removed, added)` →
   `LiveEditBinding::onContentsChange`.
2. Compute byte offset from QChar pos via `Coordinates::qtPosToByte`.
3. `MarkoffDocument::d2ApplyBufferEdit(blockId, byteOffset, removeBytes,
   insertedUtf8, transaction)` — directly mutates the per-block CRDT
   buffer.
4. `scheduleD2Changed()` queues a debounced `d2DocumentChanged` signal
   (one per event loop iteration).
5. `LiveListModelBinding::onD2Changed` rebuilds `BlockRecord`s from
   `iterateBlocks()`, runs kind-transition heuristics on Equal-op
   blocks (issuing `Cmd::changeKind` if the prefix-rule inference
   disagrees with stored kind), runs `applyOps` against the model.
6. Model emits `dataChanged` / `rowsInserted` / `rowsRemoved`. QML
   delegates re-render. `LiveCursorState` resolves pending cursor
   requests via the `structuralRowsInserted/Removed` signals.

The parser is only called at **load time** (`Document::fromMarkdown` in
`loadFromMarkdown`) and **per-block on demand** (`inlineSpansFor(blockId)`,
cached). The incremental-parse pipeline (`ParsePool`, `IncrementalParseSession`)
was deleted in D4.

## Per-library guides

- `libs/markoff-core/CLAUDE.md`
- `libs/markoff-live/CLAUDE.md` — **active view leaf**
- `libs/markoff-source/CLAUDE.md`
- `libs/markoff-parser/` (no per-lib CLAUDE.md; docs in `docs/specs/`)

## Docs layout

- `docs/handoff/2026-05-07-pivot-to-d5-first.md` — **authoritative
  posture; read first.**
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` —
  developmental record for the live-binding pipeline.
- `docs/specs/`   — design specs (dated, kebab-case). Live D-arc
  specs only; retired arcs are under `docs/archive/`.
- `docs/plans/`   — implementation plans (live D-arc only).
- `docs/handoff/` — session handoff briefs.
- `docs/d-arc/`   — D-arc roadmap, status board, scope-line.
- `docs/TODO.md`  — running todo list.
- `docs/archive/` — retired arc paper trails (C-restoration,
  pre-D5 v1.0 plan, Phase B/C). Reference only; not authoritative.
- `docs/phase-c-status.md` — master-side historical, superseded.
  Do not update.

## Branch posture

`exploration/new-foundation` diverges substantially from `master`. Don't
attempt to merge the old leaves back — they're intentionally gone. If
you need to preserve a piece of behavior from the deleted leaves,
re-implement it inside the new layout.
