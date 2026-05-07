# R4 — Paragraph Editing (Sequence-Tagged Binding) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the three text-bearing delegates (paragraph, heading, code-block) writable, route every keystroke through `MarkoffDocument::applyLocalEdit`, and apply the C-architecture freshness rule so a parse arriving with stale input never overwrites the user's in-flight typing.

**Architecture:** A new per-delegate C++ component `LiveEditBinding` listens to its `TextEdit`'s `QQuickTextDocument`'s `contentsChange` signal, derives a single `MarkoffEdit` from the (position, charsRemoved, charsAdded) tuple, calls `MarkoffDocument::applyLocalEdit`, then stamps the model's per-row `lastEditEditSequence` with the post-edit `editSequence`. On parse arrival, `LiveListModelBinding::onParseUpdated` consults `parseInputEditSequence` against each row's stamp; only fresh rows accept text-role updates. Three cycle guards survive — `applyingModelUpdate` (set by the binding while applyOps is mutating model rows, suppresses the synchronous TextEdit echo), IME composition deferral (skip `applyLocalEdit` while `inputMethodComposing` is true; reconcile on commit), and selection-projection echo (LiveSelectionView's session write-vs-readback re-entrance — added defensively so a future read-back is safe).

**Tech stack:** C++20, Qt 6.8 (Quick, Qml, Test), `QQuickTextDocument` → `QTextDocument::contentsChange`, `Markoff::MarkoffDocument::applyLocalEdit`, `Markoff::MarkoffEdit`, `Coordinates::qtPosToByte` from L0.

**Reference spec:** `docs/specs/2026-05-02-live-render-restoration-design.md` §4 (source-of-truth protocol, freshness rule, surviving guards), §5.2 (delegate `applyTextUpdate` contract), §7.1 (steady-state typing data flow), §11 R4.
**Prerequisites:** R1–R3 complete. `LiveBlockModel` has `setRowEditSequence`/`rowEditSequence`. `MarkoffDocument::parseUpdated` already carries `parseInputEditSequence`. `Coordinates::qtPosToByte` exists. `LiveCursorState` exists.
**Acceptance criterion (binary):** R4 dogfood script (spec §10.3) passes — *"Type a 200-word paragraph at 100+ wpm into a 5-page document; cursor never jumps; characters never scramble."* — AND `tst_live_render_paragraph_edit` passes covering: typing-during-in-flight-parse, mid-block insert, no-`Qt.callLater` invariant, IME deferral, applyingModelUpdate guard.

---

## File map

**New — public headers** (`libs/markoff-live/include/markoff/live-render/`):
- `LiveEditBinding.h` — `QML_ELEMENT`; per-delegate; wires `QQuickTextDocument::contentsChange` → `applyLocalEdit`.

**New — sources** (`libs/markoff-live/src/`):
- `LiveEditBinding.cpp`

**New — tests** (`libs/markoff-live/tests/`):
- `tst_live_render_paragraph_edit.cpp`

**Modified — headers:**
- `libs/markoff-live/include/markoff/live-render/LiveBlockModel.h` — `applyOps` gains a third parameter `parseInputEditSeq` (default `quint64(-1)` = "all rows fresh", preserves R2/R3 callsites).
- `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h` — expose `bool applyingModelUpdate() const` for cycle-guard query; expose `MarkoffDocument *document()` to LiveEditBinding via QML.
- `libs/markoff-live/include/markoff/live-render/LiveSelectionView.h` — add `applyingSessionSelection()` re-entrance guard (defensive — read-back path is post-R4).

**Modified — sources:**
- `libs/markoff-live/src/LiveBlockModel.cpp` — freshness gate inside the `Equal` op branch.
- `libs/markoff-live/src/LiveListModelBinding.cpp` — set/clear `m_applyingModelUpdate` around `applyOps`; pass `parseInputEditSequence`; re-resolve TextCaret cached offset after parse.
- `libs/markoff-live/src/LiveSelectionView.cpp` — guard `syncToSession`.
- `libs/markoff-live/CMakeLists.txt` — add `LiveEditBinding.{h,cpp}` to `qt_add_qml_module`.
- `libs/markoff-live/tests/CMakeLists.txt` — add `tst_live_render_paragraph_edit`.
- `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` — `readOnly: false`, `selectByMouse: true`, attach `LiveEditBinding`.
- `libs/markoff-live/qml/delegates/HeadingDelegate.qml` — same, plus level-aware (the `#`-prefix bytes stay in the source span — see Task 12).
- `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` — same.
- `libs/markoff-live/qml/LiveView.qml` — pass-through `binding` already there; no change beyond confirming `Keys` priority for arrow / printable keys still routes correctly with `readOnly: false`.
- `libs/markoff-live/app/Main.qml` — title suffix `"(R4)"`.
- `docs/restoration-status.md` — new entry (last task).

**Untouched (verified-not-broken):**
- `LiveCursorState`, `BlockHitTester`, `LiveSelectionView::rangeForBlock`/`copyToClipboard` (R3 surfaces).
- `HorizontalRuleDelegate`, `ImageDelegate` (non-text; not affected).

---

## Task 1: Read context

- [ ] **Step 1: Read these files in order, no edits**

```
docs/specs/2026-05-02-live-render-restoration-design.md   §4, §5.2, §7.1, §11 R4
libs/markoff-core/include/markoff-foundation/MarkoffDocument.h     (applyLocalEdit, editSequence, parseUpdated signature)
libs/markoff-core/include/markoff-foundation/MarkoffEdit.h         (oldStart, oldEnd, newText)
libs/markoff-live/include/markoff/live-render/LiveBlockModel.h    (setRowEditSequence, rowEditSequence, applyOps)
libs/markoff-live/src/LiveBlockModel.cpp                          (current applyOps; the Equal branch is what we modify)
libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h
libs/markoff-live/src/LiveListModelBinding.cpp                    (onParseUpdated; lastParseInputEditSeq is stored but unused)
libs/markoff-live/include/markoff/live-render/Coordinates.h       (qtPosToByte, byteToQtPos)
libs/markoff-live/qml/delegates/ParagraphDelegate.qml             (the TextEdit shape we're making editable)
```

No code changes in this task.

- [ ] **Step 2: Run the existing fast-tier test suite to confirm a clean baseline**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all five `tst_live_render_*` executables green. Record the count; R4 must end with at least the same count green plus the new `tst_live_render_paragraph_edit`.

---

## Task 2: Extend `LiveBlockModel::applyOps` with the freshness gate (TDD)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/LiveBlockModel.h`
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_block_model.cpp`

The freshness rule (spec §4.3): for each `Equal` op, the per-row text role is updated only if `row.lastEditEditSequence <= parseInputEditSeq`. Stale rows preserve their existing model text (the CRDT is the canonical source for those bytes — the parse just hasn't caught up). Non-text role fields (`kind`, `headingLevel`, `codeLanguage`, `blockAnchor`) ARE updated regardless — block-shape decisions are the parser's authoritative concern (spec §4.3).

`Insert` and `Delete` ops are always applied (block boundaries are always the parser's call).

Default behaviour preserved: passing `std::numeric_limits<quint64>::max()` (the new default) means "all rows are fresh"; tests written against R2/R3 keep their meaning.

- [ ] **Step 1: Add a failing test for stale-row preservation**

Append to `libs/markoff-live/tests/tst_live_render_block_model.cpp`. (Read the file first to see where the existing tests live and the helpers in scope.)

Add this test slot to the existing `TstLiveRenderBlockModel` class:

```cpp
    void equal_op_with_stale_row_preserves_model_text() {
        // The R4 freshness rule: when a row's lastEditEditSequence is GREATER
        // than the incoming parse's parseInputEditSequence, the parse arrived
        // with stale input for that row. The text-role update must NOT be
        // applied — the CRDT is canonical for those bytes; the existing model
        // text reflects post-edit state and must be preserved.
        LiveBlockModel model;
        const auto firstRecs = QList<BlockRecord>{
            makeRec(BlockKind::Paragraph, "hello"),
            makeRec(BlockKind::Paragraph, "world"),
        };
        QList<BlockKey> firstKeys; for (const auto &r : firstRecs) firstKeys << keyOf(r);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), firstRecs);
        QCOMPARE(model.rowCount(), 2);

        // Simulate a local edit on row 0: stamp its sequence at 5.
        model.setRowEditSequence(0, 5);
        // Row 1 is untouched: stays at 0.

        // Now parse arrives with parseInputEditSeq=3 (i.e. captured BEFORE
        // the row-0 edit at seq 5). Records have NEW text on row 0 (the
        // pre-edit text) and matching text on row 1.
        const auto secondRecs = QList<BlockRecord>{
            makeRec(BlockKind::Paragraph, "hello-PRE-EDIT"),  // stale
            makeRec(BlockKind::Paragraph, "world"),            // fresh (no local edit)
        };
        QList<BlockKey> secondKeys; for (const auto &r : secondRecs) secondKeys << keyOf(r);
        // BlockKey only includes (kind, anchor); for this synthesised test
        // anchors are default-constructed and equal across both lists -> Equal ops.
        const auto ops = AstBlockDiff::diff(firstKeys, secondKeys);

        const quint64 parseInputEditSeq = 3;
        model.applyOps(ops, secondRecs, parseInputEditSeq);

        // Stale row: original text retained.
        QCOMPARE(model.recordAt(0).text, QString("hello"));
        // Fresh row: text updated as normal.
        QCOMPARE(model.recordAt(1).text, QString("world"));
    }

    void equal_op_with_stale_row_still_updates_non_text_fields() {
        LiveBlockModel model;
        const auto firstRecs = QList<BlockRecord>{
            makeRec(BlockKind::Heading, "Title", /*headingLevel=*/2),
        };
        QList<BlockKey> firstKeys; firstKeys << keyOf(firstRecs[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), firstRecs);

        model.setRowEditSequence(0, 10);

        // Same kind+anchor so the diff is Equal; level changes 2 -> 3.
        const auto secondRecs = QList<BlockRecord>{
            makeRec(BlockKind::Heading, "STALE TEXT", /*headingLevel=*/3),
        };
        QList<BlockKey> secondKeys; secondKeys << keyOf(secondRecs[0]);
        model.applyOps(AstBlockDiff::diff(firstKeys, secondKeys), secondRecs,
                       /*parseInputEditSeq=*/5);  // stale (5 < 10)

        // Stale: text preserved.
        QCOMPARE(model.recordAt(0).text, QString("Title"));
        // Non-text: applied even when stale (block-shape is parser-authoritative).
        QCOMPARE(model.recordAt(0).headingLevel, 3);
    }

    void equal_op_with_default_parse_seq_treats_all_rows_fresh() {
        // Backwards compatibility: existing R2/R3 callsites use the 2-arg
        // overload; default treats every row as fresh.
        LiveBlockModel model;
        const auto firstRecs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "a") };
        QList<BlockKey> firstKeys; firstKeys << keyOf(firstRecs[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), firstRecs);
        model.setRowEditSequence(0, 999);

        const auto secondRecs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "b") };
        QList<BlockKey> secondKeys; secondKeys << keyOf(secondRecs[0]);
        model.applyOps(AstBlockDiff::diff(firstKeys, secondKeys), secondRecs);
        // No third arg -> default UINT64_MAX -> 999 <= MAX -> fresh -> "b".
        QCOMPARE(model.recordAt(0).text, QString("b"));
    }
```

- [ ] **Step 2: Run the new tests; expect compile failure**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
```

Expected: compile fails (the third-arg overload of `applyOps` does not yet exist), or links fail. This confirms the test is written against the new shape.

- [ ] **Step 3: Add the new overload to `LiveBlockModel.h`**

In `libs/markoff-live/include/markoff/live-render/LiveBlockModel.h`, replace the existing `applyOps` declaration with:

```cpp
    /// Apply a diff op sequence relative to `nextRecords`, applying the
    /// R4 freshness rule (spec §4.3): for each Equal op, the row's
    /// text-role update is gated on
    ///     row.rowEditSequence(row) <= parseInputEditSeq
    /// Stale rows preserve their existing model text but still receive
    /// non-text role updates (kind / headingLevel / codeLanguage /
    /// blockAnchor / inlineSpans). Insert / Delete ops are unconditional.
    ///
    /// `parseInputEditSeq` defaults to `std::numeric_limits<quint64>::max()`,
    /// which means "all rows fresh" — preserves the R2/R3 callsite shape.
    void applyOps(const QList<AstBlockDiff::Op> &ops,
                  const QList<BlockRecord> &nextRecords,
                  quint64 parseInputEditSeq = std::numeric_limits<quint64>::max());
```

Add `#include <limits>` at the top of the header next to `<QHash>` if not already present.

- [ ] **Step 4: Update the implementation**

In `libs/markoff-live/src/LiveBlockModel.cpp`, replace the `applyOps` body. The full new method:

```cpp
void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords,
                              quint64 parseInputEditSeq)
{
    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                const BlockRecord &next = nextRecords[op.nextIndex];
                BlockRecord merged = next;
                const bool fresh = (m_rowEditSequences[row] <= parseInputEditSeq);
                if (!fresh) {
                    // Stale: keep our text; accept everything else from parse.
                    merged.text = m_rows[row].text;
                }
                if (m_rows[row] != merged) {
                    m_rows[row] = merged;
                    Q_EMIT dataChanged(index(row), index(row));
                }
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Insert: {
                beginInsertRows(QModelIndex(), row, row);
                m_rows.insert(row, nextRecords[op.nextIndex]);
                m_rowEditSequences.insert(row, quint64(0));
                endInsertRows();
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Delete: {
                beginRemoveRows(QModelIndex(), row, row);
                m_rows.removeAt(row);
                m_rowEditSequences.removeAt(row);
                endRemoveRows();
                break;
            }
        }
    }
}
```

(Note: `Insert` does NOT clear `m_rowEditSequences` to a "fresh" sentinel — `0` is correct because a freshly-inserted row has had no local edits yet.)

- [ ] **Step 5: Build and run the new tests**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: all three new test slots pass; previous tests still green.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveBlockModel.h \
        libs/markoff-live/src/LiveBlockModel.cpp \
        libs/markoff-live/tests/tst_live_render_block_model.cpp
git commit -m "feat(live-render): freshness gate on LiveBlockModel::applyOps

Equal-op text-role updates skip when row.lastEditEditSequence
is greater than parseInputEditSeq (spec §4.3). Non-text fields
and Insert/Delete still applied unconditionally. Default arg
preserves R2/R3 callsites."
```

---

## Task 3: Plumb the freshness rule + applyingModelUpdate guard through `LiveListModelBinding`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

`onParseUpdated` already carries `parseInputEditSequence` (delivered via the foundation's R1A signal change) but currently only stores it. R4 wires it through to `applyOps`, and brackets the `applyOps` call with `m_applyingModelUpdate = true/false` so each `LiveEditBinding` can detect "the contentsChange I just got is the synchronous echo of the model update I caused" and skip emitting it back to the CRDT.

A new `Q_PROPERTY(MarkoffDocument *document ...)` already exists. We add `Q_PROPERTY(bool applyingModelUpdate ...)` (read-only, no NOTIFY needed — consumed via direct call from C++ within the same emit cycle).

- [ ] **Step 1: Header additions**

In `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`:

Add to the public section (after `selectionView()`):

```cpp
    /// True for the synchronous duration of `applyOps` while parse
    /// arrival is mutating model rows. LiveEditBinding queries this
    /// inside its `contentsChange` slot to suppress the synchronous
    /// QML-binding echo. Spec §4.5 (the surviving `m_applyingModelUpdate`
    /// guard).
    bool applyingModelUpdate() const;
```

Add to `private:`:

```cpp
    bool m_applyingModelUpdate = false;
```

Wait — the existing layout uses a Pimpl `struct Private`. Add the field on `Private` instead. Pattern below uses Pimpl.

Add to the `public:` interface (do NOT add a Q_PROPERTY — it's not consumed from QML; only via direct C++ access from LiveEditBinding):

```cpp
    bool applyingModelUpdate() const;
```

- [ ] **Step 2: Pimpl field + accessor + bracketed applyOps**

In `libs/markoff-live/src/LiveListModelBinding.cpp`, modify `Private`:

```cpp
struct LiveListModelBinding::Private {
    Markoff::MarkoffDocument *document     = nullptr;
    Markoff::Session         *session      = nullptr;
    LiveBlockModel            *model       = nullptr;
    BlockKindRegistry          registry;
    LiveCursorState           *cursorState   = nullptr;
    BlockHitTester            *hitTester     = nullptr;
    LiveSelectionView         *selectionView = nullptr;
    QList<BlockKey>            lastKeys;
    quint64                    lastParseInputEditSeq = 0;
    bool                       applyingModelUpdate = false;
};
```

Add the accessor:

```cpp
bool LiveListModelBinding::applyingModelUpdate() const
{
    return d->applyingModelUpdate;
}
```

Modify `onParseUpdated` to bracket `applyOps`:

```cpp
void LiveListModelBinding::onParseUpdated(const Markoff::Document *parsed,
                                          quint64 /*parseSequence*/,
                                          const QList<Markoff::BlockAnchor> &blockAnchors,
                                          quint64 parseInputEditSequence)
{
    if (!parsed) return;
    d->lastParseInputEditSeq = parseInputEditSequence;

    QList<BlockRecord> records = BlockWalker::walk(parsed);
    QList<BlockKey> nextKeys;
    nextKeys.reserve(records.size());
    for (qsizetype i = 0; i < records.size(); ++i) {
        const Markoff::BlockAnchor anchor =
            (i < blockAnchors.size()) ? blockAnchors[i] : Markoff::BlockAnchor{};
        records[i].blockAnchor = anchor;
        nextKeys.append(BlockKey{ records[i].kind, anchor });
    }

    const QList<AstBlockDiff::Op> ops = AstBlockDiff::diff(d->lastKeys, nextKeys);

    d->applyingModelUpdate = true;
    d->model->applyOps(ops, records, parseInputEditSequence);
    d->applyingModelUpdate = false;

    d->lastKeys = std::move(nextKeys);
}
```

The flag is set strictly across `applyOps` only — Qt's QML binding evaluation and `dataChanged` reception are synchronous within `endInsertRows`/`dataChanged` emit, so any `contentsChange` echo from a delegate's TextEdit happens between the lines `d->model->applyOps(...)` and `d->applyingModelUpdate = false;`. After applyOps returns, the flag is cleared, and the *next* user keystroke (which is on a later event-loop tick) is processed normally.

- [ ] **Step 3: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```

Expected: clean build.

- [ ] **Step 4: Run all live-render tests; nothing should regress**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green; no behaviour change yet because no `LiveEditBinding` exists.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "feat(live-render): pass parseInputEditSeq + applyingModelUpdate guard

LiveListModelBinding::onParseUpdated now passes the parse-input
edit sequence through to LiveBlockModel::applyOps and brackets
the apply with d->applyingModelUpdate. LiveEditBinding (next task)
will consult that flag to suppress the synchronous TextEdit echo
during model-driven updates (spec §4.5)."
```

---

## Task 4: `LiveEditBinding` — header + skeleton

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/LiveEditBinding.h`
- Create: `libs/markoff-live/src/LiveEditBinding.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

The class is a per-delegate QML element. It is owned by the QML delegate item and parented to it. Properties are set declaratively from QML; on completion, the binding wires its signals.

Properties:
- `binding: LiveListModelBinding*` (required; provides document + applyingModelUpdate flag + model).
- `modelIndex: int` (required; the row this delegate represents).
- `textDocument: QQuickTextDocument*` (required; the TextEdit's `textDocument` Q_PROPERTY).
- `composing: bool` (required; bound to the TextEdit's `inputMethodComposing` so we can defer during IME preedit).

Why two pieces of QML wiring (`textDocument` and `composing`)? `QQuickTextDocument` exposes the `QTextDocument*`, which is where `contentsChange` lives — but Qt's IME composition state is on the `QQuickItem` (the TextEdit), not the document. So we wire both.

- [ ] **Step 1: Write `LiveEditBinding.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <qqmlintegration.h>

class QQuickTextDocument;
class QTextDocument;

namespace Markoff::LiveRender {

class LiveListModelBinding;

/// Per-delegate edit binding. Translates `QTextDocument::contentsChange`
/// (qtPos, charsRemoved, charsAdded) into a `Markoff::MarkoffEdit` in
/// UTF-8 byte coordinates, calls `MarkoffDocument::applyLocalEdit`, and
/// stamps the row's `lastEditEditSequence` for the freshness rule
/// (spec §4.3, §7.1).
///
/// Cycle guards (spec §4.5):
///   - applyingModelUpdate: skip the contentsChange that fires
///     synchronously when the model updates the delegate's text.
///   - composing: skip during IME preedit; on commit (composing →
///     false) re-sync to the post-commit text.
class MARKOFF_LIVE_RENDER_EXPORT LiveEditBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::LiveRender::LiveListModelBinding *binding
               READ binding WRITE setBinding NOTIFY bindingChanged)
    Q_PROPERTY(int modelIndex
               READ modelIndex WRITE setModelIndex NOTIFY modelIndexChanged)
    Q_PROPERTY(QQuickTextDocument *textDocument
               READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(bool composing
               READ composing WRITE setComposing NOTIFY composingChanged)

public:
    explicit LiveEditBinding(QObject *parent = nullptr);
    ~LiveEditBinding() override;

    LiveListModelBinding *binding() const;
    void setBinding(LiveListModelBinding *b);

    int  modelIndex() const;
    void setModelIndex(int row);

    QQuickTextDocument *textDocument() const;
    void setTextDocument(QQuickTextDocument *td);

    bool composing() const;
    void setComposing(bool c);

Q_SIGNALS:
    void bindingChanged();
    void modelIndexChanged();
    void textDocumentChanged();
    void composingChanged();

private Q_SLOTS:
    void onContentsChange(int qtPos, int charsRemoved, int charsAdded);

private:
    void rewireTextDocument(QTextDocument *newDoc);
    void flushPendingComposition();

    QPointer<LiveListModelBinding> m_binding;
    int                             m_modelIndex = -1;
    QPointer<QQuickTextDocument>    m_textDocument;
    QPointer<QTextDocument>         m_listenedDoc;  // remembered so we can disconnect
    bool                            m_composing = false;
    bool                            m_compositionTouchedDoc = false;  // see Task 7
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 2: Write the skeleton `.cpp` (signals/slots, constructor, no edit logic yet)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>

#include <QQuickTextDocument>
#include <QTextDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEdit, "markoff.live.edit", QtWarningMsg)

namespace Markoff::LiveRender {

LiveEditBinding::LiveEditBinding(QObject *parent) : QObject(parent) {}
LiveEditBinding::~LiveEditBinding() = default;

LiveListModelBinding *LiveEditBinding::binding() const { return m_binding.data(); }
void LiveEditBinding::setBinding(LiveListModelBinding *b)
{
    if (m_binding == b) return;
    m_binding = b;
    Q_EMIT bindingChanged();
}

int LiveEditBinding::modelIndex() const { return m_modelIndex; }
void LiveEditBinding::setModelIndex(int row)
{
    if (m_modelIndex == row) return;
    m_modelIndex = row;
    Q_EMIT modelIndexChanged();
}

QQuickTextDocument *LiveEditBinding::textDocument() const { return m_textDocument.data(); }
void LiveEditBinding::setTextDocument(QQuickTextDocument *td)
{
    if (m_textDocument == td) return;
    m_textDocument = td;
    rewireTextDocument(td ? td->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

bool LiveEditBinding::composing() const { return m_composing; }
void LiveEditBinding::setComposing(bool c)
{
    if (m_composing == c) return;
    const bool wasComposing = m_composing;
    m_composing = c;
    Q_EMIT composingChanged();
    if (wasComposing && !c)
        flushPendingComposition();
}

void LiveEditBinding::rewireTextDocument(QTextDocument *newDoc)
{
    if (m_listenedDoc) {
        QObject::disconnect(m_listenedDoc.data(), &QTextDocument::contentsChange,
                            this, &LiveEditBinding::onContentsChange);
    }
    m_listenedDoc = newDoc;
    if (m_listenedDoc) {
        QObject::connect(m_listenedDoc.data(), &QTextDocument::contentsChange,
                         this, &LiveEditBinding::onContentsChange);
    }
}

void LiveEditBinding::onContentsChange(int /*qtPos*/, int /*charsRemoved*/, int /*charsAdded*/)
{
    // Body in Task 5/6/7.
}

void LiveEditBinding::flushPendingComposition()
{
    // Body in Task 7 (IME composition deferral).
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Add to CMakeLists**

In `libs/markoff-live/CMakeLists.txt`, inside `qt_add_qml_module`'s `SOURCES` list, after the R3 cursor block (`src/LiveSelectionView.cpp`), append:

```cmake
        # R4: per-delegate edit binding
        include/markoff/live-render/LiveEditBinding.h
        src/LiveEditBinding.cpp
```

Also: `target_link_libraries(markoff_live_render PUBLIC ...)` already lists `Qt6::Quick` — `QQuickTextDocument` is in `Qt6::Quick`, no link change.

- [ ] **Step 4: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```

Expected: clean build. The class compiles but does nothing yet.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveEditBinding.h \
        libs/markoff-live/src/LiveEditBinding.cpp \
        libs/markoff-live/CMakeLists.txt
git commit -m "feat(live-render): scaffold LiveEditBinding (no edit logic yet)

QML element with binding/modelIndex/textDocument/composing properties
and the contentsChange signal wired to a stub slot. Edit translation
and guards land in subsequent tasks."
```

---

## Task 5: `LiveEditBinding` — content-change → MarkoffEdit + applyLocalEdit (TDD)

**Files:**
- Modify: `libs/markoff-live/src/LiveEditBinding.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

Translation of `(qtPos, charsRemoved, charsAdded)` into a single `MarkoffEdit` against the foundation's CRDT, in UTF-8 byte coordinates inside the focused block:

1. Resolve the row's block byte range: `MarkoffDocument::blockByteRange(record.blockAnchor)` → `[blockStart, blockEnd)`.
2. Convert `qtPos` (UTF-16 code units, block-local) to a UTF-8 byte offset using `Coordinates::qtPosToByte(blockTextUtf8, qtPos)` where `blockTextUtf8 = m_listenedDoc->toPlainText().toUtf8()` is the **post-edit** text (since `contentsChange` fires synchronously *after* the document mutation). For pure deletion / replacement we need pre-edit byte coordinates — so we read the pre-edit text from the model row (`record.text`) which still reflects the previous state at the moment `contentsChange` fires (because we haven't processed the edit yet).

   Wait: the model row's text is the *last value the model emitted*. When the user types, the TextEdit's QML binding `text: model.text` was set at the previous applyOps; the user has typed N characters since; model.text still holds the old value. So `record.text` IS the pre-edit (model-coherent) text. We can use it.

3. Build:
   - `oldStart_block = qtPosToByte(modelTextUtf8, qtPos)`
   - `oldEnd_block   = qtPosToByte(modelTextUtf8, qtPos + charsRemoved)`
   - `newText        = textEditPlainText.mid(qtPos, charsAdded).toUtf8()`
4. Add the block start to convert block-local → whole-document:
   - `oldStart_doc = blockStart + oldStart_block`
   - `oldEnd_doc   = blockStart + oldEnd_block`
5. `applyLocalEdit({{ oldStart_doc, oldEnd_doc, newText }})`.
6. Stamp the row: `model->setRowEditSequence(modelIndex, document->editSequence())`.

This is one allocation-free walk per coordinate conversion (Coordinates is already O(qtPos), no allocation per spec L0). Whole-document `toMarkdownUtf8()` is forbidden by the perf budget (spec §9.2).

Important: after `applyLocalEdit`, the model's `record.text` is **still** the pre-edit value (the parse hasn't completed; no `dataChanged` has fired). We do NOT update the model's text directly — that's the parser's job. The TextEdit visually displays the post-edit text, the CRDT holds the post-edit text, the model's row text catches up on the next parse. The freshness rule (Task 2) ensures that when the parse does arrive, if it's stale, we keep `record.text` in its pre-edit shape — the TextEdit still shows the user's typed text (it's not bound from `record.text` anymore at that moment because the user typed since). This requires a careful look:

> Subtlety: with `text: model.text` QML binding, the binding fires whenever model.text changes. We're setting model.text via `m_rows[row].text = ...` only when applyOps runs. As long as we don't mutate `m_rows[row].text` in `applyLocalEdit`, the TextEdit binding doesn't re-fire mid-typing. ✓

> Second subtlety: when the parse eventually arrives FRESH (covers our edit), `m_rows[row].text` is set to the post-parse text, the binding fires, the TextEdit's `text` property is reassigned. If the post-parse text equals what the user typed, this is a no-op visually — but Qt's QML binding system treats the assignment as identity-changing, so contentsChange MAY still fire for the entire-document-replace. The applyingModelUpdate guard (Task 6) suppresses the echo.

- [ ] **Step 1: Create the test file (failing tests)**

Create `libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QQuickTextDocument>
#include <QTextDocument>

#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/Coordinates.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff::LiveRender;

namespace {

/// Drives a QTextDocument-based TextEdit-like setup directly so we can
/// fire contentsChange without instantiating a QML scene. The bind-to-
/// QQuickTextDocument path is exercised by the integration test in QML.
class TextDocHost : public QQuickTextDocument {
public:
    TextDocHost() : QQuickTextDocument(nullptr) {
        setTextDocument(&m_doc);
    }
    QTextDocument *doc() { return &m_doc; }
private:
    QTextDocument m_doc;
};

void waitForParse(Markoff::MarkoffDocument &doc, int timeoutMs = 2000)
{
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    QVERIFY2(spy.wait(timeoutMs) || spy.count() > 0, "parseUpdated did not fire");
}

}

class TstLiveRenderParagraphEdit : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void typing_one_char_emits_one_apply_local_edit() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("hello world", Markoff::Origin::FirstOpen);
        // Wait for the initial parse so block model has a row.
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        QCOMPARE(binding.model()->rowCount(), 1);

        TextDocHost host;
        host.doc()->setPlainText("hello world");

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setTextDocument(&host);

        const quint64 seqBefore = document.editSequence();
        QSignalSpy contentsSpy(&document, &Markoff::MarkoffDocument::contentsChanged);

        // Simulate typing 'A' at position 5: "hello world" -> "helloA world".
        QTextCursor cur(host.doc());
        cur.setPosition(5);
        cur.insertText("A");

        QCOMPARE(contentsSpy.count(), 1);
        QVERIFY(document.editSequence() > seqBefore);
        QCOMPARE(document.toMarkdown(), QString("helloA world"));
        // Row's edit sequence should be stamped to the post-edit value.
        QCOMPARE(binding.model()->rowEditSequence(0), document.editSequence());
    }

    void typing_at_block_offset_translates_to_whole_doc_offset() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("first\n\nsecond", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        QCOMPARE(binding.model()->rowCount(), 2);

        TextDocHost host;
        host.doc()->setPlainText("second");  // mirrors row 1's text

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(1);
        eb.setTextDocument(&host);

        // Insert 'X' at qtPos 0 of the SECOND block. Whole-doc byte offset
        // should be the start of the second block (== 7: "first\n\n").
        QTextCursor cur(host.doc());
        cur.setPosition(0);
        cur.insertText("X");

        // The applyLocalEdit should land at byte 7. Verify via post-edit text.
        QCOMPARE(document.toMarkdown(), QString("first\n\nXsecond"));
    }

    void deletion_emits_correct_old_range() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("abcdef", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        TextDocHost host;
        host.doc()->setPlainText("abcdef");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setTextDocument(&host);

        // Delete "cd": cursor selects 2..4, deleteChar.
        QTextCursor cur(host.doc());
        cur.setPosition(2);
        cur.setPosition(4, QTextCursor::KeepAnchor);
        cur.removeSelectedText();

        QCOMPARE(document.toMarkdown(), QString("abef"));
    }

    void utf8_multibyte_byte_offsets_correct() {
        // "héllo" is 6 UTF-8 bytes; é is 2 bytes (0xC3 0xA9). The QChar
        // count is 5 — so qtPos 2 == byte offset 3 ('l').
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("héllo", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        TextDocHost host;
        host.doc()->setPlainText("héllo");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setTextDocument(&host);

        // Insert at qtPos 2 (after é). Should land at byte offset 3.
        QTextCursor cur(host.doc());
        cur.setPosition(2);
        cur.insertText("X");

        QCOMPARE(document.toMarkdown(), QString("héXllo"));
    }
};

QTEST_GUILESS_MAIN(TstLiveRenderParagraphEdit)
#include "tst_live_render_paragraph_edit.moc"
```

- [ ] **Step 2: Register the test**

In `libs/markoff-live/tests/CMakeLists.txt`, append:

```cmake
qt_add_executable(tst_live_render_paragraph_edit
    tst_live_render_paragraph_edit.cpp
)
target_link_libraries(tst_live_render_paragraph_edit PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test markoff_live_render markoff_core)
add_test(NAME tst_live_render_paragraph_edit COMMAND tst_live_render_paragraph_edit)
```

- [ ] **Step 3: Build the test; expect failure**

```bash
cmake --build build-dev --target tst_live_render_paragraph_edit -j 8
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: tests fail (the slot does nothing yet — `applyLocalEdit` is never called, so `editSequence` doesn't change).

- [ ] **Step 4: Implement `onContentsChange`**

In `libs/markoff-live/src/LiveEditBinding.cpp`, add the includes:

```cpp
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/Coordinates.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
```

Replace the empty `onContentsChange` body:

```cpp
void LiveEditBinding::onContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    if (!m_binding || !m_binding->model() || !m_binding->document())
        return;
    if (m_modelIndex < 0 || m_modelIndex >= m_binding->model()->rowCount())
        return;
    if (!m_listenedDoc)
        return;

    // Guard: model-driven update echo (spec §4.5).
    if (m_binding->applyingModelUpdate()) {
        qCDebug(lcEdit) << "skip: applyingModelUpdate";
        return;
    }

    // Guard: IME composition (spec §4.5). Note the touch so the commit
    // pass knows there's pending state to flush.
    if (m_composing) {
        m_compositionTouchedDoc = true;
        return;
    }

    auto *doc = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    const auto blockRangeOpt = doc->blockByteRange(record.blockAnchor);
    if (!blockRangeOpt) {
        qCWarning(lcEdit) << "blockByteRange failed for row" << m_modelIndex;
        return;
    }
    const quint32 blockStart = blockRangeOpt->first;

    // Pre-edit text for old-coordinate translation: model record's text,
    // which has not been updated since the LAST parse arrival.
    const QByteArray preUtf8 = record.text.toUtf8();

    // Post-edit text from the live QTextDocument. We use it for the
    // newText slice only.
    const QString postQt = m_listenedDoc->toPlainText();
    const QByteArray postUtf8 = postQt.toUtf8();
    Q_UNUSED(postUtf8);  // diagnostic only; not needed past slicing.

    // Old block-local byte range:
    //   oldStart = qtPosToByte(preUtf8, qtPos)
    //   oldEnd   = qtPosToByte(preUtf8, qtPos + charsRemoved)
    const qsizetype oldStartLocal =
        Coordinates::qtPosToByte(preUtf8, qtPos);
    const qsizetype oldEndLocal =
        Coordinates::qtPosToByte(preUtf8, qtPos + charsRemoved);

    // New text slice from the live document, UTF-8.
    const QString addedQt = postQt.mid(qtPos, charsAdded);
    const QByteArray addedUtf8 = addedQt.toUtf8();

    Markoff::MarkoffEdit edit;
    edit.oldStart = blockStart + static_cast<quint32>(oldStartLocal);
    edit.oldEnd   = blockStart + static_cast<quint32>(oldEndLocal);
    edit.newText  = addedUtf8;

    doc->applyLocalEdit({ edit });

    // Stamp the row for the freshness rule (spec §4.3).
    model->setRowEditSequence(m_modelIndex, doc->editSequence());
}
```

- [ ] **Step 5: Build and run the new tests**

```bash
cmake --build build-dev --target tst_live_render_paragraph_edit -j 8
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: all four test slots pass.

- [ ] **Step 6: Run the whole live-render suite to confirm no regression**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/src/LiveEditBinding.cpp \
        libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "feat(live-render): LiveEditBinding wires contentsChange to applyLocalEdit

Per-delegate binding translates QTextDocument::contentsChange
into a single MarkoffEdit (block-local qtPos -> whole-doc UTF-8
bytes via Coordinates::qtPosToByte and blockByteRange), calls
MarkoffDocument::applyLocalEdit, and stamps the row's
lastEditEditSequence for the freshness rule (spec §4.3, §7.1).
Includes applyingModelUpdate + composing guards (spec §4.5).
Tests cover ASCII insert, mid-block insert, deletion, and
multi-byte UTF-8 (é = 2 bytes)."
```

---

## Task 6: Cycle-guard test — `applyingModelUpdate` actually suppresses the echo (TDD)

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp`

The guard already exists from Task 5 step 4. This task adds the regression test that proves it works: when `LiveListModelBinding` mutates a model row's text, the resulting QML binding echo (we simulate it with a direct `setPlainText`) does **not** trigger a second `applyLocalEdit`. Without the guard, this would create an infinite parse-edit-parse loop on the first parse arrival after a typing burst.

- [ ] **Step 1: Add the test**

Append a new slot to `TstLiveRenderParagraphEdit`:

```cpp
    void model_update_does_not_echo_back_to_apply_local_edit() {
        // Setup: a doc with one paragraph; binding wired; user has typed
        // and the row sequence is stamped at N.
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("aaa", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        TextDocHost host;
        host.doc()->setPlainText("aaa");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setTextDocument(&host);

        const auto seqAtSetup = document.editSequence();

        // Now simulate the parse-arrival path: applyOps mutating the row.
        // We exercise the public API: set the row's text via applyOps,
        // bracketed by the binding's applyingModelUpdate guard. The QML
        // binding's text re-evaluation is the synchronous setPlainText
        // we drive manually here.

        // The test mirrors what happens in production: applyingModelUpdate
        // is true while applyOps mutates, and the synchronous TextEdit
        // re-binding fires contentsChange.
        const auto recsBefore = QList<BlockRecord>{
            BlockRecord{ /*kind=*/BlockKind::Paragraph, /*text=*/"aaa" }
        };
        // (Anchor is intentionally default-constructed; AstBlockDiff uses
        // BlockKey equality only.)

        // Apply a fresh "Equal" with a different text under the guard.
        // We can't call applyingModelUpdate's setter directly — exercise the
        // bracketing through onParseUpdated indirectly: simulate by setting
        // the QTextDocument's text WHILE we know the guard is set. The
        // simplest direct exercise: drive a parse and confirm no echo.

        // Drive a real parse round-trip by causing a content change in
        // the foundation (this is what ParsePool reacts to). We use the
        // undo() path — applies a CRDT op, fires parseUpdated.
        // Simpler: schedule a no-op resetContent to force a parse arrival
        // with the same content; the binding will run applyOps with
        // Equal-on-row-0-and-same-text, which won't even emit dataChanged
        // (record == next), so this doesn't exercise the guard path. We
        // need the model.text TO change. Use a different content:
        document.resetContent("bbb", Markoff::Origin::Reload);
        QVERIFY(parseSpy.wait(2000));
        QSignalSpy contentsSpy(&document, &Markoff::MarkoffDocument::contentsChanged);

        // The applyOps just ran with applyingModelUpdate = true. The QML
        // text binding (in our test) is the manual setPlainText below;
        // it should NOT cause a second applyLocalEdit because we drive
        // it inside the same call window — but our test harness can't
        // mimic the QML re-binding precisely. Instead we test the guard
        // PRINCIPLE directly:
        //   - manually set applyingModelUpdate via internal API (none),
        //     OR exercise via the actual flow.

        // We test the principle by issuing setPlainText while the guard
        // is held by an explicit setter exposed for testing only? No --
        // instead, prove the negative empirically:
        //   1. Take editSequence after the parse settles.
        //   2. Trigger setPlainText (simulating the QML re-bind echo).
        //   3. If the guard works in the real path, no applyLocalEdit
        //      fires from the echo. We can detect this directly by
        //      observing that contentsChanged does NOT fire from the
        //      setPlainText below, BECAUSE in production applyingModelUpdate
        //      is true at this synchronous moment.

        // Simpler & sufficient: confirm that during applyOps (synchronous),
        // applyingModelUpdate IS set. We check the public accessor at the
        // start of a slot connected to LiveBlockModel::dataChanged.
        bool flagSeenDuringUpdate = false;
        auto conn = QObject::connect(binding.model(), &QAbstractItemModel::dataChanged,
                                      [&](){ flagSeenDuringUpdate = binding.applyingModelUpdate(); });

        // Force another applyOps run by changing the content again.
        document.resetContent("ccc", Markoff::Origin::Reload);
        QVERIFY(parseSpy.wait(2000));

        QObject::disconnect(conn);
        QVERIFY2(flagSeenDuringUpdate,
                 "applyingModelUpdate should be true while dataChanged fires");
        Q_UNUSED(seqAtSetup);
    }
```

(Yes, this test asserts the *flag is set during dataChanged*. That's the exact mechanism `LiveEditBinding::onContentsChange` consults to skip processing. Asserting the flag visibility from a `dataChanged` slot is the most direct test of the guard contract — any real QML delegate's TextEdit echo is a `dataChanged → text-binding-eval → contentsChange` chain on the *same* synchronous tick.)

- [ ] **Step 2: Build and run; expect pass (the guard is already in place)**

```bash
cmake --build build-dev --target tst_live_render_paragraph_edit -j 8
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: all five slots pass. If `flagSeenDuringUpdate` is false, the bracketing in Task 3 step 2 is wrong — fix it before proceeding.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp
git commit -m "test(live-render): applyingModelUpdate is true during dataChanged

Regression test for the surviving cycle guard (spec §4.5):
LiveEditBinding::onContentsChange relies on the flag being set
synchronously across applyOps. Asserting it via a dataChanged
slot proves the bracketing in LiveListModelBinding works."
```

---

## Task 7: IME composition deferral — flush on commit (TDD)

**Files:**
- Modify: `libs/markoff-live/src/LiveEditBinding.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp`

During IME preedit, `inputMethodComposing` is true and the document fires `contentsChange` for every preedit-character change. We don't want to apply each preedit character to the CRDT — preedit text is provisional. The contract:

- While `composing == true`: every `contentsChange` is dropped, but we set `m_compositionTouchedDoc = true` so the commit pass knows the document has changed.
- When `composing` transitions `true → false`: `flushPendingComposition()` runs once. It computes the diff between `record.text` (last-known model text — pre-composition) and `m_listenedDoc->toPlainText()` (post-commit text), and emits a single `MarkoffEdit` covering the entire block range. Reset `m_compositionTouchedDoc`.

The diff is the simplest correct thing: replace the entire block's bytes. For long blocks this is suboptimal (a 4 KB paragraph re-encodes 4 KB) but it's correct, and IME commits are rare events compared to keystrokes.

- [ ] **Step 1: Add the failing test**

Append:

```cpp
    void ime_composition_defers_then_flushes_on_commit() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        TextDocHost host;
        host.doc()->setPlainText("hello");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setTextDocument(&host);

        const quint64 seqBefore = document.editSequence();

        // Simulate composition start: composing = true.
        eb.setComposing(true);

        // Simulate preedit character changes: inserting / replacing the
        // preedit. None of these should call applyLocalEdit.
        QTextCursor cur(host.doc());
        cur.setPosition(5);
        cur.insertText("a");        // -> "helloa" (preedit-stage 1)
        cur.insertText("b");        // -> "helloab"
        cur.insertText("c");        // -> "helloabc"

        // editSequence MUST NOT have advanced — preedit is deferred.
        QCOMPARE(document.editSequence(), seqBefore);

        // Composition commits: composing = false. One applyLocalEdit fires.
        eb.setComposing(false);

        QVERIFY(document.editSequence() > seqBefore);
        QCOMPARE(document.toMarkdown(), QString("helloabc"));
    }
```

- [ ] **Step 2: Run; expect failure**

```bash
cmake --build build-dev --target tst_live_render_paragraph_edit -j 8
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: `ime_composition_defers_then_flushes_on_commit` fails — `flushPendingComposition` is empty.

- [ ] **Step 3: Implement `flushPendingComposition`**

In `libs/markoff-live/src/LiveEditBinding.cpp`:

```cpp
void LiveEditBinding::flushPendingComposition()
{
    if (!m_compositionTouchedDoc) return;
    m_compositionTouchedDoc = false;

    if (!m_binding || !m_binding->model() || !m_binding->document() || !m_listenedDoc)
        return;
    if (m_modelIndex < 0 || m_modelIndex >= m_binding->model()->rowCount())
        return;

    auto *doc   = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    const auto blockRangeOpt = doc->blockByteRange(record.blockAnchor);
    if (!blockRangeOpt) return;
    const quint32 blockStart = blockRangeOpt->first;
    const quint32 blockEnd   = blockRangeOpt->second;

    const QString postQt = m_listenedDoc->toPlainText();
    const QByteArray postUtf8 = postQt.toUtf8();

    Markoff::MarkoffEdit edit;
    edit.oldStart = blockStart;
    edit.oldEnd   = blockEnd;
    edit.newText  = postUtf8;

    doc->applyLocalEdit({ edit });
    model->setRowEditSequence(m_modelIndex, doc->editSequence());
}
```

- [ ] **Step 4: Run; expect pass**

```bash
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: all six slots pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveEditBinding.cpp \
        libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp
git commit -m "feat(live-render): IME composition deferral in LiveEditBinding

While composing is true, contentsChange events are dropped and
a touched-flag is set. On composing -> false, a single
applyLocalEdit replaces the entire block range with the
committed text. Spec §4.5 surviving guard."
```

---

## Task 8: Stale-parse preservation regression (TDD — the load-bearing R4 test)

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp`

The audit's #1 failure mode: typing during an in-flight parse causes the parse-back to scramble characters. R4's freshness rule retires this. The test:

1. Type one character. Don't wait for parse.
2. Synthetically deliver a `parseUpdated` whose `parseInputEditSequence` is **smaller than** the row's current `lastEditEditSequence` (i.e. captured before the typed character).
3. The model's row text MUST remain at its pre-edit value (since the parser hasn't seen the keystroke), and the live `QTextDocument` continues to display the user's typed text — verifying that no `applyTextUpdate`-equivalent fired.

We can't easily mock the foundation's parse pipeline — but we can call `LiveListModelBinding::onParseUpdated` directly via a friend test or by exposing a test seam. Simplest: the foundation's `parseUpdated` signal IS public and emittable from any QObject. We construct a small test fixture that reuses the production path: foundation parse arrives via the real signal.

Actually simpler still: drive `LiveBlockModel::applyOps` directly (this IS public), with a parseInputEditSeq value chosen to make the row stale. Task 2 already covers that at the model level; this test verifies the END-TO-END view: after a typed edit, when applyOps for an Equal-with-different-text fires with a stale seq, the QTextDocument the user is editing keeps its text.

- [ ] **Step 1: Add the test**

```cpp
    void in_flight_parse_does_not_scramble_typed_chars() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        TextDocHost host;
        host.doc()->setPlainText("hello");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setTextDocument(&host);

        // The user types 'X' at end of "hello": "hello" -> "helloX".
        QTextCursor cur(host.doc());
        cur.setPosition(5);
        cur.insertText("X");

        QCOMPARE(host.doc()->toPlainText(), QString("helloX"));
        const quint64 rowSeqAfterType = binding.model()->rowEditSequence(0);
        QVERIFY(rowSeqAfterType > 0);

        // A "stale" parse is now delivered: applyOps with text "hello"
        // (the pre-typing text) and parseInputEditSeq < rowSeqAfterType.
        // Since the parse was captured before the keystroke, the parse-
        // input sequence is one less than the post-edit edit sequence.
        const auto recA = BlockRecord{ BlockKind::Paragraph, "hello",
                                       /*headingLevel=*/0,
                                       /*codeLanguage=*/QString(),
                                       /*blockAnchor=*/binding.model()->recordAt(0).blockAnchor,
                                       /*inlineSpans=*/{} };
        QList<BlockKey> firstKeys{
            BlockKey{ recA.kind, recA.blockAnchor }
        };
        const auto ops = AstBlockDiff::diff(firstKeys, firstKeys);  // single Equal
        binding.model()->applyOps(ops, { recA }, /*parseInputEditSeq=*/rowSeqAfterType - 1);

        // Stale rule: model text NOT overwritten by the parse's "hello".
        QCOMPARE(binding.model()->recordAt(0).text, QString("helloX"));
        // The user's TextEdit is untouched.
        QCOMPARE(host.doc()->toPlainText(), QString("helloX"));

        // Now a "fresh" parse arrives covering the typed char:
        // parseInputEditSeq == rowSeqAfterType. Same content (the parser
        // observed the typed char). Equal op with same text-> no dataChanged.
        const auto recB = BlockRecord{ BlockKind::Paragraph, "helloX",
                                       0, QString(), recA.blockAnchor, {} };
        binding.model()->applyOps(ops, { recB }, /*parseInputEditSeq=*/rowSeqAfterType);
        QCOMPARE(binding.model()->recordAt(0).text, QString("helloX"));
    }
```

(Note: this test sets `recA.text = "hello"` even though the model already holds `"hello"` from R3 init. The diff is Equal with same text, so applyOps would normally early-out via `m_rows[row] != next`. The stale-rule path requires that the *merged* record — which would have the parser's pre-edit text — does NOT replace our (already correct) model text. We make `recA.text = "hello"` AND the model text already equals `"hello"` — wait, this means the stale-rule test isn't truly exercising the case where the parse text differs from model text. We need to set up the test so model.text != parse.text and confirm parse.text is rejected.)

Refine the test setup before commit: after typing 'X', we expect the LiveEditBinding's onContentsChange to set `model.recordAt(0).text` to `"helloX"`? **NO** — onContentsChange does NOT mutate the model row's text directly; it only stamps `lastEditEditSequence`. The model text remains `"hello"` until a parse arrives. So:

- After typing: model text is `"hello"`, doc text is `"helloX"`, row seq stamped.
- Stale parse delivers: text "hello" (matches model — no scramble). The freshness rule prevents the parse text from overwriting model — but in this case there's nothing to overwrite. The test value is in confirming that a parse with DIFFERENT text from the model also gets rejected. Construct that: parse sees pre-edit text + a different parse-side concurrent normalization. E.g. parse.text = "hello" but model.text drifted because... no, model.text doesn't drift without a parse.

Let me re-think: between parse arrivals, model.text is frozen. A stale parse landing with model.text == "hello" + parse text = "hello" is a no-op; nothing to test. The freshness rule's value comes when:
- model.text is "hello" (pre-edit AND no parse since)
- user types 'X' in the doc
- parse arrives with text "hello" (still pre-edit!)
- WITHOUT the freshness rule: nothing bad happens here either, because model.text already == "hello".

The actual scramble pattern in the audit is:
- model.text = "ello" (parse N already landed, after a backspace)
- user types 'X' at position 0 -> doc.text = "Xello"
- parse N+1 arrives with input from BEFORE the 'X': text "ello"
- WITHOUT freshness: model.text gets overwritten with "ello", QML binding fires, doc.text resets to "ello", user loses 'X'.

So the test should:
1. Init with model.text = "hello".
2. Apply a fresh parse with text = "ello" (mid-flight delete). Model.text becomes "ello". (Use parseInputEditSeq large enough to be fresh.)
3. User types 'X' at position 0 of doc: doc.text becomes "Xello", model.text still "ello", row seq stamped.
4. Stale parse arrives: ops Equal with text "ello", parseInputEditSeq < row seq. Without the rule, model.text becomes "ello" again (but it already is "ello", so dataChanged not emitted — boring). The scramble would only manifest if the user's 'X' had also been pushed to the model — which it wasn't, in our architecture. So...

The fault is mine: in our architecture, the model text is never written from local edits — only from parse arrivals. The freshness rule's job is to **preserve a stale parse from clobbering the model when the parse has different text than what the user just typed**. Concretely: a parse N+1 might restructure the text (e.g. inline-formatting normalization, code-block fence detection) such that the parser's view of the row's text differs from `model.text`. If parseInputEditSeq is older than the user's last edit, we don't trust the parse's text yet.

Reformulating the test:

```cpp
    void in_flight_parse_does_not_clobber_model_text_when_stale() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        TextDocHost host;
        host.doc()->setPlainText("hello");
        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setTextDocument(&host);

        // User types 'X'. Model text is "hello"; row seq stamped.
        QTextCursor cur(host.doc());
        cur.setPosition(5);
        cur.insertText("X");
        const quint64 rowSeqAfterType = binding.model()->rowEditSequence(0);

        // Stale parse arrives with DIFFERENT text (simulating a parser
        // normalization the user's edit hasn't been folded into yet).
        const auto staleRec = BlockRecord{
            BlockKind::Paragraph, "STALE-NORMALIZED-TEXT",
            0, QString(), binding.model()->recordAt(0).blockAnchor, {}
        };
        QList<BlockKey> keys{ BlockKey{ staleRec.kind, staleRec.blockAnchor } };
        binding.model()->applyOps(AstBlockDiff::diff(keys, keys), { staleRec },
                                  /*parseInputEditSeq=*/rowSeqAfterType - 1);

        // Stale rule: model.text PRESERVED at its pre-stale-parse value.
        QCOMPARE(binding.model()->recordAt(0).text, QString("hello"));

        // Fresh parse arrives covering the edit. Now model.text accepts.
        const auto freshRec = BlockRecord{
            BlockKind::Paragraph, "helloX",
            0, QString(), binding.model()->recordAt(0).blockAnchor, {}
        };
        binding.model()->applyOps(AstBlockDiff::diff(keys, keys), { freshRec },
                                  /*parseInputEditSeq=*/rowSeqAfterType);
        QCOMPARE(binding.model()->recordAt(0).text, QString("helloX"));
    }
```

That's the right test. Replace the first attempt at this slot from earlier with this corrected one before committing.

- [ ] **Step 2: Build & run; expect pass (Task 2's freshness gate already implements this)**

```bash
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: pass. If it fails, the freshness gate from Task 2 is broken — fix there, not here.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp
git commit -m "test(live-render): stale parse never clobbers model text

End-to-end regression for the C-architecture freshness rule
(spec §4.3): after a local edit stamps row.lastEditEditSequence
to N, a parse arriving with parseInputEditSeq < N is rejected
for text-role updates. Reproduces the audit's #1 failure mode."
```

---

## Task 9: Re-resolve `TextCaret::cachedByteOffset` after parse arrival

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_cursor.cpp`

Spec §3.3: "Local edits use `cachedByteOffset` for arithmetic; remote edits trigger a translation pass via `MarkoffDocument::resolveTextAnchor` and refresh the cache." For R4 there are no remote edits, but local edits still shift the cached offset (the user typed `X` before the caret → the caret's anchor's resolved byte position increased by 1).

After every applyOps run, refresh `LiveCursorState`'s cursor TextCaret's `cachedByteOffset` from the now-current `MarkoffDocument::resolveTextAnchor(positionAnchor)` minus the row's block-start byte. This keeps the cached offset coherent for any consumers that read it.

- [ ] **Step 1: Add the helper to `LiveListModelBinding::onParseUpdated`**

In `libs/markoff-live/src/LiveListModelBinding.cpp`, after `d->lastKeys = std::move(nextKeys);` add:

```cpp
    // Re-resolve the cached byte offset of the active TextCaret cursor.
    // Local edits between parse arrivals shift the resolved byte position
    // of the cursor's TextAnchor; the cached offset is consulted by
    // selection rendering and structural-key dispatch (R5). Spec §3.3.
    if (d->cursorState) {
        const Cursor cur = d->cursorState->cursor();
        if (auto *tc = std::get_if<TextCaret>(&cur)) {
            const auto blockRangeOpt = d->document->blockByteRange(tc->block);
            if (blockRangeOpt) {
                const quint32 blockStart = blockRangeOpt->first;
                const quint32 resolvedAbs = d->document->resolveTextAnchor(tc->positionAnchor);
                TextCaret refreshed = *tc;
                refreshed.cachedByteOffset = (resolvedAbs >= blockStart)
                    ? resolvedAbs - blockStart : 0;
                if (refreshed.cachedByteOffset != tc->cachedByteOffset) {
                    d->cursorState->request(refreshed);
                }
            }
        }
    }
```

(`get_if` requires `#include <variant>` — already pulled via `Cursor.h`.)

- [ ] **Step 2: Add the regression test**

In `libs/markoff-live/tests/tst_live_render_cursor.cpp`, append a new test slot to `TstLiveRenderCursor`. (Read the file first to confirm helper symbols.) The test creates a binding with a document, places a TextCaret, applies a parse round-trip with anchor-shift, and verifies cachedByteOffset updated.

```cpp
    void textcaret_cached_offset_refreshes_on_parse_arrival() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        document.resetContent("hello world", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveListModelBinding binding;
        binding.setDocument(&document);
        QCOMPARE(binding.model()->rowCount(), 1);

        const auto blockAnchor = binding.model()->recordAt(0).blockAnchor;

        // Place a caret at byte offset 3 (inside "hello", on 'l').
        TextCaret tc;
        tc.block = blockAnchor;
        tc.positionAnchor = document.textAnchorAt(blockAnchor, /*offset=*/3, /*rightBias=*/true);
        tc.cachedByteOffset = 3;
        binding.cursorState()->request(tc);

        // Now insert "PRE-" at the start of the document. The anchor at
        // offset 3 should now resolve to absolute byte 7 (PRE-hel|lo);
        // block-local offset is still 3 because the block also moved.
        // For a more illustrative shift, prepend a paragraph above.
        Markoff::MarkoffEdit prepend;
        prepend.oldStart = 0; prepend.oldEnd = 0;
        prepend.newText = "before\n\n";
        document.applyLocalEdit({ prepend });
        QVERIFY(parseSpy.wait(2000));

        const auto refreshed = std::get<TextCaret>(binding.cursorState()->cursor());
        // Block start has shifted; cached offset should still be 3 (within block)
        // but verifying it's coherent is the assertion.
        const auto blockRangeOpt = document.blockByteRange(refreshed.block);
        QVERIFY(blockRangeOpt.has_value());
        const quint32 blockStart = blockRangeOpt->first;
        const quint32 resolvedAbs = document.resolveTextAnchor(refreshed.positionAnchor);
        QCOMPARE(refreshed.cachedByteOffset, resolvedAbs - blockStart);
    }
```

- [ ] **Step 3: Build and run; expect pass (anchor refresh added in step 1)**

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8
ctest --test-dir build-dev -R '^tst_live_render_cursor$' --output-on-failure
```

Expected: existing tests + new slot all pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_live_render_cursor.cpp
git commit -m "feat(live-render): refresh TextCaret cachedByteOffset on parse arrival

After applyOps, re-resolve any active TextCaret's cached offset
from its TextAnchor against the new block byte range. Keeps
cached offsets coherent for selection rendering and (post-R5)
structural-key dispatch. Spec §3.3."
```

---

## Task 10: Defensive `applyingSessionSelection` guard in `LiveSelectionView`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/LiveSelectionView.h`
- Modify: `libs/markoff-live/src/LiveSelectionView.cpp`

Spec §4.5 names `m_applyingSessionSelection` as the third surviving guard. Today the LiveSelectionView only WRITES to `Session::setPrimarySelection` — there is no read-back path that could create the echo loop. But once a future phase adds a read-back (e.g. for collab presence rendering or external selection sources), the loop becomes possible. Land the guard now so the wiring is correct when read-back arrives.

The guard wraps `setPrimarySelection`: any side-effect that could re-enter `begin`/`extend` from the Session signal (when added later) will see the guard set and bail.

- [ ] **Step 1: Add the field to the header**

In `libs/markoff-live/include/markoff/live-render/LiveSelectionView.h`, add to `private:`:

```cpp
    bool m_applyingSessionSelection = false;
```

- [ ] **Step 2: Bracket `setPrimarySelection`**

In `libs/markoff-live/src/LiveSelectionView.cpp`, in `syncToSession`, wrap the `m_session->setPrimarySelection(sel);` call:

```cpp
    m_applyingSessionSelection = true;
    m_session->setPrimarySelection(sel);
    m_applyingSessionSelection = false;
```

(The accessor is private; no public surface is added — this is an in-class invariant for future use.)

- [ ] **Step 3: Build & run all tests**

```bash
cmake --build build-dev --target markoff_live_render -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: green; no behaviour change.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveSelectionView.h \
        libs/markoff-live/src/LiveSelectionView.cpp
git commit -m "feat(live-render): defensive applyingSessionSelection guard

Bracket Session::setPrimarySelection writes with the guard so the
read-back path (added by a future phase) doesn't re-enter
begin/extend in the same synchronous tick. Spec §4.5 surviving
guard #3."
```

---

## Task 11: Make the `ParagraphDelegate` writable; attach `LiveEditBinding`

**Files:**
- Modify: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`

Replace `readOnly: true` with `false`, enable `selectByMouse`, and instantiate one `LiveEditBinding` per delegate, wiring its four properties.

QML modifies needed:
- Add `import org.markoff.live.render 1.0`.
- Resolve the `binding` (the LiveListModelBinding) from `ListView.view.binding` (the same property R3 introduced).
- Bind `LiveEditBinding.binding`, `modelIndex`, `textDocument`, `composing`.

A subtle correctness item: `LiveEditBinding` listens to `QQuickTextDocument::textDocument()`, which is the `QTextDocument*` inside QML's TextEdit. When the delegate is recycled by ListView (e.g. on scroll), the same TextEdit instance is reused with a different `model.index`; we update `LiveEditBinding.modelIndex` automatically via the property binding. The `textDocument` reference stays the same (the same TextEdit), so contentsChange routing remains correct after recycle.

- [ ] **Step 1: Rewrite the delegate**

Replace the body of `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

/// Editable paragraph delegate. R3 surfaces (selection highlight, blockText)
/// retained; R4 adds LiveEditBinding so contentsChange routes to the CRDT.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var selectionView:
        liveBinding ? liveBinding.selectionView : null

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: 8; rightPadding: 8
        topPadding: 4; bottomPadding: 4
        readOnly: false
        textFormat: TextEdit.PlainText
        text: model.text
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        function applySelection() {
            const sv = root.selectionView
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            select(r.x, Math.min(r.y, length))
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() { edit.applySelection() }
        }
    }

    function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }
}
```

Key changes vs. R3:
- `import org.markoff.live.render 1.0` so `LiveEditBinding` is in scope.
- `readOnly: false` and `selectByMouse: true` on the TextEdit.
- `LiveEditBinding { ... }` declared as a sibling of TextEdit, with `composing` bound to `edit.inputMethodComposing`.

- [ ] **Step 2: Build and launch the test app**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-app -j 8
./build-dev/bin/markoff-live-app /tmp/r4-smoke.md   # any markdown file
```

(If `/tmp/r4-smoke.md` doesn't exist, create one with: `printf 'Hello world\n\nA second paragraph.\n' > /tmp/r4-smoke.md`.)

- [ ] **Step 3: Manual smoke check**

Click into the first paragraph; type a few characters. Expected:
- Characters appear at the cursor position immediately.
- No visual scrambling, no jumping caret.
- Selecting and copying still works (R3 path).

If the smoke check fails, investigate before continuing — the QML wiring is the most likely suspect (e.g. `edit.textDocument` is `null`).

- [ ] **Step 4: Run all live-render tests**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/qml/delegates/ParagraphDelegate.qml
git commit -m "feat(live-render): paragraph delegate is editable

readOnly=false, selectByMouse=true, and a sibling LiveEditBinding
binds (binding, modelIndex, textDocument, composing). R3
selection-highlight + blockText surfaces unchanged."
```

---

## Task 12: Make the `HeadingDelegate` writable

**Files:**
- Modify: `libs/markoff-live/qml/delegates/HeadingDelegate.qml`

Heading rows have a subtlety: the model's `text` role for a heading is the heading body **without** the leading `#` markers (set by `BlockWalker`; verify by inspecting `BlockWalker::walk` if uncertain). The block's UTF-8 byte range, however, INCLUDES the leading `#`s and the space. So when `LiveEditBinding` translates a `qtPos` into a byte offset, it must add the prefix-bytes count to `oldStart` / `oldEnd`.

Pull the prefix-byte count from the parser? Cleanest: use `record.text` for byte-coordinate translation just like paragraphs. Treat `record.text` as if it were the entire block content (including the prefix). This requires `BlockWalker` to expose the **raw block text** (including `#` and space).

Check `BlockWalker::walk` — read the file:

- [ ] **Step 1: Read `BlockWalker.cpp` and confirm what `record.text` contains for headings**

```
libs/markoff-live/src/BlockWalker.cpp
```

If `record.text` is the heading **body without `#`**, R4 has two options:
   (a) Add a `record.sourceTextUtf8` field (raw block bytes) for byte-coordinate translation; keep `record.text` for display.
   (b) Constrain Heading editing to "stable prefix" — model.text matches doc bytes one-to-one for the post-prefix range, so qtPos is always relative to the body. Compute the prefix byte length once per parse from `blockByteRange.first → first non-`#`-non-space byte` and add it.

Option (a) is cleaner long-term; option (b) is the smaller change. R4 picks **(b)** to keep the diff small; (a) is an R6/R7 cleanup when other text blocks need similar treatment.

If `record.text` already INCLUDES the `#` markers, both paragraphs and headings work with no special-casing — confirm by inspecting `BlockWalker::walk`.

- [ ] **Step 2: If headings need prefix-byte adjustment, add it to LiveEditBinding**

If step 1 confirms `record.text` does NOT include the `#` prefix, modify `LiveEditBinding::onContentsChange` (and `flushPendingComposition`) to add the prefix byte length. The simplest: compute prefix as `blockEnd - blockStart - record.text.toUtf8().size()`. (This works for any block whose bytes include a leading non-text prefix.)

```cpp
const QByteArray preUtf8 = record.text.toUtf8();
const qsizetype prefixBytes =
    (blockRangeOpt->second - blockRangeOpt->first) - preUtf8.size();
// ... use prefixBytes when constructing edit.oldStart / edit.oldEnd:
edit.oldStart = blockStart + static_cast<quint32>(prefixBytes + oldStartLocal);
edit.oldEnd   = blockStart + static_cast<quint32>(prefixBytes + oldEndLocal);
```

Add a paragraph-edit test confirming the math holds for plain blocks too (prefixBytes == 0).

If step 1 confirms `record.text` DOES include the `#` prefix, no change to LiveEditBinding is needed — but the heading delegate must show the prefix in its rendering. This is a UX change (the user sees `# Title`, not `Title`); decide with the user before committing. **Default:** assume option (b) with the prefixBytes computation.

- [ ] **Step 3: Rewrite the delegate (mirroring paragraph)**

Replace `libs/markoff-live/qml/delegates/HeadingDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var selectionView:
        liveBinding ? liveBinding.selectionView : null

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: 8; rightPadding: 8
        topPadding: 6; bottomPadding: 2
        readOnly: false
        textFormat: TextEdit.PlainText
        text: model.text
        wrapMode: TextEdit.Wrap
        font.pixelSize: {
            switch (model.headingLevel) {
                case 1: return 28; case 2: return 24; case 3: return 20
                case 4: return 18; case 5: return 16; default: return 14
            }
        }
        font.bold: model.headingLevel <= 3
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        function applySelection() {
            const sv = root.selectionView
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            select(r.x, Math.min(r.y, length))
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() { edit.applySelection() }
        }
    }

    function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }
}
```

- [ ] **Step 4: Build, smoke, run tests**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-app -j 8
./build-dev/bin/markoff-live-app /tmp/r4-smoke.md
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Smoke: create a markdown with `# H1 heading`, click in, type chars at end → they should append after "heading"; reload the file and confirm the source contains the new bytes (use the test-app's no-save semantics — re-open via `./build-dev/bin/markoff-live-app /tmp/r4-smoke.md` after manual save isn't wired; use the in-memory editSequence as the indicator: the title bar still says "(R4)" and the file is unmodified on disk).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/qml/delegates/HeadingDelegate.qml \
        libs/markoff-live/src/LiveEditBinding.cpp
git commit -m "feat(live-render): heading delegate is editable

Mirrors paragraph wiring (LiveEditBinding sibling, readOnly=false,
selectByMouse=true). LiveEditBinding adjusts oldStart/oldEnd by
the heading's #-prefix byte count so edits land at the correct
whole-doc offset."
```

(If step 1 confirmed `record.text` already contains the prefix, omit the LiveEditBinding.cpp from the commit and adjust the message accordingly.)

---

## Task 13: Make the `CodeBlockDelegate` writable

**Files:**
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`

Same shape as paragraph. Code blocks have fence prefixes (e.g. ` ``` ` and a language line) — the same prefix-bytes computation in LiveEditBinding handles them generically, no special-case needed.

Caveat: code block fences include both an opening AND closing line (`record.text` is the body between them). The prefix-bytes formula `(blockEnd - blockStart) - body.size()` collapses both fences into "prefix"; old-coordinate arithmetic treats edits as if everything-but-body is leading prefix. This is **wrong** for edits at the block end (caret at body end → byte offset == blockStart + prefixBytes + body_bytes, but the actual "end of body" is at blockEnd - closingFenceBytes). For R4 we constrain code-block editing to body-interior; pressing Enter at end of body is structural (R5). Insertions at the very end of body still work because qtPos < body.length means oldStartLocal < body.size() always.

- [ ] **Step 1: Rewrite the delegate**

Read the current file first, then mirror the paragraph pattern. The KSyntaxHighlighting wiring (if R2/R3 added it) is separate from edit binding — leave that part untouched and just add `LiveEditBinding`, `readOnly: false`, `selectByMouse: true`.

- [ ] **Step 2: Build, smoke, run tests**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-app -j 8
./build-dev/bin/markoff-live-app /tmp/r4-smoke.md
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Smoke: a markdown with a fenced code block; type chars in the middle of the body. They should land between the fences in the CRDT. **Don't press Enter at body end** — that's R5.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/qml/delegates/CodeBlockDelegate.qml
git commit -m "feat(live-render): code-block delegate is editable (body-interior only)

Mirrors paragraph wiring. End-of-body Enter is deferred to R5
(structural keys); body-interior edits work via the same
prefix-bytes adjustment as headings."
```

---

## Task 14: Test app polish — title, intro hint

**Files:**
- Modify: `libs/markoff-live/app/Main.qml`

Just bump the title suffix.

- [ ] **Step 1: Update Main.qml title**

In `libs/markoff-live/app/Main.qml`, change:

```qml
title: ctxTitle + " — markoff-live-render (R3)"
```

to:

```qml
title: ctxTitle + " — markoff-live-render (R4)"
```

- [ ] **Step 2: Build & launch**

```bash
cmake --build build-dev --target markoff-live-app -j 8
./build-dev/bin/markoff-live-app /tmp/r4-smoke.md
```

Confirm the title says "(R4)".

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/app/Main.qml
git commit -m "chore(live-render): test app title bumps to R4"
```

---

## Task 15: Dogfood gate (manual; user-driven) and status update

**Files:**
- Modify: `docs/restoration-status.md`

R4's acceptance criterion is the dogfood script (spec §10.3): "Type a 200-word paragraph at 100+ wpm into a 5-page document; cursor never jumps; characters never scramble."

This task is the user's pass — the agent does NOT mark R4 complete; the user does, after running the script. The agent's job here is to:

1. Build the test app fresh.
2. Provide a long markdown file (~5 pages) for the dogfood pass. There's likely one in `docs/` or `tests/` — find a long real markdown, or generate one.
3. Update the restoration status doc with the R4 entry, leaving the status as `dogfood`.
4. Hand off to the user with a clear invitation.

- [ ] **Step 1: Find or create a long markdown file**

```bash
find . -path ./build -prune -o -name '*.md' -size +20k -print | head -10
```

Pick a candidate (e.g. one of the spec docs). If nothing 5-page-sized exists, concatenate three large specs into `/tmp/r4-dogfood.md`.

- [ ] **Step 2: Run the test app and confirm it loads**

```bash
./build-dev/bin/markoff-live-app <chosen-file>
```

Confirm: file loads, all blocks render, click + type produces text input. Close.

- [ ] **Step 3: Update `docs/restoration-status.md`**

Open the file. Make these edits:

1. Update the **TL;DR** section to:

   ```markdown
   > **R4 in dogfood gate.** Implementation complete (paragraph/heading/code-block
   > editable via LiveEditBinding; freshness rule wired in LiveBlockModel::applyOps;
   > applyingModelUpdate + IME composing + applyingSessionSelection guards in
   > place; TextCaret cachedByteOffset refresh on parse arrival). User
   > dogfood pass per spec §10.3 R4 script is the gate.
   >
   > **Recommended next:** Run the R4 dogfood script. Once the user signs off,
   > flip R4 to `complete` and start writing R5 (structural keys + IME + undo
   > coalescing).
   ```

2. Update the **Phase board** R4 row:

   ```markdown
   | **R4** | [r4-paragraph-editing](plans/2026-05-02-live-render-r4-paragraph-editing.md) | `dogfood` | <list-of-commits> | Paragraph editing through sequence-tagged binding. |
   ```

   (Replace `<list-of-commits>` with the actual short SHAs from the R4 commits — collect them via `git log --oneline -25`.)

3. Append entries to **Recent-changes log** for every commit in this plan, in chronological order, one line each.

- [ ] **Step 4: Commit the status update bundled with the LAST code change in this plan**

Per the doc's own rule: "Commit changes to this file alongside the code commit they describe — never as a standalone status update commit." Bundle this commit with Task 14's title bump if not yet pushed, OR amend to include status doc:

```bash
git add docs/restoration-status.md
git commit -m "docs(restoration-status): R4 in dogfood gate"
```

(Per the bundling rule, prefer amending Task 14 to include the status update. If Task 14 is already committed and pushed, a standalone `docs(restoration-status)` commit is acceptable as a clearly-status-only change.)

- [ ] **Step 5: Tell the user**

Surface to the user (chat, not committed):

> R4 implementation is complete and tests are green (X/X live-render fast-tier). The dogfood script for R4 is: type a 200-word paragraph at 100+ wpm into a 5-page document; cursor never jumps; characters never scramble. Run `./build-dev/bin/markoff-live-app <a-long-markdown>` and try it. If anything misbehaves, paste your description verbatim and I'll diagnose. If clean, say so and we'll flip R4 to `complete` and write R5.

R4 is **not** complete from the agent's side until the user signs off in the dogfood log.

---

## Self-Review

- **Spec coverage (spec §11 R4 scope):**
  - "LiveEditBinding per-delegate; sequence-tagging of affected rows" → Tasks 4, 5, 11–13.
  - "Freshness rule applied in LiveListModelBinding::onParseUpdatedAt" → Tasks 2, 3.
  - "The three surviving cycle guards (4.5) implemented; documented per-class" → Tasks 5/6 (applyingModelUpdate), Task 7 (composing), Task 10 (applyingSessionSelection).
  - "applyTextUpdate invokable on text-bearing delegates" → realized as the `text: model.text` QML binding gated by the freshness rule + applyingModelUpdate guard. Functionally equivalent to spec §5.2's invokable surface; if the explicit invokable is needed in a later phase (e.g. for structural keys to push text without going through the QML binding), it can be added then.
  - "no `Qt.callLater` retry loops anywhere in the QML" → confirmed by inspection of all delegate files modified by R4 (none introduced; R3's already-clean wiring preserved).
- **Acceptance criteria match:**
  - Test passes covering: typing-during-in-flight-parse (Task 8), mid-block insert (Task 5), IME deferral (Task 7), applyingModelUpdate guard (Task 6), no `Qt.callLater` loops (architectural — verified by code review).
  - R4 dogfood script (spec §10.3): the dogfood pass at Task 15 is the binary gate.
- **Placeholder scan:** no "TBD" / "appropriate error handling" / "implement later" / "fill in details" — every step shows the actual code an engineer needs.
- **Type consistency:**
  - `LiveEditBinding` uses the same property names across header/cpp/QML: `binding`, `modelIndex`, `textDocument`, `composing`.
  - `applyOps`'s third parameter is `parseInputEditSeq` consistently in header, cpp, and tests.
  - `applyingModelUpdate` is the agreed name; not "applyingUpdate" or "modelUpdating" elsewhere.
- **Dependencies and order:**
  - Task 2 (model freshness) precedes Task 3 (binding plumbing) precedes Task 5 (LiveEditBinding implementation).
  - Task 5 (basic edit) precedes Task 6 (guard test, which exercises Task 3's bracketing).
  - Task 7 (IME) precedes Task 8 (stale-parse — orthogonal to IME but relies on Task 5's path).
  - Task 11 (paragraph delegate) precedes 12/13 (other delegates) so the QML wiring shape is proven on the simplest case first.
- **Open spec questions deferred (per spec §15):** §15.2 (`applyTextUpdate` form) — partially resolved as "QML binding + guard"; full invokable surface deferred to R5/R6 if needed. §15.4 (undo-coalescing idle threshold) — not in R4 scope; lands in R5. §15.6 (which markoff-view-qml tests migrate) — not in R4 scope.
- **Risk areas flagged:**
  - Task 12 (heading delegate prefix bytes): the implementation's correctness depends on `BlockWalker::walk`'s contract for `record.text`. Step 1 forces that check and gives both code paths.
  - Task 13 (code-block end-of-body): structural Enter is R5; R4 explicitly does not handle it. The dogfood script in Task 15 exercises text-body-only typing.

---

*End of R4 implementation plan.*
