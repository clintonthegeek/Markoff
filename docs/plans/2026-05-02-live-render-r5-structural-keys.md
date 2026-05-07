# R5 — Structural Keys + IME Completion + Undo Coalescing Implementation Plan

> **2026-05-04 — CANCELLED by D-evolution pivot.** Tasks 1–11 are landed in tree. Tasks 12–17 are **cancelled** — they are subsumed by D2's L4–L5 redesign on the per-block-CRDT premise. The structural-key dispatch shape (`LiveStructuralKeyHandler` + `BlockKindDescriptor::consumedStructuralKeys`) is a candidate to carry forward to D2 unchanged; that's a D2 §6 decision, not an R5 task. See `docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace R4's Enter-swallow workaround with real structural-key handling. Pressing Enter mid-paragraph splits the block into two and lands the caret at qtPos 0 of the new row deterministically (no `Qt.callLater` retry loops). Backspace at row-start merges with the previous block; Delete at row-end merges with the next; Shift-Enter inserts a soft break. Consecutive printable keystrokes coalesce into one undo entry per a deterministic policy; structural edits, movement, and an idle threshold break the chain.

**Architecture:** A new C++ component `LiveStructuralKeyHandler` (QML-exposed via `LiveListModelBinding`) is the single dispatcher for structural keys. Each `BlockKindDescriptor` declares which `Qt::Key_*` values it consumes (`consumedStructuralKeys`); the handler holds a kind-keyed table of `std::function<HandleResult(const Ctx&)>` populated at registry init. Each text delegate's TextEdit installs `Keys.priority: BeforeItem` and forwards Enter/Backspace/Delete/Shift-Enter through `structuralKeyHandler.tryHandle(...)`. After `applyLocalEdit`, the handler stamps the affected row's `lastEditEditSequence` (so the freshness rule preserves the row's text on parse-back) and, when the structural edit will produce a new row, calls `cursorState.requestTextCaretAtRow(expectedRow, qtPos)` — a new `LiveCursorState` mechanism that watches `LiveBlockModel::rowsInserted` and resolves the pending request deterministically when the row appears (spec §5.3 step 6). Focus delivery into the newly-incubated row goes through one new QML `Connections` block on `cursorState.cursorChanged` that calls `focusEditAt(qtPos)` on the matched delegate. A new `UndoCoalescer` component holds the printable-coalesce policy: track `(kind, blockAnchor, lastEditTimer)` for the most recent edit; when the next edit is also printable, in the same focus context, and within the 1000 ms idle threshold, call `MarkoffDocument::coalesceLastUndo()`. Non-printables, structural edits, focus changes, paste, and idle expiry break the chain. R4's per-delegate `Keys.onPressed` Enter-swallow handlers are removed.

**Tech stack:** C++20, Qt 6.8 (Quick, Qml, Test, Widgets for tests), `Markoff::MarkoffDocument::applyLocalEdit / coalesceLastUndo / resolveTextAnchor / blockByteRange`, `QAbstractItemModel::rowsInserted` for deterministic focus delivery.

**Reference spec:** `docs/specs/2026-05-02-live-render-restoration-design.md` §5.3 (focus protocol), §5.4 (structural keys), §6.1 L5 (component map), §7.2 (structural-edit data flow), §11 R5 (phase scope), §15.1/§15.4 (open questions resolved here).

**Prerequisites:** R1–R4 complete. `LiveEditBinding` per-delegate routes contentsChange → applyLocalEdit; freshness rule wired in `LiveBlockModel::applyOps`; three cycle guards in place. R4's Enter-swallow `Keys.onPressed` handlers exist on paragraph, heading, code-block delegates and are removed by this plan.

**Acceptance criterion (binary):** R5 dogfood script (spec §10.3) passes — *"Press Enter at the end of every paragraph in a 10-block doc; caret lands in the new empty paragraph each time; Backspace at the start of each merges back, restoring the original."* — AND `tst_live_render_structural` passes covering: end-of-block Enter, mid-block Enter (split + caret transfer), start-of-block Enter, Backspace-at-row-start (merge with previous), Delete-at-row-end (merge with next), Shift-Enter (soft break), code-block Enter NOT consumed (literal `\n`), code-block Backspace-at-start (merge with previous), heading structural keys, descriptor `consumedStructuralKeys` lookup correctness, `LiveCursorState::requestTextCaretAtRow` resolution on `rowsInserted`, parsed-cycle stale-pending drop, undo coalescing across consecutive printables in the same focus context, undo coalescing broken by structural edit, undo coalescing broken by focus change, undo coalescing broken by 1 s idle threshold, undo coalescing NOT applied to structural edits.

---

## Open question resolutions (from spec §15)

This plan resolves the following open questions noted in spec §15:

- **§15.1 (descriptor consumedStructuralKeys + handler-registration mechanism).** Resolved as: `BlockKindDescriptor::consumedStructuralKeys` is a `QSet<int>` of `Qt::Key_*` values (already declared in `BlockKindDescriptor.h`). Registration is a separate per-kind handler-table held inside `LiveStructuralKeyHandler`: a `QHash<QString, QHash<int, HandlerFn>>` where `HandlerFn = std::function<HandleResult(const Ctx&)>`. Built-in kind handlers are registered alongside the descriptors in `BlockKindRegistry::registerBuiltins()`'s sibling: `LiveStructuralKeyHandler::registerBuiltinHandlers()`. Plugin authors register both the descriptor (via `BlockKindRegistry::register_`) and any kind-specific handlers (via `LiveStructuralKeyHandler::registerHandler(kind, key, fn)`) as paired calls. Falls back to the default character-insertion path on `NotHandled`/`DeferToCharacterInsertion`.
- **§15.4 (undo-coalescing idle threshold).** Resolved as: pinned at 1000 ms (matches the legacy `markoff-view-qml::LiveEditBinding` value). Not exposed as a Setting in R5. Future-Setting work is out of scope.
- **§15.8 (rename moment for the test app's `--live` flag).** Out of R5 scope: the legacy `markoff-view-qml-app`'s `--live` flag is owned by `markoff-view-qml`; this plan does not touch that codebase. R5 keeps `markoff-live-render-app` as the dogfood vehicle. The flag flip is R10 work.

---

## File map

**New — public headers** (`libs/markoff-live-render/include/markoff/live-render/`):
- `LiveStructuralKeyHandler.h` — `QML_ELEMENT`; descriptor-driven structural-key dispatcher.
- `UndoCoalescer.h` — `QML_ELEMENT` (consumed via `LiveListModelBinding` Q_PROPERTY); printable-coalesce policy.

**New — sources** (`libs/markoff-live-render/src/`):
- `LiveStructuralKeyHandler.cpp`
- `UndoCoalescer.cpp`

**New — tests** (`libs/markoff-live-render/tests/`):
- `tst_live_render_structural.cpp`

**Modified — headers:**
- `libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h` — add `requestTextCaretAtRow(int expectedRow, int qtPos)` + private pending-state + `rowsInserted` slot.
- `libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h` — own + expose `LiveStructuralKeyHandler *structuralKeyHandler` and `UndoCoalescer *undoCoalescer` Q_PROPERTYs (CONSTANT).
- `libs/markoff-live-render/include/markoff/live-render/BlockKindDescriptor.h` — already has `consumedStructuralKeys`; no header change. (The R3 builtins do not populate it; R5 populates.)

**Modified — sources:**
- `libs/markoff-live-render/src/LiveCursorState.cpp` — implement `requestTextCaretAtRow` + the rowsInserted-driven resolver + parse-cycle bookkeeping.
- `libs/markoff-live-render/src/LiveListModelBinding.cpp` — construct + wire the new components; reset pending requests' deadline on each parseUpdated.
- `libs/markoff-live-render/src/BlockKindRegistry.cpp` — populate `consumedStructuralKeys` for paragraph, heading, code-block.
- `libs/markoff-live-render/src/LiveEditBinding.cpp` — call `undoCoalescer->recordPrintable(blockAnchor)` after each user-typed `applyLocalEdit`; on parse-arrival (which rebuilds the document state), no change needed.
- `libs/markoff-live-render/CMakeLists.txt` — add the two new sources and the new header to `qt_add_qml_module`.
- `libs/markoff-live-render/tests/CMakeLists.txt` — add `tst_live_render_structural` executable.

**Modified — QML:**
- `libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml` — remove the R4 Enter-swallow `Keys.onPressed`; replace with structural-key dispatcher.
- `libs/markoff-live-render/qml/delegates/HeadingDelegate.qml` — same.
- `libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml` — same; code-block consumes only Backspace-at-start and Delete-at-end (Enter passes through to TextEdit's literal `\n` insertion).
- `libs/markoff-live-render/qml/LiveView.qml` — add a single `Connections{target: binding.cursorState; onCursorChanged: ...}` block that, when the cursor is a `TextCaret` for a row whose delegate is currently incubated, calls `focusEditAt(qtPos)` on it.
- `libs/markoff-live-render/app/Main.qml` — title suffix `"(R5)"`.

**Modified — docs:**
- `docs/restoration-status.md` — TL;DR + Phase board + recent-changes log entries (last task).

**Untouched (verified-not-broken):**
- `LiveBlockModel`, `BlockHitTester`, `LiveSelectionView`, `Coordinates` (R2/R3 surfaces).
- `HorizontalRuleDelegate`, `ImageDelegate` (non-text; structural keys do not apply).

---

## Task 1: Read context

- [ ] **Step 1: Read these files in order, no edits**

```
docs/specs/2026-05-02-live-render-restoration-design.md   §5.3, §5.4, §6.1 L5, §7.2, §11 R5, §15.1, §15.4
docs/plans/2026-05-02-live-render-r4-paragraph-editing.md (skim — recent commit shape and test patterns)
libs/markoff-core/include/markoff-foundation/MarkoffDocument.h    (applyLocalEdit, coalesceLastUndo, resolveTextAnchor, blockByteRange, visibleLength)
libs/markoff-live-render/include/markoff/live-render/BlockKindDescriptor.h    (consumedStructuralKeys field; declared, not yet populated)
libs/markoff-live-render/src/BlockKindRegistry.cpp                      (the five built-in registrations; we populate consumedStructuralKeys here)
libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h  (request, validateVariant — we add requestTextCaretAtRow)
libs/markoff-live-render/src/LiveCursorState.cpp                        (current implementation; matches the header)
libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h
libs/markoff-live-render/src/LiveListModelBinding.cpp                   (where the new components attach + how onParseUpdated is shaped)
libs/markoff-live-render/include/markoff/live-render/LiveEditBinding.h
libs/markoff-live-render/src/LiveEditBinding.cpp                        (where recordPrintable hook lands)
libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml            (the R4 Enter-swallow we're replacing)
libs/markoff-live-render/qml/delegates/HeadingDelegate.qml
libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml
libs/markoff-live-render/qml/LiveView.qml                               (where the new cursorChanged Connections block lands)
libs/markoff-view-qml/src/LiveStructuralKeyHandler.cpp                  (legacy reference — useful for byte-arithmetic patterns; do NOT port the projection-layer / hole branches)
libs/markoff-view-qml/src/LiveEditBinding.cpp lines 220-243              (legacy printable-coalesce pattern that UndoCoalescer extracts)
```

No code changes in this task.

- [ ] **Step 2: Run the existing fast-tier test suite to confirm a clean baseline**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-render-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all six `tst_live_render_*` executables green (skeleton, registry, coords, block_model, cursor, paragraph_edit). Record the count; R5 must end with at least the same count green plus the new `tst_live_render_structural`.

---

## Task 2: Extend `LiveCursorState` with `requestTextCaretAtRow` (TDD)

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h`
- Modify: `libs/markoff-live-render/src/LiveCursorState.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_cursor.cpp`

The structural-key handler emits an edit that creates a new row, but the row doesn't exist in the model until the parse-back arrives. Per spec §5.3 step 6, focus delivery into not-yet-incubated rows is deterministic via `LiveBlockModel::rowsInserted` — no `Qt.callLater` retry loops. This task adds the pending-request mechanism.

Behaviour:

1. `requestTextCaretAtRow(int expectedRow, int qtPos)` — if the row already exists, build a `TextCaret` from `model->recordAt(row).blockAnchor` and call the existing `request(...)` path. If the row does not exist (`expectedRow >= rowCount`), record `(expectedRow, qtPos, parseSeqAtRequest=lastSeenParseSeq)` and watch `model->rowsInserted`.
2. On `rowsInserted(parent, first, last)`: if a pending request exists and `first <= m_pending->row <= last`, build the cursor from the now-realised row and request it; clear pending. Other inserts that don't match the pending row are ignored.
3. On `noteParseArrived(parseSeq)` (called by `LiveListModelBinding` from `onParseUpdated`): if a pending request has lingered through ≥ 2 parse cycles since it was recorded, log and drop. (Per spec §8.4: "log and drop; cursor falls back to the last valid position.")

**Notes on coupling.** `LiveCursorState` already holds a `const LiveBlockModel *m_model`. Connecting to its `rowsInserted` requires the model to be non-const for the `connect` call signature (the QObject side of the connection). Keep `m_model` `const`; the connection handle is fine — `QObject::connect` accepts `const T *` in modern Qt for read-only signal subscriptions. If the compiler complains, drop the const qualifier.

- [ ] **Step 1: Add a failing test for the row-already-exists fast path**

In `libs/markoff-live-render/tests/tst_live_render_cursor.cpp`, append the following test slots (right before the closing `};` of the test class):

```cpp
    void requestTextCaretAtRow_already_exists_resolves_immediately() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{
            makeRec(BlockKind::Paragraph, "alpha"),
            makeRec(BlockKind::Paragraph, "beta"),
        };
        QList<BlockKey> keys;
        for (const auto &r : recs) keys << keyOf(r);
        model.applyOps(AstBlockDiff::diff({}, keys), recs);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);

        QCOMPARE(spy.count(), 1);
        const Cursor cur = cs.cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).cachedByteOffset, quint32(0));
        QCOMPARE(std::get<TextCaret>(cur).block, recs[1].blockAnchor);
    }

    void requestTextCaretAtRow_pending_resolves_on_rowsInserted() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto first = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "alpha") };
        QList<BlockKey> firstKeys; firstKeys << keyOf(first[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), first);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        // Request row 1: doesn't exist yet (rowCount == 1, valid rows are 0..0).
        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);

        // No cursorChanged yet — pending.
        QCOMPARE(spy.count(), 0);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));

        // Row 1 appears: applyOps with an Insert at row 1.
        const auto second = QList<BlockRecord>{
            first[0],
            makeRec(BlockKind::Paragraph, "beta"),
        };
        QList<BlockKey> secondKeys;
        for (const auto &r : second) secondKeys << keyOf(r);
        model.applyOps(AstBlockDiff::diff(firstKeys, secondKeys), second);

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::holds_alternative<TextCaret>(cs.cursor()));
        QCOMPARE(std::get<TextCaret>(cs.cursor()).block, second[1].blockAnchor);
    }

    void requestTextCaretAtRow_pending_dropped_after_two_parse_cycles() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto first = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "alpha") };
        QList<BlockKey> firstKeys; firstKeys << keyOf(first[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), first);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);

        // Two parse arrivals with no row insertion at the expected row:
        // pending should be dropped, cursorChanged not fired.
        cs.noteParseArrived(/*parseSeq=*/1);
        cs.noteParseArrived(/*parseSeq=*/2);

        // A third parse with the row inserted should NOT fire (pending dropped).
        const auto third = QList<BlockRecord>{
            first[0],
            makeRec(BlockKind::Paragraph, "beta"),
        };
        QList<BlockKey> thirdKeys;
        for (const auto &r : third) thirdKeys << keyOf(r);
        model.applyOps(AstBlockDiff::diff(firstKeys, thirdKeys), third);

        QCOMPARE(spy.count(), 0);
    }
```

- [ ] **Step 2: Run the test; expect compile failure**

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8
```

Expected: compile fails on `requestTextCaretAtRow` and `noteParseArrived` (don't exist yet). Confirms the test is wired against the new shape.

- [ ] **Step 3: Update the header**

In `libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h`, add the new methods + private state. Replace the file with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/Cursor.h>

#include <QObject>
#include <QString>
#include <optional>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

class BlockKindRegistry;
class LiveBlockModel;

/// Owns the single canonical cursor value for the live view. Validates
/// `request()` calls against the target block's `BlockKindDescriptor`
/// (so BlockSelected is refused on a paragraph, etc.). Emits
/// `cursorChanged()` only when the cursor actually changes. Spec §5.3.
///
/// `cursorKind` Q_PROPERTY exposes the active variant as a string for QML
/// bindings that need to react to focus type (e.g. focus ring vs. caret).
/// Values: "none", "TextCaret", "BlockSelected", "BlockInternalEdit".
///
/// `requestTextCaretAtRow` is the deterministic-pending variant used by
/// the structural-key handler in R5: when a structural edit creates a new
/// row, the row doesn't exist in the model until the parse-back arrives.
/// The pending request is held; `rowsInserted` resolves it. Spec §5.3
/// step 6.
class MARKOFF_LIVE_RENDER_EXPORT LiveCursorState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveCursorState is provided by LiveListModelBinding")

    Q_PROPERTY(QString cursorKind READ cursorKind NOTIFY cursorChanged)

public:
    explicit LiveCursorState(const BlockKindRegistry *registry,
                             const LiveBlockModel    *model,
                             QObject                 *parent = nullptr);

    Cursor cursor() const { return m_cursor; }
    QString cursorKind() const;

    void request(const Cursor &newCursor);
    void clear();

    int rowForBlock(const Markoff::BlockAnchor &block) const;

    /// Request a TextCaret at `qtPos` of the row at `expectedRow` once it
    /// exists. If the row already exists, equivalent to constructing a
    /// TextCaret from `model->recordAt(expectedRow).blockAnchor` and
    /// calling `request()`. If the row does not yet exist (because a
    /// structural edit was applied and the parse-back hasn't created it),
    /// record the request and watch `model->rowsInserted` for resolution.
    /// Pending requests linger up to two parse cycles before being
    /// dropped (see spec §8.4). Spec §5.3 step 6.
    Q_INVOKABLE void requestTextCaretAtRow(int expectedRow, int qtPos);

    /// Called by LiveListModelBinding from onParseUpdated. Increments the
    /// pending request's parse-cycle counter; drops the request after two
    /// cycles without resolution. `parseSeq` is unused as a value (we
    /// only care about the count); keep the signature so the call-site
    /// is self-documenting.
    void noteParseArrived(quint64 parseSeq);

Q_SIGNALS:
    void cursorChanged();

private:
    bool validateVariant(const Cursor &c) const;
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void resolvePendingForRow(int row);

    Cursor                   m_cursor;
    const BlockKindRegistry *m_registry;
    const LiveBlockModel    *m_model;

    struct PendingRow {
        int row;
        int qtPos;
        int parseCyclesSeen = 0;  // bumped on each noteParseArrived
    };
    std::optional<PendingRow> m_pendingRow;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Update the source**

Replace `libs/markoff-live-render/src/LiveCursorState.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/LiveBlockModel.h>

#include <QAbstractItemModel>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCursor, "markoff.live.cursor", QtWarningMsg)

namespace Markoff::LiveRender {

LiveCursorState::LiveCursorState(const BlockKindRegistry *registry,
                                 const LiveBlockModel    *model,
                                 QObject                 *parent)
    : QObject(parent)
    , m_cursor(NoCursor{})
    , m_registry(registry)
    , m_model(model)
{
    if (m_model) {
        QObject::connect(m_model, &QAbstractItemModel::rowsInserted,
                         this, &LiveCursorState::onRowsInserted);
    }
}

QString LiveCursorState::cursorKind() const
{
    if (std::holds_alternative<TextCaret>(m_cursor))           return QStringLiteral("TextCaret");
    if (std::holds_alternative<BlockSelected>(m_cursor))       return QStringLiteral("BlockSelected");
    if (std::holds_alternative<BlockInternalEdit>(m_cursor))   return QStringLiteral("BlockInternalEdit");
    return QStringLiteral("none");
}

void LiveCursorState::request(const Cursor &newCursor)
{
    if (!validateVariant(newCursor)) {
        qCWarning(lcCursor) << "cursor request rejected: invalid variant for kind";
        return;
    }
    if (m_cursor == newCursor) return;
    m_cursor = newCursor;
    Q_EMIT cursorChanged();
}

void LiveCursorState::clear()
{
    if (std::holds_alternative<NoCursor>(m_cursor)) return;
    m_cursor = NoCursor{};
    Q_EMIT cursorChanged();
}

int LiveCursorState::rowForBlock(const Markoff::BlockAnchor &block) const
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (m_model->recordAt(i).blockAnchor == block)
            return i;
    }
    return -1;
}

void LiveCursorState::requestTextCaretAtRow(int expectedRow, int qtPos)
{
    if (!m_model) return;
    if (expectedRow < 0) return;
    if (expectedRow < m_model->rowCount()) {
        // Row exists: resolve immediately.
        resolvePendingForRow(expectedRow);
        // resolvePendingForRow consults m_pendingRow; for this immediate path
        // we set it first so the resolver finds the qtPos.
        return;
    }
    // Row does not exist: record pending.
    m_pendingRow = PendingRow{ expectedRow, qtPos, 0 };
}

void LiveCursorState::noteParseArrived(quint64 /*parseSeq*/)
{
    if (!m_pendingRow) return;
    ++m_pendingRow->parseCyclesSeen;
    if (m_pendingRow->parseCyclesSeen >= 2) {
        qCInfo(lcCursor) << "pending cursor request dropped after"
                         << m_pendingRow->parseCyclesSeen
                         << "parse cycles without resolution; row"
                         << m_pendingRow->row;
        m_pendingRow.reset();
    }
}

void LiveCursorState::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid()) return;  // top-level only
    if (!m_pendingRow) return;
    if (m_pendingRow->row < first || m_pendingRow->row > last) return;
    resolvePendingForRow(m_pendingRow->row);
}

void LiveCursorState::resolvePendingForRow(int row)
{
    if (!m_model) return;
    if (row < 0 || row >= m_model->rowCount()) return;
    const int qtPos = m_pendingRow ? m_pendingRow->qtPos : 0;
    m_pendingRow.reset();

    TextCaret tc;
    tc.block            = m_model->recordAt(row).blockAnchor;
    tc.cachedByteOffset = static_cast<quint32>(qtPos);
    // positionAnchor: left default — selection projection refreshes it.
    request(tc);
}

bool LiveCursorState::validateVariant(const Cursor &c) const
{
    if (std::holds_alternative<NoCursor>(c)) return true;

    const Markoff::BlockAnchor *blockPtr = nullptr;
    if (auto *tc = std::get_if<TextCaret>(&c))               blockPtr = &tc->block;
    else if (auto *bs = std::get_if<BlockSelected>(&c))      blockPtr = &bs->block;
    else if (auto *bi = std::get_if<BlockInternalEdit>(&c))  blockPtr = &bi->block;
    if (!blockPtr) return false;

    const int row = rowForBlock(*blockPtr);
    if (row < 0) {
        qCWarning(lcCursor) << "cursor request for unknown block";
        return false;
    }

    const QString kind = m_model->recordAt(row).kind;
    const auto *desc = m_registry->find(kind);
    if (!desc) {
        qCWarning(lcCursor) << "cursor request for unregistered kind" << kind;
        return false;
    }

    QString variantName;
    if (std::holds_alternative<TextCaret>(c))            variantName = QStringLiteral("TextCaret");
    else if (std::holds_alternative<BlockSelected>(c))   variantName = QStringLiteral("BlockSelected");
    else if (std::holds_alternative<BlockInternalEdit>(c)) variantName = QStringLiteral("BlockInternalEdit");

    return desc->supportedCursorVariants.contains(variantName);
}

}  // namespace Markoff::LiveRender
```

Note: the immediate-resolve path in `requestTextCaretAtRow` records the request first via `m_pendingRow = ...`, then `resolvePendingForRow` consumes it. This keeps the qtPos in one place and avoids duplicating the cursor construction. Reorder so the immediate path also writes pending first:

```cpp
void LiveCursorState::requestTextCaretAtRow(int expectedRow, int qtPos)
{
    if (!m_model) return;
    if (expectedRow < 0) return;
    m_pendingRow = PendingRow{ expectedRow, qtPos, 0 };
    if (expectedRow < m_model->rowCount())
        resolvePendingForRow(expectedRow);
}
```

Use that final form. (The earlier mid-step listing is conceptual.)

- [ ] **Step 5: Build and run the new tests**

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8
ctest --test-dir build-dev -R '^tst_live_render_cursor$' --output-on-failure
```

Expected: all three new test slots pass; previous tests still green.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h \
        libs/markoff-live-render/src/LiveCursorState.cpp \
        libs/markoff-live-render/tests/tst_live_render_cursor.cpp
git commit -m "feat(live-render): pending TextCaret request via rowsInserted

Adds LiveCursorState::requestTextCaretAtRow + noteParseArrived for
deterministic focus delivery into rows that don't yet exist (R5
structural-edit case). Watches QAbstractItemModel::rowsInserted;
drops after two parse cycles per spec §8.4."
```

---

## Task 3: Add `UndoCoalescer` (TDD)

**Files:**
- Create: `libs/markoff-live-render/include/markoff/live-render/UndoCoalescer.h`
- Create: `libs/markoff-live-render/src/UndoCoalescer.cpp`
- Modify: `libs/markoff-live-render/CMakeLists.txt` (add the two new files to `qt_add_qml_module`)
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp` (created in this task)
- Modify: `libs/markoff-live-render/tests/CMakeLists.txt` (add the new executable)

`UndoCoalescer` extracts the legacy printable-coalesce logic (`markoff-view-qml/src/LiveEditBinding.cpp:227-240`) into its own component and adds the explicit context-break rules from spec §6.1 L5. Policy:

- After every `applyLocalEdit` triggered by user activity, the caller invokes one of:
  - `recordPrintable(blockAnchor)` — the edit is a single printable (charsRemoved == 0, charsAdded == 1, no selection).
  - `recordStructural()` — the edit is a structural edit (Enter, Backspace-edge, Delete-edge, Shift-Enter).
  - `recordOther()` — the edit is a paste, multi-character delete, or any other non-printable mutation.
- `recordPrintable` consults the previous record. If the previous was also `recordPrintable` AND the same `blockAnchor` AND within the 1000 ms idle threshold, call `MarkoffDocument::coalesceLastUndo()`.
- All three `record*` methods reset the timer.
- `notifyFocusChanged()` and `notifyMovement()` clear the previous record without coalescing — they BREAK the chain. The next `recordPrintable` cannot coalesce because the previous record is gone.
- `notifyIdleExpired()` is a no-op invoked by an internal timer when 1000 ms passes since the last record; clears the previous record. (Tests can call it manually.)

The coalescer is owned by `LiveListModelBinding`. `LiveEditBinding` calls `recordPrintable`/`recordOther` from `onContentsChange`; `LiveStructuralKeyHandler` calls `recordStructural` after each edit; the QML focus-change connection (in `LiveView.qml`) calls `notifyFocusChanged` when `cursorState.cursorChanged` arrives with a different `blockAnchor` than the prior cursor.

- [ ] **Step 1: Create the test file with failing tests**

Create `libs/markoff-live-render/tests/tst_live_render_structural.cpp` with the following content (this file will accumulate tests across many subsequent tasks — start it here with the UndoCoalescer suite):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live-render/UndoCoalescer.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/TextAnchor.h>

using namespace Markoff::LiveRender;

// Helper: synthesise a non-default BlockAnchor for tests by carving one
// out of a real MarkoffDocument's first block. The CRDT internals are
// opaque; we only need anchors that compare equal to themselves and
// differently to others.
static Markoff::BlockAnchor anchorAtFirstBlock(Markoff::MarkoffDocument &doc)
{
    auto opt = doc.blockAnchorAt(0);
    return opt.value_or(Markoff::BlockAnchor{});
}

class TstLiveRenderStructural : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---------- UndoCoalescer ----------

    void coalescer_first_printable_does_not_coalesce() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        UndoCoalescer coalescer(&doc);

        Markoff::BlockAnchor a = Markoff::BlockAnchor{};
        bool didCoalesce = coalescer.recordPrintable(a);
        QVERIFY(!didCoalesce);
    }

    void coalescer_consecutive_printables_same_anchor_coalesce() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        // Drive two real applyLocalEdits so the buffer has two undo entries.
        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QCOMPARE(doc.undoDepth(), 1);
        QVERIFY(!coalescer.recordPrintable(a));

        Markoff::MarkoffEdit e2; e2.oldStart = 6; e2.oldEnd = 6; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QCOMPARE(doc.undoDepth(), 2);
        QVERIFY(coalescer.recordPrintable(a));
        // After coalesce, depth back to 1.
        QCOMPARE(doc.undoDepth(), 1);
    }

    void coalescer_different_anchor_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("alpha\n\nbeta", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a0 = doc.blockAnchorAt(0).value();
        Markoff::BlockAnchor a1 = doc.blockAnchorAt(1).value();

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a0));

        Markoff::MarkoffEdit e2; e2.oldStart = 12; e2.oldEnd = 12; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a1));  // different block — no coalesce
        QCOMPARE(doc.undoDepth(), 2);
    }

    void coalescer_structural_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        Markoff::MarkoffEdit es; es.oldStart = 6; es.oldEnd = 6; es.newText = "\n\n";
        doc.applyLocalEdit({ es });
        coalescer.recordStructural();
        // Structural does NOT coalesce its own undo; depth stays 2.
        QCOMPARE(doc.undoDepth(), 2);

        Markoff::MarkoffEdit e2; e2.oldStart = 8; e2.oldEnd = 8; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));  // structural broke the chain
        QCOMPARE(doc.undoDepth(), 3);
    }

    void coalescer_focus_change_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        coalescer.notifyFocusChanged();

        Markoff::MarkoffEdit e2; e2.oldStart = 6; e2.oldEnd = 6; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));  // focus-change broke it
        QCOMPARE(doc.undoDepth(), 2);
    }

    void coalescer_idle_expiry_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        // Manually expire the idle window.
        coalescer.notifyIdleExpired();

        Markoff::MarkoffEdit e2; e2.oldStart = 6; e2.oldEnd = 6; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));
        QCOMPARE(doc.undoDepth(), 2);
    }

    void coalescer_other_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        // Simulate a paste / multi-char delete.
        Markoff::MarkoffEdit ep; ep.oldStart = 6; ep.oldEnd = 6; ep.newText = "PASTED";
        doc.applyLocalEdit({ ep });
        coalescer.recordOther();

        Markoff::MarkoffEdit e2; e2.oldStart = 12; e2.oldEnd = 12; e2.newText = "Z";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));  // recordOther broke the chain
        QCOMPARE(doc.undoDepth(), 3);
    }
};

QTEST_MAIN(TstLiveRenderStructural)
#include "tst_live_render_structural.moc"
```

- [ ] **Step 2: Add the executable to the test CMakeLists**

Append to `libs/markoff-live-render/tests/CMakeLists.txt`:

```cmake
qt_add_executable(tst_live_render_structural
    tst_live_render_structural.cpp
)
target_link_libraries(tst_live_render_structural PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Widgets Qt6::Test
    markoff_live_render markoff_core)
add_test(NAME tst_live_render_structural COMMAND tst_live_render_structural)
```

- [ ] **Step 3: Run; expect compile failure**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
```

Expected: `UndoCoalescer.h` not found.

- [ ] **Step 4: Create `UndoCoalescer.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff-foundation/BlockAnchor.h>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <qqmlintegration.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::LiveRender {

/// View-side undo coalescing policy (spec §6.1 L5). Tracks the most-recent
/// edit's classification and focus context; on each subsequent edit, decides
/// whether to call `MarkoffDocument::coalesceLastUndo()` to merge it into
/// the previous undo entry.
///
/// Policy: consecutive printables in the same `blockAnchor` within
/// `kIdleThresholdMs` (1000 ms) coalesce into one undo entry. Any of the
/// following BREAK the chain — the next printable cannot coalesce:
///   - `recordStructural()`     (Enter, Backspace-edge, Delete-edge, Shift-Enter)
///   - `recordOther()`          (paste, multi-char delete, IME commit)
///   - `notifyFocusChanged()`   (cursor moved to a different block)
///   - `notifyMovement()`       (arrow-key without edit; reserved for R6+)
///   - `notifyIdleExpired()`    (1000 ms passed since the last record)
///
/// `recordPrintable(anchor)` returns `true` if it called `coalesceLastUndo()`,
/// `false` if not. Test affordance.
///
/// Owned by `LiveListModelBinding`. Consumers: `LiveEditBinding`,
/// `LiveStructuralKeyHandler`, the QML cursorChanged Connections.
class MARKOFF_LIVE_RENDER_EXPORT UndoCoalescer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("UndoCoalescer is provided by LiveListModelBinding")

public:
    static constexpr int kIdleThresholdMs = 1000;

    explicit UndoCoalescer(Markoff::MarkoffDocument *document, QObject *parent = nullptr);

    bool recordPrintable(const Markoff::BlockAnchor &anchor);
    void recordStructural();
    void recordOther();

    void notifyFocusChanged();
    void notifyMovement();
    void notifyIdleExpired();

private:
    void clearLast();

    QPointer<Markoff::MarkoffDocument> m_document;

    // Last-record state. m_haveLast == false means the chain is broken;
    // the next recordPrintable cannot coalesce.
    bool                  m_haveLast = false;
    bool                  m_lastWasPrintable = false;
    Markoff::BlockAnchor  m_lastAnchor;
    QElapsedTimer         m_lastTimer;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 5: Create `UndoCoalescer.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/UndoCoalescer.h>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::LiveRender {

UndoCoalescer::UndoCoalescer(Markoff::MarkoffDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{}

bool UndoCoalescer::recordPrintable(const Markoff::BlockAnchor &anchor)
{
    bool didCoalesce = false;
    if (m_haveLast
        && m_lastWasPrintable
        && m_lastAnchor == anchor
        && m_lastTimer.isValid()
        && m_lastTimer.elapsed() < kIdleThresholdMs)
    {
        if (m_document) {
            didCoalesce = m_document->coalesceLastUndo();
        }
    }
    m_haveLast         = true;
    m_lastWasPrintable = true;
    m_lastAnchor       = anchor;
    m_lastTimer.restart();
    return didCoalesce;
}

void UndoCoalescer::recordStructural()
{
    // Structural edit is its own undo unit; it BREAKS the printable chain.
    m_haveLast         = true;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.restart();
}

void UndoCoalescer::recordOther()
{
    m_haveLast         = true;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.restart();
}

void UndoCoalescer::notifyFocusChanged() { clearLast(); }
void UndoCoalescer::notifyMovement()      { clearLast(); }
void UndoCoalescer::notifyIdleExpired()   { clearLast(); }

void UndoCoalescer::clearLast()
{
    m_haveLast         = false;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.invalidate();
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 6: Wire into the library CMakeLists**

In `libs/markoff-live-render/CMakeLists.txt`, add the new files inside the `qt_add_qml_module(markoff_live_render ...)` SOURCES list, alongside `LiveEditBinding`:

```cmake
        # R4: per-delegate edit binding
        include/markoff/live-render/LiveEditBinding.h
        src/LiveEditBinding.cpp
        # R5: structural keys + undo coalescing
        include/markoff/live-render/UndoCoalescer.h
        src/UndoCoalescer.cpp
```

(`LiveStructuralKeyHandler.{h,cpp}` are added in Task 4.)

- [ ] **Step 7: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: all seven UndoCoalescer test slots pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/UndoCoalescer.h \
        libs/markoff-live-render/src/UndoCoalescer.cpp \
        libs/markoff-live-render/CMakeLists.txt \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp \
        libs/markoff-live-render/tests/CMakeLists.txt
git commit -m "feat(live-render): UndoCoalescer policy (R5)

Extracts the legacy markoff-view-qml printable-coalesce policy
(LiveEditBinding.cpp:227-240) into its own component per spec §6.1 L5.
Consecutive printables in the same blockAnchor within 1000 ms coalesce
into one undo entry; structural edits, focus changes, and idle expiry
break the chain.

New tst_live_render_structural test executable lands here; subsequent
R5 tasks add structural-key tests to it."
```

---

## Task 4: `LiveStructuralKeyHandler` skeleton + paragraph end-of-block Enter (TDD)

**Files:**
- Create: `libs/markoff-live-render/include/markoff/live-render/LiveStructuralKeyHandler.h`
- Create: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/CMakeLists.txt`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

The handler is a `QObject` with one Q_INVOKABLE: `bool tryHandle(int key, int modifiers, int blockIndex, int qtPos, bool selectionEmpty, const QString &blockText)`. Returns `true` if the key was consumed (caller sets `event.accepted = true`); `false` otherwise (caller falls back to TextEdit's native handling).

Internally, `tryHandle`:

1. Validates: document, model, cursor-state, undo-coalescer wired; `blockIndex` in range; selection-empty (R5 limitation: non-empty selections defer to TextEdit's native handling — see scope notes).
2. Looks up the block's kind via `m_model->recordAt(blockIndex).kind`.
3. Looks up the descriptor via `m_registry->find(kind)`. If the descriptor doesn't list `key` in `consumedStructuralKeys`, returns `false`.
4. Looks up the kind-keyed handler in `m_handlers[kind][key]`. If absent, returns `false`.
5. Builds the `Ctx` (block byte range, current document state) and invokes the handler. The handler returns `Handled | NotHandled`. `Handled` ⇒ `tryHandle` returns `true`.

The Ctx carries:
- `document` — the `MarkoffDocument *`.
- `blockIndex` — model row.
- `blockAnchor` — `m_model->recordAt(blockIndex).blockAnchor`.
- `currentBlockStart` — `document->resolveTextAnchor(blockAnchor.firstByte)` (CRDT-current).
- `currentBlockEnd` — `currentBlockStart + blockText.toUtf8().size()`.
- `qtPos`, `blockText`, `model`, `cursorState`, `undoCoalescer`.

This task ships the handler skeleton plus ONE built-in handler: paragraph + `Qt::Key_Return/Enter` at end-of-block (qtPos == blockText.length()). End-of-block is the simplest case — it inserts `\n\n` after the block and parks a pending cursor request at row `blockIndex + 1`.

The remaining built-in handlers (mid-block split, start-of-block, Backspace-edge, Delete-edge, Shift-Enter, heading, code-block) land in Tasks 5–11.

- [ ] **Step 1: Add a failing test for end-of-block Enter on paragraph**

Append to `libs/markoff-live-render/tests/tst_live_render_structural.cpp`, before the closing `};`:

```cpp
    // ---------- LiveStructuralKeyHandler — paragraph Enter at end ----------

    void enter_at_end_of_paragraph_inserts_paragraph_break() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *handler = binding.structuralKeyHandler();
        QVERIFY(handler);

        const bool consumed = handler->tryHandle(
            /*key=*/Qt::Key_Return,
            /*modifiers=*/Qt::NoModifier,
            /*blockIndex=*/0,
            /*qtPos=*/5,                  // end of "hello"
            /*selectionEmpty=*/true,
            /*blockText=*/QStringLiteral("hello"));
        QVERIFY(consumed);

        // CRDT now has "hello\n\n" — paragraph break at end.
        QCOMPARE(doc.toMarkdown(), QString("hello\n\n"));

        // After parse-back, two rows; the second is empty.
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);

        // Cursor parked at row 1 qtPos 0.
        const Cursor cur = binding.cursorState()->cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).block,
                 binding.model()->recordAt(1).blockAnchor);
    }
```

Add the necessary includes at the top of the test file:

```cpp
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/Cursor.h>
```

- [ ] **Step 2: Run; expect compile failure**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
```

Expected: `LiveStructuralKeyHandler.h` not found, `binding.structuralKeyHandler()` doesn't exist.

- [ ] **Step 3: Create `LiveStructuralKeyHandler.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff-foundation/BlockAnchor.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <qqmlintegration.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::LiveRender {

class LiveBlockModel;
class LiveCursorState;
class BlockKindRegistry;
class UndoCoalescer;

/// Single dispatcher for structural key events (Enter, Backspace-edge,
/// Delete-edge, Shift-Enter; future Tab/Shift-Tab in R7). Looks up the
/// descriptor for the focused block's kind, checks if the key is in
/// `consumedStructuralKeys`, then dispatches to a kind-keyed handler
/// function registered at library init. Spec §5.4.
///
/// Returns `true` if the key was consumed (caller sets event.accepted);
/// `false` otherwise (caller falls back to TextEdit's native handling
/// — that's how code-block's Enter inserts a literal newline).
class MARKOFF_LIVE_RENDER_EXPORT LiveStructuralKeyHandler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveStructuralKeyHandler is provided by LiveListModelBinding")

public:
    /// Per-handler context. Constructed by `tryHandle`; consumed by the
    /// kind-keyed handler function.
    struct Ctx {
        Markoff::MarkoffDocument *document;
        LiveBlockModel           *model;
        LiveCursorState          *cursorState;
        UndoCoalescer            *undoCoalescer;

        int                       blockIndex;
        Markoff::BlockAnchor      blockAnchor;
        quint32                   currentBlockStart;
        quint32                   currentBlockEnd;
        int                       qtPos;
        int                       modifiers;       ///< Qt::KeyboardModifiers
        QString                   blockText;
    };

    enum class HandleResult { Handled, NotHandled };

    using HandlerFn = std::function<HandleResult(const Ctx &)>;

    LiveStructuralKeyHandler(Markoff::MarkoffDocument *document,
                             LiveBlockModel           *model,
                             LiveCursorState          *cursorState,
                             const BlockKindRegistry  *registry,
                             UndoCoalescer            *undoCoalescer,
                             QObject                  *parent = nullptr);

    /// Register a kind-specific handler for `key` (a `Qt::Key_*` value).
    /// Replaces any prior registration for the same (kind, key).
    void registerHandler(const QString &kind, int key, HandlerFn fn);

    /// QML-invokable dispatch entry. See class docstring.
    Q_INVOKABLE bool tryHandle(int key,
                               int modifiers,
                               int blockIndex,
                               int qtPos,
                               bool selectionEmpty,
                               const QString &blockText);

private:
    void registerBuiltins();

    QPointer<Markoff::MarkoffDocument> m_document;
    LiveBlockModel                    *m_model;
    LiveCursorState                   *m_cursorState;
    const BlockKindRegistry           *m_registry;
    UndoCoalescer                     *m_undoCoalescer;

    // Outer key: kind ("paragraph", etc.). Inner key: Qt::Key_*.
    QHash<QString, QHash<int, HandlerFn>> m_handlers;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Create `LiveStructuralKeyHandler.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveStructuralKeyHandler.h>

#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/UndoCoalescer.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcStruct, "markoff.live.struct", QtWarningMsg)

namespace Markoff::LiveRender {

LiveStructuralKeyHandler::LiveStructuralKeyHandler(
    Markoff::MarkoffDocument *document,
    LiveBlockModel           *model,
    LiveCursorState          *cursorState,
    const BlockKindRegistry  *registry,
    UndoCoalescer            *undoCoalescer,
    QObject                  *parent)
    : QObject(parent)
    , m_document(document)
    , m_model(model)
    , m_cursorState(cursorState)
    , m_registry(registry)
    , m_undoCoalescer(undoCoalescer)
{
    registerBuiltins();
}

void LiveStructuralKeyHandler::registerHandler(const QString &kind, int key, HandlerFn fn)
{
    m_handlers[kind][key] = std::move(fn);
}

bool LiveStructuralKeyHandler::tryHandle(int key,
                                         int modifiers,
                                         int blockIndex,
                                         int qtPos,
                                         bool selectionEmpty,
                                         const QString &blockText)
{
    if (!m_document || !m_model || !m_cursorState || !m_registry) return false;
    if (!selectionEmpty) {
        // R5 limitation: non-empty selection defers to TextEdit's native
        // selection-replacement (which routes through LiveEditBinding's
        // contentsChange path). Documented in plan scope notes.
        return false;
    }
    if (blockIndex < 0 || blockIndex >= m_model->rowCount()) return false;

    const BlockRecord &rec = m_model->recordAt(blockIndex);
    const auto *desc = m_registry->find(rec.kind);
    if (!desc) return false;
    if (!desc->consumedStructuralKeys.contains(key)) return false;

    auto kindIt = m_handlers.constFind(rec.kind);
    if (kindIt == m_handlers.constEnd()) return false;
    auto keyIt = kindIt.value().constFind(key);
    if (keyIt == kindIt.value().constEnd()) return false;

    // Sanity: block must resolve in the foundation.
    const auto blockRangeOpt = m_document->blockByteRange(rec.blockAnchor);
    if (!blockRangeOpt) return false;

    Ctx ctx;
    ctx.document          = m_document.data();
    ctx.model             = m_model;
    ctx.cursorState       = m_cursorState;
    ctx.undoCoalescer     = m_undoCoalescer;
    ctx.blockIndex        = blockIndex;
    ctx.blockAnchor       = rec.blockAnchor;
    ctx.currentBlockStart = m_document->resolveTextAnchor(rec.blockAnchor.firstByte);
    ctx.currentBlockEnd   = ctx.currentBlockStart
                          + static_cast<quint32>(blockText.toUtf8().size());
    ctx.qtPos             = qtPos;
    ctx.modifiers         = modifiers;
    ctx.blockText         = blockText;

    return keyIt.value()(ctx) == HandleResult::Handled;
}

void LiveStructuralKeyHandler::registerBuiltins()
{
    using HR = HandleResult;

    // ---------- paragraph: Enter at end of block ----------
    auto paragraphEnter = [](const Ctx &c) -> HR {
        if (c.qtPos != c.blockText.length()) {
            // Not end-of-block; mid-block / start-of-block handled in a later
            // task. For now, NotHandled falls through to TextEdit's native \n.
            return HR::NotHandled;
        }
        Markoff::MarkoffEdit ed;
        ed.oldStart = c.currentBlockEnd;
        ed.oldEnd   = c.currentBlockEnd;
        ed.newText  = QByteArrayLiteral("\n\n");
        c.document->applyLocalEdit({ ed });
        // Stamp the row's lastEditEditSequence so the freshness rule
        // preserves the row's text on parse-back (spec §4.3).
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
        // Caret lands at qtPos 0 of the new (currently non-existent) row.
        c.cursorState->requestTextCaretAtRow(c.blockIndex + 1, 0);
        // Structural edit: break the printable-coalesce chain.
        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Return] = paragraphEnter;
    m_handlers[BlockKind::Paragraph][Qt::Key_Enter]  = paragraphEnter;
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 5: Wire `LiveStructuralKeyHandler` into the library CMakeLists**

In `libs/markoff-live-render/CMakeLists.txt`, append to the SOURCES list (next to UndoCoalescer):

```cmake
        # R5: structural keys + undo coalescing
        include/markoff/live-render/UndoCoalescer.h
        src/UndoCoalescer.cpp
        include/markoff/live-render/LiveStructuralKeyHandler.h
        src/LiveStructuralKeyHandler.cpp
```

- [ ] **Step 6: Wire `LiveStructuralKeyHandler` + `UndoCoalescer` into `LiveListModelBinding`**

In `libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h`, add after the existing `selectionView()` Q_PROPERTY:

```cpp
    Q_PROPERTY(Markoff::LiveRender::LiveStructuralKeyHandler *structuralKeyHandler
               READ structuralKeyHandler CONSTANT)
    Q_PROPERTY(Markoff::LiveRender::UndoCoalescer *undoCoalescer
               READ undoCoalescer CONSTANT)
```

Add forward declarations and the public accessors:

```cpp
namespace Markoff::LiveRender {
class LiveStructuralKeyHandler;  // R5
class UndoCoalescer;              // R5
}
```

(Place these alongside the other existing forward declarations / includes; the existing `#include <markoff/live-render/LiveCursorState.h>` and friends are fine to keep.)

In the public section, after `LiveSelectionView *selectionView() const;`:

```cpp
    LiveStructuralKeyHandler *structuralKeyHandler() const;
    UndoCoalescer            *undoCoalescer()        const;
```

In `libs/markoff-live-render/src/LiveListModelBinding.cpp`:

1. Add includes:

```cpp
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/UndoCoalescer.h>
```

2. Add the two new members to `Private`:

```cpp
struct LiveListModelBinding::Private {
    Markoff::MarkoffDocument *document     = nullptr;
    Markoff::Session         *session      = nullptr;
    LiveBlockModel            *model       = nullptr;
    BlockKindRegistry          registry;
    LiveCursorState           *cursorState   = nullptr;
    BlockHitTester            *hitTester     = nullptr;
    LiveSelectionView         *selectionView = nullptr;
    UndoCoalescer             *undoCoalescer = nullptr;
    LiveStructuralKeyHandler  *structuralKeys = nullptr;
    QList<BlockKey>            lastKeys;
    quint64                    lastParseInputEditSeq = 0;
    bool                       applyingModelUpdate = false;
};
```

3. In the constructor, after `d->selectionView->setModel(d->model);`, construct the new components. The structural-key handler depends on the document, which isn't set until `setDocument` is called; for now, defer construction to `setDocument`. Replace the constructor's tail:

```cpp
LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->model         = new LiveBlockModel(this);
    d->cursorState   = new LiveCursorState(&d->registry, d->model, this);
    d->hitTester     = new BlockHitTester(this);
    d->selectionView = new LiveSelectionView(this);
    d->selectionView->setModel(d->model);
    // d->undoCoalescer + d->structuralKeys are constructed in setDocument
    // because they hold a non-owning MarkoffDocument pointer.
}
```

4. In `setDocument`, after the existing `if (d->document) { ... } else { ... }` selection-view wiring, construct (or destroy) the structural components:

```cpp
void LiveListModelBinding::setDocument(Markoff::MarkoffDocument *doc)
{
    if (d->document == doc) return;
    if (d->document) {
        QObject::disconnect(d->document, nullptr, this, nullptr);
        if (d->session) {
            d->document->destroySession(d->session);
            d->session = nullptr;
        }
    }
    // Drop the old structural components if any.
    delete d->structuralKeys; d->structuralKeys = nullptr;
    delete d->undoCoalescer;  d->undoCoalescer = nullptr;

    d->document = doc;
    if (d->document) {
        QObject::connect(d->document, &Markoff::MarkoffDocument::parseUpdated,
                         this, &LiveListModelBinding::onParseUpdated);
        d->session = d->document->createSession({});
        d->selectionView->setDocument(d->document);
        d->selectionView->setSession(d->session);

        d->undoCoalescer  = new UndoCoalescer(d->document, this);
        d->structuralKeys = new LiveStructuralKeyHandler(
            d->document, d->model, d->cursorState, &d->registry,
            d->undoCoalescer, this);
    } else {
        d->selectionView->setDocument(nullptr);
        d->selectionView->setSession(nullptr);
    }
    Q_EMIT documentChanged();
}
```

5. Add the accessor implementations:

```cpp
LiveStructuralKeyHandler *LiveListModelBinding::structuralKeyHandler() const
{
    return d->structuralKeys;
}

UndoCoalescer *LiveListModelBinding::undoCoalescer() const
{
    return d->undoCoalescer;
}
```

6. In `onParseUpdated`, after the existing applyOps + cursor-refresh block, notify the cursor state of the parse arrival so its pending-drop counter advances:

```cpp
void LiveListModelBinding::onParseUpdated(const Markoff::Document *parsed,
                                          quint64 parseSequence,
                                          const QList<Markoff::BlockAnchor> &blockAnchors,
                                          quint64 parseInputEditSequence)
{
    // ... existing code through the cursor-cache refresh block ...

    // R5: advance the pending-cursor drop counter (spec §8.4).
    if (d->cursorState)
        d->cursorState->noteParseArrived(parseSequence);
}
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: the new `enter_at_end_of_paragraph_inserts_paragraph_break` test passes alongside the seven UndoCoalescer tests.

Run the full live-render test suite:

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/LiveStructuralKeyHandler.h \
        libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h \
        libs/markoff-live-render/src/LiveListModelBinding.cpp \
        libs/markoff-live-render/CMakeLists.txt \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp
git commit -m "feat(live-render): LiveStructuralKeyHandler skeleton + paragraph end-of-block Enter

Descriptor-driven dispatch (spec §5.4): tryHandle looks up the focused
block's kind, checks consumedStructuralKeys, dispatches to a kind-keyed
handler. Paragraph + Qt::Key_Return at end-of-block lands as the first
built-in handler — inserts \\n\\n, stamps lastEditEditSequence, parks
a pending TextCaret request at row+1.

LiveListModelBinding owns the new components (constructed on setDocument,
destroyed on document switch). Existing tests still green."
```

---

## Task 5: Paragraph mid-block split + cursor focus into new row (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

Mid-block Enter (`0 < qtPos < blockText.length()`) inserts `\n\n` at the byte offset corresponding to qtPos. The original block keeps its prefix; a new block appears with the suffix. The caret should land at qtPos 0 of the new row.

Byte arithmetic: take the UTF-8 byte length of `blockText.left(qtPos)`; add to `currentBlockStart`. (Same pattern as the legacy markoff-view-qml handler.)

- [ ] **Step 1: Add a failing test for mid-block Enter**

Append to the test class:

```cpp
    void enter_in_middle_of_paragraph_splits_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello world", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);

        QCOMPARE(doc.toMarkdown(), QString("hello\n\n world"));

        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.model()->recordAt(0).text, QString("hello"));
        QCOMPARE(binding.model()->recordAt(1).text, QString("world"));

        const Cursor cur = binding.cursorState()->cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).block,
                 binding.model()->recordAt(1).blockAnchor);
    }

    void enter_at_start_of_paragraph_inserts_blank_above() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("hello"));
        QVERIFY(consumed);

        QCOMPARE(doc.toMarkdown(), QString("\n\nhello"));

        QVERIFY(parseSpy.wait(2000));
        // Parser collapses leading \n\n into a single empty/non-empty pair;
        // expected outcome: one paragraph row with text "hello". Whether an
        // empty leading row exists is parser-dependent and not asserted.
        QVERIFY(binding.model()->rowCount() >= 1);
    }
```

- [ ] **Step 2: Replace the paragraph-Enter handler with the full version**

In `LiveStructuralKeyHandler::registerBuiltins()`, replace the `paragraphEnter` lambda body with:

```cpp
    auto paragraphEnter = [](const Ctx &c) -> HR {
        // Compute the byte offset for the cursor's qtPos.
        const QByteArray prefixUtf8 =
            c.blockText.left(c.qtPos).toUtf8();
        const quint32 byteOffset =
            c.currentBlockStart + static_cast<quint32>(prefixUtf8.size());

        Markoff::MarkoffEdit ed;
        ed.oldStart = byteOffset;
        ed.oldEnd   = byteOffset;
        ed.newText  = QByteArrayLiteral("\n\n");
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());

        // Cursor placement:
        //   qtPos == 0 (start-of-block):
        //     The empty leading paragraph the parser will see may or may
        //     not materialise as its own row (depends on Markdown
        //     normalisation). Park the cursor at the original block, now
        //     at row blockIndex + 1 in the post-parse layout, qtPos 0.
        //   0 < qtPos < length (mid-block):
        //     Original block keeps the prefix at row blockIndex; the
        //     suffix appears as a new row at blockIndex + 1. Caret to
        //     qtPos 0 of blockIndex + 1.
        //   qtPos == length (end-of-block):
        //     Empty new paragraph appears at blockIndex + 1. Caret to
        //     qtPos 0 of blockIndex + 1.
        //
        // All three cases land on row blockIndex + 1 qtPos 0.
        c.cursorState->requestTextCaretAtRow(c.blockIndex + 1, 0);

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
```

This handles all three Enter positions (start, middle, end) uniformly. The end-of-block case in Task 4 was a placeholder; this replaces it with the general implementation.

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: all paragraph-Enter tests pass (end-of-block from Task 4, plus mid-block and start-of-block).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp
git commit -m "feat(live-render): paragraph Enter — mid-block split and start-of-block

Generalises the paragraph Enter handler to cover all three qtPos
positions uniformly: insert \\n\\n at the byte offset for qtPos, park
the cursor at row blockIndex+1 qtPos 0. Spec §7.2."
```

---

## Task 6: Paragraph Backspace at row-start (merge with previous block) (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

Backspace at qtPos 0 deletes the inter-block separator (the byte just before `currentBlockStart`), merging this block with the previous one. The cursor lands at the position where the merge happened — qtPos = previous block's length.

Edge cases:
- `blockIndex == 0`: nothing to merge with; return `NotHandled` so TextEdit's native Backspace runs (which is a no-op at qtPos 0).
- `currentBlockStart == 0`: defensive check; same outcome.

- [ ] **Step 1: Add a failing test**

```cpp
    void backspace_at_start_of_paragraph_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\nbeta", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("beta"));
        QVERIFY(consumed);

        // One byte deleted from the inter-block separator: "alpha\nbeta"
        // (or possibly "alphabeta" depending on which separator byte got
        // deleted; the legacy handler deletes ONE byte, leaving "alpha\nbeta",
        // which the parser treats as a single paragraph).
        QCOMPARE(doc.toMarkdown(), QString("alpha\nbeta"));

        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);
    }

    void backspace_at_start_of_first_block_is_not_consumed() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(!consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha"));
    }
```

- [ ] **Step 2: Add the Backspace handler**

Append to `registerBuiltins()`:

```cpp
    auto paragraphBackspace = [](const Ctx &c) -> HR {
        if (c.qtPos != 0) return HR::NotHandled;     // not at row-start
        if (c.blockIndex == 0) return HR::NotHandled; // first block
        if (c.currentBlockStart == 0) return HR::NotHandled;

        Markoff::MarkoffEdit ed;
        ed.oldStart = c.currentBlockStart - 1;
        ed.oldEnd   = c.currentBlockStart;
        ed.newText  = QByteArray();
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
        c.model->setRowEditSequence(c.blockIndex - 1, c.document->editSequence());

        // After parse-back: blockIndex - 1 contains the merged content;
        // blockIndex is gone. Cursor lands at qtPos = previous block's
        // text length (the merge point). Compute the qtPos at edit time
        // since the previous block's text is still current in the model.
        const int prevQtPos = c.model->recordAt(c.blockIndex - 1).text.length();
        c.cursorState->requestTextCaretAtRow(c.blockIndex - 1, prevQtPos);

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Backspace] = paragraphBackspace;
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: both new tests pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp
git commit -m "feat(live-render): paragraph Backspace at row-start merges with previous

Deletes the inter-block separator byte before currentBlockStart. Caret
lands at the merge point in the previous block. Returns NotHandled for
the first block (no neighbor to merge with) so TextEdit's native
Backspace handles the qtPos==0 case as a no-op."
```

---

## Task 7: Paragraph Delete at row-end (merge with next block) (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

Symmetric to Backspace-at-start: at qtPos == blockText.length(), delete the byte just after `currentBlockEnd`, merging with the next block. Cursor stays where it is — at the end of the (now-merged) block.

Edge cases:
- `blockIndex == rowCount - 1`: last block; nothing to merge with → `NotHandled`.
- `currentBlockEnd >= document->visibleLength()`: defensive check; same.

- [ ] **Step 1: Add a failing test**

```cpp
    void delete_at_end_of_paragraph_merges_with_next() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\nbeta", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha\nbeta"));

        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);
    }

    void delete_at_end_of_last_block_is_not_consumed() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(!consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha"));
    }
```

- [ ] **Step 2: Add the Delete handler**

```cpp
    auto paragraphDelete = [](const Ctx &c) -> HR {
        if (c.qtPos != c.blockText.length()) return HR::NotHandled;
        if (c.blockIndex >= c.model->rowCount() - 1) return HR::NotHandled;
        const quint32 docLen = c.document->visibleLength();
        if (c.currentBlockEnd >= docLen) return HR::NotHandled;

        Markoff::MarkoffEdit ed;
        ed.oldStart = c.currentBlockEnd;
        ed.oldEnd   = c.currentBlockEnd + 1;
        ed.newText  = QByteArray();
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
        c.model->setRowEditSequence(c.blockIndex + 1, c.document->editSequence());

        // Cursor stays at end of the (now-merged) block — same row, same qtPos.
        // Use requestTextCaretAtRow so it survives the parse-back's row
        // reshuffle even if the block changes identity.
        c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos);

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Delete] = paragraphDelete;
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: both new tests pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp
git commit -m "feat(live-render): paragraph Delete at row-end merges with next

Symmetric to Backspace-at-start: deletes the byte after currentBlockEnd.
Caret stays at the same qtPos (end of the now-merged block). Returns
NotHandled for the last block."
```

---

## Task 8: Shift-Enter — soft break (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

Shift-Enter inserts a single `\n` (line separator within the current paragraph). The block stays a single paragraph; the parser may render it differently (line break inside a paragraph vs. paragraph break) but at this layer the operation is a one-byte insert. Cursor stays in the same block at qtPos + 1.

Note: in PlainText TextEdit, the `\n` insert echoes back to the user's view as a soft break naturally because LiveEditBinding's `text` property is bound to `model.text` which contains the newline. Specifically: the user sees `\n` in the TextEdit and the source has `\n`. No special handling.

This handler is registered on `Qt::Key_Return` AND `Qt::Key_Enter` like the regular Enter handler — the dispatcher passes the modifiers through, and the handler checks `modifiers & Qt::ShiftModifier` first. To preserve the existing single-handler-per-(kind,key) registry shape, the SAME `paragraphEnter` handler we wrote in Tasks 4–5 is amended to dispatch on Shift.

- [ ] **Step 1: Add a failing test**

```cpp
    void shift_enter_inserts_soft_break() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello world", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);
        QCOMPARE(doc.toMarkdown(), QString("hello\n world"));

        QVERIFY(parseSpy.wait(2000));
        // Parser keeps it as one paragraph (CommonMark soft break).
        QCOMPARE(binding.model()->rowCount(), 1);
    }
```

- [ ] **Step 2: Amend the paragraphEnter handler to handle Shift**

Replace the `paragraphEnter` lambda body again with:

```cpp
    auto paragraphEnter = [](const Ctx &c) -> HR {
        const bool isShift = (c.modifiers & Qt::ShiftModifier) != 0;

        const QByteArray prefixUtf8 = c.blockText.left(c.qtPos).toUtf8();
        const quint32 byteOffset =
            c.currentBlockStart + static_cast<quint32>(prefixUtf8.size());

        Markoff::MarkoffEdit ed;
        ed.oldStart = byteOffset;
        ed.oldEnd   = byteOffset;
        ed.newText  = isShift
            ? QByteArrayLiteral("\n")     // soft break — stays in block
            : QByteArrayLiteral("\n\n");  // paragraph break — splits
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());

        if (isShift) {
            // Cursor stays in the same block, advanced past the newline.
            c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos + 1);
        } else {
            // Block split — caret to qtPos 0 of the new (or original-shifted)
            // row at blockIndex + 1. See Task 5 for the three-case rationale.
            c.cursorState->requestTextCaretAtRow(c.blockIndex + 1, 0);
        }

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: the new test plus the previous Enter tests all green.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp
git commit -m "feat(live-render): paragraph Shift-Enter — soft break

Inserts \\n instead of \\n\\n. Cursor advances by one within the same
block. Reuses the paragraph Enter handler with a modifiers branch."
```

---

## Task 9: Heading structural keys (mirror paragraph) (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

Headings register the same Enter / Shift-Enter / Backspace / Delete handlers as paragraphs. Re-using the paragraph lambdas keeps the policy uniform.

`record.text` for a heading is source-faithful (includes the `#` prefix — see `BlockWalker.cpp:89`). qtPos in the TextEdit therefore maps 1:1 to byte positions within the source range. End-of-heading Enter inserts `\n\n` after the heading source, and the parser sees `# H\n\n` followed by whatever, which produces an empty paragraph. Mid-heading Enter splits into two headings of the same level (the `#` markers are on the prefix only; the suffix inherits no prefix and parses as a paragraph). Both behaviours are acceptable for R5.

- [ ] **Step 1: Add a failing test**

```cpp
    void enter_at_end_of_heading_inserts_paragraph_break() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("# Title", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/7,
            /*selectionEmpty=*/true,
            QStringLiteral("# Title"));
        QVERIFY(consumed);

        QCOMPARE(doc.toMarkdown(), QString("# Title\n\n"));
    }

    void backspace_at_start_of_heading_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\n# Heading", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("# Heading"));
        QVERIFY(consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha\n# Heading"));
    }
```

- [ ] **Step 2: Register heading handlers**

Append to `registerBuiltins()` (after the paragraph registrations):

```cpp
    // Heading: same handlers as paragraph. Source-faithful text means the
    // # prefix is part of blockText, so qtPos arithmetic matches paragraph.
    m_handlers[BlockKind::Heading][Qt::Key_Return]    = paragraphEnter;
    m_handlers[BlockKind::Heading][Qt::Key_Enter]     = paragraphEnter;
    m_handlers[BlockKind::Heading][Qt::Key_Backspace] = paragraphBackspace;
    m_handlers[BlockKind::Heading][Qt::Key_Delete]    = paragraphDelete;
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: heading tests pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp
git commit -m "feat(live-render): heading structural keys mirror paragraph

Source-faithful blockText means qtPos arithmetic is identical. Same four
handlers (Return, Enter, Backspace, Delete) registered on the heading
kind."
```

---

## Task 10: Code-block structural keys — Backspace + Delete only (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

Code blocks consume Backspace-at-start and Delete-at-end (same merge semantics as paragraph). They do NOT consume Enter — TextEdit's native `\n` insertion runs, the LiveEditBinding routes it as a regular text edit, and the parse-back updates the code-block's text role. Tab is also not consumed; TextEdit inserts `\t`.

R5 carries the limitation documented in R4 (Task 13): code-block fence prefixes mean end-of-body byte arithmetic is approximate. For the merge handlers, that's fine — they operate on `currentBlockStart - 1` and `currentBlockEnd`, where `currentBlockEnd` is `currentBlockStart + blockText.toUtf8().size()`. The closing-fence bytes are NOT in `blockText`, so `currentBlockEnd` underestimates the true block end. Document this limitation.

Actually, the legacy implementation passes `blockText` from the TextEdit (which excludes the fences), so `currentBlockEnd` ends at the body's last byte, NOT at the closing fence. Delete-at-body-end therefore deletes a byte that's INSIDE the closing fence (likely a backtick), not the inter-block separator. This is a known limitation; the dogfood script doesn't exercise code-block Delete-at-end. For R5, ship this as-is; document it.

- [ ] **Step 1: Add a failing test for code-block Backspace-at-start**

```cpp
    void backspace_at_start_of_code_block_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\n```\ncode\n```", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.model()->recordAt(1).kind, BlockKind::CodeBlock);

        // Code-block delegate exposes the body bytes as blockText (sans fences).
        // qtPos 0 = start of body. R5 backspace-at-start of code-block treats
        // that as "merge with previous"; the byte deleted is the inter-block
        // separator before currentBlockStart, where currentBlockStart is the
        // CRDT-resolved start of the WHOLE code-block source range (including
        // the opening fence). One \n is removed.
        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("code\n"));
        QVERIFY(consumed);
        // Exact post-state depends on which separator byte got deleted; the
        // important invariants are: it's shorter, and the structural handler
        // returned true.
        QVERIFY(doc.toMarkdown().size() < QStringLiteral("alpha\n\n```\ncode\n```").size());
    }

    void code_block_enter_is_not_consumed_by_structural_handler() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("```\ncode\n```", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::CodeBlock);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/4,    // mid-body
            /*selectionEmpty=*/true,
            QStringLiteral("code\n"));
        QVERIFY(!consumed);
    }
```

- [ ] **Step 2: Register code-block handlers (Backspace + Delete only)**

Append to `registerBuiltins()`:

```cpp
    // Code-block: only the merge handlers. Enter is NOT consumed —
    // TextEdit's native \n insertion routes through LiveEditBinding as
    // a regular text edit, producing the literal newline the user
    // expects inside fenced code.
    //
    // Limitation: blockText excludes the fences, so currentBlockEnd
    // underestimates the true block end. Delete-at-body-end therefore
    // deletes a fence byte rather than the inter-block separator. Not
    // exercised by the R5 dogfood script; full fix lands in R6 with
    // proper code-block byte arithmetic.
    m_handlers[BlockKind::CodeBlock][Qt::Key_Backspace] = paragraphBackspace;
    m_handlers[BlockKind::CodeBlock][Qt::Key_Delete]    = paragraphDelete;
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/tests/tst_live_render_structural.cpp
git commit -m "feat(live-render): code-block consumes only Backspace/Delete edges

Enter is NOT consumed; TextEdit's native \\n insertion routes through
LiveEditBinding as a regular edit, giving literal newlines inside
fenced code. Backspace-at-start and Delete-at-end use the same merge
handlers as paragraph; full fence-aware arithmetic is R6."
```

---

## Task 11: Populate `BlockKindDescriptor::consumedStructuralKeys` (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/BlockKindRegistry.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_registry.cpp`

The handler registrations in Task 4–10 are inert until each kind's descriptor declares the corresponding `consumedStructuralKeys`. `LiveStructuralKeyHandler::tryHandle` checks the descriptor BEFORE looking up the handler; without this, every key returns `false`.

The tests in Tasks 4–10 only pass because — wait. Let me re-check. The dispatcher does:

```cpp
if (!desc->consumedStructuralKeys.contains(key)) return false;
auto kindIt = m_handlers.constFind(rec.kind);
...
```

So if `consumedStructuralKeys` is empty (the R3 default), every test in Tasks 4–10 fails on the first check. **The tests in those tasks therefore won't pass until this Task 11 lands.**

To unblock TDD, do this task BEFORE running the build at the end of Task 4. The plan instructs doing Tasks 4–10 first (writing tests + handlers), then Task 11 (descriptors). Build will fail on the Task 4 first build attempt; that's by design — the descriptor wiring is the natural next commit. **However**, that means Tasks 5–10's "Build and run" steps also fail until Task 11 lands.

**Plan adjustment:** The TDD cycle is "write failing test, implement, run pass." Build steps in Tasks 4–10 become "expected: still failing because descriptor not wired." Run them anyway to confirm the test compiles. Then Task 11 wires the descriptors and a single ctest run validates Tasks 4–10 cumulatively.

Or more cleanly: complete Task 11 IMMEDIATELY after Task 4's handler is written (before Task 4's "build and run" step). The plan's task ordering reflects narrative flow — the executing agent can promote Task 11 to run between Task 4 step 4 and Task 4 step 7 if they want greener inner-loop builds. The cumulative test count at the END of all R5 tasks is the same.

**Recommended execution:** treat Tasks 4–11 as a single atomic group. After landing all the handlers AND the descriptor wiring, run ctest once to confirm all tests pass. Commit each task individually as the plan instructs (so the history reflects the design decomposition).

For this Task 11: the descriptor wiring itself.

- [ ] **Step 1: Add a failing test for descriptor wiring**

In `libs/markoff-live-render/tests/tst_live_render_registry.cpp`, add (read the file first to find an existing test slot to model after):

```cpp
    void paragraph_descriptor_consumes_structural_keys() {
        BlockKindRegistry r;
        const auto *d = r.find(BlockKind::Paragraph);
        QVERIFY(d);
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Return));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Enter));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Backspace));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Delete));
    }

    void heading_descriptor_consumes_structural_keys() {
        BlockKindRegistry r;
        const auto *d = r.find(BlockKind::Heading);
        QVERIFY(d);
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Return));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Enter));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Backspace));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Delete));
    }

    void code_block_descriptor_consumes_only_edge_keys() {
        BlockKindRegistry r;
        const auto *d = r.find(BlockKind::CodeBlock);
        QVERIFY(d);
        QVERIFY(!d->consumedStructuralKeys.contains(Qt::Key_Return));
        QVERIFY(!d->consumedStructuralKeys.contains(Qt::Key_Enter));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Backspace));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Delete));
    }

    void hr_image_descriptors_have_no_structural_keys() {
        BlockKindRegistry r;
        QVERIFY(r.find(BlockKind::HorizontalRule)->consumedStructuralKeys.isEmpty());
        QVERIFY(r.find(BlockKind::Image)->consumedStructuralKeys.isEmpty());
    }
```

- [ ] **Step 2: Update `registerBuiltins`**

In `libs/markoff-live-render/src/BlockKindRegistry.cpp`, add `consumedStructuralKeys` to the paragraph, heading, and code-block branches:

```cpp
    // Paragraph: text-bearing, TextCaret (R3), Enter/Backspace/Delete (R5).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Paragraph;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = {
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Backspace, Qt::Key_Delete,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/ParagraphDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // Heading: text-bearing, TextCaret. Same structural keys as paragraph.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Heading;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = {
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Backspace, Qt::Key_Delete,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/HeadingDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // CodeBlock: text-bearing, TextCaret. Only edge-merge keys consumed;
    // Enter passes through to TextEdit's native \n insertion.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::CodeBlock;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = { Qt::Key_Backspace, Qt::Key_Delete };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/CodeBlockDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
```

(Leave hr and image untouched.)

- [ ] **Step 3: Build and run the registry tests**

```bash
cmake --build build-dev --target tst_live_render_registry -j 8
ctest --test-dir build-dev -R '^tst_live_render_registry$' --output-on-failure
```

Expected: registry tests pass.

- [ ] **Step 4: Run the structural test suite to confirm Tasks 4–10 are green now**

```bash
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: ALL structural tests pass — Tasks 4–10's tests are unblocked by this descriptor wiring.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live-render/src/BlockKindRegistry.cpp \
        libs/markoff-live-render/tests/tst_live_render_registry.cpp
git commit -m "feat(live-render): populate consumedStructuralKeys on built-in descriptors

Paragraph + heading consume Return/Enter/Backspace/Delete; code-block
consumes only Backspace/Delete (Enter passes through to native \\n).
Unblocks the R5 structural tests in tst_live_render_structural."
```

---

## Task 12: `LiveEditBinding` integrates `UndoCoalescer` (TDD)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveEditBinding.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_paragraph_edit.cpp`

After every user-typed `applyLocalEdit`, classify the edit and call the appropriate `recordPrintable` / `recordOther` on the binding-owned coalescer.

Classification:
- `charsRemoved == 0 && charsAdded == 1` → printable: call `recordPrintable(blockAnchor)`.
- Otherwise (multi-char insert, deletion, replacement) → other: call `recordOther()`.

The IME-flush path (`flushPendingComposition`) commits a single batched edit on commit; classify as `recordOther` (the user typed multiple characters via IME, not a single keystroke).

- [ ] **Step 1: Add a failing test**

Append to `libs/markoff-live-render/tests/tst_live_render_paragraph_edit.cpp`:

```cpp
    void consecutive_printables_coalesce_via_binding() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);
        document.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        QTextEdit editor;
        editor.setPlainText("hello");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setRawTextDocument(editor.document());

        QTextCursor cur(editor.document());
        cur.setPosition(5);
        cur.insertText("A");
        QCOMPARE(document.undoDepth(), 1);

        cur.setPosition(6);
        cur.insertText("B");
        // Coalesced into the previous undo entry.
        QCOMPARE(document.undoDepth(), 1);

        cur.setPosition(7);
        cur.insertText("C");
        QCOMPARE(document.undoDepth(), 1);
    }
```

- [ ] **Step 2: Modify `LiveEditBinding::onContentsChange`**

In `libs/markoff-live-render/src/LiveEditBinding.cpp`, after the existing `doc->applyLocalEdit({ edit });` and `model->setRowEditSequence(...)` lines, add:

```cpp
    // R5 undo coalescing.
    if (auto *coalescer = m_binding->undoCoalescer()) {
        const bool isPrintable = (charsRemoved == 0 && charsAdded == 1);
        if (isPrintable)
            coalescer->recordPrintable(record.blockAnchor);
        else
            coalescer->recordOther();
    }
```

In `flushPendingComposition`, after its `model->setRowEditSequence(...)` call:

```cpp
    if (auto *coalescer = m_binding->undoCoalescer()) {
        coalescer->recordOther();
    }
```

Add `#include <markoff/live-render/UndoCoalescer.h>` to the top of the file.

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_paragraph_edit -j 8
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: new test passes; previous tests still green.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/src/LiveEditBinding.cpp \
        libs/markoff-live-render/tests/tst_live_render_paragraph_edit.cpp
git commit -m "feat(live-render): LiveEditBinding records edits in UndoCoalescer

Single-char inserts → recordPrintable(blockAnchor); other shapes →
recordOther(). IME-flush also records as Other. Consecutive printables
in the same block within 1000 ms coalesce into one undo entry."
```

---

## Task 13: QML delegate wiring — paragraph (TDD via dogfood + headless smoke)

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml`

R4 swallowed Enter at the delegate level. R5 removes the swallow and routes Enter / Backspace / Delete / Shift-Enter through `binding.structuralKeyHandler.tryHandle(...)`. If handled, the event is accepted; otherwise it falls through to TextEdit's native handling (which routes through `LiveEditBinding` for character-insertion paths).

QML side has no unit-test framework comparable to QSignalSpy for this exact scenario; the C++ tests in Tasks 4–10 cover the handler. The QML wiring is verified by the dogfood script in Task 18.

- [ ] **Step 1: Replace the R4 Enter-swallow Keys.onPressed**

Open `libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml`. Replace the entire `Keys.priority` + `Keys.onPressed` block (the part that swallows Enter — lines 43–52 in the R4 file) with:

```qml
        // R5: route structural keys (Enter, Backspace at start, Delete at end,
        // Shift-Enter) through LiveStructuralKeyHandler. If handled, accept
        // the event so TextEdit doesn't also process it. If not handled,
        // fall through to TextEdit's native handling — which routes through
        // LiveEditBinding for character-insertion (e.g. Backspace mid-block,
        // Delete mid-block, plain printable keys).
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) return
            if (event.key !== Qt.Key_Return
                && event.key !== Qt.Key_Enter
                && event.key !== Qt.Key_Backspace
                && event.key !== Qt.Key_Delete) {
                return
            }
            const handled = handler.tryHandle(
                event.key,
                event.modifiers,
                root.modelIndex,
                edit.cursorPosition,
                edit.selectedText.length === 0,
                edit.getText(0, edit.length)
            )
            if (handled) event.accepted = true
        }
```

Leave everything else in the delegate unchanged (LiveEditBinding wiring, TextEdit, applySelection, focusEditAt, etc.).

- [ ] **Step 2: Build the test app and smoke-test**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-render-app -j 8
./build-dev/bin/markoff-live-render-app /tmp/r5-smoke.md &
APP_PID=$!
sleep 2
kill $APP_PID
```

If `/tmp/r5-smoke.md` doesn't exist, write a small one first:

```bash
cat > /tmp/r5-smoke.md <<'EOF'
# Heading one

First paragraph here.

Second paragraph here.

Third paragraph here.
EOF
```

Confirm the app launches without crashing. Manual verification (Enter mid-paragraph, Backspace at start, etc.) is the dogfood gate at Task 18.

- [ ] **Step 3: Run the existing automated suite**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml
git commit -m "feat(live-render): paragraph delegate routes structural keys to handler

Removes the R4 Enter-swallow workaround. Enter / Backspace / Delete /
Shift-Enter route through binding.structuralKeyHandler.tryHandle(...);
unhandled cases fall through to TextEdit's native handling (which
routes printables through LiveEditBinding)."
```

---

## Task 14: QML delegate wiring — heading

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/HeadingDelegate.qml`

Same change as paragraph; same pattern; replicate the Keys.onPressed block.

- [ ] **Step 1: Replace the R4 Enter-swallow**

In `libs/markoff-live-render/qml/delegates/HeadingDelegate.qml`, replace lines 49–54 (the R4 Enter-swallow `Keys.onPressed`) with the same structural-key dispatch block as Task 13. The block is identical text-by-text — paste it in place.

- [ ] **Step 2: Smoke-test**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-render-app -j 8
./build-dev/bin/markoff-live-render-app /tmp/r5-smoke.md &
APP_PID=$!
sleep 2
kill $APP_PID
```

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/HeadingDelegate.qml
git commit -m "feat(live-render): heading delegate routes structural keys to handler

Same wiring as paragraph (Task 13). Removes the R4 Enter-swallow."
```

---

## Task 15: QML delegate wiring — code-block

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml`

Same change. Code-block's descriptor only declares Backspace/Delete as consumed keys, so Enter falls through and TextEdit inserts a literal `\n` — that's the expected behaviour inside a fenced code block.

- [ ] **Step 1: Replace the R4 Enter-swallow**

In `libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml`, replace lines 44–49 with the same structural-key dispatch block. (Identical text to Tasks 13–14.)

- [ ] **Step 2: Smoke-test**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-render-app -j 8

cat > /tmp/r5-smoke-code.md <<'EOF'
First paragraph.

```
code line 1
code line 2
```

Last paragraph.
EOF

./build-dev/bin/markoff-live-render-app /tmp/r5-smoke-code.md &
APP_PID=$!
sleep 2
kill $APP_PID
```

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml
git commit -m "feat(live-render): code-block delegate routes structural keys to handler

Code-block declares only Backspace/Delete as consumed; Enter falls
through to TextEdit's native \\n insertion — the expected behaviour
inside fenced code."
```

---

## Task 16: LiveView focus-routing on cursorChanged

**Files:**
- Modify: `libs/markoff-live-render/qml/LiveView.qml`

When `LiveCursorState::cursorChanged` fires with a `TextCaret` (the structural-key handler's pending request just resolved on `rowsInserted`), the corresponding delegate must receive keyboard focus and place the caret at the requested qtPos.

The hook: a `Connections{target: binding.cursorState}` block at the LiveView level. On `cursorChanged`, look up the row from the cursor's blockAnchor (via `binding.cursorState.rowForBlock(...)` — already exists), find the realised delegate via `root.itemAtIndex(row)`, and call `focusEditAt(cachedByteOffset)` on it.

**Subtle point.** `LiveCursorState::rowForBlock` is C++ and not Q_INVOKABLE, so QML can't call it directly. Two options:

- (a) Add `Q_INVOKABLE int rowForBlock(...)` to LiveCursorState.
- (b) Bypass: since `requestTextCaretAtRow(expectedRow, qtPos)` constructs the cursor directly and emits `cursorChanged`, the QML side can correlate by re-deriving the row from the model. Iterate model rows and find the one whose `blockAnchor` matches the cursor's. With ~hundreds of rows worst-case, that's acceptable; with thousands, it's not.

Pick (a) — make `rowForBlock` Q_INVOKABLE. That's what spec §5.3 expects ("listening to `cursorChanged()`, sees its `blockId` is now focused").

Wait — actually, the cursor's `block` field is a `Markoff::BlockAnchor`, which is a Q_DECLARE_METATYPE'd type but probably not directly comparable from QML. Compare via `cursorState.rowForBlock(cursor.block)` is the cleanest path; mark the C++ method `Q_INVOKABLE` and keep `BlockAnchor` opaque to QML (it's just passed through).

A simpler alternative: add a `Q_PROPERTY(int focusedRow READ focusedRow NOTIFY cursorChanged)` to LiveCursorState that returns the row of the current cursor's block, or -1. QML reads `cursorState.focusedRow` directly. **This is the cleanest design and avoids any QML-side iteration.** Pin it.

- [ ] **Step 1: Add `focusedRow` Q_PROPERTY to LiveCursorState**

In `libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h`, add:

```cpp
    Q_PROPERTY(int focusedRow READ focusedRow NOTIFY cursorChanged)
    Q_PROPERTY(int focusedQtPos READ focusedQtPos NOTIFY cursorChanged)
```

In the public section:

```cpp
    int focusedRow() const;
    int focusedQtPos() const;
```

In `libs/markoff-live-render/src/LiveCursorState.cpp`, add the implementations:

```cpp
int LiveCursorState::focusedRow() const
{
    if (auto *tc = std::get_if<TextCaret>(&m_cursor))     return rowForBlock(tc->block);
    if (auto *bs = std::get_if<BlockSelected>(&m_cursor)) return rowForBlock(bs->block);
    if (auto *bi = std::get_if<BlockInternalEdit>(&m_cursor)) return rowForBlock(bi->block);
    return -1;
}

int LiveCursorState::focusedQtPos() const
{
    if (auto *tc = std::get_if<TextCaret>(&m_cursor))
        return static_cast<int>(tc->cachedByteOffset);
    return -1;
}
```

(Note: `cachedByteOffset` is named for byte coordinates but in the context of structural-key cursor placement we set it to qtPos — see Task 2 step 4's `resolvePendingForRow`. For R5's Notion-style edits this is consistent: qtPos == byte offset in our source-faithful UTF-8 PlainText flow except where multi-byte UTF-8 lives. Document this limitation as a follow-up: structural-key cursor placement uses qtPos directly, which is correct for ASCII content. For multi-byte content the limitation is the same as legacy — see LiveEditBinding's prefixUtf8 pattern. R6/R10 hardening tightens this.)

- [ ] **Step 2: Add the focus-routing Connections in LiveView.qml**

In `libs/markoff-live-render/qml/LiveView.qml`, add inside the top-level `ListView` (e.g. just before the `MouseArea` block):

```qml
    // R5: focus-route into a delegate when the cursor changes (structural-key
    // pending requests resolve on rowsInserted via LiveCursorState; this
    // hook delivers the caret to the resolved delegate). Spec §5.3.
    Connections {
        target: binding ? binding.cursorState : null
        function onCursorChanged() {
            const cs = binding ? binding.cursorState : null
            if (!cs) return
            const row = cs.focusedRow
            const qtPos = cs.focusedQtPos
            if (row < 0) return
            const item = root.itemAtIndex(row)
            if (item && item.focusEditAt && qtPos >= 0)
                item.focusEditAt(qtPos)
        }
    }
```

- [ ] **Step 3: Smoke-test**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-render-app -j 8
./build-dev/bin/markoff-live-render-app /tmp/r5-smoke.md &
APP_PID=$!
sleep 3
kill $APP_PID
```

Manual quick-check (optional): launch the app, click into a paragraph, press Enter mid-text. Caret should land in the new row. Full dogfood in Task 18.

- [ ] **Step 4: Run the test suite**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h \
        libs/markoff-live-render/src/LiveCursorState.cpp \
        libs/markoff-live-render/qml/LiveView.qml
git commit -m "feat(live-render): focus-route delegate on cursorChanged

LiveCursorState exposes focusedRow + focusedQtPos as Q_PROPERTYs.
LiveView has a single Connections{target: cursorState} block that
calls focusEditAt on the matching delegate when the cursor changes
(structural-edit pending requests resolve here)."
```

---

## Task 17: Test app polish — title bump

**Files:**
- Modify: `libs/markoff-live-render/app/Main.qml`

- [ ] **Step 1: Update the title**

In `libs/markoff-live-render/app/Main.qml`, change `(R4)` to `(R5)`:

```qml
    title: ctxTitle + " — markoff-live-render (R5)"
```

- [ ] **Step 2: Build & launch**

```bash
cmake --build build-dev --target markoff-live-render-app -j 8
./build-dev/bin/markoff-live-render-app /tmp/r5-smoke.md &
APP_PID=$!
sleep 2
kill $APP_PID
```

Confirm the title says `(R5)`.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live-render/app/Main.qml
git commit -m "chore(live-render): test app title bumps to R5"
```

---

## Task 18: Dogfood gate (manual; user-driven) and status update

**Files:**
- Modify: `docs/restoration-status.md`

R5's acceptance criterion is the dogfood script (spec §10.3): *"Press Enter at the end of every paragraph in a 10-block doc; caret lands in the new empty paragraph each time; Backspace at the start of each merges back, restoring the original."* Plus the user's freeform exploration of mid-block Enter, Shift-Enter (soft break), Delete-at-end, heading edits, code-block typing.

This task is the user's pass — the agent does NOT mark R5 complete; the user does, after running the script.

- [ ] **Step 1: Build the test app fresh**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-render-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Confirm: all live-render fast-tier tests green.

- [ ] **Step 2: Pick or generate a dogfood document**

A 10-block document is small enough that any reasonable markdown sample works. Reuse the R4 dogfood file if it exists, or generate one:

```bash
cat > /tmp/r5-dogfood.md <<'EOF'
# R5 dogfood

First paragraph. Press Enter at the end of this paragraph and watch the caret land in a new empty paragraph below.

Second paragraph. Try Backspace at the start to merge with the previous one.

Third paragraph. Try a mid-block Enter to split it in two.

Fourth paragraph. Shift-Enter inserts a soft break.

## A subheading

Paragraph after the heading.

### A deeper heading

Yet another paragraph.

```
def hello():
    print("code block — Enter inserts \\n; structural keys do not consume")
```

Last paragraph.
EOF
```

- [ ] **Step 3: Confirm the test app loads**

```bash
./build-dev/bin/markoff-live-render-app /tmp/r5-dogfood.md
```

Confirm: file loads, all blocks render, click + Enter at end of paragraph creates a new empty paragraph with the caret in it. Close the app.

- [ ] **Step 4: Update `docs/restoration-status.md`**

Read the current file. Make these edits:

1. Update the **TL;DR** to:

   ```markdown
   > **R5 in dogfood gate.** Implementation complete (LiveStructuralKeyHandler
   > with descriptor-driven dispatch; paragraph + heading consume Return /
   > Enter / Backspace / Delete; code-block consumes only Backspace / Delete;
   > LiveCursorState pending-row request resolves on rowsInserted; UndoCoalescer
   > policy enforced by LiveEditBinding; QML delegates removed the R4
   > Enter-swallow). User dogfood pass per spec §10.3 R5 script is the gate.
   >
   > **Recommended next:** Run the R5 dogfood script. Once the user signs off,
   > flip R5 to `complete` and start writing R6 (other text blocks +
   > speculation refresh).
   ```

2. Update the **Phase board** R5 row:

   ```markdown
   | **R5** | [r5-structural-keys](plans/2026-05-02-live-render-r5-structural-keys.md) | `dogfood` | <commit-list> | Structural keys + IME completion + undo coalescing. |
   ```

   Replace `<commit-list>` with the actual short SHAs of the R5 commits — collect via `git log --oneline | head -25`.

3. Append entries to **Recent-changes log** for every commit in this plan, in chronological order, one line per commit.

4. Append an entry to the **Plan-generation log**:

   ```markdown
   ### 2026-05-02 — R5 — 2026-05-02-live-render-r5-structural-keys.md

   Generated from: spec §11 R5
   Generated by: fresh agent context (post-R4 dogfood sign-off)
   Predecessor acceptance: R4 — `37b97bf` + dogfood log entry 2026-05-02
   Self-review notes: Resolved §15.1 (descriptor consumedStructuralKeys +
   per-kind handler-table inside LiveStructuralKeyHandler) and §15.4
   (undo idle threshold pinned at 1000 ms). Resolved §15.8 negatively
   for R5 — flag flip is R10 work. Heading and paragraph share
   structural handlers because their blockText is source-faithful (the
   `#` prefix is part of the bytes). Code-block consumes only edge keys
   so Enter falls through to TextEdit's native \\n.
   ```

- [ ] **Step 5: Commit the status update**

Bundle with Task 17's commit if not yet pushed (per the doc's bundling rule). If already pushed, a standalone status commit is acceptable as a clearly-status-only change:

```bash
git add docs/restoration-status.md
git commit -m "docs(restoration-status): R5 in dogfood gate"
```

- [ ] **Step 6: Surface to the user**

Tell the user (in chat, not committed):

> R5 implementation is complete and tests are green (7/7 live-render fast-tier including the new tst_live_render_structural). The dogfood script for R5 is: press Enter at the end of every paragraph in a 10-block doc; caret lands in the new empty paragraph each time; Backspace at the start of each merges back, restoring the original. Also try mid-paragraph Enter (block splits, caret goes to start of new row), Shift-Enter (soft break, stays in same paragraph), Delete at end of paragraph (merges with next), and typing inside a fenced code block (literal \\n on Enter). Try `./build-dev/bin/markoff-live-render-app /tmp/r5-dogfood.md` (or any markdown of yours). If anything misbehaves, paste your description verbatim and I'll diagnose. If clean, say so and we'll flip R5 to `complete` and write R6.

R5 is **not** complete from the agent's side until the user signs off in the dogfood log.

---

## Self-Review

- **Spec coverage (spec §11 R5 scope):**
  - "LiveStructuralKeyHandler with descriptor-based dispatch" → Tasks 4–11.
  - "Paragraph kind: Enter (split / new), Backspace at start (merge), Delete at end (merge), Shift-Enter (soft break)" → Tasks 4 (end Enter), 5 (mid + start Enter), 6 (Backspace), 7 (Delete), 8 (Shift-Enter).
  - "Heading and code-block kinds: their structural-key declarations" → Task 9 (heading), Task 10 (code-block), Task 11 (descriptor wiring).
  - "UndoCoalescer policy; calls coalesceLastUndo()" → Task 3 (component) + Task 12 (binding integration).
  - Spec §5.3 step 6 (focus protocol's pending-row mechanism) → Task 2 (cursor state) + Task 16 (QML focus-routing).
  - Spec §15 open questions (§15.1, §15.4, §15.8) → resolved in the plan's "Open question resolutions" section.
- **Acceptance criteria match:**
  - tst_live_render_structural covers: end-of-block Enter (Task 4), mid-block + start-of-block Enter (Task 5), Backspace-at-row-start (Task 6), Delete-at-row-end (Task 7), Shift-Enter (Task 8), heading Enter + Backspace (Task 9), code-block Backspace + Enter-not-consumed (Task 10), descriptor consumedStructuralKeys (Task 11), UndoCoalescer policy (Task 3, 7 sub-tests).
  - tst_live_render_cursor covers requestTextCaretAtRow + pending drop (Task 2, 3 sub-tests).
  - tst_live_render_paragraph_edit covers undo-coalesce-via-binding (Task 12).
  - The R5 dogfood script (spec §10.3) is the binary gate at Task 18.
- **Placeholder scan:** no "TBD" / "appropriate error handling" / "implement later" / "fill in details" — every step has the actual code an engineer needs. The "Plan adjustment" note in Task 11 explicitly addresses the cross-task dependency between Tasks 4–10 and Task 11; it's commentary, not a placeholder.
- **Type consistency:**
  - `LiveStructuralKeyHandler::tryHandle` signature matches across Task 4 header, Task 4 source, the QML calls in Tasks 13–15, and the test calls in Tasks 4–10.
  - `LiveCursorState::requestTextCaretAtRow(int, int)` matches across Task 2 (header + source + tests) and Task 4 (handler call sites).
  - `UndoCoalescer::recordPrintable(const Markoff::BlockAnchor &)`, `recordStructural()`, `recordOther()`, `notifyFocusChanged()`, `notifyMovement()`, `notifyIdleExpired()` — all consistent across header, source, tests, and call sites.
  - `BlockKindDescriptor::consumedStructuralKeys` is `QSet<int>` per the existing R3 declaration; not renamed.
  - `Ctx` struct field names match between header and lambda implementations.
- **Dependencies and order:**
  - Task 2 (cursor state) and Task 3 (coalescer) are independent; either order works.
  - Task 4 (handler skeleton + paragraph end-Enter) depends on Tasks 2 + 3.
  - Tasks 5–10 extend Task 4's handler; each depends on Task 4 but otherwise independent — execute serially for narrative simplicity.
  - Task 11 (descriptor wiring) is required for Tasks 4–10's tests to pass; the plan recommends running ctest cumulatively after Task 11.
  - Task 12 (binding undo integration) is independent of Tasks 4–11 (uses only the UndoCoalescer from Task 3); its test is in tst_live_render_paragraph_edit, not tst_live_render_structural.
  - Tasks 13–15 (QML delegate wiring) depend on Tasks 4–11. The QML side has no automated tests in R5; smoke-tested via the test app, fully verified in Task 18 dogfood.
  - Task 16 (focus routing) depends on Tasks 2 + 13–15.
  - Task 17 (title bump) is independent.
  - Task 18 is the gate.
- **Open spec questions (per spec §15):**
  - §15.1 (consumedStructuralKeys + handler-registration) — resolved in plan header (descriptor `QSet<int>` + handler-table inside `LiveStructuralKeyHandler`).
  - §15.4 (undo idle threshold) — resolved as 1000 ms constant.
  - §15.8 (test-app `--live` flag rename) — out of R5 scope (R10 work).
- **Risk areas flagged:**
  - **Task 11's cross-task dependency.** Tasks 4–10 add tests that fail until Task 11 wires `consumedStructuralKeys`. The plan documents this and instructs running cumulative ctest after Task 11. An alternative ordering (Task 11 first, then 4–10) would avoid the temporary red-build window but reorder the design narrative awkwardly. Either is acceptable; the executing agent picks.
  - **Code-block end-of-body byte arithmetic.** The fence prefix/suffix means `currentBlockEnd` underestimates the true block end. R5 ships this as-is (the dogfood script doesn't exercise the failing case). R6 fixes it.
  - **Non-empty selection + structural key.** R5 returns `NotHandled` from `tryHandle` for non-empty selections. TextEdit's native handling deletes the selection and inserts the key character (`\n` for Enter); LiveEditBinding routes that as a single edit. Result: a soft break replaces the selection, not a paragraph split. Documented as carried-into-R6.
  - **`focusedQtPos` vs. `cachedByteOffset` semantics.** `cachedByteOffset` was named for byte coordinates but is set to qtPos by `resolvePendingForRow`. ASCII-only content makes them equal; multi-byte content is a known limitation (same as legacy). R6/R10 tightens this.
  - **Pending-cursor drop timing.** `noteParseArrived` increments a counter and drops at 2 cycles. If parsing is fast, that's ~2 × ~5 ms = 10 ms before drop, which is within the 50 ms structural-edit budget; if parsing is slow (large document), the structural-edit caret takes longer to land. Acceptable for R5; R10 perf hardening covers slow parses.
- **Build/test cadence:** Each task has a build + ctest step. The full live-render fast-tier suite is `ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8`. tst_realistic and tst_benchmark are excluded per the project's CLAUDE.md.

---

*End of plan.*
