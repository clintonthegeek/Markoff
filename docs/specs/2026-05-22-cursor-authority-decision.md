# Cursor authority — decision and refactor

**Status:** spec-approved (pending implementation)
**Date:** 2026-05-22
**Trigger:** dogfood report of ~20-block data loss on Delete + Enter at a paragraph adjacent to a table (kddw-evaluation-comparison.md, 167 → 146 blocks).
**Related invariants:** docs/INVARIANTS.md #2 (L4 authority decided in writing), #3 (no dual authority), #7 (re-entrance guards are smells).
**Cited prior art:**
- `docs/2026-05-02-live-view-architectural-audit.md` — the original framing of "bidirectional gossip protocol over multiple sources of truth, hidden behind unidirectional-data-flow language."
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` — pipeline-feature provenance.
- `docs/queue.md` §#2 — the twelve cursor-architecture concerns the Tier 2 attempt surfaced.

---

## 0. What happened

User loaded a 167-block markdown document containing a paragraph followed by a table followed by lists, headings, and more paragraphs. User clicked at the end of the paragraph (one position before the table block). User pressed Delete, expecting either a no-op (block-only-fence per spec §4 — not yet implemented for Table) or a merge with the next block. User pressed Enter, expecting a block split.

Result: 20 contiguous blocks vanished — the table, the paragraph after it, two list items, three headings, three paragraphs, two more list items, and so on. The document went from 167 to 146 blocks. Saved to disk via Ctrl+S (after a separate save-path fix landed), the file's missing content was permanently gone.

The user had not drag-selected, had not copied, had not pressed any other keys, and the entire interaction consisted of one click + Delete + Enter.

## 1. The trace evidence

Instrumented build, fresh launch, no prior interaction:

```
[bug-trace] syncFromTextEdit row=0 qtPos=56 anchorWasSet=false …  ⎫
[bug-trace] syncFromTextEdit row=1 qtPos=183 anchorWasSet=false…  ⎪
…                                                                  ⎬ load-time: each visible
[bug-trace] syncFromTextEdit row=23 qtPos=113 anchorWasSet=false   ⎪ delegate's TextEdit fires
[bug-trace] syncFromTextEdit row=24 qtPos=46 anchorWasSet=false …  ⎭ cursorPositionChanged once

[dogfood] CursorState: request TextCaret(innerRow=0, qtPos=0)         ← initial-focus seed
[bug-trace] syncFromTextEdit row=0 qtPos=0 anchorWasSet=false

[bug-trace] begin row=6 qtPos=73                                       ⎫
[bug-trace] syncFromTextEdit row=6 qtPos=73 anchorWasSet=false         ⎬ user clicks at end of
[bug-trace] setSelectionAnchor block.row=6 qtPos=73 wasSet=false       ⎪ "PlanStan has 6…" line
[bug-trace] syncSelectionToSession ENTRY anchorRow=6 anchorQtPos=73    ⎭ — anchor set at click point

[bug-trace] onSessionPrimarySelectionChanged ENTRY                     ⎫ session round-trip
[bug-trace] begin row=6 qtPos=73                                       ⎬ redundant but idempotent
[bug-trace] syncFromTextEdit row=6 qtPos=73 anchorWasSet=true …        ⎭

qml: collapseSelectionIfMutating ENTER key=Delete hasSelection=false  ← guard passes (anchor==active)
[bug-trace] tryHandle key=Delete blockIndex=6 qtPos=73 kind=paragraph
[bug-trace] deleteMerge cur=…654 next=…655 totalBlocksBefore=167       ⎫ paragraph+table merge.
[bug-trace] d2RemoveBlock block=…655 formerRow=7                       ⎭ Expected.

⮕  [bug-trace] syncFromTextEdit row=24 qtPos=86 anchorWasSet=true anchorRow=6   ⎫
⮕  [bug-trace] syncFromTextEdit row=25 qtPos=83 anchorWasSet=true anchorRow=6   ⎬ THE BUG
⮕  [bug-trace] syncFromTextEdit row=26 qtPos=42 anchorWasSet=true anchorRow=6   ⎭

qml: collapseSelectionIfMutating ENTER key=Return hasSelection=true anchorBlock=6 activeBlock=26
qml: collapseSelectionIfMutating WILL CALL deleteSelection
[bug-trace] deleteSelectionRange ENTRY hasSelection=true anchorBlock=6 anchorQtPos=73 activeBlock=26 activeQtPos=42
[bug-trace] applyFlatEdit oldStart=474 oldEnd=2441 removedBytes=1967 addedBytes=0
[bug-trace] d2RemoveBlock block=…656 formerRow=7   ⎫
…                                                  ⎬ 20 consecutive block removals
[bug-trace] d2RemoveBlock block=…675 formerRow=7   ⎭
```

The Delete merge ran cleanly. **The bug is the three spurious `syncFromTextEdit` calls between the merge and the Enter**, each from a non-focused delegate, each clobbering `m_cursor` onto a row the user did not navigate to.

## 2. Mechanism

After `deleteMerge` removes the table:

1. `MarkoffDocument` emits `d2DocumentChanged` (debounced; flushed synchronously by `paragraphDelete` via `flushPendingD2Changed`).
2. `LiveListModelBinding::onD2Changed` runs `applyOps` against `LiveBlockModel`. The table row is removed; every subsequent row's `modelIndex` shifts down by one.
3. ListView recycles delegates. Each visible delegate's `model.text` binding now resolves to a *different* block's content (row 25's delegate now displays row 24's text, etc.).
4. `LiveEditBinding::text` Q_PROPERTY re-evaluates. `pushTextToDocument` runs `m_listenedDoc->setPlainText(m_text)` to install the new content.
5. `QQuickTextDocument::setPlainText` resets the inner `QQuickTextEdit`'s `cursorPosition` to end-of-text. `cursorPositionChanged` fires synchronously.
6. The delegate's `onCursorPositionChanged` handler runs. It checks the existing guard:
   ```js
   if (editBinding.isApplyingTextUpdate()
           && cs.focusedAnchorRow === root.modelIndex) {
       // …suppress + force-correct cursor…
       return
   }
   cs.syncFromTextEdit(model.blockAnchor, edit.cursorPosition)
   ```
   For **non-focused** rebound delegates (24, 25, 26 in the trace), `focusedAnchorRow=6 !== modelIndex` → guard fails → `syncFromTextEdit` fires.
7. `LiveCursorState::syncFromTextEdit` accepts the cross-block change. `m_cursor` becomes `TextCaret(row 26, qtPos 42)` — wherever the last delegate to settle happened to be.
8. `m_selectionAnchor` still sits at `(row 6, qtPos 73)` from the user's click. Nothing cleared it.

When Enter fires, `KeyDispatch.collapseSelectionIfMutating` correctly observes `hasSelection() == true` (anchor at row 6, active at row 26, different blocks) and routes Enter through `LiveCursorState::deleteSelection()`, which deletes the byte range [474, 2441) — everything between the stale anchor and the spurious active end.

## 3. The architectural flaw

`LiveCursorState::syncFromTextEdit(anchor, qtPos)` accepts cursor updates from any delegate, treating delegate `cursorPosition` as an authoritative report of user intent. But delegate cursor positions change for non-user reasons:

| Trigger | Source | User intent? |
|---|---|---|
| TextEdit native arrow / typing in focused TextEdit | user keyboard | yes |
| Mouse click (via `begin(row, qtPos)`) | user mouse | yes |
| Cross-block keyboard nav (via `establishFocus`) | user keyboard | yes |
| **`setPlainText` cursor reset after `pushTextToDocument`** | **binding refresh** | **no** |
| **ListView delegate rebound to a different block's text** | **model rebuild** | **no** |
| `applySelection`'s `moveCursorSelection` (selection rendering) | view-state refresh | no |
| Initial `Component.onCompleted` cursor placement | load-time view-init | no |

The chokepoint cannot tell these apart at the `syncFromTextEdit` API surface. It treats every report as authoritative and writes `m_cursor`.

This is the **bidirectional gossip protocol over multiple sources of truth, hidden behind unidirectional-data-flow language** named by `docs/2026-05-02-live-view-architectural-audit.md`. Two authorities for "where is the cursor":

1. `LiveCursorState::m_cursor` — the chokepoint store.
2. Each delegate's `QQuickTextEdit::cursorPosition` — the per-delegate view-state store.

Both are mutated by both directions:
- Model → view: `tryResolvePending` → `takeFocus(qtPos)` → `cellEdit.cursorPosition = qtPos`.
- View → model: `cursorPositionChanged` → `onCursorPositionChanged` → `syncFromTextEdit(anchor, qtPos)` → `m_cursor` updated.

The reconciliation has accumulated three re-entrance guards (invariant 7 smells), each papering over a class of unwanted echo:

- `m_applyingTextUpdate` (LiveEditBinding) — suppress contentsChange echo during pushTextToDocument.
- `m_applyingSelectionEmit` (LiveCursorState) — suppress applySelection's cursorPosition writes during selection rendering.
- `isApplyingSelection()` Q_INVOKABLE (LiveCursorState) — same, exposed for QML cell handlers.

The current bug is the residue: the `m_applyingTextUpdate` guard is checked in `UnifiedInlineTextDelegate.qml`'s `onCursorPositionChanged`, but **only with a `focusedAnchorRow === modelIndex` conjunction**. The author thought non-focused delegates would never be in `pushTextToDocument` at the same time as the focused row — but they are, because **every visible delegate's text binding re-evaluates after a structural edit shifts row indices**.

## 4. The decision

**L3 cursor authority: the chokepoint (`LiveCursorState::m_cursor`) is canonical. Delegates render from it. Delegate cursor changes are events sent to the chokepoint **only on user input**.**

This is the explicit decision invariant 2 says must be written down before the seam is touched. It was always implicit — most of the code is structured this way — but the `syncFromTextEdit` API surface treats delegates as a second authority, and that is the load-bearing exception that produces the bug.

## 5. Refactor

### 5.1 `LiveCursorState::syncFromTextEdit` becomes within-block-only

```cpp
void LiveCursorState::syncFromTextEdit(BlockAnchor anchor, int qtPos)
{
    // Contract: the focused delegate's TextEdit caret moved within
    // itself. Same-block updates only. Cross-block moves are
    // categorically echoes of non-user events (binding refreshes,
    // ListView recycling, setPlainText after model rebuild) and must
    // be dropped here. Cross-block moves go through request() /
    // begin() / establishFocus() / requestTextCaretAtRow() — paths
    // the chokepoint initiates.
    auto curCaret = currentTextCaret();
    if (!curCaret) return;                           // no focused block
    if (curCaret->block != anchor) return;           // not the focused block
    if (curCaret->cachedQtPos == quint32(qtPos)) return;  // no change
    TextCaret tc;
    tc.block       = anchor;
    tc.cachedQtPos = quint32(qtPos);
    request(tc);                                     // route through validator
}
```

This is the single load-bearing change. The chokepoint enforces the L3 decision at its own boundary — non-focused-delegate syncs are rejected by the chokepoint regardless of QML-side discipline.

### 5.2 Call-site updates: `begin()` and `extend()` no longer use `syncFromTextEdit`

`begin()` and `extend()` legitimately move the cursor across blocks (mouse click on a different block, Shift+click extending across blocks). They currently go through `syncFromTextEdit` and would now be rejected. They must use `request()` directly:

```cpp
void LiveCursorState::begin(int blockIndex, int qtPos)
{
    const auto anchor = blockAnchorAt(blockIndex);
    if (anchor.isNull()) return;
    TextCaret tc;
    tc.block       = anchor;
    tc.cachedQtPos = quint32(qtPos);
    request(tc);                                     // cross-block move via chokepoint
    setSelectionAnchor({anchor, quint32(qtPos)});
    syncSelectionToSession();
}

void LiveCursorState::extend(int blockIndex, int qtPos)
{
    const auto anchor = blockAnchorAt(blockIndex);
    if (anchor.isNull()) return;
    TextCaret tc;
    tc.block       = anchor;
    tc.cachedQtPos = quint32(qtPos);
    request(tc);                                     // cross-block move via chokepoint
    syncSelectionToSession();
    emitSelectionChanged();
}
```

The semantic doesn't change. Only the wiring does.

### 5.3 Delegate-side QML cleanup: drop the focused-row conjunction

In `UnifiedInlineTextDelegate.qml`'s `onCursorPositionChanged`:

```js
onCursorPositionChanged: {
    const cs = root.liveBinding ? root.liveBinding.cursorState : null
    if (cs && cs.isApplyingSelection()) return    // applySelection rendering, not user
    if (!cs || model.blockAnchor === undefined) return

    // Focused-row cursor correction: when pushTextToDocument refreshes our
    // text, setPlainText resets cursorPosition to end. Restore from m_cursor.
    // This branch handles the focused row's own setPlainText echo.
    if (editBinding.isApplyingTextUpdate()
            && cs.focusedAnchorRow === root.modelIndex) {
        if (cs.focusedQtPos >= 0 && cs.focusedQtPos <= edit.length
                && edit.cursorPosition !== cs.focusedQtPos) {
            edit.cursorPosition = cs.focusedQtPos
        }
        return
    }

    // Defense in depth: drop non-focused-delegate reports.
    // The C++ chokepoint also rejects these, but stopping them here
    // saves a meta-object-call round trip and keeps the load-time
    // mass-fire from polluting the chokepoint's debug logs.
    if (!edit.activeFocus) return

    cs.syncFromTextEdit(model.blockAnchor, edit.cursorPosition)
}
```

Apply the same pattern to:
- `CodeBlockDelegate.qml`'s `edit.onCursorPositionChanged`
- `MathDelegate.qml`'s `latexEdit.onCursorPositionChanged` (if applicable)
- `TableDelegate.qml`'s cell `onCursorPositionChanged` (already gates on `cellEdit.activeFocus` indirectly via the memo capture; needs explicit early-return for `syncFromTextEdit`)

### 5.4 Selection anchor lifecycle (corollary)

The trace also shows that **the selection anchor persists across operations that don't deserve to leave one**. A user click sets `m_selectionAnchor` at the click position. Subsequent navigation away from that block via the chokepoint (`establishFocus`, `requestTextCaretAtRow`) doesn't clear it. This is the "phantom anchor" failure mode the `KeyDispatch.js:107` comment already names — and the cross-block guard it added only handles the narrow within-block case.

Once §5.1 is in place, the immediate data-loss path is closed. But a stale anchor + a legitimate cross-block keyboard nav (e.g., a user clicks at block 6, then arrow-Down to block 7, then Delete) still produces `hasSelection() == true` with a phantom range.

**Corollary fix:** `request()` clears the selection anchor when the new cursor's block differs from the anchor's block, UNLESS the caller is `extend()` (Shift-arrow / drag). Concretely:

```cpp
void LiveCursorState::request(const Cursor &newCursor, bool preserveSelectionAnchor)
{
    // … existing body …
    if (!preserveSelectionAnchor && m_selectionAnchor) {
        // A move that didn't come from extend() abandons any prior
        // selection anchor as soon as it crosses a block boundary.
        // Within-block moves preserve the anchor (selection within
        // the focused block is legitimate; phantom-anchor within the
        // focused block is already guarded by KeyDispatch.js).
        auto newTc = std::get_if<TextCaret>(&newCursor);
        if (newTc && newTc->block != m_selectionAnchor->block) {
            clearSelectionAnchor();
        }
    }
    // …
}
```

`extend()` calls `request(tc, /*preserveSelectionAnchor=*/true)`.

This closes the latent phantom-anchor surfaces that don't depend on the §5.1 bug for their trigger.

### 5.5 Retire the cross-block guard in `KeyDispatch.js`

With §5.4 in place, `m_selectionAnchor` is always cleared when the cursor crosses blocks via a non-extend path. `hasSelection()` will not return true for phantom-anchor cases. The cross-block guard in `KeyDispatch.collapseSelectionIfMutating` (line 116-119) becomes redundant and can be removed:

```js
function collapseSelectionIfMutating(event, ctx) {
    const binding = ctx.binding
    if (!binding) return { handled: false, accepted: false }
    const cs = binding.cursorState
    if (!cs || !cs.hasSelection || !cs.hasSelection())
        return { handled: false, accepted: false }
    // No cross-block guard needed — anchor lifecycle is now correct.
    // … rest of the function …
}
```

The within-block-typing-after-click case the comment was protecting against (`begin()` sets anchor + cursor; subsequent typing advances cursor leaving anchor stale within same block, qtPos different) is handled by `hasSelection()`'s own logic: anchor and active in same block but different qtPos. **This is actually a real intra-block selection** (click-then-type-elsewhere = drag-select in some editors). For our editor it's a phantom because we don't track explicit drag-start. The corollary in §5.4 should also clear anchor on within-block non-extend moves, OR `hasSelection()` should require the active end to be in a different block-or-position from where the anchor was set AND `extend()` was called at least once.

The simpler resolution: **`begin()` sets anchor; `extend()` is the only thing that establishes the selection has been extended.** Track a `m_selectionExtended` bool, set true in `extend()`, false in `setSelectionAnchor()` and `clearSelectionAnchor()`. `hasSelection()` requires `m_selectionExtended == true`. Then begin-then-type-elsewhere doesn't register as a selection unless the user actually shift-arrowed or dragged.

This makes the four selection-state pieces concrete:

| State | Meaning |
|---|---|
| `m_selectionAnchor.empty()` | no selection at all |
| `m_selectionAnchor.set() && !m_selectionExtended` | anchor placed by click; never extended; not a selection (yet) |
| `m_selectionAnchor.set() && m_selectionExtended && anchor==active` | extension dragged back to anchor; collapsed but anchor preserved |
| `m_selectionAnchor.set() && m_selectionExtended && anchor!=active` | real selection |

`hasSelection()` returns true only for the last row.

### 5.6 The 2026-05-21 within-block re-entrance guards stay where they are

The `m_applyingTextUpdate` flag in `LiveEditBinding` and the `isApplyingSelection()` Q_INVOKABLE in `LiveCursorState` remain as-is for now. They suppress different echo classes (pushTextToDocument and applySelection respectively); §5.1–5.5 don't supersede them. Their retirement is a separate refactor (the edit-pipeline echo-suppression spec referenced at LiveEditBinding.cpp:75 — still TBW).

Per invariant 7, this is a documented acceptance: the guards remain documented smells whose retirement is tracked, not pretended-away.

## 6. Falsifiability + regression tests

Per invariant 4, the regression tests come **before** the production change. Each is built on `QmlIntegrationFixture` + `LiveRealisticInputHarness` so it exercises the production callsite (invariant 5).

### 6.1 `tst_live_cursor_no_crossblock_sync_from_nonfocused_delegate`

Fixture: doc with paragraph + table + several blocks after. Place caret at end of the paragraph (row N, qtPos=length). Trigger a structural edit that shifts subsequent row indices (e.g., `doc.applyFlatEdit` removing a block). After settle: assert `cursorState.focusedAnchorRow == N` (unchanged); assert `cursorState.focusedQtPos == length` (unchanged). Falsifiability proof: stub `syncFromTextEdit` to accept cross-block updates → assertion fails.

### 6.2 `tst_live_cursor_anchor_clears_on_crossblock_nav`

Click at row M, qtPos K (sets anchor). `cursorState.requestTextCaretAtRow(N, 0)` where N != M. Assert `m_selectionAnchor` is now empty (corollary §5.4). Falsifiability proof: remove the anchor-clear in §5.4 → assertion fails.

### 6.3 `tst_live_render_dataloss_chain_repro`

End-to-end regression for the original bug: load the kddw fixture (or a synthesized equivalent), place caret at end of "PlanStan has 6…", press Delete, press Enter, assert block count is `original - 1` (not `original - 21`). Falsifiability proof: revert §5.1 → assertion fails.

### 6.4 `tst_live_render_begin_extend_unchanged`

`begin()` and `extend()` should still work for legitimate selection cases. Drag-selection across blocks via `begin(M, K) + extend(N, L)` produces `hasSelection() == true` and `deleteSelection()` deletes the legitimate range. This is already covered by `tst_live_render_cross_block_drag_selection_qml` (2026-05-21); confirm it still passes after §5.2.

## 7. Blast radius

**Files changed:**

- `libs/markoff-live/include/markoff/live/LiveCursorState.h` — `request()` signature gains `preserveSelectionAnchor` default-arg
- `libs/markoff-live/src/LiveCursorState.cpp` — §5.1 (syncFromTextEdit), §5.2 (begin/extend), §5.4 (request body)
- `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` — §5.3
- `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` — §5.3
- `libs/markoff-live/qml/delegates/MathDelegate.qml` — §5.3 (if applicable)
- `libs/markoff-live/qml/delegates/TableDelegate.qml` — §5.3 (cell `onCursorPositionChanged`)
- `libs/markoff-live/qml/delegates/KeyDispatch.js` — §5.5 (drop cross-block guard, add `selectionExtended` check)
- `libs/markoff-live/include/markoff/live/LiveCursorState.h` — `m_selectionExtended` member + Q_PROPERTY for QML
- `libs/markoff-live/tests/` — new test binaries §6.1–6.3

**Tests at risk of needing updates:**

- `tst_live_render_cross_block_drag_selection_qml` — extension semantics unchanged; should still pass
- `tst_live_render_cursor_qml` and other cursor companions — within-block sync behavior unchanged; should pass
- Any test calling `syncFromTextEdit` directly with a cross-block anchor — those tests were calling a now-rejected API path; if they exist, they were testing the bug, not the contract, and should be rewritten

## 8. Out of scope (followups)

- **Retiring `m_applyingTextUpdate`** in LiveEditBinding (invariant 7) — separate edit-pipeline echo-suppression spec, TBW.
- **One-way data flow refactor** — pushTextToDocument's setPlainText could be replaced with a path that doesn't fire `contentsChange` for non-user writes. Larger structural change; tracked as a future invariant-7 retirement.
- **Block-only-for-Table** — the original dogfood report's first bug ("Backspace at start of paragraph after table deletes the paragraph") was supposed to be Plan §E. The data-loss path documented here is an INDEPENDENT bug — closing this spec doesn't close §E. Both must land.

## 9. Implementation order

1. Land §6.1 (first regression test) — verify it fails on current code.
2. Land §6.2 (anchor-clear test) — verify it fails on current code.
3. Land §6.3 (end-to-end test) — verify it fails on current code.
4. Implement §5.1 + §5.2 (syncFromTextEdit + begin/extend) — §6.1 + §6.3 turn green.
5. Implement §5.4 (anchor-clear on cross-block request) — §6.2 turns green.
6. Implement §5.5 (KeyDispatch cleanup) — §6.4 still passes.
7. Implement §5.3 (QML delegate cleanup) — defense-in-depth; tests still green.
8. Add Discipline-Log entry for the bug at landing (closes the loop on invariant 8).

Each step lands as its own commit with the falsifiability stub + revert pair where applicable.
