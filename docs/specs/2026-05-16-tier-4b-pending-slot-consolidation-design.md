# Tier 4b — Pending-slot consolidation + auto-focus seam close-out

**Date:** 2026-05-16
**Branch:** `exploration/new-foundation`
**Predecessors:**
- `docs/specs/2026-05-11-focus-chokepoint-design.md` (tier 1; chokepoint + `m_pendingFocus` introduced)
- `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` (tier 2; docstring + rename + invariant test)
- `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md` (tier 3; UnifiedInlineTextDelegate)
- Tier-4a (partial, in-flight commits `bb4fd45`, `033292e`, `d6818f5`) — closed queue #2 concerns **#5**, **#9**, **#12**.

**Plan to follow:** `docs/plans/2026-05-16-tier-4b-pending-slot-consolidation.md` (to be written).

## 0. One-paragraph summary

Tier 4b closes queue #2 concerns **#3** (three overlapping `requestTextCaretAt*` APIs) and **#4** (two coexisting pending slots) and the **2026-05-16 discipline-log entry** on the post-tier-3 auto-focus gap. The headline finding is that **the consolidation already happened de facto in tier 1**: every production call site for structural-event cursor placement reaches `LiveCursorState::establishFocus` (~25 callers in `LiveStructuralKeyHandler`, `LiveListModelBinding`, `LiveView.qml`) or `requestTextCaretAtRow` (~9 callers in `LiveNavigationController`), which itself is a thin wrapper over `establishFocus`. The two pending-slot mechanism, the `requestTextCaretAtNewRow` API, the `requestTextCaretAtAnchor` API, and the `resolvePendingForRow`/`resolvePendingForAnchor` resolvers exist **only to serve test fixtures** — verified by exhaustive grep across `libs/markoff-live/`, `apps/`. Tier 4b deletes the dead production path, migrates the orphaned tests, and seeds initial focus through the chokepoint per the discipline-log entry. **No new authority is introduced; the old authority (`m_pendingRow`) is retired** per invariant 3, in the same plan.

## 1. Motivation

The tier-1 focus chokepoint (commit chain ending `9b30d75`) introduced `establishFocus` and `m_pendingFocus` as a *new* canonical mechanism without retiring the older `m_pendingRow` slot that backed `requestTextCaretAtNewRow` and `requestTextCaretAtAnchor`. Tier-1's spec §10 deferred the cleanup to a future tier on the assumption that both slots would have live callers. Tier-1, tier-2, tier-3, and tier-4a structural migrations have since collapsed every production call site onto the chokepoint:

- `requestTextCaretAtRow` was rewritten as `flushPendingDocumentChanges()` → `recordAt(row).blockAnchor` → `establishFocus` (`LiveCursorState.cpp:112-127`, commit `033292e`).
- `requestTextCaretAtRowVisualX` delegates to `requestTextCaretAtRow` (one caller: `LiveNavigationController.cpp:168`).
- `requestTextCaretAtNewRow` has **no production callsite** (`grep -rn requestTextCaretAtNewRow libs/markoff-live/src/ libs/markoff-live/qml/ apps/` returns only the definition + tests).
- `requestTextCaretAtAnchor` has **no production callsite** (same grep).
- All structural-key callers (`LiveStructuralKeyHandler`: 25 sites) call `establishFocus` directly.
- The two `LiveListModelBinding`-side mid-cascade callers (lines 465, 585, 597) also call `establishFocus` directly.

The `structuralRowsInserted` / `structuralRowRemoved` signals on `LiveListModelBinding` exist solely to drive the `m_pendingRow` resolvers; production has no other consumer (`grep -rn structuralRowsInserted libs/markoff-live/`).

Leaving dead-code-with-tests-as-life-support violates invariant 3 (a new authority retires the old one **in the same plan**, not a follow-up) and invariant 5 (tests must exercise the production callsite, not a synonym). The tests that exercise `requestTextCaretAtNewRow` / `requestTextCaretAtAnchor` are protecting a production path that no longer exists; they are exactly the "synonym" failure mode invariant 5 names.

The 2026-05-16 discipline-log entry on `UnifiedInlineTextDelegate.qml` is the same shape one rung down: production papers over the auto-focus gap via the chokepoint, but `ListView.focus = true` does not deliver focus to the TextEdit descendant, so two test slots (`enter_at_paragraph_end_migrates_focus`, `delete_at_row_end_merges_next`) had to migrate to explicit `requestCursor` chokepoint calls. Initial focus is the one production path that doesn't yet route through `establishFocus`. Tier 4b closes it.

## 2. Scope and explicit non-goals

### 2.1 In scope

- **#3 (full).** Delete `requestTextCaretAtNewRow` and `requestTextCaretAtAnchor` from `LiveCursorState`'s public API. The two remaining row-keyed conveniences (`requestTextCaretAtRow`, `requestTextCaretAtRowVisualX`) stay — they have live navigation callers — but their docstrings are corrected to name them as thin wrappers over `establishFocus`.
- **#4 (full).** Delete `m_pendingRow`, `PendingRow`, `onStructuralRowsInserted`, `onStructuralRowRemoved`, `resolvePendingForRow`, `resolvePendingForAnchor`. `m_pendingFocus` is the sole pending slot. The `structuralRowsInserted` / `structuralRowRemoved` signals on `LiveListModelBinding` are deleted (no consumer remains after the resolver removal).
- **Auto-focus gap close-out.** `LiveView.qml`'s `Component.onCompleted` gains an explicit `establishFocus(firstTextBearingAnchor, 0)` call. The 2026-05-16 discipline-log entry is closed (`~~...~~ → fixed in <commit>`).
- **Orphaned-test migration.** The four tests that exercise the deleted APIs (`requestTextCaretAtNewRow_landsAtQtPos0` and the three setext / structural slots that mention them in comments) are migrated to call `establishFocus` directly, or deleted if the chokepoint coverage already subsumes them. Inventory in §6.
- **Falsifiability proof** (per invariant 4) covering the auto-focus gap close-out.

### 2.2 Explicit non-goals (deferred)

- **#10 — `LiveSelectionView` / `LiveCursorState` dual canonical stores.** This is the next-biggest seam refactor and remains tier 4c, owning its own spec, plan, and falsifiability fixture. Tier 4b deliberately does not touch `LiveSelectionView` — bundling that refactor with this one risks repeating the tier-2 overreach pattern.
- **Queue #4 — buffer-trailing-`\n` invariant.** Defer until tier-4b/c land, then pick B1 (terminator-free) per the plan body in `docs/queue.md`. Touching two seam invariants concurrently is the failure mode the post-mortems keep reproducing.
- **`structuralRowsInserted` / `structuralRowRemoved` as observability hooks.** The two signals are deleted, not deprecated. If we later need them as test observability, re-introduce them deliberately at that point — invariant 1 says don't keep dead signals around "just in case."
- **`requestTextCaretAtRow` rename.** Tempting (the name says nothing about the chokepoint), but it has 9+ live callers and renaming churns code outside the cursor-state seam. Defer to a tier-5 polish pass; tier-4b only updates the docstring.

## 3. The L4 / ownership decision (per invariant 2)

**Decision: `m_pendingFocus` is the sole pending-cursor authority. There is no second mechanism.**

- For *structural-event cursor placement* (Enter, Backspace, kind transition, click, arrow cross-block, mid-cascade re-anchor in `LiveListModelBinding::onD2Changed`), `m_pendingFocus` is canonical. Resolution gates: `delegateAvailable`, `endStructuralCascade`, and the 500ms timeout.
- For *in-block caret position during typing*, `QQuickTextEdit::cursorPosition` is canonical (tier-2 decision, unchanged).
- The row→anchor pre-resolution previously done by `requestTextCaretAtNewRow` (waiting for `rowsInserted`) is *no longer required*: every production caller already has the anchor in hand (`LiveStructuralKeyHandler` reads it from `Cmd::*` return values or `c.blockAnchor`; `LiveNavigationController` reads it from `model->recordAt(targetRow).blockAnchor`; `LiveListModelBinding`'s mid-cascade callers compute it from the `Op` they're processing).
- The anchor-keyed pre-resolution previously done by `requestTextCaretAtAnchor` (waiting for any `rowsInserted` to find the anchor at its post-shift row) is subsumed by `establishFocus`'s §7.3 fallback: when the anchor is unknown to model and delegates but present in the document, the request stays pending until `delegateAvailable` fires for it. That is the same "wait for the anchor to materialise" semantics, gated on the chokepoint's signal rather than on `rowsInserted`.

**Retirement (per invariant 3).** The retiring store is **`m_pendingRow`** (struct `PendingRow`, slot `std::optional<PendingRow> m_pendingRow`, resolvers `resolvePendingForRow` and `resolvePendingForAnchor`, slot handlers `onStructuralRowsInserted` and `onStructuralRowRemoved`, and the binding-side signals `structuralRowsInserted` / `structuralRowRemoved`). Deletion is a work-unit in this plan, not a follow-up — invariant 3 enforced by name.

## 4. Architecture

### 4.1 End-state call graph

```
structural events (Enter, Backspace, kind transition, click, arrow cross-block)
    └─→ LiveStructuralKeyHandler   ── establishFocus(anchor, qtPos)
        LiveNavigationController   ── requestTextCaretAtRow(row, qtPos)  // thin wrapper
        LiveListModelBinding mid-cascade ── establishFocus(anchor, qtPos)
        LiveView.qml startup       ── establishFocus(firstTextRow.anchor, 0)
            └─→ m_pendingFocus = { anchor, qtPos, enqueuedMs }
                ├─→ tryResolvePending() ── if not in cascade
                │       └─→ pick variant via registry; takeFocus on delegate
                ├─→ endStructuralCascade() ── after applyOps
                └─→ delegateAvailable() ── when QQuickItem registers
```

The dead branches (`m_pendingRow`, the two resolvers, the two slot handlers, the two signals) all disappear.

### 4.2 What stays

- `m_pendingFocus`, `PendingFocus`, `tryResolvePending`, `expireIfTimedOut`, `delegateAvailable`, `delegateGoingAway`, `beginStructuralCascade`, `endStructuralCascade`, `establishFocus`. The tier-1 chokepoint, unchanged.
- `m_cursor`, `cursorChanged`, `cursorKind`, `focusedAnchorRow`, `focusedQtPos`, `request`, `clear`, `syncFromTextEdit`. The tier-2 typing path, unchanged.
- `m_delegates`, `DelegateRecord`, the registration plumbing. Unchanged.
- `desiredVisualX`, `pendingVisualLineHint`, `clearDesiredVisualX`. The cross-block column-preservation state. Unchanged.
- `validateVariant`, the doc-aware variant gate (closed tier-4a). Unchanged.
- `requestTextCaretAtRow` (Q_INVOKABLE, thin wrapper over `establishFocus`) and `requestTextCaretAtRowVisualX`. Kept for navigation callers.

### 4.3 What goes

| Symbol | Location | Reason |
|---|---|---|
| `PendingRow` struct | `LiveCursorState.h:219-229` | sole consumer is `m_pendingRow` |
| `m_pendingRow` member | `LiveCursorState.h:230` | dead in production |
| `requestTextCaretAtNewRow` decl + impl | `.h:126`, `.cpp:129-141` | zero production callsites |
| `requestTextCaretAtAnchor` decl + impl | `.h:152`, `.cpp:160-176` | zero production callsites |
| `onStructuralRowsInserted` | `.h:208`, `.cpp:178-194` | sole signals it serves are deleted |
| `onStructuralRowRemoved` | `.h:209`, `.cpp:196-208` | same |
| `resolvePendingForRow` | `.h:210`, `.cpp:238-268` | sole caller is `onStructuralRowsInserted` |
| `resolvePendingForAnchor` | `.h:231`, `.cpp:210-236` | sole callers are the two slot handlers |
| `structuralRowsInserted` signal | `LiveListModelBinding.h:146`, emitter at `.cpp:648` | sole consumer is `LiveCursorState::onStructuralRowsInserted` |
| `structuralRowRemoved` signal | `LiveListModelBinding.h:150`, emitter at `.cpp:650` | same |
| `connect(binding, structuralRowsInserted, ...)` and the Removed analogue | `LiveCursorState.cpp:30-32` | sole consumer of deleted signals |

### 4.4 Auto-focus gap close-out

Production startup currently relies on `ListView.focus = true` (set in `LiveView.qml`) to give the first delegate focus. Per the 2026-05-16 discipline-log entry, this puts the *delegate root* in the focus chain but not the TextEdit child — every realistic interaction recovers via the chokepoint, but the initial moment after construction is not chokepoint-routed.

Replace the implicit `ListView.focus = true` reliance with an explicit chokepoint call:

```qml
// LiveView.qml — Component.onCompleted (extending the existing handler)
Component.onCompleted: {
    if (binding && binding.navigationController)
        binding.navigationController.setListView(root)
    // Seed initial focus through the chokepoint. Without this, ListView.focus
    // delivers focus to the delegate root but not the text-bearing TextEdit
    // descendant — the path that production papers over for every other event.
    if (binding && binding.cursorState && root.count > 0) {
        const firstAnchor = binding.model.recordAt(0).blockAnchor
        if (firstAnchor)
            binding.cursorState.establishFocus(firstAnchor, 0)
    }
}
```

Constraints:
- `recordAt(0).blockAnchor` may be a default-constructed anchor for a synthetic empty model (test fixtures load empty documents). `establishFocus`'s §7.3 drop-silent already handles this; we just early-return on the QML side to avoid a no-op log line.
- The first row may be a non-text-bearing kind (HR, Image) for documents starting with such a block. `establishFocus`'s tier-1 variant-aware path (commit `9b30d75`) handles this: the chokepoint stages `BlockSelected` rather than `TextCaret` per the registry.
- For the seven affected test fixtures that construct empty bindings (no `establishFocus` to call), the new path early-returns and behaviour is unchanged.

## 5. Components

### 5.1 `LiveCursorState` header changes

`libs/markoff-live/include/markoff/live/LiveCursorState.h`:

- Delete lines 126 (`requestTextCaretAtNewRow` decl), 142-152 (`requestTextCaretAtAnchor` block), 208-210 (the two slot handlers + `resolvePendingForRow`), 219-231 (`PendingRow` struct + `m_pendingRow` member + `resolvePendingForAnchor` decl).
- Update the class-header docblock (tier-2 §5.1 framing) to remove references to `requestTextCaretAtAnchor` and rewrite the `requestTextCaretAtRow` description as "row-keyed convenience wrapper over `establishFocus`."

### 5.2 `LiveCursorState` implementation changes

`libs/markoff-live/src/LiveCursorState.cpp`:

- Delete the slot handler + resolver bodies named in §4.3.
- Delete the two `connect(binding, structuralRows...)` lines in the ctor (`.cpp:30-32`).
- `requestTextCaretAtRow` body unchanged (it already routes through `establishFocus`). Update its comment to drop the "register row-keyed pending" language inherited from pre-tier-1.

### 5.3 `LiveListModelBinding` changes

`libs/markoff-live/include/markoff/live/LiveListModelBinding.h:146, 150` — delete the two signal declarations.
`libs/markoff-live/src/LiveListModelBinding.cpp:648, 650` — delete the two `Q_EMIT` lines and the surrounding `op.kind` switch arms iff they become empty.

### 5.4 `LiveView.qml` initial-focus seed

Edit `libs/markoff-live/qml/LiveView.qml`'s `Component.onCompleted` per §4.4. The existing `binding.navigationController.setListView(root)` call stays.

### 5.5 Test migration

Inventory of tests that touch the deleted symbols (from `grep -rn requestTextCaretAtNewRow libs/markoff-live/tests/`):

| Test slot | File | Migration |
|---|---|---|
| `requestTextCaretAtNewRow_landsAtQtPos0` | `tst_live_render_cursor.cpp:234` | Migrate to `establishFocus(anchor, 0)` after the row exists; assert the same end-state. The "pre-resolve before insert" behaviour is gone — the new test asserts the post-insert chokepoint reaches the same delegate. |
| `tst_live_render_setext_e2e.cpp:121` comment block | comment-only | Update the cited mechanism (`requestTextCaretAtAnchor` → `establishFocus`); the test itself doesn't call the deleted API. |
| `tst_live_render_structural_qml.cpp:7` comment | comment-only | Same. |
| `tst_live_render_cursor_qml.cpp:15-16` comment | comment-only | Same. |
| `QmlIntegrationFixture.cpp:318` (`requestTextCaretAtRow` invoker) | `QmlIntegrationFixture.cpp` | Unchanged — still calls `requestTextCaretAtRow`, which still exists. |
| `tst_live_render_cursor.cpp:300, 323` (signal-spy on `structuralRows*`) | `tst_live_render_cursor.cpp` | These spies confirm `LiveListModelBinding` emits the signals on row-insert / row-remove. With the signals deleted, **the spies are deleted**. The model-side ops are already covered by `LiveBlockModel`'s `rowsInserted` / `rowsRemoved` Qt signals (a `QSignalSpy` on those is the equivalent and uses the standard Qt API). |

Total: one slot rewritten, two spies deleted, four comment touch-ups.

The 2026-05-16 discipline-log entry calls out two slots that migrated *from* auto-focus *to* `requestCursor` (`enter_at_paragraph_end_migrates_focus`, `delete_at_row_end_merges_next` in `tst_live_render_qml_integration`). With §5.4's explicit seed, **those migrations remain in place** — explicit chokepoint calls are the new normal, not a workaround. The discipline-log entry is closed because the underlying gap (production-startup not chokepoint-routed) is fixed, not because the tests change back.

### 5.6 Falsifiability proof (per invariant 4)

Two proofs, both on `LiveRealisticInputHarness`:

**Proof A — pending-slot deletion is safe.** Before removing `m_pendingRow`, prove no production path depends on it by:

1. Commit: `markoff-live: stub — m_pendingRow inert (FALSIFIABILITY PROOF, REVERTS NEXT)`. Body: gate every `m_pendingRow = ...` write behind `if (false)`; gate every read on the same.
2. Run the full live-render suite plus `tst_live_render_focus_chokepoint_invariant`. **All production tests must pass.** If any fails, that test is exercising the dead path; either it has a real production analogue (in which case we have a bug, stop and investigate), or it is a test of the deleted-mechanism-itself (in which case migrate per §5.5 before deletion).
3. Commit: `Revert "markoff-live: stub — m_pendingRow inert (FALSIFIABILITY PROOF, REVERTS NEXT)"`.

**Proof B — initial-focus seed is necessary and load-bearing.** Add a new invariant test slot to `tst_live_render_focus_chokepoint_invariant` (or wherever the chokepoint slots live):

`initial_focus_lands_on_textedit_not_delegate_root` — after `LiveView.qml` finishes `Component.onCompleted` and the first paint cycle, the focused QQuickItem must be the TextEdit descendant of the first text-bearing delegate, not the delegate root. Assertion via `QTRY_COMPARE` with the standard 5s timeout.

Falsifiability:
1. Commit: `markoff-live: stub — Component.onCompleted skips establishFocus (FALSIFIABILITY PROOF, REVERTS NEXT)`. Body: comment out the new establishFocus block in `LiveView.qml`.
2. Run the new slot. **Must fail.** If it passes, `ListView.focus = true` is reaching the TextEdit somehow (e.g. in test environment but not production); investigate the difference before declaring the test load-bearing.
3. Commit: `Revert "..."`.

Pattern follows `0aef9f3` / `6b32482` / the tier-2 §5.4 prescription.

## 6. Data flow

No new data paths. Tier 4b deletes paths and seeds one new chokepoint call at startup.

## 7. Testing — supporting work

- The two signal-spy tests in `tst_live_render_cursor.cpp:300, 323` are replaced by spies on `LiveBlockModel`'s `rowsInserted` / `rowsRemoved` (standard Qt API on the model). If the original intent was to verify "the binding fires its custom signal in addition to the model's Qt signal," that intent is retired with the signal — the binding no longer has anything custom to fire.
- The chokepoint invariant suite (`tst_live_render_focus_chokepoint_invariant`) is the protective fixture for tier-4b. Adding one slot (`initial_focus_lands_on_textedit_not_delegate_root`) brings it to whatever the current count + 1 is.
- The four comment-only test touch-ups are documentation hygiene; they are not validated by CI but the spec demands them for invariant 8 trail-keeping.

## 8. Definition of done

- [ ] `requestTextCaretAtNewRow` decl + impl deleted; `git grep requestTextCaretAtNewRow` returns zero hits.
- [ ] `requestTextCaretAtAnchor` decl + impl deleted; `git grep requestTextCaretAtAnchor` returns zero hits.
- [ ] `PendingRow`, `m_pendingRow`, `resolvePendingForRow`, `resolvePendingForAnchor`, `onStructuralRowsInserted`, `onStructuralRowRemoved` deleted; `git grep -E "PendingRow|m_pendingRow|resolvePendingFor|onStructuralRow"` returns zero hits.
- [ ] `LiveListModelBinding::structuralRowsInserted` / `structuralRowRemoved` signals deleted; `git grep -E "structuralRowsInserted|structuralRowRemoved"` returns zero hits.
- [ ] `LiveView.qml` `Component.onCompleted` calls `establishFocus` on the first text-bearing row (or early-returns for empty models).
- [ ] `requestTextCaretAtNewRow_landsAtQtPos0` test migrated to exercise `establishFocus`.
- [ ] Two `QSignalSpy` blocks in `tst_live_render_cursor.cpp` migrated to spy on `LiveBlockModel::rowsInserted` / `rowsRemoved`, or deleted if redundant after migration.
- [ ] Four comment-only test files updated to cite `establishFocus` instead of the deleted APIs.
- [ ] `tst_live_render_focus_chokepoint_invariant::initial_focus_lands_on_textedit_not_delegate_root` added and passing.
- [ ] Falsifiability Proof A committed and reverted; all production tests pass under the stub.
- [ ] Falsifiability Proof B committed and reverted; the new slot demonstrably fails under the stub.
- [ ] Discipline-log entry at `docs/queue.md` for the 2026-05-16 auto-focus gap closed: `~~...~~ → fixed in <commit>`.
- [ ] Tier-1 chokepoint suite and the full live-render suite show no new failures vs. the baseline at start-of-tier-4b.
- [ ] No new `Qt.callLater` or re-entrance guards introduced. Verify by grep against the seam guidance.
- [ ] `docs/queue.md` §#2 banner updated: concerns #3 + #4 closed; only #10 remains. Tier-4c framing added.
- [ ] `docs/e-arc/e-arc-status.md` recent-changes log entry filed.

## 9. Future work — tier 4c, tier 5

- **Tier 4c — `LiveSelectionView` / `LiveCursorState` unification** (concern #10). The remaining "dual canonical stores" in the seam. Spec-and-plan caliber on its own; estimated 3-5 days; mandatory falsifiability fixture; will need a fresh dogfood gate. Do not begin until tier-4b lands and the chokepoint invariant suite is green.
- **Tier 5 — polish.** Rename `requestTextCaretAtRow` if a better name surfaces (candidates: `requestFocusAtRow`, `establishFocusAtRow`, `focusRow`); collapse `requestTextCaretAtRow` + `requestTextCaretAtRowVisualX` into one signature with an optional `VisualLineHint` parameter; consider whether `m_pendingFocus`'s 500ms timeout should be configurable for slow CI.
- **Queue #4 — buffer-trailing-`\n` invariant.** Pick B1 (terminator-free) per the plan body in `docs/queue.md`. Do not begin until tier-4c lands; two seam invariants moving in parallel is the failure mode the post-mortems keep reproducing.

Discipline rule (from tier-1 spec §10, tier-2 §9, unchanged): interactive dogfood between tiers, tag held pending sign-off. Tier 4b's dogfood gate is light because there are no production-behavior changes outside the initial-focus seed; the gate is *"the user opens a document and the first text-bearing row has caret focus immediately, without clicking,"* plus the new invariant slot passing.

## 10. Citations

- `docs/INVARIANTS.md` — invariants **1, 2, 3, 4, 5, 8** enforced here. #3 is the headline invariant: tier-4b retires the old store (`m_pendingRow`) in the same plan that confirms `m_pendingFocus` is sole authority.
- `docs/specs/2026-05-11-focus-chokepoint-design.md` — tier-1 spec; §5.1 documents `establishFocus` + `m_pendingFocus`; §10 framed tier-4 as "API consolidation" pre-discovery that the consolidation already happened.
- `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` — tier-2 spec; §3 documents the L4 typing/structural authority split that this spec inherits.
- `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md` — tier-3 spec; §5 (`delegateClass`) and §6 (kind-transition flow) reshaped the structural-event volume that flows through `establishFocus` — context for why the chokepoint volume grew without anyone noticing.
- Commit `033292e` — tier-4a; `requestTextCaretAtRow` routed through `flushPendingDocumentChanges` + `establishFocus`. This was the de facto consolidation; tier-4b admits it in writing and retires the alternative.
- Commit `d6818f5` — tier-4a; `validateVariant` doc-aware + null-safe; the `tryResolvePending` bypass removed. Tier-4b inherits this surface.
- Commit `9b30d75` — variant-aware chokepoint; staged variant chosen per `BlockKindRegistry.supportedCursorVariants`. The reason tier-4b's §4.4 initial-focus seed is safe for first-row HR/Image documents.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` — developmental record (per invariant 1). §A.7 erratum + §A.8 (pending-slot history, if present) cited as provenance for `m_pendingRow`.
- `docs/queue.md` §#2 — twelve concerns; this spec resolves **#3** and **#4** in full and closes the 2026-05-16 discipline-log entry. Remaining: **#10** → tier 4c.
- 2026-05-16 discipline-log entry at `docs/queue.md` (auto-focus gap, `UnifiedInlineTextDelegate.qml`) — closed by §4.4 + §5.4.

## 11. Open questions deferred to the plan

- **First text-bearing row selection.** §4.4's QML pseudocode uses `recordAt(0)`. If the document starts with an HR or Image (block-only kind), the chokepoint stages `BlockSelected` — correct, but does the user expect *caret* focus on the next text-bearing row instead? Default for the plan: stage what the registry says, accept that HR-led documents start in BlockSelected (matches the click-on-HR case landed in `9b30d75`). Reconsider if dogfood disagrees.
- **CMake / test target wiring for `initial_focus_lands_on_textedit_not_delegate_root`.** Likely added to `tst_live_render_focus_chokepoint_invariant`. Follow the precedent in the existing CMakeLists.
- **`requestTextCaretAtRow` docstring rewrite scope.** Tier-4b minimally corrects "row-keyed pending" language. Does the corrected docstring still claim "deterministic-pending variant"? Default: no — name it as "row-keyed convenience over `establishFocus`."
- **Should the deleted signals be re-emitted as `QSignalSpy`-friendly internal observability?** Default: no (invariant 1: don't keep dead signals "just in case"). If a test really wants to verify "the binding processed a structural Op," it can spy on `LiveBlockModel::rowsInserted` / `rowsRemoved` directly.
- **Plan-to-spec deviation budget.** Tier-2 and tier-3 each held to "spec is the truth; plan executes the spec." Tier-4b's plan should not change the L4 decision or the retirement choice; if the falsifiability proofs surface a reason to, write a spec amendment first (per invariant 3).
