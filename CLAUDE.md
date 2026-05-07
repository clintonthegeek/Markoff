# Markoff (exploration/new-foundation branch)

> **D4 complete; D5 (collab activation) pending substantive design.**
> D4 retired the incremental-parse pipeline (`ParsePool`,
> `IncrementalParseSession`, `parseUpdated`, `parseSequence`, `MarkoffEdit`,
> `applyLocalEdit`), migrated source-widget to D2 via `applyFlatEdit`,
> retired markoff-bench and view-qml live mode, and deleted dead legacy
> `Cmd::*` + `CommandFacade` + `ReplaceController`. 103/103 tests pass.
>
> **Fresh agent context — read in order:**
>
> 1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
> 2. `docs/d-arc/d-arc-status.md` — live status board (D4 complete; D5 next)
> 3. `docs/d-arc/collabtext-scope-line.md` — six "won't do" items
> 4. `docs/specs/2026-05-04-d5-collab-activation-STUB.md` — **D5 stub (next design target)**
> 5. `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` — D4 spec (background; complete)
> 6. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — D3 spec (background; complete)
> 7. `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` — D3 corrective (background; complete)
>
> The C-restoration's status board (`docs/restoration-status.md`) is now
> historical. The marker-paragraph design and R5.5 plan are retired. R5.5
> Bug 3 is **cancelled, not paused** — the bug lives inside the parser-vs-
> CRDT race window that D removes structurally.
>
> The legacy `libs/markoff-view-qml` continues to ship (source mode only;
> live mode retired in D4); the in-tree `libs/markoff-live-render` carries
> L0–L8 and all D3 work.
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
- `libs/markoff-foundation`    — `Markoff::MarkoffDocument` (D2: per-
                                 block CRDT buffers + `IdList` for block
                                 order + sibling causal-LWW maps for
                                 kind/attrs/link-refs/footnotes/
                                 frontmatter). `Cmd::*` command set,
                                 `UndoLog`, `WatermarkCoordinator`,
                                 `applyFlatEdit` (D4: flat-text entry
                                 point for source-widget edits).
- `libs/markoff-view-qml`      — legacy QML view (source mode only;
                                 live mode retired in D4). Source mode
                                 still ships; do not delete prematurely.
- `libs/markoff-live-render`   — **the active live-preview view leaf.**
                                 Built on D2's per-block CRDT buffers via
                                 `LiveListModelBinding`, `LiveBlockModel`,
                                 `LiveCursorState`, `LiveStructuralKeyHandler`,
                                 `LiveEditBinding`, `BlockKindRegistry`.
                                 Layered L0–L8 (see lib's CLAUDE.md). D3
                                 is implemented here; D3-correction (per-
                                 item ListItem blocks) is the active rework.
- `libs/markoff-source-widget` — canonical QPlainTextEdit-based source
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

- `libs/markoff-foundation/CLAUDE.md`
- `libs/markoff-live-render/CLAUDE.md` — **active view leaf**
- `libs/markoff-view-qml/CLAUDE.md`
- `libs/markoff-source-widget/CLAUDE.md`
- `libs/markoff-parser/` (no per-lib CLAUDE.md; docs in `docs/specs/`)

## Docs layout

- `docs/specs/`   — design specs (dated, kebab-case).
- `docs/plans/`   — implementation plans (one per feature/phase).
- `docs/handoff/` — session handoff briefs.
- `docs/TODO.md`  — running todo list. Read first.
- `docs/phase-c-status.md` — historical, superseded by the
  new-foundation branch direction. Do not update.

## Branch posture

`exploration/new-foundation` diverges substantially from `master`. Don't
attempt to merge the old leaves back — they're intentionally gone. If
you need to preserve a piece of behavior from the deleted leaves,
re-implement it inside the new layout.
