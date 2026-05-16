# Tier 4c — Selection/cursor unification — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close queue #2 concern **#10** by retiring `LiveSelectionView`'s canonical state and moving the optional selection anchor into `LiveCursorState` (alongside `m_cursor`). `LiveSelectionView` becomes a stateless Q_OBJECT facade preserving the QML-exposed API. Retire the `m_applyingSessionSelection` re-entrance guard via equality short-circuit. After this tier lands, queue #2 has zero remaining concerns.

**Architecture:** Five-phase rollout. Phase A adds the canonical store + bridge in `LiveCursorState` (alongside the existing `LiveSelectionView` state — both exist in parallel; cursor side is shadow). Phase B makes `LiveSelectionView`'s writers dual-write (to both stores). Phase C migrates `LiveSelectionView`'s readers to source from `LiveCursorState`. Phase D retires the shadow state in `LiveSelectionView` (facade rewrite). Phase E adds the new invariant test slots + falsifiability proof. Phase F updates docs.

**Tech Stack:** C++20, Qt 6.8+, QML, CMake 3.19+. Tests via QTest + `LiveRealisticInputHarness` + `QmlIntegrationFixture`. Build cap: `-j 8` always. Tests run via `scripts/run-tests.sh` (defaults to `QT_QPA_PLATFORM=offscreen`).

**Spec:** `docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md` is authoritative; cite section numbers when in doubt.

**Reading order before starting:**
1. `docs/INVARIANTS.md` (invariants 1, 2, 3, 4, 5, 7, 8)
2. `docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md` (full; section §3 is the L4 decision, §4 the architecture, §5 the components, §5.6 the test design)
3. `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` (predecessor — established the chokepoint-only canonical-store pattern this spec extends)
4. `libs/markoff-live/src/LiveSelectionView.cpp` (current implementation — most lines move; read it cover-to-cover)
5. `libs/markoff-live/include/markoff/live/LiveSelectionView.h` (current public surface to preserve)
6. `libs/markoff-live/include/markoff/live/LiveCursorState.h` (where the canonical store lands)
7. `libs/markoff-live/src/LiveCursorState.cpp` (where the new methods land)
8. `libs/markoff-live/src/LiveListModelBinding.cpp:163-184, 242-268` (wiring sites — pimpl ctor + setSession + setDocument)
9. `libs/markoff-live/qml/LiveView.qml:250-320` (the 5 QML consumer sites — the facade preserves the surface they consume)
10. `libs/markoff-live/tests/QmlIntegrationFixture.h` (fixture API for new test slots)

**Build / test commands:**
```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake --build build-dev -j 8

# Full fast suite (excludes the slow benchmark + realistic tests):
scripts/run-tests.sh -E 'realistic|benchmark'

# Selection/cursor focused subset (handy during phases):
scripts/run-tests.sh -R 'selection|clipboard|format|nav_shift|session'

# Single binary:
cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant
```

**Commit-message prefix convention:** `markoff-live: <slot summary>` for code; `docs:` for spec/plan/queue updates.

---

## Files touched

| File | Phase | Change |
|---|---|---|
| `libs/markoff-live/include/markoff/live/LiveCursorState.h` | A, D | New `SelectionAnchor` struct + `m_selectionAnchor` member + new public API + new private bridge slots/helpers + `selectionChanged` signal. |
| `libs/markoff-live/src/LiveCursorState.cpp` | A, D | Bodies for the new API. Constructor gains `setSession` wire-up. |
| `libs/markoff-live/include/markoff/live/LiveSelectionView.h` | C, D | Constructor signature changes to take `LiveCursorState*`. State members deleted. API surface unchanged (Q_INVOKABLE methods preserved). |
| `libs/markoff-live/src/LiveSelectionView.cpp` | B, C, D | Method bodies first dual-write, then read from `LiveCursorState`, then collapse to thin forwarders. State + helpers deleted. |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | D | Pimpl ctor passes `d->cursorState` to `LiveSelectionView`. `setSession`/`setDocument` route through cursor state. Drop calls to `selectionView->setModel/setDocument/setSession`. |
| `libs/markoff-live/tests/tst_live_render_selection_cursor_unification.cpp` | E | **New** — 7 invariant slots per spec §5.6. |
| `libs/markoff-live/tests/CMakeLists.txt` | E | Register the new target. |
| `docs/queue.md` | F | Banner for tier-4c; new discipline-log entry on `m_applyingSessionSelection` retirement; concern #10 closed; queue #2 banner now has no remaining concerns. |
| `docs/e-arc/e-arc-status.md` | F | Recent-changes log row. |

---

## Phase A: Add canonical state and bridge in `LiveCursorState` (cursor side becomes shadow)

After Phase A, both stores exist in parallel. `LiveSelectionView` is still authoritative; `LiveCursorState`'s new state is added but **not yet written**. Full suite must still pass cleanly because nothing has changed observable behaviour.

### Task 1: Pre-flight checks

**Files:** none (verification only).

- [ ] **Step 1:** Confirm worktree is on `exploration/new-foundation` and HEAD includes the tier-4c spec commit:

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
git branch --show-current
git log --oneline -5
```

Expected: branch `exploration/new-foundation`. Top commits include the tier-4c spec (committed in a prior session).

- [ ] **Step 2:** Working tree clean except for noise:

```bash
git status --short
```

Expected: untracked files only.

- [ ] **Step 3:** Build green:

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: no errors.

- [ ] **Step 4:** Record baseline failures for regression-check at later tasks:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4c-baseline-failures.txt
cat /tmp/tier4c-baseline-failures.txt
wc -l /tmp/tier4c-baseline-failures.txt
```

Expected: 1 entry (`tst_live_render_setext_e2e (Failed)` — pre-existing, unrelated). Confirm the exact set.

- [ ] **Step 5:** Confirm the dual-store premise is still accurate:

```bash
grep -n 'm_anchorBlock\|m_activeBlock\|m_applyingSessionSelection' libs/markoff-live/src/LiveSelectionView.cpp libs/markoff-live/include/markoff/live/LiveSelectionView.h
```

Expected: substantial hits in `LiveSelectionView.{h,cpp}` (the state members + guard). If unexpectedly missing, the spec's premise is wrong; **stop** and re-validate.

- [ ] **Step 6:** Verify `TextAnchor` equality:

```bash
grep -nE 'operator==|operator!=' libs/markoff-core/include/markoff/core/{Selection,TextAnchor}.h 2>/dev/null
grep -rnE 'class TextAnchor|struct TextAnchor' libs/markoff-core/include/ | head
```

Expected: locate `TextAnchor`'s header. Read its public surface — does it define `operator==`? Does `Markoff::Selection` define equality? This informs whether the §4.3 equality short-circuit can compare TextAnchors directly or must compare resolved `(BlockAnchor, qtPos)` pairs (spec §11 open question — resolve now before Phase A).

If `Markoff::Selection::operator==` exists, Phase A's §4.3 implementation uses it. If not, the plan uses the resolved-pair comparison (slightly more code, no semantic difference). Record your finding in the Step 6 report.

---

### Task 2: Add `SelectionAnchor` struct + accessor methods to `LiveCursorState`

Pure additive header + .cpp. No behaviour change because nothing writes the new member.

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveCursorState.h`
- `libs/markoff-live/src/LiveCursorState.cpp` (only if accessor bodies don't fit inline)

- [ ] **Step 1:** In `LiveCursorState.h`, add the struct definition near the top of the `Markoff::Live` namespace (before the `LiveCursorState` class declaration):

```cpp
/// Selection anchor — the "other end" of a text selection. The active
/// end is `m_cursor` (variant: TextCaret). When a selection is active,
/// `m_selectionAnchor` holds the BlockAnchor + qtPos where the selection
/// started (anchored by Click, Shift+Click `begin`, or session-incoming).
/// Identity is by `BlockAnchor` (stable across structural edits); the
/// inner-row index is derived on demand. Tier 4c canonical store.
struct SelectionAnchor {
    Markoff::BlockAnchor block;
    quint32              qtPos;
    bool operator==(const SelectionAnchor &other) const noexcept {
        return block == other.block && qtPos == other.qtPos;
    }
};
```

- [ ] **Step 2:** Inside the `LiveCursorState` class declaration, in the public section (after the existing `requestTextCaretAtRow` / `establishFocus` / `syncFromTextEdit` etc. declarations), add:

```cpp
    // ---- Selection state (tier 4c) ----

    /// True when a selection is active — i.e., `m_selectionAnchor` is set
    /// AND it points to a different (block, qtPos) than the cursor's
    /// active end. Collapsed selections (anchor == active) report false.
    bool hasSelection() const noexcept;

    /// The anchor end of an active text selection, or nullopt if no
    /// selection is active. The active end is read from `cursor()` /
    /// `currentTextCaret()`.
    std::optional<SelectionAnchor> selectionAnchor() const noexcept {
        return m_selectionAnchor;
    }

    /// Sets the selection anchor. Used by `LiveSelectionView::begin` (and
    /// equivalent paths) to park the anchor at the click-time position.
    /// Does NOT mutate `m_cursor` — caller is responsible for moving the
    /// active end via `establishFocus` or `syncFromTextEdit`. Emits
    /// `selectionChanged` if the value changes.
    void setSelectionAnchor(SelectionAnchor anchor);

    /// Clears the selection anchor. Used by `LiveSelectionView::clear`
    /// and the orphaned-anchor branch in session-incoming. Does NOT
    /// mutate `m_cursor`. Emits `selectionChanged` if the slot was set.
    void clearSelectionAnchor() noexcept;

Q_SIGNALS:
    void selectionChanged();
```

(Move the existing `Q_SIGNALS:` block to include `selectionChanged` if separating signals into one block is more idiomatic; either way works as long as `selectionChanged` is emitted at the right times.)

- [ ] **Step 3:** Add the private member to the bottom of the private section:

```cpp
    // Tier 4c — selection anchor (canonical store; the active end is m_cursor).
    std::optional<SelectionAnchor> m_selectionAnchor;
```

- [ ] **Step 4:** In `LiveCursorState.cpp`, add the three method bodies (place them after `clear()` so they're grouped with cursor-mutating ops):

```cpp
bool LiveCursorState::hasSelection() const noexcept
{
    if (!m_selectionAnchor) return false;
    const auto tc = currentTextCaret();
    if (!tc) return false;  // selection only meaningful when active end is TextCaret
    return !(m_selectionAnchor->block == tc->block
             && m_selectionAnchor->qtPos == tc->cachedQtPos);
}

void LiveCursorState::setSelectionAnchor(SelectionAnchor anchor)
{
    if (m_selectionAnchor && *m_selectionAnchor == anchor) return;
    m_selectionAnchor = anchor;
    Q_EMIT selectionChanged();
}

void LiveCursorState::clearSelectionAnchor() noexcept
{
    if (!m_selectionAnchor) return;
    m_selectionAnchor.reset();
    Q_EMIT selectionChanged();
}
```

- [ ] **Step 5:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean. The new struct + slot + methods compile; nothing yet writes them.

- [ ] **Step 6:** Run full fast suite — must match baseline:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
```

Expected: empty diff.

- [ ] **Step 7:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: add SelectionAnchor + m_selectionAnchor scaffold (tier-4c phase A)"
```

---

### Task 3: Port `rangeForBlock`, `copyToClipboard`, `selectAll`, `deleteSelection` to `LiveCursorState`

These four operations currently live on `LiveSelectionView`. Phase A adds **shadow** copies on `LiveCursorState` — same logic, sourced from the new canonical store. The originals on `LiveSelectionView` stay alive for now (they're still the authoritative path until Phase C migrates the readers). After Task 3, both implementations exist in parallel.

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveCursorState.h` (new method declarations)
- `libs/markoff-live/src/LiveCursorState.cpp` (new method bodies)

- [ ] **Step 1:** In `LiveCursorState.h` public section, add the four declarations:

```cpp
    /// Selection range for `row`, or `QPoint(-1, -1)` if untouched. The
    /// `end` component may be `INT_MAX` ("to end of block") — consumers
    /// must clamp via `Math.min(r.y, textEdit.length)` before calling
    /// `TextEdit.select`. Mirror of `LiveSelectionView::rangeForBlock`
    /// during Phase A; will replace it in Phase C.
    QPoint selectionRangeForBlock(int row) const;

    /// Copy the current selection to the system clipboard. Reads block
    /// texts from `m_model`. Mirror of `LiveSelectionView::copyToClipboard`
    /// during Phase A.
    void copySelectionToClipboard() const;

    /// Select all text in the document. Mutates both cursor active end
    /// (places at end of last block) and selection anchor (places at
    /// start of first block). Mirror of `LiveSelectionView::selectAll`
    /// during Phase A.
    void selectAllBlocks();

    /// Delete the currently-selected range and clear the selection.
    /// Mirror of `LiveSelectionView::deleteSelection` during Phase A.
    void deleteSelectionRange();
```

- [ ] **Step 2:** In `LiveCursorState.cpp`, add the four bodies. They are ports of `LiveSelectionView`'s logic, swapping state lookups. Below, `anchorRow()` / `activeRow()` / `anchorQtPos()` / `activeQtPos()` are convenient locals — define them at the top of each body:

```cpp
namespace {
// Helpers — derive selection corners from canonical state.
struct SelectionCorners {
    int anchorRow, anchorQtPos, activeRow, activeQtPos;
    bool valid;
};
} // namespace

static SelectionCorners cornersFromCanonical(const LiveCursorState *cs,
                                             const LiveBlockModel *model)
{
    SelectionCorners c{-1, -1, -1, -1, false};
    if (!model) return c;
    const auto anchor = cs->selectionAnchor();
    const auto active = cs->currentTextCaret();
    if (!anchor || !active) return c;
    const int aRow = cs->rowForBlock(anchor->block);
    const int xRow = cs->rowForBlock(active->block);
    if (aRow < 0 || xRow < 0) return c;
    c.anchorRow  = aRow;
    c.anchorQtPos = static_cast<int>(anchor->qtPos);
    c.activeRow  = xRow;
    c.activeQtPos = static_cast<int>(active->cachedQtPos);
    c.valid = true;
    return c;
}

static void normalizeCorners(const SelectionCorners &c, int &fb, int &fo, int &lb, int &lo)
{
    if (c.anchorRow < c.activeRow
        || (c.anchorRow == c.activeRow && c.anchorQtPos <= c.activeQtPos)) {
        fb = c.anchorRow; fo = c.anchorQtPos;
        lb = c.activeRow; lo = c.activeQtPos;
    } else {
        fb = c.activeRow; fo = c.activeQtPos;
        lb = c.anchorRow; lo = c.anchorQtPos;
    }
}

QPoint LiveCursorState::selectionRangeForBlock(int row) const
{
    const auto c = cornersFromCanonical(this, m_model);
    if (!c.valid) return QPoint(-1, -1);

    int fb, fo, lb, lo;
    normalizeCorners(c, fb, fo, lb, lo);

    if (row < fb || row > lb)         return QPoint(-1, -1);
    if (fb == lb)                     return QPoint(qMin(fo, lo), qMax(fo, lo));
    if (row == fb)                    return QPoint(fo, INT_MAX);
    if (row == lb)                    return QPoint(0, lo);
    return QPoint(0, INT_MAX);
}

void LiveCursorState::copySelectionToClipboard() const
{
    if (!hasSelection() || !m_model) return;

    const auto c = cornersFromCanonical(this, m_model);
    if (!c.valid) return;
    int fb, fo, lb, lo;
    normalizeCorners(c, fb, fo, lb, lo);

    const int rowCount = m_model->rowCount();
    QString text;
    for (int i = fb; i <= lb && i < rowCount; ++i) {
        const QString bt = m_model->recordAt(i).text;
        const int start = (i == fb) ? fo : 0;
        const int end   = qMin((i == lb) ? lo : bt.length(), bt.length());
        if (start > end) continue;
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += bt.mid(start, end - start);
    }

    QApplication::clipboard()->setText(text);
}

void LiveCursorState::selectAllBlocks()
{
    if (!m_model) return;
    const int rowCount = m_model->rowCount();
    if (rowCount <= 0) return;

    const auto firstAnchor = m_model->recordAt(0).blockAnchor;
    const auto lastRow     = rowCount - 1;
    const auto lastAnchor  = m_model->recordAt(lastRow).blockAnchor;
    const auto lastText    = m_model->recordAt(lastRow).text;

    // Active end at the end of the last block.
    syncFromTextEdit(lastAnchor, lastText.length());
    // Anchor at the start of the first block.
    setSelectionAnchor({firstAnchor, /*qtPos=*/0});
}

void LiveCursorState::deleteSelectionRange()
{
    if (!hasSelection() || !m_model || !m_binding || !m_binding->document())
        return;

    auto *doc = m_binding->document();
    const auto c = cornersFromCanonical(this, m_model);
    if (!c.valid) return;
    int fb, fo, lb, lo;
    normalizeCorners(c, fb, fo, lb, lo);

    const int rowCount = m_model->rowCount();
    if (fb < 0 || fb >= rowCount || lb < 0 || lb >= rowCount) return;

    // Compute flat byte start/end by walking iterateBlocks().
    const auto blocks = doc->iterateBlocks();
    uint32_t startByte = 0, endByte = 0, cursor = 0;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const QByteArray rawText = doc->blockText(blocks[i]);
        const uint32_t blockSize = static_cast<uint32_t>(rawText.size());

        if (i == fb) {
            const QByteArray modelUtf8 = m_model->recordAt(fb).text.toUtf8();
            startByte = cursor + static_cast<uint32_t>(
                Coordinates::qtPosToByte(modelUtf8, fo));
        }
        if (i == lb) {
            const QByteArray modelUtf8 = m_model->recordAt(lb).text.toUtf8();
            endByte = cursor + static_cast<uint32_t>(
                Coordinates::qtPosToByte(modelUtf8, lo));
            break;
        }
        cursor += blockSize;
    }

    if (endByte <= startByte) return;

    doc->applyFlatEdit(startByte, endByte, QByteArray(), Markoff::Origin::UserEdit);
    clearSelectionAnchor();
}
```

Add includes at the top of `LiveCursorState.cpp` if missing: `<QApplication>`, `<QClipboard>`, `<climits>`, `<markoff/live/Coordinates.h>`, `<markoff/core/MarkoffDocument.h>`.

- [ ] **Step 3:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 4:** Full fast suite must still match baseline (no readers consume the new methods yet):

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
```

Expected: empty diff.

- [ ] **Step 5:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: port selection ops to LiveCursorState (tier-4c phase A)"
```

---

### Task 4: Add session bridge to `LiveCursorState`

`setSession`, `syncSelectionToSession`, `onSessionPrimarySelectionChanged`. The equality short-circuit per spec §4.3 replaces the re-entrance guard.

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveCursorState.h`
- `libs/markoff-live/src/LiveCursorState.cpp`

- [ ] **Step 1:** Add forward declaration at the top of `LiveCursorState.h`:

```cpp
namespace Markoff {
class Session;
struct Selection;
}
```

(or include the appropriate header if forward-decl doesn't work for the Q_OBJECT slot signature).

- [ ] **Step 2:** In the `LiveCursorState` class declaration's public section, add:

```cpp
    /// Wires up the Session for primary-selection round-trips. Idempotent.
    /// On change, disconnects from the prior session before reconnecting.
    /// During Phase A this is added alongside the existing path in
    /// `LiveSelectionView::setSession`; the binding wires both. In Phase D
    /// the LiveSelectionView path is removed.
    void setSession(Markoff::Session *session);

    /// Outgoing: pushes the canonical selection state to
    /// `m_session->setPrimarySelection`. No-op if no session or no model.
    /// Mirror of `LiveSelectionView::syncToSession` during Phase A.
    void syncSelectionToSession();
```

In the private section, add:

```cpp
private slots:
    void onSessionPrimarySelectionChanged(const Markoff::Selection &sel);

private:
    Markoff::Session *m_session = nullptr;
```

- [ ] **Step 3:** In `LiveCursorState.cpp`, add:

```cpp
void LiveCursorState::setSession(Markoff::Session *session)
{
    if (m_session == session) return;
    if (m_session)
        QObject::disconnect(m_session, &Markoff::Session::primarySelectionChanged,
                            this, &LiveCursorState::onSessionPrimarySelectionChanged);
    m_session = session;
    if (m_session)
        QObject::connect(m_session, &Markoff::Session::primarySelectionChanged,
                         this, &LiveCursorState::onSessionPrimarySelectionChanged);
}

void LiveCursorState::syncSelectionToSession()
{
    if (!m_session || !m_binding || !m_binding->document() || !m_model) return;
    if (!hasSelection()) return;
    const auto anchor = m_selectionAnchor;
    const auto active = currentTextCaret();
    if (!anchor || !active) return;
    const int aRow = rowForBlock(anchor->block);
    const int xRow = rowForBlock(active->block);
    if (aRow < 0 || xRow < 0) return;

    auto *doc = m_binding->document();
    const auto makeAnchor = [&](Markoff::BlockAnchor block, int qtPos)
                                  -> Markoff::TextAnchor {
        const int row = rowForBlock(block);
        if (row < 0) return Markoff::TextAnchor{};
        const auto utf8 = m_model->recordAt(row).text.toUtf8();
        const int byteOff = static_cast<int>(
            Coordinates::qtPosToByte(utf8, qMax(0, qtPos)));
        return doc->textAnchorAt(block, byteOff, /*rightBias=*/true);
    };

    Markoff::Selection sel;
    sel.kind   = Markoff::Selection::Kind::Primary;
    sel.anchor = makeAnchor(anchor->block, static_cast<int>(anchor->qtPos));
    sel.active = makeAnchor(active->block, static_cast<int>(active->cachedQtPos));
    m_session->setPrimarySelection(sel);
    // No m_applying re-entrance guard — onSessionPrimarySelectionChanged
    // short-circuits on equality when the round-trip fires back.
}

void LiveCursorState::onSessionPrimarySelectionChanged(const Markoff::Selection &sel)
{
    if (!m_binding || !m_binding->document() || !m_model) return;

    auto *doc = m_binding->document();
    const auto resolveAnchor = [&](const Markoff::TextAnchor &ta)
                                      -> std::optional<SelectionAnchor> {
        if (ta.isNull()) return std::nullopt;
        const Markoff::BlockAnchor ba = ta.block();
        if (rowForBlock(ba) < 0) return std::nullopt;  // not in model
        const int byteOff = doc->offsetInBlock(ba, ta);
        const int row = rowForBlock(ba);
        const auto utf8 = m_model->recordAt(row).text.toUtf8();
        const int clamped = qBound(0, byteOff, static_cast<int>(utf8.size()));
        const int qtPos = static_cast<int>(Coordinates::byteToQtPos(utf8, clamped));
        return SelectionAnchor{ba, static_cast<quint32>(qtPos)};
    };

    const auto resolvedAnchor = resolveAnchor(sel.anchor);
    const auto resolvedActive = resolveAnchor(sel.active);

    if (!resolvedAnchor || !resolvedActive) {
        // Orphaned anchor → clear selection. Active end stays put.
        clearSelectionAnchor();
        return;
    }

    // Equality short-circuit (supersedes the m_applyingSessionSelection guard).
    const auto currentActive = currentTextCaret();
    const bool sameActive = currentActive
        && currentActive->block == resolvedActive->block
        && currentActive->cachedQtPos == resolvedActive->qtPos;
    const bool sameAnchor = m_selectionAnchor
        && *m_selectionAnchor == *resolvedAnchor;
    if (sameActive && sameAnchor) return;

    if (!sameActive) {
        // Active end mutates via the typing-authority hook (idempotent).
        syncFromTextEdit(resolvedActive->block, static_cast<int>(resolvedActive->qtPos));
    }
    if (!sameAnchor) {
        setSelectionAnchor(*resolvedAnchor);
    }
}
```

Add `#include <markoff/core/Session.h>` and `#include <markoff/core/Selection.h>` at the top of `LiveCursorState.cpp` if not already present.

- [ ] **Step 4:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 5:** Full fast suite must match baseline (cursor-side session bridge is still unwired — `setSession(nullptr)` from the binding's pimpl is the only caller, which is a no-op):

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
```

Expected: empty diff.

- [ ] **Step 6:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: add session bridge to LiveCursorState with equality short-circuit (tier-4c phase A)"
```

---

## Phase B: Dual-write through `LiveSelectionView` writers

`LiveSelectionView`'s `begin`/`extend`/`clear`/`selectAll`/`deleteSelection` methods write to **both** the existing state AND the new canonical state in `LiveCursorState`. The shadow becomes synchronized. The Session round-trip path also writes to both via dual subscription.

### Task 5: Dual-write in `LiveSelectionView::begin/extend/clear`

**Files:** Modify `libs/markoff-live/src/LiveSelectionView.cpp`.

`LiveSelectionView` needs a pointer to `LiveCursorState`. Add it.

- [ ] **Step 1:** In `LiveSelectionView.h`, add a private member + a setter:

```cpp
public:
    /// Tier-4c: wire the canonical store so dual-write keeps both in sync.
    /// Phase D removes the dual-write entirely (cursor state becomes sole).
    void setCursorState(LiveCursorState *cs) { m_cursorState = cs; }

private:
    LiveCursorState *m_cursorState = nullptr;
```

Add a forward declaration `class LiveCursorState;` near the top of the file.

- [ ] **Step 2:** In `LiveListModelBinding.cpp` pimpl ctor (around line 165), after the existing `selectionView` construction and `setModel` call, add:

```cpp
    d->selectionView->setCursorState(d->cursorState);
```

(This must happen AFTER both `cursorState` and `selectionView` are constructed and before any user-driven event.)

- [ ] **Step 3:** In `LiveSelectionView.cpp`, modify `begin` to dual-write:

```cpp
void LiveSelectionView::begin(int blockIndex, int qtPos)
{
    m_anchorBlock = blockIndex; m_anchorQtPos = qtPos;
    m_activeBlock = blockIndex; m_activeQtPos = qtPos;

    // Tier 4c: mirror to canonical store. Identity is BlockAnchor.
    if (m_cursorState && m_model
        && blockIndex >= 0 && blockIndex < m_model->rowCount()) {
        const auto anchor = m_model->recordAt(blockIndex).blockAnchor;
        m_cursorState->establishFocus(anchor, qtPos);
        m_cursorState->setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});
    }

    syncToSession();
    Q_EMIT selectionChanged();
}
```

- [ ] **Step 4:** Modify `extend`:

```cpp
void LiveSelectionView::extend(int blockIndex, int qtPos)
{
    m_activeBlock = blockIndex;
    m_activeQtPos = qtPos;

    // Tier 4c: mirror active-end change to canonical store.
    // Anchor stays where begin() parked it.
    if (m_cursorState && m_model
        && blockIndex >= 0 && blockIndex < m_model->rowCount()) {
        const auto anchor = m_model->recordAt(blockIndex).blockAnchor;
        m_cursorState->establishFocus(anchor, qtPos);
    }

    syncToSession();
    Q_EMIT selectionChanged();
}
```

- [ ] **Step 5:** Modify `clear`:

```cpp
void LiveSelectionView::clear()
{
    if (m_anchorBlock < 0) return;
    m_anchorBlock = m_anchorQtPos = m_activeBlock = m_activeQtPos = -1;

    // Tier 4c: clear canonical anchor (active end unchanged).
    if (m_cursorState) m_cursorState->clearSelectionAnchor();

    Q_EMIT selectionChanged();
}
```

- [ ] **Step 6:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 7:** Full fast suite, with focused subset emphasised (selection + clipboard + nav + format + session):

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
scripts/run-tests.sh -R 'selection|clipboard|format|nav_shift|session' 2>&1 | tail -3
```

Expected: empty diff for the first. Second confirms the focused subset is green.

- [ ] **Step 8:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveSelectionView.h \
        libs/markoff-live/src/LiveSelectionView.cpp \
        libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "markoff-live: dual-write begin/extend/clear to canonical store (tier-4c phase B)"
```

---

### Task 6: Dual-write in `selectAll` and `deleteSelection`

**Files:** Modify `libs/markoff-live/src/LiveSelectionView.cpp`.

- [ ] **Step 1:** Modify `selectAll`:

```cpp
void LiveSelectionView::selectAll()
{
    if (!m_model) return;
    const int rowCount = m_model->rowCount();
    if (rowCount <= 0) return;
    const int lastRow = rowCount - 1;
    const QString lastText = m_model->recordAt(lastRow).text;

    m_anchorBlock = 0;
    m_anchorQtPos = 0;
    m_activeBlock = lastRow;
    m_activeQtPos = lastText.length();

    // Tier 4c: mirror to canonical store.
    if (m_cursorState) m_cursorState->selectAllBlocks();

    syncToSession();
    Q_EMIT selectionChanged();
}
```

- [ ] **Step 2:** Modify `deleteSelection`. The flat-edit byte-walk currently lives here; the canonical-store version (`LiveCursorState::deleteSelectionRange`) does the same walk. Phase B duplicates the call — both paths run. The flat-edit is idempotent: the second call would no-op because the first call already cleared the selection. Net effect: the canonical-store path runs first, the LiveSelectionView path becomes a no-op for the same write. To avoid double-flat-edit ambiguity, route through the canonical-store version:

```cpp
void LiveSelectionView::deleteSelection()
{
    if (!hasSelection() || !m_model || !m_document) return;

    // Tier 4c: delegate to canonical store. The two stores are in
    // sync at this point (begin/extend/clear all dual-write); the
    // canonical-store implementation performs the same flat-edit.
    if (m_cursorState) {
        m_cursorState->deleteSelectionRange();
        // The canonical store's clearSelectionAnchor was called inside
        // deleteSelectionRange. Mirror back to local state.
        m_anchorBlock = m_anchorQtPos = m_activeBlock = m_activeQtPos = -1;
        Q_EMIT selectionChanged();
        return;
    }

    // Fallback: legacy path (no cursorState wired — should not happen
    // in production after the binding pimpl wires setCursorState).
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    const int rowCount = m_model->rowCount();
    if (fb < 0 || fb >= rowCount || lb < 0 || lb >= rowCount) return;
    const auto blocks = m_document->iterateBlocks();
    uint32_t startByte = 0, endByte = 0, cursor = 0;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const QByteArray rawText = m_document->blockText(blocks[i]);
        const uint32_t blockSize = static_cast<uint32_t>(rawText.size());
        if (i == fb) {
            const QByteArray modelUtf8 = m_model->recordAt(fb).text.toUtf8();
            startByte = cursor + static_cast<uint32_t>(
                Coordinates::qtPosToByte(modelUtf8, fo));
        }
        if (i == lb) {
            const QByteArray modelUtf8 = m_model->recordAt(lb).text.toUtf8();
            endByte = cursor + static_cast<uint32_t>(
                Coordinates::qtPosToByte(modelUtf8, lo));
            break;
        }
        cursor += blockSize;
    }
    if (endByte <= startByte) return;
    m_document->applyFlatEdit(startByte, endByte, QByteArray(), Markoff::Origin::UserEdit);
    clear();
}
```

The legacy fallback is preserved temporarily because Phase B's invariant is "both stores agree after every write" — if a test fixture skips `setCursorState`, the legacy path must still produce correct behaviour. Phase D removes the fallback when `m_cursorState` is non-nullable.

- [ ] **Step 3:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 4:** Full fast suite — particularly verify selection/clipboard/format/session tests:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
scripts/run-tests.sh -R 'selection|clipboard|format|nav_shift|session' 2>&1 | tail -3
```

Expected: empty diff; focused subset green.

- [ ] **Step 5:** Commit:

```bash
git add libs/markoff-live/src/LiveSelectionView.cpp
git commit -m "markoff-live: dual-write selectAll/deleteSelection to canonical store (tier-4c phase B)"
```

---

### Task 7: Wire `LiveCursorState::setSession` from the binding (cursor side begins receiving session events)

After this task, BOTH `LiveCursorState` AND `LiveSelectionView` subscribe to `Session::primarySelectionChanged`. The equality short-circuit in `LiveCursorState::onSessionPrimarySelectionChanged` prevents double-application; the `m_applyingSessionSelection` guard in `LiveSelectionView` still protects its own write-back path.

**Files:** Modify `libs/markoff-live/src/LiveListModelBinding.cpp`.

- [ ] **Step 1:** Read the existing `setSession` at line 266-269:

```cpp
void LiveListModelBinding::setSession(Markoff::Session *session)
{
    d->selectionView->setSession(session);
}
```

- [ ] **Step 2:** Modify to wire both:

```cpp
void LiveListModelBinding::setSession(Markoff::Session *session)
{
    d->selectionView->setSession(session);
    d->cursorState->setSession(session);  // tier-4c: canonical session bridge
}
```

Order matters: `LiveSelectionView::setSession` connects first (so it sees the next round-trip via its existing guard). `LiveCursorState::setSession` connects second. When the round-trip fires, both handlers run; both should resolve to the same incoming selection; the cursor-side equality short-circuit prevents an echo write through `syncSelectionToSession` (which is not yet called from any production path — only from new tests in Phase E).

- [ ] **Step 3:** Also update `setDocument` calls that pass `nullptr` to `selectionView->setSession`. Around line 242-243 and 253-254 of `LiveListModelBinding.cpp`, find:

```cpp
d->selectionView->setDocument(d->document);
d->selectionView->setSession(nullptr);
```

and

```cpp
d->selectionView->setDocument(nullptr);
d->selectionView->setSession(nullptr);
```

Both should additionally call `d->cursorState->setSession(nullptr)`:

```cpp
d->selectionView->setDocument(d->document);
d->selectionView->setSession(nullptr);
d->cursorState->setSession(nullptr);  // tier-4c
```

and

```cpp
d->selectionView->setDocument(nullptr);
d->selectionView->setSession(nullptr);
d->cursorState->setSession(nullptr);  // tier-4c
```

- [ ] **Step 4:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 5:** Full fast suite, with `tst_live_render_session_*` particularly important:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
scripts/run-tests.sh -R 'session' 2>&1 | tail -3
```

Expected: empty diff; session tests all pass.

- [ ] **Step 6:** Commit:

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "markoff-live: wire LiveCursorState::setSession dual-subscription (tier-4c phase B)"
```

---

## Phase C: Migrate readers — `LiveSelectionView` queries become forwarders

After Phase C, all read paths through `LiveSelectionView` source from `LiveCursorState`. The state members `m_anchorBlock/m_activeBlock/m_anchorQtPos/m_activeQtPos` are still written (by Phase B's dual-write) but no longer consulted — they're shadow now. Phase D deletes them.

### Task 8: Make `hasSelection`, accessors read from `LiveCursorState`

**Files:** Modify `libs/markoff-live/src/LiveSelectionView.cpp` and header.

- [ ] **Step 1:** In `LiveSelectionView.h`, move the accessor declarations out of inline (they need access to `m_cursorState` and `m_model`'s rowForBlock-equivalent — easier with out-of-line definitions):

```cpp
    Q_INVOKABLE int anchorBlock() const;
    Q_INVOKABLE int anchorQtPos() const;
    Q_INVOKABLE int activeBlock() const;
    Q_INVOKABLE int activeQtPos() const;
```

(Remove the inline `{ return m_anchorBlock; }` etc.)

- [ ] **Step 2:** In `LiveSelectionView.cpp`, add the four out-of-line bodies that delegate to `m_cursorState` (with a fallback to legacy state if cursor state is unwired):

```cpp
bool LiveSelectionView::hasSelection() const
{
    if (m_cursorState) return m_cursorState->hasSelection();
    return m_anchorBlock >= 0 && m_activeBlock >= 0
        && !(m_anchorBlock == m_activeBlock && m_anchorQtPos == m_activeQtPos);
}

int LiveSelectionView::anchorBlock() const
{
    if (m_cursorState) {
        const auto a = m_cursorState->selectionAnchor();
        if (!a) return -1;
        return m_cursorState->rowForBlock(a->block);
    }
    return m_anchorBlock;
}

int LiveSelectionView::anchorQtPos() const
{
    if (m_cursorState) {
        const auto a = m_cursorState->selectionAnchor();
        if (!a) return -1;
        return static_cast<int>(a->qtPos);
    }
    return m_anchorQtPos;
}

int LiveSelectionView::activeBlock() const
{
    if (m_cursorState) {
        const auto tc = m_cursorState->currentTextCaret();
        if (!tc) return -1;
        return m_cursorState->rowForBlock(tc->block);
    }
    return m_activeBlock;
}

int LiveSelectionView::activeQtPos() const
{
    if (m_cursorState) {
        const auto tc = m_cursorState->currentTextCaret();
        if (!tc) return -1;
        return static_cast<int>(tc->cachedQtPos);
    }
    return m_activeQtPos;
}
```

`LiveCursorState::rowForBlock` is currently private (declared `int rowForBlock(...) const` near line 105 of the header). Promote it to public for tier-4c (it's needed by the facade):

In `LiveCursorState.h`, move `rowForBlock` declaration from private to public:

```cpp
public:
    /// Resolve a BlockAnchor to its current model row. -1 if not in model.
    /// Tier 4c: used by `LiveSelectionView` facade.
    int rowForBlock(const Markoff::BlockAnchor &block) const;
```

- [ ] **Step 3:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 4:** Full fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
scripts/run-tests.sh -R 'selection|clipboard|format|nav_shift|session' 2>&1 | tail -3
```

Expected: empty diff; focused subset green.

- [ ] **Step 5:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveSelectionView.h \
        libs/markoff-live/src/LiveSelectionView.cpp \
        libs/markoff-live/include/markoff/live/LiveCursorState.h
git commit -m "markoff-live: route LiveSelectionView accessors through cursor state (tier-4c phase C)"
```

---

### Task 9: Migrate `rangeForBlock`, `copyToClipboard`, `selectAll`, `deleteSelection`

**Files:** Modify `libs/markoff-live/src/LiveSelectionView.cpp`.

- [ ] **Step 1:** Modify `rangeForBlock`:

```cpp
QPoint LiveSelectionView::rangeForBlock(int blockIndex) const
{
    if (m_cursorState) return m_cursorState->selectionRangeForBlock(blockIndex);

    // Legacy fallback (unwired cursorState — should not happen in production).
    if (m_anchorBlock < 0 || m_activeBlock < 0) return QPoint(-1, -1);
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    if (blockIndex < fb || blockIndex > lb) return QPoint(-1, -1);
    if (fb == lb) return QPoint(qMin(fo, lo), qMax(fo, lo));
    if (blockIndex == fb) return QPoint(fo, INT_MAX);
    if (blockIndex == lb) return QPoint(0, lo);
    return QPoint(0, INT_MAX);
}
```

- [ ] **Step 2:** Modify `copyToClipboard`:

```cpp
void LiveSelectionView::copyToClipboard() const
{
    if (m_cursorState) { m_cursorState->copySelectionToClipboard(); return; }
    // Legacy fallback...
    if (!hasSelection() || !m_model) return;
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    const int rowCount = m_model->rowCount();
    QString text;
    for (int i = fb; i <= lb && i < rowCount; ++i) {
        const QString bt = m_model->recordAt(i).text;
        const int start = (i == fb) ? fo : 0;
        const int end   = qMin((i == lb) ? lo : bt.length(), bt.length());
        if (start > end) continue;
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += bt.mid(start, end - start);
    }
    QApplication::clipboard()->setText(text);
}
```

- [ ] **Step 3:** Modify `selectAll`:

```cpp
void LiveSelectionView::selectAll()
{
    if (m_cursorState) {
        m_cursorState->selectAllBlocks();
        // Mirror back to local state so anchorBlock() etc. still report
        // correctly during the dual-store window. Phase D drops these.
        if (m_model && m_model->rowCount() > 0) {
            m_anchorBlock = 0;
            m_anchorQtPos = 0;
            m_activeBlock = m_model->rowCount() - 1;
            m_activeQtPos = m_model->recordAt(m_activeBlock).text.length();
        }
        syncToSession();
        Q_EMIT selectionChanged();
        return;
    }
    // Legacy fallback...
    if (!m_model) return;
    const int rowCount = m_model->rowCount();
    if (rowCount <= 0) return;
    const int lastRow = rowCount - 1;
    const QString lastText = m_model->recordAt(lastRow).text;
    m_anchorBlock = 0;
    m_anchorQtPos = 0;
    m_activeBlock = lastRow;
    m_activeQtPos = lastText.length();
    syncToSession();
    Q_EMIT selectionChanged();
}
```

(`deleteSelection` was already migrated in Task 6; no change here.)

- [ ] **Step 4:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 5:** Full fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
scripts/run-tests.sh -R 'selection|clipboard|format|nav_shift|session' 2>&1 | tail -3
```

Expected: empty diff; focused subset green.

- [ ] **Step 6:** Commit:

```bash
git add libs/markoff-live/src/LiveSelectionView.cpp
git commit -m "markoff-live: route range/copy/selectAll through cursor state (tier-4c phase C)"
```

---

## Phase D: Retire `LiveSelectionView` shadow state — facade rewrite

After Phase D, `LiveSelectionView` has no state of its own (other than the cached `m_cursorState` pointer). The `m_applyingSessionSelection` re-entrance guard is gone. Cursor state is the sole canonical store.

### Task 10: Falsifiability Proof — shadow state reintroduction

Per spec §5.6. Before deleting the shadow state, prove that the test suite catches drift between shadow and canonical state.

**Files:** Modify `libs/markoff-live/src/LiveSelectionView.cpp` (stub, reverts).

- [ ] **Step 1:** Add a deliberate divergence to the dual-write path. In `begin`:

```cpp
void LiveSelectionView::begin(int blockIndex, int qtPos)
{
    m_anchorBlock = blockIndex; m_anchorQtPos = qtPos;
    m_activeBlock = blockIndex; m_activeQtPos = qtPos;

    // FALSIFIABILITY PROOF, REVERTS NEXT — skip the canonical store write.
    // The shadow + canonical states will diverge; tests that consume the
    // canonical path should fail.
    // if (m_cursorState && m_model && blockIndex >= 0 && blockIndex < m_model->rowCount()) {
    //     const auto anchor = m_model->recordAt(blockIndex).blockAnchor;
    //     m_cursorState->establishFocus(anchor, qtPos);
    //     m_cursorState->setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});
    // }

    syncToSession();
    Q_EMIT selectionChanged();
}
```

(Comment out the dual-write block in `begin` only — that's enough to cause divergence.)

- [ ] **Step 2:** Commit the stub:

```bash
git add libs/markoff-live/src/LiveSelectionView.cpp
git commit -m "markoff-live: stub — LiveSelectionView::begin skips canonical write (FALSIFIABILITY PROOF, REVERTS NEXT)"
```

- [ ] **Step 3:** Build + run focused tests:

```bash
cmake --build build-dev --target markoff_live -j 8
scripts/run-tests.sh -R 'selection|nav_shift|session' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4c-proofb-failures.txt
diff /tmp/tier4c-baseline-failures.txt /tmp/tier4c-proofb-failures.txt
```

Expected: **non-empty diff with new failures**. Selection tests should fail because Phase C's readers now source from the canonical store, but the canonical store wasn't written by `begin`. If the diff is empty, the test coverage is too lenient — investigate before reverting.

- [ ] **Step 4:** Revert:

```bash
git revert HEAD --no-edit
```

- [ ] **Step 5:** Confirm we're back to baseline:

```bash
cmake --build build-dev --target markoff_live -j 8
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
```

Expected: empty diff.

---

### Task 11: Delete the shadow state from `LiveSelectionView`

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveSelectionView.h`
- `libs/markoff-live/src/LiveSelectionView.cpp`

- [ ] **Step 1:** From `LiveSelectionView.h`, delete:

```cpp
private:
    void normalized(int &fb, int &fo, int &lb, int &lo) const;
    void syncToSession();
    bool m_applyingSessionSelection = false;
    int m_anchorBlock = -1, m_anchorQtPos = -1;
    int m_activeBlock = -1, m_activeQtPos = -1;
    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::Session         *m_session  = nullptr;
    const LiveBlockModel     *m_model    = nullptr;
```

Keep only `LiveCursorState *m_cursorState = nullptr;` and the `setCursorState` setter.

Also delete `setDocument`, `setSession`, `setModel` declarations from the public section (they're not needed when the canonical store reaches the doc/session/model via `m_binding` and `m_cursorState`):

```cpp
    void setDocument(Markoff::MarkoffDocument *doc);    // delete
    Q_INVOKABLE void setSession(Markoff::Session *session);  // delete
    void setModel(const LiveBlockModel *model);  // delete
```

- [ ] **Step 2:** From `LiveSelectionView.cpp`, delete:
- The bodies of `setDocument`, `setSession`, `setModel`.
- The body of `normalized`.
- The body of `syncToSession`.
- The body of `onSessionPrimarySelectionChanged` and the slot declaration.

Also simplify all the dual-write methods to drop the legacy fallback:

```cpp
LiveSelectionView::LiveSelectionView(QObject *parent) : QObject(parent) {}

bool LiveSelectionView::hasSelection() const
{
    return m_cursorState && m_cursorState->hasSelection();
}

void LiveSelectionView::begin(int blockIndex, int qtPos)
{
    if (!m_cursorState || !m_cursorState->model()) return;
    if (blockIndex < 0 || blockIndex >= m_cursorState->model()->rowCount()) return;
    const auto anchor = m_cursorState->model()->recordAt(blockIndex).blockAnchor;
    m_cursorState->establishFocus(anchor, qtPos);
    m_cursorState->setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});
    m_cursorState->syncSelectionToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::extend(int blockIndex, int qtPos)
{
    if (!m_cursorState || !m_cursorState->model()) return;
    if (blockIndex < 0 || blockIndex >= m_cursorState->model()->rowCount()) return;
    const auto anchor = m_cursorState->model()->recordAt(blockIndex).blockAnchor;
    m_cursorState->establishFocus(anchor, qtPos);
    m_cursorState->syncSelectionToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::clear()
{
    if (!m_cursorState) return;
    m_cursorState->clearSelectionAnchor();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::selectAll()
{
    if (!m_cursorState) return;
    m_cursorState->selectAllBlocks();
    m_cursorState->syncSelectionToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::deleteSelection()
{
    if (!m_cursorState) return;
    m_cursorState->deleteSelectionRange();
    Q_EMIT selectionChanged();
}

QPoint LiveSelectionView::rangeForBlock(int blockIndex) const
{
    if (!m_cursorState) return QPoint(-1, -1);
    return m_cursorState->selectionRangeForBlock(blockIndex);
}

void LiveSelectionView::copyToClipboard() const
{
    if (!m_cursorState) return;
    m_cursorState->copySelectionToClipboard();
}

int LiveSelectionView::anchorBlock() const
{
    if (!m_cursorState) return -1;
    const auto a = m_cursorState->selectionAnchor();
    if (!a) return -1;
    return m_cursorState->rowForBlock(a->block);
}

int LiveSelectionView::anchorQtPos() const
{
    if (!m_cursorState) return -1;
    const auto a = m_cursorState->selectionAnchor();
    if (!a) return -1;
    return static_cast<int>(a->qtPos);
}

int LiveSelectionView::activeBlock() const
{
    if (!m_cursorState) return -1;
    const auto tc = m_cursorState->currentTextCaret();
    if (!tc) return -1;
    return m_cursorState->rowForBlock(tc->block);
}

int LiveSelectionView::activeQtPos() const
{
    if (!m_cursorState) return -1;
    const auto tc = m_cursorState->currentTextCaret();
    if (!tc) return -1;
    return static_cast<int>(tc->cachedQtPos);
}
```

If `LiveCursorState::model()` is not currently a public accessor, add it:

```cpp
// LiveCursorState.h public:
const LiveBlockModel *model() const noexcept { return m_model; }
```

Add includes to `LiveSelectionView.cpp` as needed for the new code paths (most already present).

- [ ] **Step 3:** In `LiveListModelBinding.cpp`, update the pimpl ctor + the setSession/setDocument plumbing:

Around line 165-167, change:
```cpp
d->selectionView   = new LiveSelectionView(this);
d->selectionView->setModel(d->model);
```
to:
```cpp
d->selectionView   = new LiveSelectionView(this);
d->selectionView->setCursorState(d->cursorState);
```

Around line 242-243 (the document-loaded branch), delete:
```cpp
d->selectionView->setDocument(d->document);
d->selectionView->setSession(nullptr);
```
keeping only `d->cursorState->setSession(nullptr);` (the cursor state already reaches the document via `m_binding`).

Around line 253-254 (the document-unloaded branch), delete:
```cpp
d->selectionView->setDocument(nullptr);
d->selectionView->setSession(nullptr);
```
keeping only `d->cursorState->setSession(nullptr);`.

Around line 266-269 (`setSession`), simplify:
```cpp
void LiveListModelBinding::setSession(Markoff::Session *session)
{
    d->cursorState->setSession(session);
}
```

- [ ] **Step 4:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean. If a compile error mentions `setDocument`/`setSession`/`setModel`/`m_anchorBlock` etc. on `LiveSelectionView`, fix the caller (it must be on the binding side; the QML / test surface preserves only the Q_INVOKABLE methods that remain).

- [ ] **Step 5:** Confirm `m_applyingSessionSelection` and the four state members are gone:

```bash
grep -nE 'm_applyingSessionSelection|m_anchorBlock|m_activeBlock|m_anchorQtPos|m_activeQtPos' \
    libs/markoff-live/src/LiveSelectionView.cpp libs/markoff-live/include/markoff/live/LiveSelectionView.h
```

Expected: zero hits.

- [ ] **Step 6:** Full fast suite must match baseline. Selection/clipboard/format/nav/session subsets all green:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
scripts/run-tests.sh -R 'selection|clipboard|format|nav_shift|session' 2>&1 | tail -3
```

Expected: empty diff; focused subset green.

- [ ] **Step 7:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveSelectionView.h \
        libs/markoff-live/src/LiveSelectionView.cpp \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/include/markoff/live/LiveCursorState.h
git commit -m "markoff-live: retire LiveSelectionView shadow state; facade-only (tier-4c phase D, queue #2 concern #10)"
```

---

## Phase E: New invariant test slots + falsifiability proof

### Task 12: Add `tst_live_render_selection_cursor_unification` test binary

**Files:**
- `libs/markoff-live/tests/tst_live_render_selection_cursor_unification.cpp` (new)
- `libs/markoff-live/tests/CMakeLists.txt` (register target)

- [ ] **Step 1:** Read the existing chokepoint test binary's CMake registration to copy the pattern:

```bash
grep -A 8 'tst_live_render_focus_chokepoint_invariant' libs/markoff-live/tests/CMakeLists.txt
```

Note the link libs + AUTOMOC + test discovery lines.

- [ ] **Step 2:** Create the new test file at `libs/markoff-live/tests/tst_live_render_selection_cursor_unification.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>

#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestSelectionCursorUnification : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void click_then_shift_click_keeps_anchor_at_first();
    void shift_arrow_cross_block_extends_active();
    void double_click_selects_word_via_facade();
    void clear_via_left_arrow_collapses_to_active();
    void session_round_trip_no_echo();
    void selection_survives_structural_edit_above();
    void selection_cleared_on_orphaned_anchor();

private:
    // Optional shared fixture members can go here if multiple slots share state.
};

void TestSelectionCursorUnification::init() {}
void TestSelectionCursorUnification::cleanup() {}

void TestSelectionCursorUnification::click_then_shift_click_keeps_anchor_at_first()
{
    // Click row 0 col 3 → anchor at (row0.anchor, 3), active same.
    // Shift+click row 2 col 7 → anchor unchanged, active moves to (row2.anchor, 7).
    QmlIntegrationFixture fx(QByteArrayLiteral("alpha alpha\n\nbeta beta\n\ngamma gamma"),
                             /*expectedRowCount=*/3);
    auto *binding = qobject_cast<LiveListModelBinding *>(fx.binding());
    QVERIFY(binding);
    auto *cs = binding->cursorState();
    QVERIFY(cs);

    binding->selectionView()->begin(/*row=*/0, /*qtPos=*/3);
    QTRY_VERIFY(cs->selectionAnchor().has_value());
    QCOMPARE(cs->selectionAnchor()->block, fx.model()->index(0, 0).data(/*BlockAnchorRole*/).value<Markoff::BlockAnchor>());
    QCOMPARE(cs->selectionAnchor()->qtPos, 3u);

    binding->selectionView()->extend(/*row=*/2, /*qtPos=*/7);
    QTRY_COMPARE(cs->rowForBlock(cs->currentTextCaret()->block), 2);
    QCOMPARE(cs->currentTextCaret()->cachedQtPos, 7u);

    // Anchor unchanged.
    QCOMPARE(cs->selectionAnchor()->qtPos, 3u);
    // Facade reports consistent state.
    QCOMPARE(binding->selectionView()->anchorBlock(), 0);
    QCOMPARE(binding->selectionView()->anchorQtPos(), 3);
    QCOMPARE(binding->selectionView()->activeBlock(), 2);
    QCOMPARE(binding->selectionView()->activeQtPos(), 7);
}

void TestSelectionCursorUnification::shift_arrow_cross_block_extends_active()
{
    QmlIntegrationFixture fx(QByteArrayLiteral("alpha\n\nbeta"), /*expectedRowCount=*/2);
    auto *binding = qobject_cast<LiveListModelBinding *>(fx.binding());
    auto *cs = binding->cursorState();
    binding->selectionView()->begin(0, 5);
    binding->selectionView()->extend(1, 5);
    QCOMPARE(cs->selectionAnchor()->qtPos, 5u);
    QCOMPARE(cs->rowForBlock(cs->currentTextCaret()->block), 1);
}

void TestSelectionCursorUnification::double_click_selects_word_via_facade()
{
    // Production double-click goes through the QML MouseArea → selectionView.
    // Simulate the begin/extend the QML side would produce.
    QmlIntegrationFixture fx(QByteArrayLiteral("alpha bravo charlie"), /*expectedRowCount=*/1);
    auto *binding = qobject_cast<LiveListModelBinding *>(fx.binding());
    binding->selectionView()->begin(0, 6);
    binding->selectionView()->extend(0, 11);  // "bravo" range
    QVERIFY(binding->selectionView()->hasSelection());
    const auto pt = binding->selectionView()->rangeForBlock(0);
    QCOMPARE(pt, QPoint(6, 11));
}

void TestSelectionCursorUnification::clear_via_left_arrow_collapses_to_active()
{
    QmlIntegrationFixture fx(QByteArrayLiteral("alpha bravo"), /*expectedRowCount=*/1);
    auto *binding = qobject_cast<LiveListModelBinding *>(fx.binding());
    auto *cs = binding->cursorState();
    binding->selectionView()->begin(0, 0);
    binding->selectionView()->extend(0, 5);
    QVERIFY(cs->hasSelection());

    binding->selectionView()->clear();
    QVERIFY(!cs->hasSelection());
    // Cursor active end is unchanged.
    QCOMPARE(cs->currentTextCaret()->cachedQtPos, 5u);
}

void TestSelectionCursorUnification::session_round_trip_no_echo()
{
    QmlIntegrationFixture fx(QByteArrayLiteral("alpha\n\nbeta"), /*expectedRowCount=*/2);
    auto *binding = qobject_cast<LiveListModelBinding *>(fx.binding());
    auto *session = fx.session();
    QVERIFY(session);

    QSignalSpy spy(session, &Markoff::Session::primarySelectionChanged);

    // Programmatically set a selection on Session.
    binding->selectionView()->begin(0, 0);
    binding->selectionView()->extend(1, 3);

    // Wait a tick. Expect exactly one Session emission per begin and per extend
    // (two total). If the equality short-circuit fails, we'd see more.
    QTRY_VERIFY(spy.count() >= 2);
    const int countAfterBeginExtend = spy.count();

    // Programmatically push the same selection via Session.setPrimarySelection.
    // The round-trip should arrive at LiveCursorState's slot, find equality,
    // and NOT echo back. spy count should not grow.
    Markoff::Selection sel;
    sel.kind = Markoff::Selection::Kind::Primary;
    // Reconstruct the current selection's TextAnchors from the canonical state
    // by querying the binding's syncSelectionToSession output via a peek
    // mechanism… or simpler: trigger the same syncSelectionToSession.
    binding->cursorState()->syncSelectionToSession();
    QTest::qWait(50);
    QCOMPARE(spy.count(), countAfterBeginExtend + 1);  // exactly one outgoing emit
    // No echo.
}

void TestSelectionCursorUnification::selection_survives_structural_edit_above()
{
    QmlIntegrationFixture fx(QByteArrayLiteral("first\n\nsecond\n\nthird\n\nfourth"),
                             /*expectedRowCount=*/4);
    auto *binding = qobject_cast<LiveListModelBinding *>(fx.binding());
    auto *cs = binding->cursorState();
    auto *doc = fx.document();

    binding->selectionView()->begin(2, 0);
    binding->selectionView()->extend(3, 3);
    const auto anchorBefore = cs->selectionAnchor()->block;

    // Insert a new row at index 1 via Cmd::enterAtEnd on row 0.
    const auto block0 = fx.model()->recordAt(0).blockAnchor;
    Markoff::Cmd::enterAtEnd(*doc, block0);
    QTRY_COMPARE(fx.model()->rowCount(), 5);

    // The selection's anchor BlockAnchor is unchanged.
    QCOMPARE(cs->selectionAnchor()->block, anchorBefore);
    // Facade reports the new row index (anchor row was 2, now 3).
    QCOMPARE(binding->selectionView()->anchorBlock(), 3);
}

void TestSelectionCursorUnification::selection_cleared_on_orphaned_anchor()
{
    QmlIntegrationFixture fx(QByteArrayLiteral("first\n\nsecond"), /*expectedRowCount=*/2);
    auto *binding = qobject_cast<LiveListModelBinding *>(fx.binding());
    auto *cs = binding->cursorState();
    auto *doc = fx.document();

    binding->selectionView()->begin(1, 0);
    binding->selectionView()->extend(1, 6);
    QVERIFY(cs->hasSelection());

    // Delete the selected block via D2 API.
    const auto block1 = fx.model()->recordAt(1).blockAnchor;
    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2RemoveBlock(block1, t);
    }

    QTRY_COMPARE(fx.model()->rowCount(), 1);
    QVERIFY(!cs->hasSelection());
}

} // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestSelectionCursorUnification)
#include "tst_live_render_selection_cursor_unification.moc"
```

- [ ] **Step 3:** Register in CMake. Edit `libs/markoff-live/tests/CMakeLists.txt` — find the block that registers `tst_live_render_focus_chokepoint_invariant` and add a parallel block for the new binary. Copy the link-libs + AUTOMOC + add_test pattern verbatim.

- [ ] **Step 4:** Build the new target:

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_live_render_selection_cursor_unification -j 8
```

Expected: builds clean. If unresolved-symbol errors mention `BlockAnchorRole` or similar, look up the right model role from `LiveBlockModel.h` and fix the test.

- [ ] **Step 5:** Run the new slot file:

```bash
scripts/run-tests.sh --bin tst_live_render_selection_cursor_unification
```

Expected: all 7 slots PASS. If any fail, the canonical-store wiring is wrong — fix before proceeding.

- [ ] **Step 6:** Confirm full suite still green:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
```

Expected: empty diff.

- [ ] **Step 7:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_selection_cursor_unification.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: add tst_live_render_selection_cursor_unification (tier-4c phase E)"
```

---

### Task 13: Falsifiability Proof — re-introduce shadow state

Per spec §5.6 Proof. The Phase D commits deleted the shadow state. This proof re-introduces it as a stub, verifies `session_round_trip_no_echo` fails (because the round-trip path now reaches a divergent state), then reverts.

**Files:** Modify `libs/markoff-live/src/LiveSelectionView.cpp` (stub, reverts).

- [ ] **Step 1:** Re-introduce a small piece of shadow state with deliberate divergence. The simplest divergence: make `LiveSelectionView::begin` write to a private `_shadowAnchorQtPos` and have `anchorQtPos()` return that shadow instead of delegating:

```cpp
// At top of LiveSelectionView.cpp, after the namespace open:
namespace {
// FALSIFIABILITY PROOF, REVERTS NEXT — shadow state to force divergence.
int g_shadowAnchorQtPos = -1;
}

// In begin():
void LiveSelectionView::begin(int blockIndex, int qtPos)
{
    if (!m_cursorState || !m_cursorState->model()) return;
    if (blockIndex < 0 || blockIndex >= m_cursorState->model()->rowCount()) return;
    const auto anchor = m_cursorState->model()->recordAt(blockIndex).blockAnchor;
    m_cursorState->establishFocus(anchor, qtPos);
    m_cursorState->setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});

    // STUB: shadow diverges from canonical.
    g_shadowAnchorQtPos = qtPos + 100;  // deliberately wrong

    m_cursorState->syncSelectionToSession();
    Q_EMIT selectionChanged();
}

// In anchorQtPos():
int LiveSelectionView::anchorQtPos() const
{
    // STUB: read from shadow, not canonical.
    if (g_shadowAnchorQtPos >= 0) return g_shadowAnchorQtPos;
    if (!m_cursorState) return -1;
    const auto a = m_cursorState->selectionAnchor();
    if (!a) return -1;
    return static_cast<int>(a->qtPos);
}
```

- [ ] **Step 2:** Commit the stub:

```bash
git add libs/markoff-live/src/LiveSelectionView.cpp
git commit -m "markoff-live: stub — LiveSelectionView shadow anchorQtPos diverges (FALSIFIABILITY PROOF, REVERTS NEXT)"
```

- [ ] **Step 3:** Build + run the new invariant test:

```bash
cmake --build build-dev --target tst_live_render_selection_cursor_unification -j 8
scripts/run-tests.sh --bin tst_live_render_selection_cursor_unification
```

Expected: at least one slot fails (likely `click_then_shift_click_keeps_anchor_at_first` because `binding->selectionView()->anchorQtPos()` now returns `qtPos + 100`). If all pass, the test isn't pinning the contract — investigate.

- [ ] **Step 4:** Revert:

```bash
git revert HEAD --no-edit
```

- [ ] **Step 5:** Confirm green:

```bash
cmake --build build-dev --target tst_live_render_selection_cursor_unification -j 8
scripts/run-tests.sh --bin tst_live_render_selection_cursor_unification
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
```

Expected: all slots pass + empty diff.

> **Falsifiability Proof B — PASSED (2026-05-16).** Stub commit: `dc31eab`.
> Revert commit: `d0cc3d8`. Stub: `g_shadowAnchorQtPos = qtPos + 100` in
> `begin()`; `anchorQtPos()` reads shadow instead of canonical store.
> Result: 3 of 7 slots failed —
> `click_then_shift_click_keeps_anchor_at_first` (got 103, expected 3),
> `double_click_selects_word_via_facade` (got 106, expected 6),
> `session_round_trip_no_echo` (got 100, expected 0). After revert: 9/9
> pass. Tests correctly detect canonical-store regression — they are not
> inert.

---

### Task 14: Full-suite regression check

**Files:** none (verification).

- [ ] **Step 1:** Clean rebuild:

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

- [ ] **Step 2:** Full fast suite vs baseline:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4c-final-failures.txt
diff /tmp/tier4c-baseline-failures.txt /tmp/tier4c-final-failures.txt
```

Expected: empty diff.

- [ ] **Step 3:** Chokepoint suite still 48 slots (tier 4b baseline):

```bash
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant 2>&1 | tail -3
```

Expected: `Totals: 48 passed, 0 failed`.

- [ ] **Step 4:** New unification suite 7 slots:

```bash
scripts/run-tests.sh --bin tst_live_render_selection_cursor_unification 2>&1 | tail -3
```

Expected: `Totals: 7 passed, 0 failed`.

- [ ] **Step 5:** Realistic/benchmark sweep:

```bash
scripts/run-tests.sh -R 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4c-realistic-failures.txt
wc -l /tmp/tier4c-realistic-failures.txt
```

Expected: ≤ pre-tier-4c count.

- [ ] **Step 6:** Grep-based invariant checks:

```bash
# Qt.callLater inventory unchanged:
echo "Qt.callLater count:"
git grep -c 'Qt\.callLater' libs/markoff-live/qml/

# Re-entrance guards — m_applyingSessionSelection must be gone:
echo "Re-entrance guards:"
git grep -nE 'm_applying|isApplying' libs/markoff-live/src/ libs/markoff-live/include/

# Dead symbols gone:
echo "Dead-symbols sweep:"
git grep -nE 'm_applyingSessionSelection|LiveSelectionView::syncToSession|LiveSelectionView::normalized' \
    libs/markoff-live/src/ libs/markoff-live/include/
```

Expected:
- `Qt.callLater`: still 1 (`MathDelegate.qml:113`).
- Re-entrance guards: `m_applyingTextUpdate` only. `m_applyingSessionSelection` is gone.
- Dead-symbols: zero hits.

---

## Phase F: Documentation updates

### Task 15: Update `docs/queue.md`

**Files:** Modify `docs/queue.md`.

- [ ] **Step 1:** Add a new banner block above the existing tier-4b banner:

```
> **2026-05-16 — Tier 4c implemented.** Last queue #2 concern
> closed: **#10** (`LiveSelectionView` / `LiveCursorState` dual
> canonical stores). `LiveSelectionView` is now a stateless Q_OBJECT
> facade preserving the QML-exposed API; `m_selectionAnchor` (new)
> + `m_cursor` (existing) on `LiveCursorState` are the sole canonical
> store, keyed by `BlockAnchor`. Session bridge (`syncSelectionToSession`,
> `onSessionPrimarySelectionChanged`) moved with the state. The
> `m_applyingSessionSelection` re-entrance guard is retired via the
> equality short-circuit on the resolved (BlockAnchor, qtPos) pair
> (invariant 7 cleared at this site). Spec
> `docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md`;
> plan `docs/plans/2026-05-16-tier-4c-selection-cursor-unification.md`.
> Two falsifiability proofs in history (stub-then-revert pairs). New
> invariant binary `tst_live_render_selection_cursor_unification`
> with 7 slots covering click-then-shift-click, cross-block shift-arrow,
> double-click, clear-via-arrow, session round-trip no-echo,
> selection-survives-structural-edit-above, and orphaned-anchor cleanup.
> Queue #2 now has no remaining concerns.
>
```

- [ ] **Step 2:** Add a new discipline-log entry recording the retirement:

```
- ~~prior `m_applyingSessionSelection` re-entrance guard in `LiveSelectionView`~~ → retired in tier 4c (`docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md` §4.3). Equality short-circuit on the resolved `(BlockAnchor, qtPos)` pair supersedes the guard. Invariant 7 cleared at this site.
```

(Filed retrospectively; there was no prior discipline-log entry to strike-through, but the format follows the closed-entry convention so a future reader can trace the retirement.)

- [ ] **Step 3:** Commit:

```bash
git add docs/queue.md
git commit -m "docs: queue — tier-4c closes concern #10; queue #2 fully resolved"
```

---

### Task 16: Update `docs/e-arc/e-arc-status.md`

**Files:** Modify `docs/e-arc/e-arc-status.md`.

- [ ] **Step 1:** Update "Last updated":

```
**Last updated:** 2026-05-16 (tier-4c complete — selection/cursor unification; queue #2 fully resolved).
```

- [ ] **Step 2:** Update "Active phase":

```
**Active phase:** **dogfood pending** — E2.5 (S1/S2/S3 fixes) + E2.6 (theme + zoom) + tier-4b + tier-4c all implemented; queue #2 is fully resolved. Tags held until user runs the interactive checklist (tier-4c heavier dogfood — see spec §9). Next executable work (if dogfood unavailable): queue #4 (buffer-`\n` invariant) — see queue.md for ordering.
```

- [ ] **Step 3:** Add new row at the top of the recent-changes log table:

```
| 2026-05-16 | (tier-4c) | **Tier-4c selection/cursor unification: queue #2 concern #10 closed.** `LiveSelectionView`'s canonical state (`m_anchorBlock`/`m_activeBlock` quadruple, `m_document`/`m_session`/`m_model`, `m_applyingSessionSelection` re-entrance guard, plus `normalized`/`syncToSession`/`onSessionPrimarySelectionChanged` helpers) is retired. `LiveCursorState` becomes the sole canonical store for both cursor (active end) and selection (anchor end) via new `m_selectionAnchor: optional<SelectionAnchor>` member. Identity by BlockAnchor (stable across structural edits) instead of row-index. Session bridge moved; equality short-circuit on resolved (BlockAnchor, qtPos) pair supersedes the re-entrance guard (invariant 7 cleared). `LiveSelectionView` becomes a stateless Q_OBJECT facade preserving the QML-exposed API (5 LiveView.qml sites + delegate consumers unchanged). New invariant binary `tst_live_render_selection_cursor_unification` with 7 slots. Two falsifiability proofs (stub-then-revert pairs) in history. Queue #2 fully resolved; next is queue #4 (buffer-\\`\\n\\`). |
```

- [ ] **Step 4:** Commit:

```bash
git add docs/e-arc/e-arc-status.md
git commit -m "docs: e-arc-status — record tier-4c session (concern #10 closed)"
```

---

### Task 17: Final verification

**Files:** none (verification).

- [ ] **Step 1:** Full fast suite vs baseline (one more time):

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4c-baseline-failures.txt -
```

Expected: empty diff.

- [ ] **Step 2:** Confirm new binary still green:

```bash
scripts/run-tests.sh --bin tst_live_render_selection_cursor_unification 2>&1 | tail -3
```

Expected: 7 passed.

- [ ] **Step 3:** Commit chain review:

```bash
git log --oneline <BASE_SHA>..HEAD
```

Where `<BASE_SHA>` is the commit before Task 1's pre-flight (typically the tier-4b final commit). Expect ~17-19 commits:
- Phase A: 3 commits (scaffold, ops, session bridge)
- Phase B: 3 commits (begin/extend/clear dual-write, selectAll/deleteSelection dual-write, session subscription)
- Phase C: 2 commits (accessors, range/copy/selectAll)
- Phase D: 4 commits (falsifiability stub, revert, state deletion, plus possibly a separate facade-rewrite commit if you split)
- Phase E: 4 commits (test binary, falsifiability stub, revert, full regression)
- Phase F: 3 commits (queue.md, e-arc-status, this final-verify commit if any code change needed)

- [ ] **Step 4:** Working tree clean:

```bash
git status --short
```

Expected: untracked-only.

- [ ] **Step 5:** Produce a one-paragraph handoff summary citing:
- Queue #2 concern #10 closed; queue #2 fully resolved.
- `m_applyingSessionSelection` retired (invariant 7 cleared).
- Two falsifiability proofs in history (cite the stub commit SHAs).
- New test binary at 7/7 slots; existing suites unchanged.
- Heavier dogfood gate than 4b — spec §9 has the seven scenarios; needs user's local desktop.
- Remaining open work: queue #4 (buffer-`\n` invariant) → tier 4d.
- Tags untouched.

---

## Spec coverage check

Cross-reference each spec section against the plan tasks:

- **§2.1 in scope:**
  - `#10 (full)` → Tasks 2-11 (canonical state, dual-write, migration, facade rewrite).
  - Preserve QML surface → §4.2 facade design + Tasks 8-11 retain Q_INVOKABLE signatures.
  - Move Session bridge → Task 4 + Task 7 + Task 11.
  - Retire `m_applyingSessionSelection` → Task 11 (state deletion) + spec §4.3 equality short-circuit in Task 4.
  - Single identity system (BlockAnchor) → struct definition in Task 2; facade-side derivation in Tasks 8-9.
  - Falsifiability test → Tasks 10 + 13.
  - Dogfood gate → spec §9; user-run after Phase F.

- **§2.2 explicit non-goals:**
  - LiveSelectionView deletion — preserved (Phase D keeps the Q_OBJECT alive).
  - BlockInternalEdit/BlockSelected interaction — out of scope; `hasSelection()` returns false unless cursor is TextCaret (Task 2 method body checks `currentTextCaret()`).
  - Queue #4 — deferred to tier 4d.
  - `m_applyingTextUpdate` — out of scope.
  - TextAnchor equality — Task 1 Step 6 resolves the open question before Phase A.

- **§3 L4 decision:** Enforced via Tasks 2-11. Retiring stores enumerated in §3 and deleted in Phase D (invariant 3 satisfied by name).

- **§4 Architecture:** §4.1 types implemented in Task 2; §4.2 facade implemented in Task 11; §4.3 equality short-circuit in Task 4; §4.4 selectionChanged emission rules in Tasks 2 + 4 (followed throughout); §4.5 "what stays untouched" preserved.

- **§5 Components:** §5.1-5.4 mapped to Tasks 2 (struct + accessors), 3 (ops), 4 (session bridge), 11 (facade rewrite + binding wiring). §5.5 navigation controller — no change required per spec (default; verify in Task 14). §5.6 invariant tests — Task 12. §5.7 discipline-log entry — Task 15.

- **§6 Data flow:** Behavioural — verified end-to-end by Phase E's seven test slots.

- **§7 Testing:** Two-stage rollout — Phases A-C land canonical-state additively; Phase D removes shadow. Existing tests pass at every phase boundary (verified by build+test gates per task).

- **§8 Definition of done:** Maps 1:1 with Task 17's final verification list.

- **§9 Future work:** Tier 4d (queue #4) deferred — noted in Task 15's queue.md banner.

- **§11 Open questions:** Resolved per the plan:
  - TextAnchor equality — Task 1 Step 6.
  - LiveSelectionView ctor signature — current signature kept (`LiveSelectionView(QObject *parent)`); cursor state attached via `setCursorState`.
  - `Markoff::Selection::operator==` — used if present (Task 1 Step 6 finding informs); otherwise element-wise inline (already in the equality short-circuit code).
  - Selection clearing on `establishFocus` from non-extending paths — facade controls: `begin` sets both ends; `extend` moves only active; `clear` clears anchor only. Structural callers in `LiveStructuralKeyHandler` may also call `establishFocus` directly; those paths should call `clearSelectionAnchor` if they want to clear selection. **Open for plan execution**: scan the ~25 `establishFocus` callsites in `LiveStructuralKeyHandler` and add `clearSelectionAnchor` calls where appropriate (Enter, Backspace, Delete, kind-transition). Default: structural edits clear selection (because typing past a selection in production today already clears the selection via the `deleteSelection` path); add a Task 11.X step if Task 11 surfaces uncleared-selection regressions in the focused suite.
  - Two-bindings-share-session — the per-instance state means equality short-circuit works per `LiveCursorState`; `tst_live_render_session_two_bindings` already exercises this and must pass at every phase boundary.
  - Plan-to-spec deviation budget — any deviation requires a spec amendment before the deviation lands.
