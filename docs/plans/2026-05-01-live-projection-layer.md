# Live Projection Layer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Order matters: refactor first, extend second.** Stages 1–3 are pure restructure with no behavior change — every existing test must remain green. Only after the layer's shape has soaked does Stage 4 add the empty-paragraph hole. Resist the temptation to land the hole alongside the refactor; the design pressure of restructuring will reshape the layer's API and you want to absorb that before introducing a new concept.

**Goal:** Land the `LiveProjectionLayer` per `docs/specs/2026-05-01-live-projection-layer.md`. Two existing speculations (`InlineFormatHighlighter`, `LiveSpeculativeFenceController`) move behind it without behavior change. One new citizen — the empty-paragraph hole — fixes the user-reported "Enter does nothing at end of paragraph" bug.

**Architecture:** A single C++ class (`LiveProjectionLayer`) sits between `EditorBackend`/`MarkoffDocument` (authority) and `LiveBlockModel` (view-binding). It owns two item kinds (Predictions, Holes) under a shared reconciliation protocol. Public API of `LiveBlockModel`, `EditorBackend`, `Session`, and all QML components is unchanged. Inline-prediction painting and block-kind speculation continue to work; the empty-paragraph hole is added as the layer's first hole consumer.

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+. Build with `-j 8` (no bare `-j`).

**Branch:** `exploration/new-foundation` (worktree `.worktrees/foundation-exploration/`).

**Related docs:**
- Spec: [`docs/specs/2026-05-01-live-projection-layer.md`](../specs/2026-05-01-live-projection-layer.md)
- Editing invariants: [`docs/specs/2026-04-30-live-editing-design.md`](../specs/2026-04-30-live-editing-design.md) §4
- Library guide: [`libs/markoff-view-qml/CLAUDE.md`](../../libs/markoff-view-qml/CLAUDE.md)

**Out of scope (this plan):** Hole inventory beyond empty-paragraph (list items, checklists, code-fence interior, blockquote, callout, table cells, links, wikilinks, footnotes, math). Multi-cursor holes. Configurable abandonment timeouts. Hole serialization. Heuristic create-on-click. Each is its own follow-on plan once v0 lands.

---

## File Structure

### Files created

```
libs/markoff-view-qml/
  include/markoff/view/qml/
    LiveProjectionLayer.h
    ProjectionItem.h           (BlockHole / InlineHole / InlinePrediction / BlockKindPrediction value types)
  src/
    LiveProjectionLayer.cpp
    ProjectionItem.cpp         (mostly trivial; can be header-only if it stays small)
  tests/
    tst_view_qml_live_projection_layer.cpp
    tst_view_qml_live_paragraph_hole.cpp
```

### Files modified

```
libs/markoff-view-qml/
  src/InlineFormatHighlighter.{h,cpp}        — register predictions with layer instead of self-painting
  src/LiveSpeculativeFenceController.{h,cpp} — register kind-predictions with layer
  src/LiveStructuralKeyHandler.{h,cpp}       — call layer.createBlockHole at end-of-block Enter
  src/LiveListModelBinding.{h,cpp}           — own a LiveProjectionLayer, wire it up
  src/LiveBlockModel.{h,cpp}                 — accept a projection-layer reference, interleave hole rows
  qml/LiveView.qml                           — pass projection-layer reference to delegates
  qml/delegates/ParagraphDelegate.qml        — distinguish hole-row from real-row visually only if needed (likely not — holes look identical)
  CMakeLists.txt                             — add new source / test files
  tests/CMakeLists.txt                       — add new test executables
```

### Files deleted

None. (No retirements; the refactor is in-place.)

---

## Stage 1 — Layer skeleton (no behavior change)

Stand up the `LiveProjectionLayer` class with the public API in spec §7, but no producers wired in yet. All existing tests pass unchanged.

- [ ] **T1** Create `include/markoff/view/qml/ProjectionItem.h` with `BlockHole`, `InlineHole`, `InlinePrediction`, `BlockKindPrediction` value types per spec §7.
- [ ] **T2** Create `include/markoff/view/qml/LiveProjectionLayer.h` with the public API surface from spec §7.
- [ ] **T3** Create `src/LiveProjectionLayer.cpp` with empty implementations (storage of items, pass-through getters, no-op reconcile slots).
- [ ] **T4** Wire CMakeLists: add new files to `markoff_view_qml` target. Build clean.
- [ ] **T5** Create `tests/tst_view_qml_live_projection_layer.cpp` covering:
    - Construction.
    - Add prediction → `predictionsForRow` returns it.
    - Add block-kind prediction → `blockKindPredictionFor(row)` returns it.
    - Add hole → `rowIsHole(row)` returns true.
    - Reconcile-on-parse: (placeholder, asserts no crash with empty parse output).
- [ ] **T6** Register tests in `tests/CMakeLists.txt`. Run full ctest. **Verification gate:** all existing 78 tests still green; new `tst_view_qml_live_projection_layer` green.

## Stage 2 — Move `LiveSpeculativeFenceController` behind the layer

Pure refactor. Behavior is identical pre/post.

- [ ] **T7** `LiveListModelBinding` constructs and owns a `LiveProjectionLayer`. Expose it via a `projectionLayer()` getter. (No QML-visible change.)
- [ ] **T8** `LiveSpeculativeFenceController` gains a `setProjectionLayer(LiveProjectionLayer *)` setter. Wire it from `LiveListModelBinding` at construction.
- [ ] **T9** `LiveSpeculativeFenceController::onEditApplied` (or whatever currently mutates `LiveBlockModel` directly) is rewritten to *register a `BlockKindPrediction` with the layer*. The layer applies the kind change to the model on the controller's behalf.
- [ ] **T10** Reconciliation on parse return moves from controller into `LiveProjectionLayer::reconcileAgainstParse`. The controller becomes a producer only; consumption is the layer's job.
- [ ] **T11** Run `tst_view_qml_live_speculative_fence`. **Verification gate:** all assertions pass unchanged. If they don't, the refactor mis-translates the protocol — fix before proceeding.

## Stage 3 — Move `InlineFormatHighlighter` behind the layer

Same shape as Stage 2 but for inline predictions.

- [ ] **T12** `InlineFormatHighlighter` gains a `setProjectionLayer(LiveProjectionLayer *)` setter. Wire from `LiveListModelBinding` (or from each delegate's binding — pick the one that mirrors current ownership; minimize churn).
- [ ] **T13** Source-scan logic in the highlighter is rewritten to *emit `InlinePrediction`s into the layer* rather than painting directly. The highlighter's `formatBlock` reads predictions back from the layer for the current block range and applies `QTextCharFormat`.
- [ ] **T14** Parser-confirm reconciliation (the existing logic that drops/keeps speculative styling on parse return) moves into `LiveProjectionLayer::reconcileAgainstParse`.
- [ ] **T15** Run `tst_view_qml_inline_format_highlighter`. **Verification gate:** all assertions pass unchanged.
- [ ] **T16** Run full ctest. **Verification gate:** 78/78 (or current count) green. The layer now owns both prediction kinds with no behavior change.

## Stage 4 — Empty-paragraph hole (the new behavior)

Now the additive work. With the layer's shape settled, holes are mostly bookkeeping.

- [ ] **T17** Add `LiveBlockModel::interleaveHoles` (or analogous mechanism): the model accepts a list of hole rows from the layer and emits them interleaved with parsed blocks. Anchor sort keeps order stable. Hole rows expose the same roles (`kind`, `text`, `blockAnchor`) — `text` is empty, `blockAnchor` is the synthetic-anchor sentinel.
- [ ] **T18** Add a hole-creation hook to `LiveStructuralKeyHandler::tryHandle`: when Enter at `qtPos == blockText.length()` AND the block is the last in the document (or the block following has zero parser visibility), insert `\n\n` as today AND call `layer->createBlockHole(BlockHole{kind="paragraph", origin=currentBlockEnd+2})`.
- [ ] **T19** Wire focus routing for newly-created holes: after the next parse arrives, `LiveListModelBinding` (or `LiveView.qml`) detects the new hole row and routes focus into its delegate at `cursorPosition: 0`. Mirror the existing `m_firstInsertPending` / `Qt.callLater` chain in `LiveView.qml`.
- [ ] **T20** Reification path: `ParagraphDelegate` (or `LiveEditBinding`) detects "first printable character into a hole-backed row" and routes the keystroke through `LiveProjectionLayer::reify(holeId, text)`. The layer builds the canonical `MarkoffEdit`, calls `applyLocalEdit`, and drops the hole. Next parse produces the real block at the same model index.
- [ ] **T21** Abandonment: layer subscribes to focus changes. Focus leaves a hole-row delegate → drop the hole. Idle timer (30s, hard-coded for v0) → drop the hole.
- [ ] **T22** Backspace inside an empty hole at qtPos 0: drop the hole, route focus back to the previous row's end. No CRDT edit.
- [ ] **T23** Undo coalescing: when a hole is created paired with a real CRDT edit (`\n\n` insert), register the pairing on the hole. Ctrl+Z while the hole is unreified drops the hole AND triggers the CRDT undo for the paired edit, in one user-visible step.
- [ ] **T24** Create `tests/tst_view_qml_live_paragraph_hole.cpp`:
    - End-of-doc Enter creates a hole; `listView.count` becomes 2; focus lands in the new row at cursor 0.
    - First printable character reifies; `listView.count` stays 2 (hole replaced by real block at same index); doc source contains the typed character.
    - Focus-out abandons; `listView.count` returns to 1; doc source contains trailing `\n\n` only.
    - Backspace inside empty hole at qtPos 0 drops the hole and routes focus to previous row.
    - Ctrl+Z inside unreified hole drops the hole and the paired `\n\n` edit; doc source returns to pre-Enter state.
    - Mid-block Enter still works (regression check on the existing path).
- [ ] **T25** Run full ctest. **Verification gate:** all prior tests green; new paragraph-hole tests green.

## Stage 5 — Dogfood and documentation

- [ ] **T26** Manual-test the demo app: `cmake --build build-dev --target markoff-view-qml-app -j 8` and exercise Enter at end of file, Enter on blank line, Enter then immediately Ctrl+Z, Enter then click elsewhere, Enter twice in a row. Verify each matches spec §3.3 / §6 / §9.
- [ ] **T27** Update `libs/markoff-view-qml/CLAUDE.md`: add a "Projection layer" section noting (a) the layer exists, (b) new speculations and holes go through it, (c) invariants 11–16 from the spec.
- [ ] **T28** Mention in the spec frontmatter: status flips from `draft` to `implemented (v0)`. Cross-reference this plan from the spec.
- [ ] **T29** Run the slow tail: `tst_realistic` and `tst_benchmark` once to confirm no perf regressions from the extra layer.

---

## Verification gates summary

| Stage | Gate | Test command |
|---|---|---|
| 1 | Skeleton compiles, no behavior change | `ctest --test-dir build-dev -R '^tst_view_qml_' -j 8` |
| 2 | Fence speculation unchanged behavior | `ctest --test-dir build-dev -R live_speculative_fence` |
| 3 | Inline prediction unchanged behavior | `ctest --test-dir build-dev -R inline_format_highlighter` |
| 4 | Empty-paragraph hole works | `ctest --test-dir build-dev -R 'live_paragraph_hole|live_view_qml'` |
| 5 | Manual dogfood + perf neutral | (manual) + `ctest --test-dir build-dev -R 'tst_realistic|tst_benchmark'` |

Final gate: full `ctest --test-dir build-dev -j 8 -E 'tst_realistic|tst_benchmark'` green; manual demo-app pass on Enter / focus-out / undo / mid-block-split.

---

## Risks and mitigations

- **Risk:** The refactor (Stages 2–3) changes `InlineFormatHighlighter` painting timing in ways tests don't cover, manifesting as visible flicker only.
  **Mitigation:** After Stage 3, manually scroll a long file in the demo app and watch for inline-formatting flicker on speculative bold/italic. If present, profile the layer's reconcile path and tighten its synchronous-call discipline (invariant 15).

- **Risk:** Hole reification races the next parse, producing a duplicate row briefly.
  **Mitigation:** The reification path drops the hole *before* `applyLocalEdit` returns; the next parse arrives later. Confirm in T24 with a `QTRY_COMPARE` on `listView.count` immediately post-reification — must be 2, not 3.

- **Risk:** Undo of an Enter that created a hole leaves orphan view state if the pairing isn't tracked.
  **Mitigation:** T23 explicitly tests this path; the pairing field on `BlockHole` is the mechanism.

- **Risk:** The 30s idle-abandonment threshold is wrong (too short → user thinks it's flaky; too long → save-with-stale-hole confusion).
  **Mitigation:** v0 hard-codes 30s and we revisit after a week of dogfooding (spec §9). If needed, the threshold can become per-hole-kind in a follow-on spec.

- **Risk:** Anchor invalidation under collaborative edits drops a hole the user is actively typing into.
  **Mitigation:** Out of scope for v0; collab-edit hole-invalidation lands in a follow-on. v0 single-user dogfood doesn't exercise this.
