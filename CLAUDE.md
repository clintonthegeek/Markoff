# Markoff

Qt6/C++ markdown editor widget family: a CRDT-backed foundation library
(`markoff-core` + `markoff-parser`) and **three canonical view leaves**
(`markoff-live` QML live-preview, `markoff-source` plain-text source
widget, `markoff-styled` QTextEdit inline-styled editor). Primary
consumer: Corbomite (submodules this repo at `libs/markoff-family`).

## Current status — 2026-06-09

- **Active workfront:** the flat-view leaves (`markoff-styled` +
  `markoff-source`) and the `markoff-core` single-document binding,
  plus **public-API finalization for Corbomite** (cross-leaf parity:
  find/theme/format/undo through `Markoff::MarkdownView` instead of
  per-leaf escape hatches). The QML live leaf is feature-stable;
  E-arc is dormant (see `docs/STATUS.md`).
- **Test baseline:** **260/263** via
  `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`. The 3
  failing binaries (`tst_live_render_e2_nav_shift_extend`,
  `tst_live_render_focus_chokepoint_invariant`,
  `tst_live_render_cursor_typing_invariant`, 6 slots total) are
  **deterministic failures, not flakes** — triage filed as queue #10.
- **Open items:** queue #8.3 (source-view list-item markers),
  queue #10 (deterministic live-test failures), queue #11 (legacy
  `findAll`/`CompletionDetector` retirement), queue #12 (EmbedRegistry
  test coverage), deferred styled-table editing (in-grid cell edit,
  row/col ops). Full list: `docs/queue.md`.
- **Recent history:** dated session banners live in
  [`docs/STATUS-LOG.md`](docs/STATUS-LOG.md) (newest first).

**Fresh agent — read in order:**

1. [`docs/VIEW-IMPLEMENTORS-GUIDE.md`](docs/VIEW-IMPLEMENTORS-GUIDE.md)
   — cross-cutting view↔model concerns and contracts. Required before
   any seam work in any leaf.
2. [`docs/INVARIANTS.md`](docs/INVARIANTS.md) — the eight discipline
   rules (summarised below).
3. [`docs/STATUS.md`](docs/STATUS.md) — live status board (replaces
   the retired per-arc boards).
4. [`docs/queue.md`](docs/queue.md) — open work items + Discipline Log.
5. The per-library `libs/*/CLAUDE.md` guide for whichever lib you're
   touching.

Background (historical, cite-don't-follow): D-arc docs under
`docs/d-arc/`, E-arc docs under `docs/e-arc/` (both closed boards),
the pivot record `docs/handoff/2026-05-07-pivot-to-d5-first.md`, and
the developmental record
`docs/handoff/2026-05-07-live-binding-developmental-history.md`
(authoritative for *why the live-binding code looks like this*; cited
by invariant 1).

---

## Engineering discipline — read before any non-trivial change

This branch has three serious post-mortems on file, each of which
correctly named the failure pattern the **next** refactor went on
to reproduce. The bottleneck is not awareness, it is discipline.
The eight invariants below are how we break that cycle. Full text
and citations in [`docs/INVARIANTS.md`](docs/INVARIANTS.md) — read
it once, then it's yours.

**You are responsible** for following these in your own work, **and
for noticing when you see them violated in code you pass through —
even if the violation is off-topic from your current task.** A
smell you leave unmarked is a vote for it being normal. The
Discipline Log (invariant 8 below; section at the top of
`docs/queue.md`) is the cheap mechanism — log the smell, finish
your task, move on.

These rules scope to the **focus/caret/block-change seam** (see
`docs/INVARIANTS.md` §"Scope and exceptions" for the file list).
Outside the seam, normal engineering judgement applies.

1. **Cite the developmental record before refactoring the seam.**
   `docs/handoff/2026-05-07-live-binding-developmental-history.md`
   is authoritative for *why this code looks like this*. Cite the
   relevant section in your spec.

2. **L4 (block-content authority) is decided in writing first.**
   Model wins, or delegate wins — pick one in your spec, before
   the plan, before the code. The current implicit decision is
   the foundational fault (2026-05-02 audit).

3. **A new authority retires the old one in the same plan.** Dual
   sources of truth → pairwise reconciliation → race window →
   focus loss. Every regression in this seam has come through this
   mechanism. Name the retiring store in the spec; delete it as a
   work-unit in the same plan, not a follow-up.

4. **Falsifiable invariant tests come first.** On
   `LiveRealisticInputHarness`. Prove falsifiable by breaking the
   target seam in a throwaway stub — if the test doesn't fail,
   the test is too lenient; fix the test before touching
   production. (Prescribed by R5-holes post-mortem §6.2; never
   enforced; queue #2 concern #6 names the specific invariant the
   cursor seam needs.)

5. **Tests exercise the production callsite, not a synonym.** A
   C++ test that calls a slot directly does not protect that slot
   if production reaches it through QML. See the
   `pendingVisualLineHint` precedent.

6. **`Qt.callLater` / `QTimer::singleShot(0, ...)` are smells.**
   "I gave up on understanding the timing and brute-forced it."
   When adding: justify in the commit. When seeing: log it.

7. **Re-entrance guards (`m_applyingX`, `isApplyingY()`) are
   smells.** Same rule.

8. **Notice and note — the Discipline Log.** Append to
   `docs/queue.md` § Discipline Log: one line, `file:line`,
   invariant number, one phrase of context. No fix required in
   the same session. The point is the smell becomes visible to
   the next agent.

If you are about to deliberately violate one of these and have a
real reason: **write the reason in the spec**, cite the rule by
number, and proceed. The rules exist to make deviations visible,
not to forbid them.

---

## Layout

- `libs/rapidyaml`             — vendored YAML parser (`ryml::ryml`).
- `libs/markoff-parser`        — tree-sitter Markdown AST + frontmatter.
                                 Public type `Markoff::Document` is a
                                 value snapshot; `TreeSitterParser`
                                 parses on demand (no incremental path
                                 after D4).
- `libs/collabtext`            — CRDT text engine, **git submodule**
                                 (`codeberg.org:clintonthegeek/collabtext.git`).
- `libs/markoff-core`          — `Markoff::MarkoffDocument` (D2: per-
                                 block CRDT buffers + `IdList` for block
                                 order + sibling causal-LWW maps for
                                 kind/attrs/link-refs/footnotes/
                                 frontmatter). `Cmd::*` command set,
                                 `UndoLog`, `WatermarkCoordinator`,
                                 `applyFlatEdit` (D4: flat-text entry
                                 point), `SourceTextDocumentBinding`
                                 (shared flat-view binding for the two
                                 QWidget leaves, incl. the opaque-block
                                 seam), `FindController`, `Theme`,
                                 `MarkdownView` (common view base).
- `libs/markoff-live`          — QML live-preview view leaf. Built on
                                 D2's per-block CRDT buffers via
                                 `LiveListModelBinding`, `LiveBlockModel`,
                                 `LiveCursorState`, `LiveStructuralKeyHandler`,
                                 `LiveEditBinding`, `BlockKindRegistry`.
                                 Layered L0–L8 (see lib's CLAUDE.md).
                                 Public widget wrapper:
                                 `Markoff::Live::EditorWidget`.
- `libs/markoff-source`        — QPlainTextEdit-based source widget
                                 (`Markoff::Source::Editor`).
- `libs/markoff-styled`        — QTextEdit-based inline-styled editor
                                 (`Markoff::Styled::Editor` +
                                 headless `DocumentRenderer`). No QML,
                                 no KF6. Renders tables as read-only
                                 `QTextTable` grids via the core's
                                 opaque-block seam.
- `libs/jkqtmathtext`          — LaTeX math rendering, **vendored**
                                 (tracked in this repo).

## Building

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```

## Testing

Run all tests via `scripts/run-tests.sh`. It defaults to
`QT_QPA_PLATFORM=offscreen` so Qt renders to memory buffers — no windows
appear, no focus is stolen, no virtual X server needed.

```bash
scripts/run-tests.sh                     # full suite
scripts/run-tests.sh -R 'cursor'         # ctest pattern
scripts/run-tests.sh --bin tst_block_id  # one test binary
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'  # fast inner loop
```

Three modes:

- **offscreen** (default) — no window server interaction. Use for everything.
- **nested** (`--nested`) — spawns a nested Weston compositor in a single
  window on your desktop; tests run inside it. Use when you need to *see*
  rendering without it stealing focus from your main session.
- **direct** (`--direct`, requires `MARKOFF_ALLOW_DIRECT=1`) — runs against
  your real Wayland/X11 session. Expect focus interruptions. Reserved for
  cases where you explicitly want windows on your screen.

**Agents must default to offscreen.** Never invoke `--direct` without
explicit per-task user permission. Even `--nested` should be reserved for
visual verification work; the offscreen path is the daily driver.

Expected baseline: **260/263** on the fast inner loop. The 3 failing
binaries are deterministic, documented failures (queue #10) — any
*other* failure is a new regression. When a test fails, **classify
before fixing**: rapid design evolution means many failures are
contract drift, not bugs (see queue #8.6/#8.7 for the worked example).

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

In D2, typing does not reparse the document. The live-leaf path is:

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

The flat-view leaves (`markoff-styled`, `markoff-source`) instead edit
through `SourceTextDocumentBinding`: forward path dispatches QTextDocument
`contentsChange` via `Detail::findBlockAtSepByte` to `d2ApplyBufferEdit` /
direct merge primitives / `applyFlatEdit`; structural keys are intercepted
and routed to the pure `StructuralKeyHandler`. See
`libs/markoff-core/CLAUDE.md` § Single-document binding.

The parser is only called at **load time** (`Document::fromMarkdown` in
`loadFromMarkdown`) and **per-block on demand** (`inlineSpansFor(blockId)`,
cached). The incremental-parse pipeline (`ParsePool`, `IncrementalParseSession`)
was deleted in D4.

## Per-library guides

- `libs/markoff-core/CLAUDE.md`
- `libs/markoff-live/CLAUDE.md`
- `libs/markoff-source/CLAUDE.md`
- `libs/markoff-styled/CLAUDE.md`
- `libs/markoff-parser/` (no per-lib CLAUDE.md; docs in `docs/specs/`)

## Docs layout

- `docs/STATUS.md` — **live status board.** Replaces the retired
  per-arc boards; update it when the workfront or baseline changes.
- `docs/STATUS-LOG.md` — append-only history of dated status banners.
- `docs/queue.md`  — open work items + the Discipline Log. Closed
  items are archived at `docs/archive/2026-06-09-queue-closed-items.md`.
- `docs/VIEW-IMPLEMENTORS-GUIDE.md` — evergreen; required reading for
  any view-leaf work.
- `docs/INVARIANTS.md` — the discipline rules.
- `docs/specs/`   — design specs (dated, kebab-case).
- `docs/plans/`   — implementation plans.
- `docs/handoff/` — session handoff briefs.
- `docs/d-arc/`, `docs/e-arc/` — closed arc boards (reference only;
  both carry disposition banners).
- `docs/archive/` — retired arc paper trails (C-restoration, pre-D5
  v1.0 plan, Phase B/C, retired TODO/bench baselines). Reference only.
- `docs/phase-c-status.md` — master-side historical, superseded. Do
  not update. (Kept in place: it is the crash-repro fixture cited by
  queue #8.8 and the 2026-05-31 SIGSEGV fix.)

**For any new spec work:** use `superpowers:brainstorming` first; specs
go in `docs/specs/`, plans in `docs/plans/` (no other locations).

## Branch posture

Master is the active branch (single line of development). The old
four-leaf layout was retired wholesale during the
`exploration/new-foundation` → `master` merge at `3c7afa9` (2026-05-25);
the current layout (foundation library + **three** canonical view
leaves) is canonical. Don't attempt to resurrect the old leaves — if
you need behavior from them, re-implement it in the new layout.
