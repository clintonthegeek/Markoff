# D4 — Parser scope reduction

**Date:** 2026-05-07
**Status:** spec-approved (pending user review of this document)
**Supersedes:** `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md`
**Branch:** `exploration/new-foundation`
**Working tree:** `.worktrees/foundation-exploration/`

---

## 1. Goal

Retire the document-wide-incremental-parse pipeline. After D4, every parse is one of two shapes:

| Shape | Entry point | When it fires | Thread |
|---|---|---|---|
| Cold load | `Markoff::Document::fromMarkdown(QString)` | At load (`MarkoffDocument::loadFromMarkdown`) and on `resetContent` | Calling thread |
| Per-block on demand | free function `Markoff::inlineSpansFor(const QByteArray &) → QList<SourceSpan>`, invoked through `MarkoffDocument::inlineSpansFor(BlockId)` on cache miss | First read of a block's inline spans after content/kind change | Calling thread |

There is no third shape. No reparse on edit. No worker thread for parse. No `parseUpdated` signal, no `parseSequence` watermark, no `MarkoffEdit` / `applyLocalEdit` chain. No `IncrementalParseSession`. No `ParsePool`. No `parseIncremental`. No `Document::fromComponents`.

This rests on a strong claim, accepted at design time: **a Markdown editor's full feature set is expressible through those two parse shapes plus the per-block CRDT edit primitives (`Cmd::*`, `d2ApplyBufferEdit`).**

## 2. Why now

Three conditions hold at the start of D4:

1. **D2 landed and shipped a parallel D2 path.** D2 Phase 11 (`473fb1f`) routed `LiveEditBinding` through `d2ApplyBufferEdit`; the live-render hot path no longer goes through `applyLocalEdit`. D2 Phase 14 (`6b45b4a`) marked the deprecated symbols `[[deprecated]]`.
2. **D3 landed and dogfooded.** Per-item `ListItem` blocks, caller-driven renumbering, marker attrs, delegate rendering. 146/146 tests pass. Live-render is the canonical view leaf.
3. **The deprecated machinery is still alive at runtime.** `MarkoffDocument` constructs a `ParsePool` unconditionally. Every `applyLocalEdit` / `undo` / `redo` / `applyRemoteOps` / `resetContent` still calls `parsePool.schedule(toMarkdownUtf8(), editSequence)` — serializing the whole document and shipping it off-thread for reparse — and the `parseUpdated` relay still fires. D4 is what makes the deprecation real.

## 3. End-state architecture

```
                   ┌─────────────────────────────────────┐
                   │       Markoff::MarkoffDocument      │
                   │                                     │
                   │  Per-block CRDT buffers             │
                   │  IdList block order                 │
                   │  Sibling causal-LWW maps            │
                   │  UndoLog + Transactions             │
                   │  WatermarkCoordinator               │
                   │  InlineParseCache (per block)       │
                   └────┬──────────────┬─────────────┬───┘
                        │              │             │
       d2ApplyBufferEdit│   Cmd::*     │  applyFlatEdit (new)
       (per-block,      │  (structural │  (buffer-global,
        from a per-     │   + intra-   │   from a flat-text
        block view)     │   block)     │   view; decomposes
                        │              │   internally)
                        │              │
   ┌────────────────────┴─┐   ┌────────┴─────────────┐
   │  markoff-live-render │   │  markoff-source-     │
   │                      │   │  widget              │
   │  LiveEditBinding     │   │  QPlainTextEdit-     │
   │  LiveStructuralKey   │   │  based binding;      │
   │  Handler             │   │  listens to          │
   │  LiveListModel-      │   │  d2DocumentChanged.  │
   │  Binding             │   │                      │
   └──────────────────────┘   └──────────────────────┘

                   ┌─────────────────────────────────────┐
                   │  Markoff::Document (markoff-parser) │
                   │                                     │
                   │  fromMarkdown(QString)              │ ← load only
                   │  extract(QString) → ExtractedSource │
                   │  topLevelBlocks(), headings(),      │
                   │  links(), tags(), footnotes(),      │
                   │  parsedFrontmatter(), ...           │
                   │                                     │
                   │  inlineSpansFor(QByteArray)         │ ← per-block,
                   │    → QList<SourceSpan>              │   cache miss,
                   │                                     │   sync
                   └─────────────────────────────────────┘
```

**Signal surface on `MarkoffDocument` post-D4:** `d2DocumentChanged`, `contentsChanged`, undo/redo signals, watermark-coordinator notifications. **No** `parseUpdated`. **No** `parseSequence` accessor.

**Threading post-D4:** all parser work runs on the calling thread. Per-block inline parse is microseconds for paragraph-sized blocks; the worry case (large fenced code blocks) isn't a typing-hot-path concern.

**Caches:** `InlineParseCache` (per-block, keyed by block content + kind) is the only persistent parse cache. Foundation owns it; survives unchanged.

## 4. Scope of deletions and migrations, by library

### 4.1 `libs/markoff-parser`

**Delete:**

- `TreeSitterParser::parseIncremental(...)`
- `TreeSitterParser::buildDocumentQueries(prior, edits)` (the incremental overload — the no-arg overload survives)
- `TreeSitterParser::findBlockBoundaries()` and the nested `BlockBoundary` struct
- `TreeSitterParser::buildSpanMap()` as a public method (becomes a private helper or is inlined into `inlineSpansFor` / `Document::fromMarkdown`'s pipeline)
- The observability counters: `inlineTreeReuseCount()`, `blockChangedByteCount()`, `lastParseBlockNs()`, `lastParseInlineNs()` and their backing members
- The inline-tree shift/reuse internals: `m_inlineTrees`, `m_inlineRanges`, `m_lastChangedRanges`, `m_lastInlineReuseCount`
- The `ByteEdit` struct and the nested `ByteRange` struct
- `Document::fromComponents(...)` (only `IncrementalParseSession` calls it)
- `tst_incremental_parse.cpp` entirely
- Any `ExtractedSource` fields that were only consumed by `fromComponents` (verify at plan time: `frontmatterBlockStart`, `frontmatterBlockEnd`, `frontmatterEofClose`)

**Keep:**

- `Document::fromMarkdown(QString)`, `Document::extract(QString)`
- All `Document` accessors (frontmatter / headings / links / tags / footnotes / top-level blocks / word + character counts / subpath extract)
- `TreeSitterParser::parse(QString)`, `TreeSitterParser::buildDocumentQueries()` (one-shot, no-arg overload)
- The free function `Markoff::inlineSpansFor(const QByteArray &) → QList<SourceSpan>` — signature unchanged
- All other parser tests: `tst_document.cpp`, `tst_document_queries.cpp`, `tst_document_top_level_blocks.cpp`, `tst_frontmatter.cpp`, `tst_linktext.cpp`, `tst_parser_inline_span_bake.cpp`, `tst_parser_list_items.cpp`, `tst_splitter.cpp`, `tst_splitter_table_passthrough.cpp`, `tst_table.cpp`

### 4.2 `libs/markoff-foundation`

**Delete:**

- `src/ParsePool.h` / `.cpp`, `src/ParsePoolWorker.h` / `.cpp`, `src/IncrementalParseSession.h` / `.cpp`, `src/ParsePhases.h`
- The `parseUpdated` signal, the `parseSequence()` accessor, the backing `parseSequence` and `latestBlockAnchors` members on `MarkoffDocument`
- The constructor's `parsePool` ownership and its `parseReady → parseUpdated` relay lambda
- The `parsePool.schedule(...)` and `parsePool.scheduleReset(...)` calls inside `applyLocalEdit` / `undo` / `redo` / `applyRemoteOps` / `resetContent`
- `MarkoffEdit` (the type, header, `Q_DECLARE_METATYPE`) — gated on the source-widget migration in §4.4 landing first
- `MarkoffDocument::applyLocalEdit(...)` — same gate
- `parsePool.isPending()` accessor and `parsePool.setRenderPhaseTaps(...)` accessor (both retire with the pool)
- Tests dedicated to the deprecated machinery:
  - `tst_foundation_parse_pool`
  - `tst_foundation_parse_phases`
  - `tst_foundation_parse_input_edit_seq`
  - `tst_foundation_parse_sequence`
  - `tst_foundation_block_anchor_compute`, `tst_foundation_block_anchor_perf`, `tst_foundation_block_anchor_queries`, `tst_foundation_block_anchor_stability` (all hinge on `parseUpdated`/`applyLocalEdit` reparse-survival, which has no analogue in D2)

**Keep:**

- `BlockAnchor` and its accessors — the type stays as a public anchor primitive; only the `latestBlockAnchors` cache and its `parseUpdated`-payload role retire
- All D2 surface: `Cmd::*`, `d2ApplyBufferEdit`, `iterateBlocks`, `loadFromMarkdown`, `serializeForSave`, `inlineSpansFor(BlockId)`, `d2DocumentChanged`, `editSequence()`, `textAnchorAt`/`resolveTextAnchor`, `blockAnchorAt`/`blockByteRange`/`blockAt`/`offsetInBlock`, `Selection`, `Origin`, `Theme`
- `WatermarkCoordinator`, `InlineParseCache`, `UndoLog`, `BlockId`, `BlockEdit`, `StructuralOp`, `CausalLwwMap`, `BlockKindRegistry`, all per-block CRDT internals
- All foundation tests not listed as deletable

**Add — flat-edit decomposition primitive:**

A buffer-global edit entry point on `MarkoffDocument` that does not go through `MarkoffEdit` or the deprecated pool. Tentative shape:

```cpp
// In an UndoLog::Transaction, decompose a flat byte-range edit
// (oldStart..oldEnd → newText) into the appropriate sequence of
// per-block d2ApplyBufferEdit calls plus structural ops
// (splitBlock / mergeBlocks) when the edit crosses a block boundary,
// and commit. Returns whatever the D2 contract returns for a
// transaction (typically an Origin-tagged change-set; final shape
// pinned at plan time).
void MarkoffDocument::applyFlatEdit(quint32 oldStart,
                                    quint32 oldEnd,
                                    QByteArray newText,
                                    Origin origin);
```

The exact name, return shape, and whether the decomposition lives behind a public `Cmd::applyFlatEdit` versus a non-`Cmd` method on `MarkoffDocument` are pinned at plan time. The spec commits only to: (a) foundation owns the decomposition, (b) the decomposition uses only D2 primitives (`d2ApplyBufferEdit` + structural `Cmd::*`), (c) the API is callable from a flat-text view without the view needing to know block boundaries.

**Why a foundation primitive (not view-side decomposition):** keeps decomposition logic centralised, testable, reusable by any future flat-text consumer, and free of foundation-internal CRDT plumbing leaking into views.

### 4.3 `libs/markoff-bench`

**Delete entirely.** Library, headers, source, tests (`tst_benchmark`, `tst_realistic`), CMake target, JSON reporter, scenario types, and `apps/bench/` (including `markoff-bench-render.cpp` and any scaffolding). The library benchmarks Tier 1 (direct `IncrementalParseSession`) and Tier 1b (`MarkoffDocument` + `ParsePool` + `parseUpdated`), neither of which exists post-D4.

A fresh bench harness that measures the D2 hot path (per-block buffer edit latency, inline-parse cache miss latency, cold load) is **out of scope** for D4 and may be re-introduced separately in a future arc.

### 4.4 `libs/markoff-source-widget` — migration

The source-widget moves entirely off `MarkoffEdit` / `applyLocalEdit`.

**Forward direction (QPlainTextEdit → MarkoffDocument):** the binding listens to `QPlainTextEdit::contentsChange(int qtPos, int charsRemoved, int charsAdded)`, computes UTF-8 byte offsets, and dispatches to the new `applyFlatEdit` primitive. No `MarkoffEdit` construction, no `applyLocalEdit` call.

**Reverse direction (MarkoffDocument → QPlainTextEdit):** the binding subscribes to `d2DocumentChanged` (replacing any subscription to `parseUpdated` or to the old `contentsChanged` shape if its payload type was `QList<MarkoffEdit>`). On notification, the binding regenerates the QPlainTextEdit's text via `serializeForSave` (or an incremental walk if profiling at plan time shows full-text resync is too costly), with re-entrance guarded so the forward path doesn't re-fire during the reverse-update.

**Tests to rewrite:** `tst_source_widget_binding_roundtrip`, `tst_source_widget_findbar`, `tst_source_widget_editor`. Test edits issue through `QPlainTextEdit::insertPlainText`, `setPlainText`, simulated keystrokes, or direct calls to `applyFlatEdit` — never `applyLocalEdit`. The `parseUpdated` `QSignalSpy` line in `tst_source_widget_editor::setDocument_attaches_and_seed_text_appears` is removed; the `qWait(50)` settle is replaced with a `d2DocumentChanged`-based wait or a synchronous post-call check.

**App update:** `libs/markoff-source-widget/app/main.cpp` updated to the new entry point.

### 4.5 `libs/markoff-view-qml` — live-mode retirement

CLAUDE.md says live mode retires "when markoff-live-render reaches dogfood-stable." D2 was signed off 2026-05-05 and D3 landed 2026-05-06; live-render is the canonical view. D4 is the moment.

**Delete (live-mode-only):**

- `LiveProjectionLayer`, `LiveStructuralKeyHandler`, `LiveClipboardController`, `InlineFormatHighlighter`, `ProjectionItem`, `LiveSpeculativeFenceController`
- `EditorBackend`'s `parseUpdated → parseUpdatedAt` relay (and the `parseUpdatedAt` signal if no other consumer remains; verify at plan time)
- The live-mode QML layer (component files, registrations)
- Live-mode tests: any `tst_view_qml_live_*` file, `tst_view_qml_inline_format_highlighter`, `tst_view_qml_block_walker`, `tst_view_qml_ast_block_diff`, `tst_view_qml_app_smoke` and `tst_view_qml_integration` if they exercise live mode (each test classified at plan time)

**Keep (source-mode):**

- The QML source view's widget, bindings, and tests
- `tst_view_qml_search_backend` if its target is the source-mode search; otherwise it goes too — classified at plan time

### 4.6 `libs/markoff-live-render`

A single comment update in `tst_live_render_paragraph_edit.cpp` (it references `parseUpdated` in a comment only). Zero behaviour change.

### 4.7 Top-level

- `CMakeLists.txt`: drop `add_subdirectory` lines for `libs/markoff-bench` and `apps/bench`. Verify view-qml's CMake disables/removes live-mode targets without breaking source-mode targets.
- `CTestTestfile.cmake` regeneration follows from CMake reconfigure.
- Any references to retired libraries / apps in `scripts/` are updated or removed.
- `docs/d-arc/d-arc-status.md`: D4 → `complete`. Roadmap updated. Recent-changes log appended. Phase board's D4 row → `complete`.
- `CLAUDE.md` (worktree): banner updated to reflect D4 completion and point to D5 stub. Library list adjusted (markoff-bench removed, view-qml notes adjusted).
- `libs/markoff-foundation/CLAUDE.md`: removes mention of `parseUpdated` / `parseSequence` / `applyLocalEdit` accessor / `latestBlockAnchors` / ParsePool. Adds `applyFlatEdit` description.

## 5. Testing strategy

**Pre-existing tests that survive:** every parser, foundation, live-render, and source-mode test that doesn't touch the deprecated symbols continues to pass without modification. After the migrations land, the "all tests pass" bar is restored on the new shape.

**Tests deleted:** all tests dedicated to the deprecated machinery (listed under §4.1, §4.2). They tested behaviour that no longer exists; there is nothing to port.

**Tests rewritten:** `tst_source_widget_binding_roundtrip`, `tst_source_widget_findbar`, `tst_source_widget_editor` (per §4.4). The behavioural intent is preserved (binding round-trips text, find-bar finds, editor attaches to a document); only the edit-issuing mechanism changes.

**Tests added:** the new `applyFlatEdit` primitive needs coverage. Minimum cases:

1. Intra-block edit (insert / delete / replace inside one paragraph) decomposes to a single `d2ApplyBufferEdit`.
2. Edit at exactly one block boundary (e.g. inserting `\n` at end of a paragraph) decomposes correctly: typing newline at block end produces a structural split.
3. Edit that crosses a block boundary (replace text spanning two blocks) decomposes to the right sequence of structural + buffer ops within one transaction; undo restores both blocks atomically.
4. Edit equivalent to a backspace-merge at the start of a block decomposes to `mergeBlocks`.
5. Round-trip parity: for any flat text S and edit E, `applyFlatEdit(E)` followed by `serializeForSave` equals what S would produce after applying E to the flat string. This is the canonical contract that lets the source widget treat the document as a flat byte-stream.

**Acceptance bar for D4 completion:**

1. Standalone build green: `cmake -S . -B build-dev && cmake --build build-dev -j 8 && cd build-dev && ctest -j 8` passes.
2. No build references to deleted symbols anywhere in the tree.
3. `grep -r "parseIncremental\|IncrementalParseSession\|ParsePool\|parseUpdated\|parseSequence\|MarkoffEdit\|applyLocalEdit\|fromComponents\|findBlockBoundaries\|buildSpanMap" libs/ apps/ tests/` returns zero hits outside of (a) the parser library's private internals, (b) git history, (c) docs/archived material.
4. Source-widget dogfood pass: launch the source-widget app on a real document, edit, undo/redo, save; behaviour matches pre-D4.
5. Live-render dogfood pass: launch the live-render-hosting app, edit, undo/redo, save; behaviour matches pre-D4.
6. User signs off after dogfood.

## 6. Out of scope

- **Tree-sitter grammar changes.** The Markdown grammar is unchanged. D4 trims the parser-library wrapper, not tree-sitter itself.
- **Per-block parse cache redesign.** `InlineParseCache` is unchanged.
- **Bench rewrite.** A D2-aware bench harness is a separate future arc.
- **D5 (collab activation).** Stubbed at `docs/specs/2026-05-04-d5-collab-activation-STUB.md`.
- **Removing `BlockAnchor`.** It stays as a public anchor primitive.
- **Source-mode QML view in markoff-view-qml.** Source mode and its tests stay; only live mode retires.
- **Adding new public types to the parser library.** Surviving signatures are unchanged.

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | `applyFlatEdit` decomposition has a subtle bug at block boundaries (off-by-one in byte coords; wrong attr inheritance on a split-induced new block; wrong loose-list flag propagation). | Comprehensive boundary-case tests (§5). Plan-time dogfood on a corpus that exercises list / heading / fenced-code / paragraph boundaries. |
| R2 | Source-widget reverse-direction full-text resync via `serializeForSave` is too costly for large documents. | Acceptable as v1 (source mode is QPlainTextEdit; large-doc cost is dominated by Qt's text layout, not the resync). If profiling at plan time disagrees, escalate to incremental update. |
| R3 | Some surviving consumer (in source-widget or elsewhere) implicitly depended on `parseUpdated` or `parseSequence` ordering for non-parse purposes (e.g. as an event-loop settle barrier). | Plan-time grep + replace each call site; test rewrites name the replacement settle explicitly. |
| R4 | `ExtractedSource` fields turn out to have hidden consumers we miss. | Plan-time grep; only delete fields with zero hits. |
| R5 | Live-mode retirement under view-qml leaves orphan files that build-flag wrap (e.g. CMake conditionals). | Plan-time CMake audit. Keep the diff to deletions, not to flag-and-defer. |

## 8. Sequencing principles (for the plan)

These belong in the implementation plan, but the spec commits to two principles that constrain it:

1. **Bottom-up: retire consumers first, then delete symbols.** Build stays green throughout. Specifically: (a) build the new `applyFlatEdit` primitive and migrate source-widget to it; (b) retire markoff-bench and markoff-view-qml live mode; (c) delete the deprecated foundation tests; (d) delete `parseUpdated`/`parseSequence`/`MarkoffEdit`/`applyLocalEdit` and the parsePool; (e) delete the parser-library deprecated machinery; (f) update top-level CMake and docs.
2. **Source-widget migration is the gate for `applyLocalEdit` deletion.** The plan does not delete `applyLocalEdit` / `MarkoffEdit` until §4.4 is landed and source-widget tests pass on the new shape.

## 9. Open questions for plan time

| # | Question | Resolution path |
|---|---|---|
| Q1 | `applyFlatEdit` exact API: method on `MarkoffDocument` vs `Cmd::applyFlatEdit` vs both. Return shape. Origin propagation. | Plan task-zero: pick a shape based on calling-site ergonomics in source-widget. |
| Q2 | Source-widget's reverse-direction strategy: full-text resync vs incremental walk of changed blocks. | Plan task: prototype full-text resync, profile on a 100-block document, escalate if needed. |
| Q3 | Exact set of view-qml live-mode tests to delete. | Plan task: classify each test by inspection; one row per test in the plan. |
| Q4 | `tst_view_qml_search_backend` mode classification. | Plan task: read and decide. |
| Q5 | `ExtractedSource` field deletions (`frontmatterBlockStart`/`...End`/`EofClose`). | Plan task: grep; delete only with zero hits outside the parser library. |
| Q6 | Whether `contentsChanged(QList<MarkoffEdit>)` survives or is reshaped. | Plan task: it's currently typed on `MarkoffEdit` which is being deleted. Either reshape its payload or retire the signal entirely (consumers may already prefer `d2DocumentChanged`). |

## 10. Acceptance and sign-off

D4 → `complete` when the §5 acceptance bar is met and the user signs off after dogfood. The status board records the commit SHA at completion; `master` is append-only.
