# Markoff (exploration/new-foundation branch)

> **D2 complete — D3 is next.** D2 user sign-off received 2026-05-05. D3 (view-layer adaptation) is the active phase. Read `docs/specs/2026-05-04-d3-view-layer-adaptation-STUB.md` for stub scope and inputs. Brainstorm the substantive D3 spec before writing the plan.
>
> **A fresh agent context picking up this work must read, in order:**
>
> 1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — orientation across the whole D arc (D0 → D5)
> 2. `docs/d-arc/d-arc-status.md` — live status board (D2 = dogfood; D3 = stubbed)
> 3. `docs/d-arc/collabtext-scope-line.md` — the six "won't do" items binding every D-arc spec
> 4. `docs/specs/2026-05-04-d2-foundation-reshape-design.md` — the binding D2 spec (reference for dogfood verification)
>
> The C-restoration's status board (`docs/restoration-status.md`) is now
> historical. The marker-paragraph design and R5.5 plan are retired. R5.5
> Bug 3 is **cancelled, not paused** — the bug lives inside the parser-vs-
> CRDT race window that D removes structurally.
>
> The legacy `libs/markoff-view-qml` continues to ship; the in-tree
> `libs/markoff-live-render` keeps L0–L3 (carries forward to D3) and the existing
> R5/R5.5 work (will be rewritten or retired during D3 implementation).
> Do not delete `libs/markoff-view-qml` prematurely.
>
> All other content below describes the project at large; D2 dogfood is the active subject within that.

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
                                 supports incremental reparse via
                                 `parseIncremental()`.
- `libs/collabtext`            — CRDT text engine, sibling-symlinked
                                 from `/home/clinton/dev/collabtext`.
- `libs/markoff-foundation`    — `Markoff::MarkoffDocument` (CRDT-
                                 backed canonical text + Sessions +
                                 ParsePool). The ParsePool worker owns
                                 a long-lived `IncrementalParseSession`
                                 that reuses tree-sitter trees across
                                 calls.
- `libs/markoff-view-qml`      — canonical QML view (Phase 1: source-
                                 mode editor; Phase 2: live preview).
- `libs/markoff-source-widget` — canonical QPlainTextEdit-based source
                                 widget (replaces the retired Qutepart-
                                 based `markoff-source`).
- `libs/jkqtmathtext`          — LaTeX math rendering (untracked
                                 sibling, used by future view work).

## Building

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j
cd build-dev && ctest -j
```

78/78 tests pass at the tip of `exploration/new-foundation`.

`tst_benchmark` (~7 minutes wall) and `tst_realistic` (~90 seconds) are
the slow tail; everything else completes in <10 seconds. Use
`-E "tst_realistic|tst_benchmark"` for a fast inner loop.

## Conventions

- C++20, Qt6.8+, CMake 3.19+.
- `tr()` for user-visible strings.
- `QIcon::fromTheme()` for icons.
- SPDX header `GPL-3.0-or-later` on every source file.
- Tests define expected behavior — when a test fails, fix the code,
  not the test. Exception: tests that probed behavior we're explicitly
  changing (rename test contracts to match the new shape, don't
  retrofit).

## Parser hot path (typing latency)

For each keystroke the foundation runs:

1. `MarkoffDocument::applyLocalEdit` → `ParsePool::schedule(utf8)`.
2. Worker: `Document::extract(raw)` strips frontmatter and harvests
   footnote metadata (definitions and per-reference numbering). It
   does NOT mutate the body — `extracted.body == raw.mid(frontmatter
   BlockEnd)` byte-for-byte. Footnote refs are surfaced via
   `Document::footnoteRefs()` for renderers.
3. `IncrementalParseSession` diffs prior body vs new body via prefix/
   suffix scan to derive a single `ByteEdit`. Since body equals post-
   frontmatter source, body diff equals source diff modulo the
   frontmatter offset.
4. `TreeSitterParser::parseIncremental({edit}, newBody)`:
   - Block tree: `ts_tree_edit` + `ts_parser_parse(prevTree, …)`,
     reusing unchanged subtrees.
   - Inline trees: snapshot old ranges before the edit, shift each
     through `sortedEdits` to derive its post-edit range, and reuse
     `m_inlineTrees[i]` for any region whose byte range is unchanged.
     Overlap-with-edit invalidates a region; unmatched new ranges
     parse fresh. Reuse count exposed via
     `TreeSitterParser::inlineTreeReuseCount()` for benchmarks.
5. `parser.buildDocumentQueries()` walks the tree to bake
   `DocumentQueryResult`.
6. `Document::fromComponents()` snapshots a value-shaped Document.

`resetContent` uses `ParsePool::scheduleReset(utf8)` which drops
session state and full-parses. The pool's pending-coalesce honors
Reset precedence (Reset wins over a pending incremental update).

## Per-library guides

- `libs/markoff-foundation/CLAUDE.md`
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
