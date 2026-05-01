# Live Projection Layer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Status (2026-05-01).** Stages 1-3 + the prep rename **shipped** at branch tip (`b07b07c`). Stages 4 v0 + 5 were attempted, the empty-paragraph hole failed dogfood with five distinct failure modes (spec §3.6), and the work was reverted at `cfbc30f`. Stage 4 v1 (IME-preedit redesign) and v1 of Stage 5 are below, **deferred to a fresh session**. Read the handoff brief at `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md` before starting.

> **Order matters: refactor first, extend second.** Stages 1–3 are pure restructure with no behavior change. Stage 4 v1 introduces the first hole, only after the layer's shape has soaked. Resist the temptation to ship Stage 4 v1 alongside any other refactor; treat it as its own focused change.

**Goal:** Land the `LiveProjectionLayer` per `docs/specs/2026-05-01-live-projection-layer.md`. Two existing speculations (`InlineFormatHighlighter`, `LiveSpeculativeFenceController`) move behind it without behavior change (Stages 1-3, **done**). One new citizen — the empty-paragraph hole — fixes the user-reported "Enter does nothing at end of paragraph" bug (Stage 4 v1, **deferred**).

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

## Stage 4 (v0) — REVERTED

The original Stage 4 (T17-T25) implemented a "reify-on-first-keystroke" pattern that wrote `\n\n` to source at hole-creation time and reified each hole on first printable keystroke via a separate `applyLocalEdit`. Five failure modes surfaced during dogfood (spec §3.6). Reverted at commit `cfbc30f`.

**Do not re-attempt the v0 pattern.** The v1 plan below replaces it.

## Stage 4 (v1) — Empty-paragraph hole (IME-preedit pattern, deferred)

Per spec §3.2-§3.5. The hole's delegate is a real local-typing surface; commits are debounced and atomic; navigation is non-destructive.

- [ ] **T17** `BlockHole` value type: `{ id: quint64, kind: QString, reifyOffset: quint32, bufferText: QString, afterParsedRow: int }`. Drop `pairedSourceEditByteCount` (no paired source edit in v1). Drop `synthetic` flag (the bufferText / reifyOffset distinction is enough).
- [ ] **T18** `LiveBlockModel::interleaveHoles`: accept a list of hole rows from the layer and interleave with parsed blocks. Hole rows expose `kind = "paragraph"`, `text = bufferText` (live, mutates as user types), `IsHoleRole = true`, `HoleIdRole = id`. The `text` role is the buffer's current contents — the delegate's TextEdit binds to this and stays in sync via the binding's contentsChange path.
- [ ] **T19** `LiveProjectionLayer` v1 API:
    - `createBlockHole(hole)` returns id; pure view-state add, **no source edit**.
    - `setBlockHoleBuffer(holeId, text)` updates buffer; emits `bufferChanged(holeId)` for the model row to update.
    - `commitBlockHole(holeId)` builds `MarkoffEdit("\n\n" + bufferText)` at `reifyOffset`, calls `applyLocalEdit`, drops the hole, schedules `holeReified(viewRow, qtPos)` to fire on the next `parseUpdated`.
    - `dropBlockHole(holeId)` abandons; no source mutation; emits `holeDropped(viewRow)` so the view can route focus to the nearest neighbor.
    - `commitAllPendingHoles()` for the save path.
- [ ] **T20** `LiveStructuralKeyHandler::tryHandle` for Enter at EOB: only `createBlockHole(...)`, no `applyLocalEdit`. Stacked-Enter coalesce: if a hole already exists at this `afterParsedRow`, no-op (hole stays, second Enter does nothing because there's nothing to commit).
- [ ] **T21** `ParagraphDelegate.qml` hole-row binding:
    - `isHole === true`: TextEdit accepts keys normally (no special routing on first keystroke).
    - `onTextChanged` (or via binding) calls `layer.setBlockHoleBuffer(holeId, currentText)`.
    - Idle commit: a `Timer` with `interval: 250`, `repeat: false`, restarted on each text change; on triggered, calls `layer.commitBlockHole(holeId)` if buffer non-empty.
    - Focus-out commit: `onActiveFocusChanged: if (!activeFocus && bufferText.length > 0) layer.commitBlockHole(holeId)`.
    - Focus-out abandon: `if (!activeFocus && bufferText.length === 0) layer.dropBlockHole(holeId)`.
    - Esc key abandons regardless.
    - Backspace at qtPos 0 with empty buffer: `layer.dropBlockHole(holeId)` and route focus to previous block's end.
    - Arrow keys at edges: let TextEdit's default behavior fire (focus may move to neighbor delegate via QML focus chain); the `onActiveFocusChanged` handler picks up commit/abandon.
- [ ] **T22** `LiveView.qml` focus routing on commit:
    - Connect to `layer.holeReified(viewRow, qtPos)`, queued on next `parseUpdated`.
    - When fired, `_routeFocusToRow(viewRow, qtPos, useStart=false)` — but the bounded retry loop is now justified for "wait for delegate to materialise after parse" only, not "wait for delegate that's racing with focus loss."
    - Confirm: this single focus-routing is the only user-visible delegate destruction during the typing flow. Stress-typing into the hole no longer crosses a destroy boundary because the hole's delegate stays alive throughout typing.
- [ ] **T23** Save flush: in `app/main.cpp` (or wherever Ctrl+S is handled), call `binding.projectionLayer.commitAllPendingHoles()` before invoking the document save. Add a test that creates a hole, types into it, Ctrl+S, exits the app, reopens — verify the file contains the typed text.
- [ ] **T24** Tests:
    - `tst_view_qml_live_paragraph_hole.cpp` (unit, direct surfaces):
      - End-of-doc Enter creates a hole; doc source unchanged; row count goes 1 → 2; buffer empty.
      - `setBlockHoleBuffer(id, "abc")` updates buffer; doc source still unchanged.
      - `commitBlockHole(id)` performs one `applyLocalEdit("\n\nabc")`; row count drops then climbs as parse arrives; final source is `Hello\n\nabc`.
      - `dropBlockHole(id)` with empty buffer: row count goes 2 → 1; doc source unchanged.
      - `dropBlockHole(id)` with non-empty buffer (abandon path): doc source unchanged; user's typed text is lost (acceptable; documented behavior — the abandon path is for empty buffers).
      - Stacked Enter on existing hole: no-op (hole stays, no second hole created, doc source still unchanged).
    - `tst_view_qml_live_paragraph_hole_integration.cpp` (QQuickView integration, slow):
      - **Stress-typing test** (load-bearing for v1): seed `"Hello"`, drive `Key_End` then `Key_Return`, then a sequence of `keyClick(Key_T)`, `keyClick(Key_H)`, `keyClick(Key_I)`, ..., with `QTest::qWait(20)` between each (approximating real keystroke timing). Drive `processEvents()` between each pair. Wait for idle commit (qWait 300+). Assert final source equals `"Hello\n\nthis is interesting"` exactly — character order preserved.
      - Arrow-key navigation test: create hole, type "abc", press Down arrow. Buffer commits; focus moves to wherever Down goes; doc source includes "abc".
      - Backspace-in-empty-hole test: create hole, immediately Backspace. Hole drops; focus returns to previous block's end; doc source unchanged.
      - Save-while-pending test: create hole, type "X", trigger save (programmatic), verify file contains `"Hello\n\nX"`.
      - Esc abandons test: create hole, type "X", press Esc. Hole drops; doc source unchanged. (User loses preedit; acceptable abandonment.)
      - Mid-block Enter regression: existing behavior unchanged.
- [ ] **T25** Run full ctest. **Verification gate:** all prior tests green; new paragraph-hole tests green; specifically the stress-typing test must produce in-order source bytes.

## Stage 5 (v1) — Dogfood and documentation

- [ ] **T26** Manual-test the demo app on the v1 implementation: exercise Enter at end of file, type fast, navigate with arrow keys, save, click elsewhere, Esc, double-Enter. Confirm none of the v0 failure modes recur. Verify against spec §3.3 / §3.6 / §6 / §9.
- [ ] **T27** Update `libs/markoff-view-qml/CLAUDE.md`'s "Projection layer" section: add the v1 hole semantics (preedit buffer, commit triggers, abandon triggers); flip the old hole description if it remains.
- [ ] **T28** Spec frontmatter: flip status from "Stage 4 deferred" to "Stage 4 v1 implemented." Update landing-commits list. Mark spec §9 open questions resolved-or-deferred per dogfood findings.
- [ ] **T29** Run the slow tail: `tst_realistic` and `tst_benchmark` once to confirm no perf regressions.

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

## Risks and mitigations (v1)

- **Risk:** The 250ms idle-debounce is wrong (too short → distracting parse round-trips, jittery delegate swap during a typing burst; too long → user saves and loses content).
  **Mitigation:** Save path commits unconditionally first (T23) so save can never lose content. Adjust 250ms based on dogfood feedback in §9.

- **Risk:** The buffer-mirror path (TextEdit text → `setBlockHoleBuffer`) introduces a feedback loop if the model's `text` role push-back drives further `onTextChanged` events.
  **Mitigation:** Cycle-guard mirroring `LiveEditBinding`'s `m_applyingModelUpdate` pattern. The hole's binding flags model-driven updates so `onTextChanged` ignores them.

- **Risk:** Commit-on-focus-out fires before the user's `onActiveFocusChanged` handler runs (Qt focus order quirk), causing the buffer to be lost during normal navigation.
  **Mitigation:** Test T24's "arrow-key navigation test" asserts final source contains the typed buffer; if it fails, switch to a `Connections` on the parent ListView's focus item changes, which fire before the delegate's `activeFocus` flips.

- **Risk:** Qt's `QQuickTextDocument` synchronously emits `contentsChange` for the very first keystroke into a freshly-bound TextEdit, before the binding has a chance to install its cycle guard. The buffer ends up double-applied.
  **Mitigation:** The hole's binding initializes with `bufferText` already set in the model role; the first user keystroke produces a `contentsChange(0, 0, 1)` that the binding interprets as a normal append, not as a model-driven full-replace. Verify in T24 unit tests.

- **Risk:** The stress-typing test passes locally but real users still see scrambled letters.
  **Mitigation:** The stress test uses `qWait(20)` per keystroke + `processEvents()` between, mimicking real keyboard timing. v0's failure was that `keyClick` events landed within a single event-loop spin, masking the race. If v1's stress test still doesn't reproduce a real user's experience, the test isn't strict enough; iterate.

- **Risk:** Commit-on-focus-out leaves a brief visual flicker when the new delegate replaces the hole (delegate destroyed → parse round-trip → new delegate materialises).
  **Mitigation:** The hole's delegate visually displays the buffered text already; the new real delegate displays the same text from source. Visually identical content; the swap is cosmetically silent unless layout differs (e.g. the real paragraph adopts a different style). Acceptable for v1; refine in v2 if dogfood objects.

- **Risk:** Anchor invalidation under collaborative edits drops a hole the user is actively typing into.
  **Mitigation:** Out of scope for v0; collab-edit hole-invalidation lands in a follow-on. v0 single-user dogfood doesn't exercise this.
