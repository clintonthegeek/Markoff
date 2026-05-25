# Tier 4c — Selection/cursor unification

**Date:** 2026-05-16
**Branch:** `exploration/new-foundation`
**Predecessors:**
- `docs/specs/2026-05-11-focus-chokepoint-design.md` (tier 1; chokepoint, `m_pendingFocus`)
- `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` (tier 2; typing/structural authority split)
- `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md` (tier 3; UnifiedInlineTextDelegate)
- Tier 4a (partial, commits `bb4fd45`, `033292e`, `d6818f5`) — queue #2 concerns #5, #9, #12
- `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` (tier 4b; queue #2 concerns #3, #4 closed)

**Plan to follow:** `docs/plans/2026-05-16-tier-4c-selection-cursor-unification.md` (to be written).

## 0. One-paragraph summary

Tier 4c is the **last remaining queue #2 concern** (#10): `LiveSelectionView` and `LiveCursorState` are two independent canonical stores for what is, semantically, the same thing — *where the user is pointing in the document*. The cursor store owns `m_cursor` (variant: `NoCursor | TextCaret | BlockSelected | BlockInternalEdit`) keyed by `BlockAnchor`; the selection store owns four scalar fields `(anchorBlock, anchorQtPos, activeBlock, activeQtPos)` keyed by inner-row index. They have separate sync paths to `Session::primarySelectionChanged`, each with its own reconciliation logic and (in selection's case) a re-entrance guard `m_applyingSessionSelection` that the seam guidance flags as a smell (invariant 7). Tier 4c **retires the canonical state in `LiveSelectionView`** by moving the selection anchor into `LiveCursorState` as a new optional companion to `m_cursor`. `LiveSelectionView` becomes a thin Q_OBJECT facade that preserves its QML-exposed API (`begin/extend/clear/rangeForBlock/copyToClipboard/selectAll/deleteSelection/hasSelection`) but holds **no state of its own** — every method delegates to `LiveCursorState`. The session-bridge slots (`syncToSession`, `onSessionPrimarySelectionChanged`) and the re-entrance guard move with the state. Single canonical store, single identity system (BlockAnchor, not inner-row), single bridge to Session, one fewer re-entrance guard in the seam.

## 1. Motivation

Every prior post-mortem on this branch named the same pattern: **a new authority over user-position state is added without retiring the old one; cycle guards multiply; focus/caret loss results.** (`docs/INVARIANTS.md` opening section; the rule that follows is invariant 3.)

Tier 1 introduced `establishFocus`/`m_pendingFocus` without retiring `m_pendingRow` — tier 4b finally retired it (after the de facto consolidation had already happened). The selection-side equivalent is now the last open instance of the pattern in this codebase: **`LiveSelectionView` and `LiveCursorState` model the active end of selection (i.e., the caret) in two stores simultaneously**, with separate sync paths to `Session`, separate identity systems (row-index vs. BlockAnchor), and separate cycle guards (`m_applyingSessionSelection` on the selection side; the `establishFocus` / `syncFromTextEdit` discipline on the cursor side).

Concrete consequences in production code today:

- **Two paths can disagree.** Clicking a paragraph: `LiveNavigationController` calls `cursorState->requestTextCaretAtRow(r, 0)` AND `selectionView->begin(r, 0)`. Two writes. If a structural cascade fires between them, they can resolve to different rows.
- **Two identity systems.** `LiveCursorState` keys by `BlockAnchor` (stable across structural edits); `LiveSelectionView` keys by inner-row index (shifts on every insert/remove). Selection survives row shifts only because `onSessionPrimarySelectionChanged` re-resolves indices from `Session::primarySelectionChanged` events on every applyOps cycle. This works, but the model is "anchor-canonical via round-trip-through-Session" rather than "anchor-canonical directly."
- **Re-entrance guard.** `m_applyingSessionSelection` (`LiveSelectionView.cpp:179, 252-254`) prevents `syncToSession` from echoing back through `onSessionPrimarySelectionChanged`. Invariant 7 flags this as a smell: *"'I gave up on understanding the timing and brute-forced it.' When adding: justify in the commit. When seeing: log it."* Tier 4c removes the smell along with the duplication.
- **Tier-2 critique #10 names this directly.** From `docs/queue.md` § Concern #10: *"Selection state (`LiveSelectionView`) and cursor state (`LiveCursorState`) are independent canonical stores with their own sync paths; they overlap in concept."*

The block-only kinds work (`docs/specs/2026-05-13-block-only-kinds-design.md`) and the focus chokepoint (tier 1) both quietly accommodated this dual-store reality by routing through `LiveCursorState` only, leaving `LiveSelectionView` as a sibling that has to be kept in sync via `LiveNavigationController` updates and the Session round-trip. Tier 4c finishes the consolidation.

## 2. Scope and explicit non-goals

### 2.1 In scope

- **#10 (full).** Retire `LiveSelectionView`'s canonical state. `LiveCursorState` becomes the sole canonical store for both the cursor (active end) and the optional selection anchor.
- **Preserve `LiveSelectionView`'s public surface.** All Q_INVOKABLE methods (`begin`, `extend`, `clear`, `selectAll`, `deleteSelection`, `rangeForBlock`, `copyToClipboard`, `anchorBlock`, `anchorQtPos`, `activeBlock`, `activeQtPos`, `hasSelection`) keep their signatures and observable semantics. The class becomes a stateless facade over `LiveCursorState`. QML consumers (5 callsites in `LiveView.qml`; several in delegates) do not change.
- **Move the Session bridge.** `syncToSession` and `onSessionPrimarySelectionChanged` migrate from `LiveSelectionView` to `LiveCursorState`. The selection view's `setSession`/`setDocument`/`setModel` become forwarders to `LiveCursorState`'s equivalents (or are retired entirely if those wiring points are already attached).
- **Retire the `m_applyingSessionSelection` re-entrance guard.** Replaced by an equality short-circuit on `onSessionPrimarySelectionChanged` (the guard's intent — "don't echo my own write back" — is achieved by "if the incoming selection equals my current selection, return early"). Invariant 7 cleared on this one site.
- **Single identity system: `BlockAnchor`.** The selection anchor stored in `LiveCursorState` is `optional<SelectionAnchor>` where `SelectionAnchor = {BlockAnchor block; quint32 qtPos;}`. Row indices become derived (resolved on demand via the model walk that `rowForBlock` already does). Selection naturally survives structural edits without a round-trip through `Session`.
- **Falsifiability invariant test** (per invariant 4) on `LiveRealisticInputHarness` covering the cross-block Shift+arrow + click sequence that surfaced the original re-entrance pain (queue.md Bug 3 lineage / `docs/handoff/2026-05-09-setext-dogfood-findings.md` D6 / dogfood-pass-2 follow-up).
- **Dogfood gate.** Spec §9 — runs an interactive pass against the same fixtures that surfaced the dual-store seams (multi-line Shift+arrow, cross-block triple-click, paste-into-selection, undo-after-selection-delete).

### 2.2 Explicit non-goals (deferred)

- **`LiveSelectionView` deletion.** Keep the Q_OBJECT alive as a facade. Removing it entirely would force every QML consumer to thread `LiveCursorState` through instead, broadening the blast radius outside the seam. The facade is the surgical retirement (invariant 3 satisfied: the **canonical state** is retired, even though the class is retained). A future polish pass (tier 5) may delete the class once the QML surface grows a unified `LiveCursorState`-facing API; that is not tier-4c's scope.
- **`BlockInternalEdit` / `BlockSelected` interaction with range selection.** The cursor has four variants; range selection is only meaningful when the active end is `TextCaret`. The facade rejects `begin`/`extend` when `m_cursor` is non-text-bearing. Behavior under HR/Image/Math focus is unchanged from today (no selection extension across them).
- **Queue #4 — buffer-trailing-`\n` invariant.** The post-tier-4b queue #4 deferral stands: tier 4c first, then queue #4. Two seam invariants moving in parallel is the failure mode the post-mortems keep reproducing.
- **`m_applyingTextUpdate` in `LiveEditBinding`.** Different re-entrance guard, different seam (D2 buffer edits). Not tier 4c's scope; consider for a future tier-5 polish or the typing-authority side of the seam if dogfood surfaces a justification.
- **Mathematical equality on `TextAnchor`.** The equality short-circuit in §3 below assumes `Markoff::TextAnchor` comparison is stable. If it isn't, the plan resolves it by comparing the *derived* `(BlockAnchor, qtPos)` pair rather than `TextAnchor` directly (the resolved pair is what we ultimately store).

## 3. The L4 / ownership decision (per invariant 2)

**Decision: `LiveCursorState` is the sole canonical store for both cursor position AND selection anchor. `LiveSelectionView` retains its QML-exposed API as a stateless facade.**

The four pieces of state involved:

| State | Pre-tier-4c | Post-tier-4c |
|---|---|---|
| Active end of caret/selection | `LiveSelectionView::m_activeBlock`/`m_activeQtPos` (row-keyed) AND `LiveCursorState::m_cursor` (BlockAnchor-keyed) | `LiveCursorState::m_cursor` only |
| Selection anchor end | `LiveSelectionView::m_anchorBlock`/`m_anchorQtPos` (row-keyed) | `LiveCursorState::m_selectionAnchor: optional<SelectionAnchor>` (BlockAnchor-keyed) |
| Session bridge — outgoing | `LiveSelectionView::syncToSession()` | `LiveCursorState::syncSelectionToSession()` |
| Session bridge — incoming | `LiveSelectionView::onSessionPrimarySelectionChanged(sel)` | `LiveCursorState::onSessionPrimarySelectionChanged(sel)` |

**Active end reconciliation rule.** When `LiveSelectionView::begin(row, qtPos)` is called from QML:

1. Facade resolves `row` to `BlockAnchor` via `m_model->recordAt(row).blockAnchor`.
2. Calls `cursorState->establishFocus(anchor, qtPos)` (cursor's active end).
3. Sets `cursorState->m_selectionAnchor = {anchor, qtPos}` (selection's anchor end is the same point at `begin` time).

When `extend(row, qtPos)` is called:

1. Facade resolves `row` to `BlockAnchor`.
2. Calls `cursorState->establishFocus(anchor, qtPos)` (move the active end).
3. Leaves `m_selectionAnchor` alone (it stays where `begin` parked it).

When `clear()` is called:

1. `cursorState->m_selectionAnchor.reset()`.
2. The cursor's active end is unchanged.

**Retirement (per invariant 3).** The retiring stores are **`LiveSelectionView::m_anchorBlock`, `m_anchorQtPos`, `m_activeBlock`, `m_activeQtPos`, `m_document` (the LiveSelectionView side; LiveCursorState already has access via its binding), `m_session` (same), `m_model` (same), and `m_applyingSessionSelection`**. They are deleted as work-units in the same plan, not a follow-up. Invariant 3 satisfied by name.

## 4. Architecture

### 4.1 End-state types

In `libs/markoff-live/include/markoff/live/LiveCursorState.h`:

```cpp
struct SelectionAnchor {
    Markoff::BlockAnchor block;
    quint32              qtPos;
    bool operator==(const SelectionAnchor &) const = default;
};

class LiveCursorState : public QObject {
    // ... existing members ...
    std::optional<SelectionAnchor> m_selectionAnchor;

    // New public API (used by LiveSelectionView facade and direct callers):
    bool hasSelection() const noexcept;
    void setSelectionAnchor(SelectionAnchor anchor);  // mirrors m_cursor's active end at click-time
    void clearSelectionAnchor() noexcept;
    std::optional<SelectionAnchor> selectionAnchor() const { return m_selectionAnchor; }

    // Selection-range queries (rangeForBlock semantics moved here):
    QPoint selectionRangeForBlock(int row) const;

    // Session bridge (moved from LiveSelectionView):
    void setSession(Markoff::Session *session);  // owns the connection
    void syncSelectionToSession();
private slots:
    void onSessionPrimarySelectionChanged(const Markoff::Selection &sel);
};
```

### 4.2 End-state facade

`LiveSelectionView` keeps its Q_INVOKABLE surface but contains **no state**. All methods delegate to `LiveCursorState`. Example:

```cpp
class LiveSelectionView : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
public:
    explicit LiveSelectionView(LiveCursorState *cursorState, QObject *parent = nullptr)
        : QObject(parent), m_cursorState(cursorState)
    {
        connect(m_cursorState, &LiveCursorState::selectionChanged,
                this, &LiveSelectionView::selectionChanged);
    }

    bool hasSelection() const { return m_cursorState->hasSelection(); }

    Q_INVOKABLE void begin(int blockIndex, int qtPos) {
        if (!m_cursorState->model()) return;
        if (blockIndex < 0 || blockIndex >= m_cursorState->model()->rowCount()) return;
        const auto anchor = m_cursorState->model()->recordAt(blockIndex).blockAnchor;
        m_cursorState->establishFocus(anchor, qtPos);
        m_cursorState->setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});
    }

    Q_INVOKABLE void extend(int blockIndex, int qtPos) {
        if (!m_cursorState->model()) return;
        if (blockIndex < 0 || blockIndex >= m_cursorState->model()->rowCount()) return;
        const auto anchor = m_cursorState->model()->recordAt(blockIndex).blockAnchor;
        m_cursorState->establishFocus(anchor, qtPos);
        // m_selectionAnchor untouched.
    }

    Q_INVOKABLE void clear() { m_cursorState->clearSelectionAnchor(); }
    Q_INVOKABLE void selectAll() { /* delegates */ }
    Q_INVOKABLE void deleteSelection() { /* delegates */ }
    Q_INVOKABLE QPoint rangeForBlock(int row) const { return m_cursorState->selectionRangeForBlock(row); }
    Q_INVOKABLE void copyToClipboard() const { /* delegates */ }
    Q_INVOKABLE int anchorBlock() const;  // derived: rowForBlock(m_selectionAnchor->block)
    Q_INVOKABLE int anchorQtPos() const;  // m_selectionAnchor->qtPos
    Q_INVOKABLE int activeBlock() const;  // derived: rowForBlock(cursor.activeAnchor())
    Q_INVOKABLE int activeQtPos() const;  // cursor.focusedQtPos()

Q_SIGNALS:
    void selectionChanged();
};
```

The facade adds a `selectionChanged` signal that forwards from `LiveCursorState::selectionChanged` (new — see §4.4 for emission rules).

### 4.3 Re-entrance guard retirement

`onSessionPrimarySelectionChanged` (now on `LiveCursorState`):

```cpp
void LiveCursorState::onSessionPrimarySelectionChanged(const Markoff::Selection &sel)
{
    if (!m_binding || !m_binding->document() || !m_model) return;

    auto resolved = resolveSessionSelection(sel);  // {(activeBlock, activeQtPos), opt<anchor>}
    if (!resolved) {
        // Orphaned anchor: clear selection, keep cursor where it is.
        if (m_selectionAnchor) {
            m_selectionAnchor.reset();
            Q_EMIT selectionChanged();
        }
        return;
    }

    // Equality short-circuit — supersedes the m_applyingSessionSelection guard.
    // The check is on the resolved (BlockAnchor, qtPos) pair, not on TextAnchor
    // directly, because TextAnchor equality is not guaranteed stable across
    // CRDT-internal mutations.
    const bool sameActive    = activeMatchesCursor(resolved->active);
    const bool sameAnchor    = selectionAnchorMatches(resolved->selectionAnchor);
    if (sameActive && sameAnchor) return;

    // Apply.
    if (!sameActive) {
        // syncFromTextEdit semantics for the active end (idempotent if matched).
        syncFromTextEdit(resolved->active.block, resolved->active.qtPos);
    }
    if (!sameAnchor) {
        m_selectionAnchor = resolved->selectionAnchor;
        Q_EMIT selectionChanged();
    }
}
```

`syncSelectionToSession` (outgoing) calls `m_session->setPrimarySelection(sel)`. The slot above will fire — and short-circuit on the equality check. Re-entrance guard retired (invariant 7 cleared at this site).

### 4.4 New `selectionChanged` signal vs. existing `cursorChanged`

`LiveCursorState` currently emits `cursorChanged` when `m_cursor` mutates. Tier 4c adds `selectionChanged` which fires when `m_selectionAnchor` mutates OR `m_cursor`'s active end mutates while a selection is active. Rationale: QML consumers (delegates' `rangeForBlock` consumers; `LiveSelectionView`'s `hasSelection` Q_PROPERTY) need to invalidate independently of focus-only changes.

Emission rules:

| Mutation | `cursorChanged` | `selectionChanged` |
|---|---|---|
| `m_cursor` changes, no selection active | ✓ | (no) |
| `m_cursor` changes, selection active (extend) | ✓ | ✓ |
| `m_selectionAnchor` set (selection begins) | (no) | ✓ |
| `m_selectionAnchor` cleared | (no) | ✓ |

### 4.5 What stays untouched

- The chokepoint mechanism (`establishFocus`, `m_pendingFocus`, `tryResolvePending`, `delegateAvailable`, `delegateGoingAway`). All unchanged.
- The cursor variant model (`NoCursor | TextCaret | BlockSelected | BlockInternalEdit`). All unchanged.
- The pending-focus 500ms timeout.
- All QML consumers of `binding.selectionView.xxx` (LiveView.qml has 5 sites; delegates have a handful more) — unchanged because the facade preserves the API.
- The `syncFromTextEdit` typing-authority path.
- `LiveNavigationController`'s cross-block extension logic — it still reads `selectionView.anchorBlock()` etc. via the facade.

## 5. Components

Numbered as work units; the plan will sequence them.

### 5.1 New types in `LiveCursorState.h`

- `struct SelectionAnchor { BlockAnchor block; quint32 qtPos; ... };` with `operator==`.
- Member `std::optional<SelectionAnchor> m_selectionAnchor;`.
- Public accessors: `hasSelection()`, `selectionAnchor()`, `setSelectionAnchor(SelectionAnchor)`, `clearSelectionAnchor()`.
- Public ops: `selectionRangeForBlock(int row) -> QPoint`, `copySelectionToClipboard() const`, `selectAllBlocks()`, `deleteSelectionRange()` (the last three implement the existing `LiveSelectionView` ops; they live on `LiveCursorState` because they read the canonical state).
- Public bridge: `setSession(Session*)`, `syncSelectionToSession()`.
- Private slot: `onSessionPrimarySelectionChanged(const Selection&)`.
- Private helper: `resolveSessionSelection(const Selection&) -> optional<{...}>` (refactored from the existing `LiveSelectionView::onSessionPrimarySelectionChanged` lambda).
- New signal: `selectionChanged()`.

### 5.2 Implementation changes in `LiveCursorState.cpp`

- Constructor: take an optional `Session*` (or have `setSession` called separately from `LiveListModelBinding`'s pimpl ctor). Connect to `Session::primarySelectionChanged` if non-null.
- `establishFocus`: if a selection is active when this is called outside of an `extend` path, the selection should NOT be auto-cleared — the caller (facade) decides. The facade clears on `begin`-and-no-shift, leaves alone on `extend`-with-shift.
- `clear()`: also resets `m_selectionAnchor` (the existing semantic — "clear cursor" implies "clear selection" too).
- `selectionRangeForBlock(int row)`: port the existing `LiveSelectionView::rangeForBlock` body verbatim, sourcing `m_anchorBlock`/`m_anchorQtPos`/`m_activeBlock`/`m_activeQtPos` from the new canonical state (the anchor via `m_selectionAnchor`; the active via `rowForBlock(activeAnchor)` + `focusedQtPos()`).
- `copySelectionToClipboard()`, `selectAllBlocks()`, `deleteSelectionRange()`: port verbatim from `LiveSelectionView`, swap state lookups for canonical-store reads.
- `onSessionPrimarySelectionChanged`: rewrite per §4.3 (equality short-circuit, no re-entrance guard).
- `syncSelectionToSession`: same body as the existing `LiveSelectionView::syncToSession` but reads from canonical state.

### 5.3 `LiveSelectionView` rewrite

- Reduce to a Q_OBJECT facade per §4.2. Constructor takes a `LiveCursorState*`. No state members. No `m_applyingSessionSelection`. No `m_document`/`m_session`/`m_model` (those are reached via the cursor state).
- Connect `LiveCursorState::selectionChanged` → forward as `LiveSelectionView::selectionChanged`.
- All Q_INVOKABLE methods delegate.

### 5.4 `LiveListModelBinding` wiring

- The pimpl already constructs both `LiveCursorState` and `LiveSelectionView`. Adjust construction order: `LiveCursorState` first, then `LiveSelectionView(cursorState)`.
- `setSession(session)` on the binding currently calls `m_selectionView->setSession(session)`. After tier 4c, it calls `m_cursorState->setSession(session)`. The selection view doesn't need it.
- `setDocument(doc)` similarly — `m_cursorState` gains a `setDocument` if it doesn't already have it via `m_binding`; the selection view's setter is removed.

### 5.5 `LiveNavigationController` adjustments

- Currently reads `selectionView->anchorBlock()`, `selectionView->activeBlock()`, etc. After tier 4c, those still work (facade preserves the API). No code change required if the facade methods return the same observable semantics.
- One internal change: the controller can now optionally read from `LiveCursorState` directly — `cursorState->hasSelection()`, `cursorState->selectionAnchor()` — if it simplifies any cross-block extension logic. **Default: no change** (preserve scope; if the plan finds the new API cleaner inline, fold it in).

### 5.6 Falsifiability test

`tst_live_render_selection_cursor_unification` (new binary, or new slots on an existing chokepoint suite). The invariant test:

> *After every selection-mutating user action — click, Shift+click, Shift+arrow within a block, Shift+arrow cross-block, drag-select, double-click, triple-click — `cursorState->selectionAnchor()` (when present) is consistent with `cursorState->cursor()`'s active end, and `selectionView->anchorBlock()`/`anchorQtPos()` (the facade) resolves to the same `(BlockAnchor, qtPos)` pair.*

Concrete slots, all running on `LiveRealisticInputHarness` against the production QML view (invariant 5):

| Slot | Scenario | Asserts |
|---|---|---|
| `click_then_shift_click_keeps_anchor_at_first` | Click row 0 col 3, Shift+click row 2 col 7 | `selectionAnchor() = (row0.anchor, 3)`; cursor active = `(row2.anchor, 7)`; facade `anchorBlock()=0`, `activeBlock()=2`. |
| `shift_arrow_cross_block_extends_active` | Click row 0 col 5, Shift+Down | Anchor unchanged at `(row0.anchor, 5)`; active moves to row 1 same column. |
| `double_click_selects_word_via_facade` | Double-click in middle of word at row 0 | Facade `begin`+`extend` were called; canonical store now has selection covering the word. |
| `clear_via_left_arrow_collapses_to_active` | Make a selection, press Left (collapses to anchor by convention) | `m_selectionAnchor` cleared; cursor at the original anchor position. |
| `session_round_trip_no_echo` | Programmatically set a selection on `Session`; verify it propagates to `LiveCursorState` exactly once; programmatically mutate canonical state; verify `Session::setPrimarySelection` is called exactly once with the new selection. | Re-entrance via the equality short-circuit, not via a guard. |
| `selection_survives_structural_edit_above` | Make a selection in row 5; insert a new row at index 2 via `Cmd::enterAtEnd`; selection's anchor `BlockAnchor` is unchanged; facade `anchorBlock()` now returns 6 (renumbered). | Identity is `BlockAnchor`, not row index. |
| `selection_cleared_on_orphaned_anchor` | Make a selection in row 5; delete that block via `Cmd::backspaceMerge` collapsing the row; selection cleared (orphan path). | Matches today's `onSessionPrimarySelectionChanged` orphaned-anchor branch. |

**Falsifiability proof.** Before declaring the suite green, prove falsifiability:

1. Commit `markoff-live: stub — LiveSelectionView re-introduces shadow state (FALSIFIABILITY PROOF, REVERTS NEXT)`. Body: re-introduce the four scalar state fields on `LiveSelectionView`, have `begin`/`extend` write to BOTH the canonical store AND the shadow, have `anchorBlock()` etc. read from the shadow.
2. Run the test suite. **Slot `session_round_trip_no_echo` must fail** (the shadow state and the canonical state will diverge across the Session round-trip). If it passes, the test isn't pinning the consolidation; tighten it before proceeding.
3. Revert the stub.

Pattern follows `6943f6c`/`80f22bf` (tier-4b Proof A) and `e4932c9`/`1b73651` (tier-4b Proof B).

### 5.7 Discipline-log entry on retirement

Append to `docs/queue.md` § Discipline Log at the time of tier-4c landing:

```
- ~~prior `m_applyingSessionSelection` re-entrance guard in `LiveSelectionView`~~ → retired in tier 4c (`docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md` §4.3). Equality short-circuit on the resolved `(BlockAnchor, qtPos)` pair supersedes the guard. Invariant 7 cleared at this site.
```

(This is a *new* discipline-log entry filed retrospectively to record the retirement — there's no prior entry to strike-through.)

## 6. Data flow

### 6.1 Click at (row, qtPos)

```
QML MouseArea → LiveNavigationController::handleClick (or LiveView.qml inline)
    → binding.selectionView.begin(row, qtPos)   [facade]
        → cursorState->establishFocus(anchor, qtPos)
            ├→ m_pendingFocus = ...
            └→ tryResolvePending() (chokepoint resolves to delegate)
        → cursorState->setSelectionAnchor({anchor, qtPos})
            └→ Q_EMIT selectionChanged()
    → cursorState->syncSelectionToSession()
        └→ session->setPrimarySelection(sel)
            └→ Session emits primarySelectionChanged
                └→ cursorState::onSessionPrimarySelectionChanged
                    └→ equality short-circuit (incoming == current) → return
```

One write path, one round-trip, equality terminates the round-trip.

### 6.2 Shift+arrow within block

```
TextEdit handles arrow natively → cursorPositionChanged
    → delegate's onCursorPositionChanged → cursorState->syncFromTextEdit(anchor, newQtPos)
        ├→ m_cursor updated (active end moved)
        └→ Q_EMIT cursorChanged + selectionChanged (if m_selectionAnchor set)
```

No selection-side write needed; the canonical store updates atomically. Facade's `extend` isn't called for in-block arrow.

### 6.3 Shift+arrow cross-block

```
LiveStructuralKeyHandler / LiveNavigationController detects cross-block extend
    → binding.selectionView.extend(targetRow, targetQtPos)   [facade]
        → cursorState->establishFocus(targetAnchor, targetQtPos)
            └→ chokepoint moves active end to new row
                └→ on delegate available: takeFocus, m_cursor updated
                    └→ Q_EMIT cursorChanged + selectionChanged
    → cursorState->syncSelectionToSession()
```

Note: `m_selectionAnchor` is untouched (only set on `begin`). Active end moves; anchor stays.

### 6.4 Clear via Left/Right arrow (collapse selection)

```
LiveNavigationController detects Left/Right with selection present
    → binding.selectionView.clear()
        → cursorState->clearSelectionAnchor()
            └→ Q_EMIT selectionChanged
    → arrow handled normally → cursor active end mutates
```

### 6.5 Programmatic session selection (collab path)

```
Session emits primarySelectionChanged (from collab or another binding)
    → cursorState::onSessionPrimarySelectionChanged
        ├→ resolve TextAnchors to (BlockAnchor, qtPos) pairs
        ├→ equality short-circuit (vs. current state)
        ├→ if not equal: update m_cursor (via syncFromTextEdit) and m_selectionAnchor
        └→ emit appropriate signals
```

No second store to keep in sync, no echo, no guard.

## 7. Testing — supporting work

- The new `tst_live_render_selection_cursor_unification` binary (or slots added to `tst_live_render_focus_chokepoint_invariant` if the existing fixture supports the scenarios).
- The existing 14 `tst_live_render_selection_*` / `tst_live_render_clipboard_*` / `tst_live_render_format_*` / `tst_live_render_e2_nav_shift_extend*` files all consume `LiveSelectionView` via QML or directly. **None should require behavioral changes** — they consume the facade's API which is preserved. Any failure under the canonical-state migration is a regression to investigate, not a test-rewrite signal.
- The two `tst_live_render_session_*` files (`session_apply`, `session_clamp`, `session_orphaned_block`, `session_two_bindings`) directly exercise the session bridge. They will continue to drive `Session::setPrimarySelection` and observe `LiveSelectionView::hasSelection()` / `rangeForBlock`. After tier 4c, the assertions should still hold.

**Two-stage rollout for safety.** Land §5.1 + §5.2 (canonical-store changes in `LiveCursorState`) BEFORE §5.3 (facade rewrite). At the §5.2/§5.3 boundary, `LiveSelectionView` still has its own state and the canonical store in `LiveCursorState` is the "shadow"; once the canonical store passes all session-tests, the facade rewrite swaps which one is canonical and which one is the derived view. This mirrors the tier-4b "delete dead code last" pattern.

## 8. Definition of done

- [ ] `SelectionAnchor` struct added to `LiveCursorState.h` with `operator==`.
- [ ] `LiveCursorState::m_selectionAnchor` member added; `hasSelection`/`selectionAnchor`/`setSelectionAnchor`/`clearSelectionAnchor` methods added.
- [ ] `LiveCursorState::selectionRangeForBlock`, `copySelectionToClipboard`, `selectAllBlocks`, `deleteSelectionRange` methods added (ported from `LiveSelectionView`).
- [ ] `LiveCursorState::setSession` + `syncSelectionToSession` + `onSessionPrimarySelectionChanged` added (moved from `LiveSelectionView`).
- [ ] `LiveCursorState::selectionChanged` signal added.
- [ ] `LiveSelectionView` rewritten as stateless facade; `m_anchorBlock`/`m_anchorQtPos`/`m_activeBlock`/`m_activeQtPos`/`m_document`/`m_session`/`m_model`/`m_applyingSessionSelection` deleted; `git grep` confirms zero hits in `LiveSelectionView.cpp` for those members.
- [ ] `LiveListModelBinding` pimpl ctor wires `LiveSelectionView(cursorState)`; `setSession`/`setDocument` route to `cursorState` rather than `selectionView`.
- [ ] All existing `tst_live_render_selection_*` / `tst_live_render_clipboard_*` / `tst_live_render_format_*` / `tst_live_render_e2_nav_shift_extend*` / `tst_live_render_session_*` tests pass unchanged.
- [ ] New `tst_live_render_selection_cursor_unification` binary builds and passes all seven slots from §5.6.
- [ ] Falsifiability proof committed and reverted per §5.6. `session_round_trip_no_echo` demonstrably fails under the shadow-state stub.
- [ ] Discipline-log entry filed in `docs/queue.md` per §5.7.
- [ ] queue.md § #2 banner updated: tier-4c records concern #10 closed; queue #2 has no remaining concerns.
- [ ] `docs/e-arc/e-arc-status.md` recent-changes log entry filed.
- [ ] `Qt.callLater` count unchanged (still 1 at `MathDelegate.qml:113`).
- [ ] Re-entrance guard inventory: `m_applyingTextUpdate` (LiveEditBinding) only. `m_applyingSessionSelection` is gone. Verify with `git grep -nE 'm_applying|isApplying' libs/markoff-live/src/ libs/markoff-live/include/`.
- [ ] Interactive dogfood pass signs off the cross-block selection scenarios (spec §9).

## 9. Future work — tier 4d, tier 5

- **Tier 4d (light) — buffer-trailing-`\n` invariant (queue #4).** The plan body in `docs/queue.md` §#4 is the seed. Buffer-convention pick (B1 vs A1) decided in a short spec. Estimated ~1 day refactor + audit.
- **Tier 5 — polish.** Candidate items:
  - Delete `LiveSelectionView` entirely if a follow-on QML refactor removes the QML-side dependency (the facade is sustainable indefinitely; deletion is optional, not required).
  - Audit `m_applyingTextUpdate` in `LiveEditBinding` for retirement (invariant 7).
  - Rename `requestTextCaretAtRow` to a name that names the chokepoint relationship (`establishFocusAtRow` candidate).

Discipline rule (from tier 1 spec §10, unchanged): interactive dogfood between tiers, tag held pending sign-off.

**Tier 4c's dogfood gate** is heavier than tier 4b's. The scenarios to walk through, against a multi-block fixture (the existing `/tmp/setext-dogfood.md`-style document is fine):

- Click anywhere; nothing else jumps. Selection clears cleanly.
- Click in para A; Shift+click in para B three rows away. Selection covers everything from A to B. Copy → clipboard contains exactly that range. Click elsewhere → selection clears.
- Drag-select within a paragraph. Anchor and active should both update as you drag.
- Triple-click a paragraph. The whole block is selected; the cursor active end is at the block's end.
- Make a multi-block selection, press Delete. The selected range is removed; the cursor lands at the join point (matches today's `deleteSelection` semantic).
- Make a selection, press Ctrl+B (bold). The selected text gains `**`; selection is preserved across the edit (`LiveFormatController` reads `selectionView->rangeForBlock`).
- Open the same document in two `markoff-live-app` windows (or one app with a second binding); make a selection in window A; window B's binding should reflect it via Session round-trip; no echo or oscillation.

If any of those scenarios produces a focus regression or selection-loss case, **stop and triage** — that's exactly the failure mode tier 4c is supposed to eliminate.

## 10. Citations

- `docs/INVARIANTS.md` — invariants **1, 2, 3, 4, 5, 7, 8** all enforced here. Invariant 3 is the headline (retire `LiveSelectionView`'s canonical state in this same plan). Invariant 7 is the secondary headline (`m_applyingSessionSelection` retired with equality short-circuit).
- `docs/specs/2026-05-11-focus-chokepoint-design.md` §5.1 — chokepoint mechanism that `LiveSelectionView::begin`/`extend` delegates into.
- `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` §3 — typing/structural authority split; tier 4c's `m_cursor` active-end behavior inherits from this.
- `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` — the immediate predecessor; tier 4c is the last queue-#2 concern.
- `docs/queue.md` § Concern #10 — the originating critique.
- `docs/handoff/2026-05-09-e2.5-dogfood-findings.md` — D6 (Shift+↓ anchor) and the dogfood pass that introduced Option B (TextEdit reduced to renderer + cursor + IME, `LiveSelectionView` as canonical store for selection). Tier 4c completes the consolidation Option B started.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` — developmental record (per invariant 1). The `LiveSelectionView` introduction and its sync-via-Session design is documented there.
- `libs/markoff-live/src/LiveSelectionView.cpp:179, 252-254` — the `m_applyingSessionSelection` smell that invariant 7 names.
- `libs/markoff-live/qml/LiveView.qml:250-320` — the 5 QML consumer sites that the facade preserves API for.

## 11. Open questions deferred to the plan

- **`TextAnchor` equality.** §4.3 assumes the equality short-circuit can compare the resolved `(BlockAnchor, qtPos)` pair rather than `TextAnchor` directly. The plan verifies this is sufficient by inspecting `TextAnchor`'s internal state (`docs/specs/2026-05-04-d2-foundation-reshape-design.md` is the reference). If `TextAnchor` carries non-derivable state that the resolved pair doesn't reflect, the plan picks between (a) comparing TextAnchors directly via Markoff::Selection::operator== if it exists, or (b) extending the resolved pair with the missing field.
- **`LiveSelectionView` constructor signature.** The current constructor is `LiveSelectionView(QObject *parent)`. Adding the required `LiveCursorState*` argument changes the signature. The plan checks: are there any callers (tests, fixtures) that construct `LiveSelectionView` directly without the binding? The seven `tst_live_render_session_*` files are candidates — they construct bindings, which own the selection view, so probably safe. But the plan re-greps to confirm.
- **`Markoff::Selection::operator==`.** Does `Markoff::Selection` (in `markoff-core`) define equality? If yes, the equality short-circuit can use it directly (cleaner). If no, the plan picks: define it in core (and back-port across collabtext if needed), or compute equality element-wise inline.
- **Selection clearing on `establishFocus` from non-extending paths.** §4.5 says `establishFocus` doesn't auto-clear. But what about `LiveStructuralKeyHandler`'s Enter-at-paragraph-end (which calls `establishFocus` to place the cursor at the new block's start)? Should that clear an active selection? Today's behavior: structural edits implicitly clear selection because the typing event consumed the selection first via `deleteSelectionRange` (if relevant) and then `m_anchorBlock` reset via `LiveSelectionView::clear()` called by the binding. The plan threads through every `establishFocus` callsite (~25 in `LiveStructuralKeyHandler`, several in `LiveNavigationController` and `LiveListModelBinding`) and decides per-site whether to also `clearSelectionAnchor`. Default: structural edits that consume a selection (Enter, Backspace, Delete, character insert) clear; navigation that doesn't extend (click without shift, plain arrow) clears; navigation that extends (Shift+arrow, Shift+click) does NOT clear.
- **Two-bindings-share-session edge case.** `tst_live_render_session_two_bindings` covers the case where two `LiveListModelBinding` instances share a `Session`. After tier 4c, both bindings' `LiveCursorState`s subscribe to the same `primarySelectionChanged` signal. The equality short-circuit must work *per binding* — each binding's `LiveCursorState` checks its own state, not a global state. This is automatic (per-instance comparison) but the plan adds a test slot to confirm.
- **Plan-to-spec deviation budget.** If the plan finds that the equality short-circuit can't fully retire `m_applyingSessionSelection` (e.g., because `Session::setPrimarySelection` runs through queued connections and the round-trip is asynchronous in a way the equality check can't catch), the spec must be amended (per invariant 3) before the plan proceeds. The smell isn't deferrable to a follow-up tier.
