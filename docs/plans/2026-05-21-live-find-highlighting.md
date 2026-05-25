# Live-mode find highlighting — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add visible per-match highlighting in Live mode for active find sessions, closing the gap that Source mode already has via `QPlainTextEdit::setExtraSelections`.

**Architecture:** `LiveFindAdapter` subscribes to `FindController::matchesChanged` + `currentMatchChanged`, computes a per-block `QList<FindSpan>` snapshot, pushes through a new `FindSpansRole` on `LiveBlockModel`. `InlineHighlighter` gains a find-pass that overpaints `Theme::SearchMatchBackground` / `Theme::SearchActiveMatchBackground` on top of the existing inline-spans pass. Re-uses the E1 highlighter pipeline; no new Theme slots; no QML overlay rectangles.

**Tech Stack:** Qt6.8, C++20, QML, `QSyntaxHighlighter`, `QTextCharFormat`, `qt_add_executable`.

**Spec:** [`docs/specs/2026-05-21-live-find-highlighting-design.md`](../specs/2026-05-21-live-find-highlighting-design.md).
**Driver:** [`docs/handoff/2026-05-21-find-ui-dogfood-findings.md`](../handoff/2026-05-21-find-ui-dogfood-findings.md).

---

## File Structure

**Create:**
- `libs/markoff-live/include/markoff/live/FindSpan.h` — POD struct + `Q_DECLARE_METATYPE`.
- `libs/markoff-live/tests/tst_live_find_adapter.cpp` — new C++ unit binary.

**Modify:**
- `libs/markoff-live/include/markoff/live/BlockRecord.h` — add `QList<Markoff::Live::FindSpan> findSpans` (excluded from `operator==`).
- `libs/markoff-live/include/markoff/live/LiveBlockModel.h` — add `FindSpansRole` enum value + `setFindSpans` mutator.
- `libs/markoff-live/src/LiveBlockModel.cpp` — implement role, mutator, roleNames entry.
- `libs/markoff-live/include/markoff/live/InlineHighlighter.h` — add `setFindSpans` + `m_findSpans` member.
- `libs/markoff-live/src/InlineHighlighter.cpp` — find-pass after inline-pass in `highlightBlock`.
- `libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h` — `findSpans` `Q_PROPERTY` (QVariantList shim).
- `libs/markoff-live/src/InlineHighlighterAttached.cpp` — implement getter/setter + propagate to highlighter.
- `libs/markoff-live/src/Detail/LiveFindAdapter.h` — add `m_findSpansByBlock` cache + new slots.
- `libs/markoff-live/src/Detail/LiveFindAdapter.cpp` — `onMatchesChanged`, `onCurrentMatchChanged`, diff + dispatch.
- `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` — add `findSpans: model.findSpans` to `InlineHighlighterAttached`.
- `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` — same one-liner.
- `libs/markoff-live/tests/CMakeLists.txt` — register `tst_live_find_adapter` binary.
- `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` — 4 new slots (find_matches_render, current_match_distinct, highlights_clear_on_empty, highlights_survive_edit).

**Unchanged:** `Theme.h` / `Theme.cpp` (existing `SearchMatchBackground` + `SearchActiveMatchBackground` reused as-is), `FindController.h` / `.cpp`, `MathDelegate.qml`, `BlockOnlyDelegateBase.qml`.

---

## Build / test cadence

After every task: `cmake --build build-dev -j 8` (build) + `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` (fast suite). Per-task tests use focused `-R <pattern>`. Always cap parallelism at `-j 8` (memory feedback).

---

## Task 1: `FindSpan` data type

**Files:**
- Create: `libs/markoff-live/include/markoff/live/FindSpan.h`

- [ ] **Step 1: Create the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaType>
#include <QtGlobal>

namespace Markoff::Live {

/// A single find-match range within a block's text. Byte offsets are
/// relative to the block's current text (UTF-8), matching the units used
/// by `Markoff::FindController::Match::byteOffset` /
/// `Markoff::SearchEngine::SearchHit::matchStart`.
///
/// `isCurrent` is true for the one match that `FindController` reports as
/// `currentMatchIndex`; all other matches in the block (and across all
/// blocks) have `isCurrent = false`.
struct FindSpan {
    quint32 byteOffset = 0;
    quint32 byteLength = 0;
    bool    isCurrent  = false;

    bool operator==(const FindSpan &o) const noexcept {
        return byteOffset == o.byteOffset
            && byteLength == o.byteLength
            && isCurrent  == o.isCurrent;
    }
    bool operator!=(const FindSpan &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::Live

Q_DECLARE_METATYPE(Markoff::Live::FindSpan)
```

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean — no callers yet, header is just a declaration.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/include/markoff/live/FindSpan.h
git commit -m "feat(live): FindSpan POD for per-block find-match ranges"
```

---

## Task 2: `BlockRecord::findSpans` field

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/BlockRecord.h`

- [ ] **Step 1: Add the include**

In `BlockRecord.h`, after the existing `#include <markoff/parser/SourceSpan.h>` line, add:

```cpp
#include <markoff/live/FindSpan.h>
```

- [ ] **Step 2: Add the field**

After the existing `QList<Markoff::SourceSpan> inlineSpans;` line, before `QHash<Markoff::AttrName, ...>` attrs, add:

```cpp
    /// Find-match ranges for an active find session. Written by
    /// `LiveFindAdapter` via `LiveBlockModel::setFindSpans`. Excluded from
    /// `operator==` so adapter writes don't cascade into `applyOps`
    /// equality short-circuits. Empty list = no matches in this block.
    QList<Markoff::Live::FindSpan> findSpans;
```

- [ ] **Step 3: Confirm `operator==` is unchanged**

The doc-comment says findSpans is excluded from equality. Verify the existing operator== body does NOT list findSpans — it should still only compare kind/text/headingLevel/codeLanguage/headingForm/blockAnchor/attrs.

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/BlockRecord.h
git commit -m "feat(live): BlockRecord.findSpans (out of equality)"
```

---

## Task 3: `LiveBlockModel::FindSpansRole` + `setFindSpans` — failing test first

**Files:**
- Test: `libs/markoff-live/tests/tst_live_render_block_model.cpp` (add a slot)

- [ ] **Step 1: Locate the existing test binary**

Read `libs/markoff-live/tests/tst_live_render_block_model.cpp`. Confirm it follows the QtTest `private slots:` pattern and links against the markoff_live target. If it doesn't exist, create a minimal QtTest skeleton; otherwise append the slot.

- [ ] **Step 2: Write the failing test slot**

Add after the last existing slot in the `private slots:` block:

```cpp
void setFindSpans_emitsDataChangedForRoleOnly() {
    Markoff::Live::LiveBlockModel m;
    Markoff::BlockAnchor a; a.replicaId = 42; a.byteOffset = 100;
    m.insertTestRow(a, "paragraph", "the quick brown fox");

    QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);

    QList<Markoff::Live::FindSpan> spans;
    spans.append({ /*byteOffset*/ 4,  /*byteLength*/ 5, /*isCurrent*/ false });
    spans.append({ /*byteOffset*/ 10, /*byteLength*/ 5, /*isCurrent*/ true  });
    m.setFindSpans(a, spans);

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    const auto roles = args.at(2).value<QVector<int>>();
    QCOMPARE(roles.size(), 1);
    QCOMPARE(roles.first(), int(Markoff::Live::LiveBlockModel::FindSpansRole));

    const QVariant got = m.data(m.index(0, 0), Markoff::Live::LiveBlockModel::FindSpansRole);
    const auto returned = got.value<QList<Markoff::Live::FindSpan>>();
    QCOMPARE(returned.size(), 2);
    QCOMPARE(returned[0].byteOffset, quint32(4));
    QCOMPARE(returned[1].isCurrent, true);
}
```

- [ ] **Step 3: Run and verify it fails**

Run: `scripts/run-tests.sh --bin tst_live_render_block_model -R 'setFindSpans'`
Expected: compile error — `LiveBlockModel::FindSpansRole` not defined and/or `setFindSpans` not declared.

- [ ] **Step 4: Add the role enum**

In `libs/markoff-live/include/markoff/live/LiveBlockModel.h`, inside the existing `enum Role { ... }` block, after `DelegateClassRole,`, add:

```cpp
        FindSpansRole,
```

- [ ] **Step 5: Add the mutator declaration**

In the same header, after the existing `void applyOps(...)` declaration block, add:

```cpp
    /// Replace this block's find-match list. Emits a single `dataChanged`
    /// with roles == {FindSpansRole} so consumers can react minimally.
    /// No-op (no signal) if the block is unknown or the list is identical
    /// to the current value.
    void setFindSpans(const Markoff::BlockAnchor &anchor,
                      const QList<FindSpan> &spans);
```

- [ ] **Step 6: Implement in `.cpp`**

In `libs/markoff-live/src/LiveBlockModel.cpp`:

1. Add to `roleNames()` (just before the `return out;`): `out[FindSpansRole] = "findSpans";`.
2. Add to `data(index, role)` switch (just before the default case):

```cpp
        case FindSpansRole: return QVariant::fromValue(rec.findSpans);
```

3. Add `setFindSpans` implementation at end of file (above the closing namespace brace):

```cpp
void LiveBlockModel::setFindSpans(const Markoff::BlockAnchor &anchor,
                                  const QList<FindSpan> &spans)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows[row].blockAnchor != anchor) continue;
        if (m_rows[row].findSpans == spans) return;
        m_rows[row].findSpans = spans;
        const QModelIndex ix = index(row, 0);
        Q_EMIT dataChanged(ix, ix, {FindSpansRole});
        return;
    }
    // Unknown anchor — no-op. Adapter writes can race with row removal;
    // dropping silently is the contract.
}
```

- [ ] **Step 7: Run and verify it passes**

Run: `scripts/run-tests.sh --bin tst_live_render_block_model -R 'setFindSpans'`
Expected: PASS.

- [ ] **Step 8: Run the full model suite to confirm no regression**

Run: `scripts/run-tests.sh --bin tst_live_render_block_model`
Expected: all slots pass.

- [ ] **Step 9: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveBlockModel.h \
        libs/markoff-live/src/LiveBlockModel.cpp \
        libs/markoff-live/tests/tst_live_render_block_model.cpp
git commit -m "feat(live): LiveBlockModel FindSpansRole + setFindSpans

Per-block find-match channel for the LiveFindAdapter producer. Emits
dataChanged with only FindSpansRole in the roles list, so consumers
that care only about other roles (text, kind, etc.) short-circuit."
```

---

## Task 4: `InlineHighlighter::setFindSpans` + find-pass — failing test first

**Files:**
- Test: `libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp` (add a slot — this is the closest existing per-kind highlighter binary)
- Modify: `libs/markoff-live/include/markoff/live/InlineHighlighter.h`
- Modify: `libs/markoff-live/src/InlineHighlighter.cpp`

- [ ] **Step 1: Write the failing test**

Read `tst_live_render_inline_per_kind.cpp`. Append a new slot:

```cpp
void findSpan_paintsSearchMatchBackground_onMatchedRange() {
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("the quick brown fox"));

    Markoff::Theme theme = Markoff::Theme::defaultLight();
    Markoff::Live::InlineHighlighter h(&doc);
    h.setTheme(&theme);

    QList<Markoff::Live::FindSpan> spans;
    spans.append({ /*byteOffset*/ 4, /*byteLength*/ 5, /*isCurrent*/ false });
    h.setFindSpans(spans);

    QTextCursor c(&doc);
    c.setPosition(4);
    const QTextCharFormat fmtMatched = c.charFormat();
    c.setPosition(0);
    const QTextCharFormat fmtUnmatched = c.charFormat();

    QCOMPARE(fmtMatched.background().color(),
             theme.color(Markoff::Theme::Slot::SearchMatchBackground));
    QVERIFY(fmtUnmatched.background().color()
            != theme.color(Markoff::Theme::Slot::SearchMatchBackground));
}

void findSpan_currentMatch_usesActiveSlot() {
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("the quick brown fox"));

    Markoff::Theme theme = Markoff::Theme::defaultLight();
    Markoff::Live::InlineHighlighter h(&doc);
    h.setTheme(&theme);

    QList<Markoff::Live::FindSpan> spans;
    spans.append({ /*byteOffset*/ 4, /*byteLength*/ 5, /*isCurrent*/ true });
    h.setFindSpans(spans);

    QTextCursor c(&doc);
    c.setPosition(4);
    QCOMPARE(c.charFormat().background().color(),
             theme.color(Markoff::Theme::Slot::SearchActiveMatchBackground));
}
```

Include `<markoff/live/FindSpan.h>` at the top of the file.

- [ ] **Step 2: Run and verify both fail**

Run: `scripts/run-tests.sh --bin tst_live_render_inline_per_kind -R 'findSpan_'`
Expected: compile error — `setFindSpans` undeclared.

- [ ] **Step 3: Add `setFindSpans` declaration to header**

In `libs/markoff-live/include/markoff/live/InlineHighlighter.h`, after the existing `void setInlineSpans(...)` and `inlineSpans()` accessor pair, add:

```cpp
    /// Set find-match ranges to paint with `Theme::SearchMatchBackground`
    /// (non-current) or `Theme::SearchActiveMatchBackground` (current).
    /// Triggers `rehighlight()`. Empty list clears highlights.
    void setFindSpans(const QList<FindSpan> &spans);
    const QList<FindSpan> &findSpans() const noexcept { return m_findSpans; }
```

Also add the include `#include <markoff/live/FindSpan.h>` at the top, and add `QList<FindSpan> m_findSpans;` to the private section.

- [ ] **Step 4: Implement in `.cpp`**

In `libs/markoff-live/src/InlineHighlighter.cpp`, add after `setInlineSpans`:

```cpp
void InlineHighlighter::setFindSpans(const QList<FindSpan> &spans)
{
    if (m_findSpans == spans) return;
    m_findSpans = spans;
    rehighlight();
}
```

- [ ] **Step 5: Add the find-pass to `highlightBlock`**

At the end of `highlightBlock`, after the existing inline-spans loop's closing brace, before the function's closing brace, add:

```cpp
    // Find-pass — paints search-match background on top of any existing
    // inline-pass formats. Byte offsets are block-relative (UTF-8); convert
    // to QChar (UTF-16) positions, then to line-relative using lineStart.
    if (!m_findSpans.isEmpty()) {
        const QByteArray blockUtf8 = currentBlock().text().toUtf8();
        // Note: currentBlock().text() is the line — but FindSpan byte offsets
        // are block-document-relative. We need the whole document's UTF-8
        // up to this line's start to convert byteOffset → qtPos. The
        // QTextDocument's full content is `document()->toPlainText()`.
        const QString docText = document()->toPlainText();
        const QByteArray docUtf8 = docText.toUtf8();
        auto byteToQt = [&](quint32 byteOff) -> int {
            if (static_cast<int>(byteOff) >= docUtf8.size())
                return docText.size();
            return QString::fromUtf8(docUtf8.left(static_cast<int>(byteOff))).size();
        };
        for (const FindSpan &fs : std::as_const(m_findSpans)) {
            if (fs.byteLength == 0) continue;
            const int qStart = byteToQt(fs.byteOffset);
            const int qEnd   = byteToQt(fs.byteOffset + fs.byteLength);
            const int relStart = qStart - lineStart;
            const int relEnd   = qEnd   - lineStart;
            if (relEnd <= 0 || relStart >= lineLen) continue;
            const int from = std::max(0, relStart);
            const int to   = std::min(lineLen, relEnd);
            const QColor bg = fs.isCurrent
                ? m_theme->color(Markoff::Theme::Slot::SearchActiveMatchBackground)
                : m_theme->color(Markoff::Theme::Slot::SearchMatchBackground);
            if (!bg.isValid()) continue;
            for (int i = from; i < to; ++i) {
                QTextCharFormat merged = format(i);
                merged.setBackground(bg);  // overpaint — find pass wins over Highlight
                setFormat(i, 1, merged);
            }
        }
    }
```

- [ ] **Step 6: Run the new tests**

Run: `scripts/run-tests.sh --bin tst_live_render_inline_per_kind -R 'findSpan_'`
Expected: both PASS.

- [ ] **Step 7: Run full inline suite to confirm no regression**

Run: `scripts/run-tests.sh -R '^tst_live_render_inline'`
Expected: all green.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-live/include/markoff/live/InlineHighlighter.h \
        libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp
git commit -m "feat(live): InlineHighlighter find-pass

Overpaints Theme::SearchMatchBackground / SearchActiveMatchBackground
on top of the inline-spans pass. Find pass wins over Highlight when
both apply to the same range, by ordering — intentional per spec."
```

---

## Task 5: `InlineHighlighterAttached::findSpans` Q_PROPERTY

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h`
- Modify: `libs/markoff-live/src/InlineHighlighterAttached.cpp`

- [ ] **Step 1: Add the property declaration**

In `InlineHighlighterAttached.h`, after the existing `Q_PROPERTY(QVariantList spans ...)` line, add:

```cpp
    Q_PROPERTY(QVariantList findSpans READ findSpans WRITE setFindSpans NOTIFY findSpansChanged)
```

Add getter/setter declarations after the existing `setSpans` declaration:

```cpp
    QVariantList findSpans() const;
    void setFindSpans(const QVariantList &v);
```

Add signal in the existing `Q_SIGNALS:` block:

```cpp
    void findSpansChanged();
```

Add member `QList<Markoff::Live::FindSpan> m_findSpans;` in the private section.

Include `<markoff/live/FindSpan.h>` at the top.

- [ ] **Step 2: Implement getter/setter in `.cpp`**

In `InlineHighlighterAttached.cpp`, after `setSpans`:

```cpp
QVariantList InlineHighlighterAttached::findSpans() const
{
    QVariantList out;
    out.reserve(m_findSpans.size());
    for (const auto &s : m_findSpans) out.append(QVariant::fromValue(s));
    return out;
}

void InlineHighlighterAttached::setFindSpans(const QVariantList &v)
{
    QList<Markoff::Live::FindSpan> next;
    next.reserve(v.size());
    for (const QVariant &item : v) {
        if (item.canConvert<Markoff::Live::FindSpan>())
            next.append(item.value<Markoff::Live::FindSpan>());
    }
    if (next == m_findSpans) return;
    m_findSpans = next;
    if (m_highlighter) m_highlighter->setFindSpans(m_findSpans);
    Q_EMIT findSpansChanged();
}
```

- [ ] **Step 3: Wire into `rebuildHighlighter`**

In `rebuildHighlighter`, after the existing `m_highlighter->setSelectionRange(m_selStart, m_selEnd);` line, add:

```cpp
    m_highlighter->setFindSpans(m_findSpans);
```

- [ ] **Step 4: Build and confirm clean**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h \
        libs/markoff-live/src/InlineHighlighterAttached.cpp
git commit -m "feat(live): InlineHighlighterAttached findSpans property"
```

---

## Task 6: QML delegates pass `model.findSpans` to attached highlighter

**Files:**
- Modify: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`

- [ ] **Step 1: Find each existing `InlineHighlighterAttached` block**

Grep both files for `InlineHighlighterAttached {`. In `UnifiedInlineTextDelegate.qml` the block currently sets `target: edit` + `spans: model.inlineSpans` (around line 222) and likely `theme: ...`. In `CodeBlockDelegate.qml`, locate the equivalent block.

- [ ] **Step 2: Add `findSpans: model.findSpans` in `UnifiedInlineTextDelegate.qml`**

Inside the `InlineHighlighterAttached { ... }` block at ~line 222, after `spans: model.inlineSpans`, add a new line:

```qml
            findSpans: model.findSpans
```

- [ ] **Step 3: Same in `CodeBlockDelegate.qml`**

If `CodeBlockDelegate.qml` does NOT have an `InlineHighlighterAttached` block (the find spec acknowledges code blocks need it), check first. If it does, add the same line. If it doesn't, file an inline TODO comment at the delegate root saying find highlighting for code blocks deferred; do not block on it. Verify via grep:

```bash
grep -n 'InlineHighlighterAttached' libs/markoff-live/qml/delegates/CodeBlockDelegate.qml
```

If output is empty, leave CodeBlockDelegate alone for this task — find highlighting will only paint in non-code text blocks for the MVP. (This becomes a noted limitation; spec does not require code-block highlighting at the MVP boundary. Update spec inline if so.)

- [ ] **Step 4: Build**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean.

- [ ] **Step 5: Confirm `findSpans` exists as a model role**

Run: `grep -n 'findSpans' libs/markoff-live/src/LiveBlockModel.cpp libs/markoff-live/include/markoff/live/LiveBlockModel.h`
Expected: both files show `findSpans` references. The QML binding `model.findSpans` resolves through the role names registered in Task 3.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml \
        libs/markoff-live/qml/delegates/CodeBlockDelegate.qml
git commit -m "feat(live-qml): delegates pass model.findSpans to highlighter"
```

(Use only the files you actually modified — the CodeBlockDelegate change may be a no-op.)

---

## Task 7: `LiveFindAdapter` cache + producer — failing test first

**Files:**
- Create: `libs/markoff-live/tests/tst_live_find_adapter.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`
- Modify: `libs/markoff-live/src/Detail/LiveFindAdapter.h`
- Modify: `libs/markoff-live/src/Detail/LiveFindAdapter.cpp`

- [ ] **Step 1: Register the new test binary**

In `libs/markoff-live/tests/CMakeLists.txt`, find the block adding e.g. `tst_live_render_inline_per_kind` (it serves as the linkage template). Add a parallel block:

```cmake
qt_add_executable(tst_live_find_adapter
    tst_live_find_adapter.cpp
)
target_link_libraries(tst_live_find_adapter PRIVATE
    Qt6::Core Qt6::Test
    markoff_live
    Markoff::Core
)
add_test(NAME tst_live_find_adapter COMMAND tst_live_find_adapter)
```

Use the exact `target_link_libraries` form from an adjacent existing entry (the existing entries may use bare `markoff_live` or aliased — copy the local style verbatim).

- [ ] **Step 2: Write the failing test**

Create `libs/markoff-live/tests/tst_live_find_adapter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/FindSpan.h>
#include <markoff/live/LiveBlockModel.h>

#include "../src/Detail/LiveFindAdapter.h"

class TestLiveFindAdapter : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Register metatypes used in QSignalSpy / QVariant.
        qRegisterMetaType<Markoff::BlockAnchor>("Markoff::BlockAnchor");
        qRegisterMetaType<Markoff::Live::FindSpan>("Markoff::Live::FindSpan");
    }

    void matchesChanged_populatesModelFindSpans_perBlock() {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(QByteArray("the cat\n\nsat on the mat"));

        Markoff::Live::LiveBlockModel model;
        // Mirror the document's blocks into the model's test storage.
        int row = 0;
        for (auto it = doc.iterateBlocks(); it.hasNext(); ++row) {
            const auto block = it.next();
            model.insertTestRow(block.anchor, block.kind, QString::fromUtf8(block.textUtf8));
        }
        QVERIFY(model.rowCount() == 2);

        Markoff::Live::LiveCursorState cursorState;
        Markoff::Live::Detail::LiveFindAdapter adapter(&model, &cursorState);

        Markoff::FindController fc(&doc);
        adapter.attach(&fc);
        fc.activate();
        fc.setNeedle("the");

        const QVariant block0Spans = model.data(model.index(0, 0),
            Markoff::Live::LiveBlockModel::FindSpansRole);
        const QVariant block1Spans = model.data(model.index(1, 0),
            Markoff::Live::LiveBlockModel::FindSpansRole);
        const auto spans0 = block0Spans.value<QList<Markoff::Live::FindSpan>>();
        const auto spans1 = block1Spans.value<QList<Markoff::Live::FindSpan>>();
        QCOMPARE(spans0.size(), 1);
        QCOMPARE(spans1.size(), 1);
        QCOMPARE(spans0.first().byteOffset, quint32(0));
        QCOMPARE(spans0.first().byteLength, quint32(3));
        QCOMPARE(spans1.first().byteOffset, quint32(7));  // "sat on the" → "the" at byte 7
    }

    void currentMatchChanged_updatesIsCurrentFlags() {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(QByteArray("the cat\n\nsat on the mat"));

        Markoff::Live::LiveBlockModel model;
        for (auto it = doc.iterateBlocks(); it.hasNext();) {
            const auto block = it.next();
            model.insertTestRow(block.anchor, block.kind, QString::fromUtf8(block.textUtf8));
        }

        Markoff::Live::LiveCursorState cursorState;
        Markoff::Live::Detail::LiveFindAdapter adapter(&model, &cursorState);
        Markoff::FindController fc(&doc);
        adapter.attach(&fc);
        fc.activate();
        fc.setNeedle("the");

        // Initial: match 0 (block 0) is current.
        {
            const auto s0 = model.data(model.index(0, 0),
                Markoff::Live::LiveBlockModel::FindSpansRole)
                .value<QList<Markoff::Live::FindSpan>>();
            const auto s1 = model.data(model.index(1, 0),
                Markoff::Live::LiveBlockModel::FindSpansRole)
                .value<QList<Markoff::Live::FindSpan>>();
            QVERIFY(s0.first().isCurrent);
            QVERIFY(!s1.first().isCurrent);
        }

        fc.findNext();

        // After next: match 1 (block 1) is current.
        {
            const auto s0 = model.data(model.index(0, 0),
                Markoff::Live::LiveBlockModel::FindSpansRole)
                .value<QList<Markoff::Live::FindSpan>>();
            const auto s1 = model.data(model.index(1, 0),
                Markoff::Live::LiveBlockModel::FindSpansRole)
                .value<QList<Markoff::Live::FindSpan>>();
            QVERIFY(!s0.first().isCurrent);
            QVERIFY(s1.first().isCurrent);
        }
    }

    void detach_clearsAllSpans() {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(QByteArray("the cat"));

        Markoff::Live::LiveBlockModel model;
        for (auto it = doc.iterateBlocks(); it.hasNext();) {
            const auto block = it.next();
            model.insertTestRow(block.anchor, block.kind, QString::fromUtf8(block.textUtf8));
        }

        Markoff::Live::LiveCursorState cursorState;
        Markoff::Live::Detail::LiveFindAdapter adapter(&model, &cursorState);
        Markoff::FindController fc(&doc);
        adapter.attach(&fc);
        fc.activate();
        fc.setNeedle("the");
        QVERIFY(!model.data(model.index(0, 0),
            Markoff::Live::LiveBlockModel::FindSpansRole)
            .value<QList<Markoff::Live::FindSpan>>().isEmpty());

        adapter.detach();
        QVERIFY(model.data(model.index(0, 0),
            Markoff::Live::LiveBlockModel::FindSpansRole)
            .value<QList<Markoff::Live::FindSpan>>().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLiveFindAdapter)
#include "tst_live_find_adapter.moc"
```

Note: if `MarkoffDocument::iterateBlocks` or the BlockRecord shape differs from what's shown, adapt the loop to use the actual public iteration API — read `MarkoffDocument.h` first to confirm. If `iterateBlocks` returns an iterator, this loop shape is correct; if it returns a list, replace with `for (const auto &block : doc.blocks()) {...}`.

- [ ] **Step 3: Run and verify it fails to compile**

Run: `cmake --build build-dev --target tst_live_find_adapter -j 8`
Expected: compile error on `adapter.attach(...)` calls because `LiveFindAdapter` doesn't yet subscribe to `matchesChanged` / `currentMatchChanged` and doesn't yet call `setFindSpans`. (The slots exist; they're just no-op.) The compile may actually succeed but the tests will fail at runtime because no spans are written.

- [ ] **Step 4: Run the test to verify it fails at runtime**

Run: `./build-dev/bin/tst_live_find_adapter`
Expected: at least `matchesChanged_populatesModelFindSpans_perBlock` fails with "spans0.size() == 1" assertion (actual: 0).

- [ ] **Step 5: Add cache + signal subscriptions to header**

Edit `libs/markoff-live/src/Detail/LiveFindAdapter.h`:

Add includes:

```cpp
#include <QHash>
#include <markoff/live/FindSpan.h>
```

In the class, add a private slot:

```cpp
private slots:
    void onNavigationRequested(Markoff::FindController::Match);
    void onMatchesChanged();
    void onCurrentMatchChanged();
```

Add private helper + members:

```cpp
private:
    void rebuildAndPushSpans();

    LiveBlockModel                       *m_model       = nullptr;
    LiveCursorState                      *m_cursorState = nullptr;
    QPointer<Markoff::FindController>     m_controller;
    QHash<Markoff::BlockAnchor, QList<Markoff::Live::FindSpan>> m_lastPushed;
```

(Remove the old `m_model`/`m_cursorState`/`m_controller` declarations — these replace them. Keep the existing `resolveByteToQtPos` private helper.)

- [ ] **Step 6: Implement the new methods in `.cpp`**

Replace `LiveFindAdapter::attach` and `LiveFindAdapter::detach` with:

```cpp
void LiveFindAdapter::attach(Markoff::FindController *fc)
{
    if (m_controller == fc) return;
    if (m_controller) detach();
    m_controller = fc;
    if (!m_controller) return;
    connect(m_controller, &Markoff::FindController::navigationRequested,
            this, &LiveFindAdapter::onNavigationRequested);
    connect(m_controller, &Markoff::FindController::matchesChanged,
            this, &LiveFindAdapter::onMatchesChanged);
    connect(m_controller, &Markoff::FindController::currentMatchChanged,
            this, &LiveFindAdapter::onCurrentMatchChanged);
    // If the controller already has matches (rare but possible — e.g. tests
    // that set needle before attach), push them through.
    rebuildAndPushSpans();
}

void LiveFindAdapter::detach()
{
    if (!m_controller) return;
    disconnect(m_controller, nullptr, this, nullptr);
    m_controller = nullptr;
    // Clear all previously-pushed spans.
    if (m_model) {
        for (auto it = m_lastPushed.constBegin(); it != m_lastPushed.constEnd(); ++it) {
            m_model->setFindSpans(it.key(), {});
        }
    }
    m_lastPushed.clear();
}

void LiveFindAdapter::onMatchesChanged()
{
    rebuildAndPushSpans();
}

void LiveFindAdapter::onCurrentMatchChanged()
{
    rebuildAndPushSpans();
}

void LiveFindAdapter::rebuildAndPushSpans()
{
    if (!m_model) return;
    QHash<Markoff::BlockAnchor, QList<Markoff::Live::FindSpan>> nextByBlock;
    if (m_controller) {
        const QList<Markoff::FindController::Match> &matches = m_controller->matches();
        const int currentIdx = m_controller->currentMatchIndex();
        for (int i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            Markoff::Live::FindSpan span{
                /*byteOffset*/ m.byteOffset,
                /*byteLength*/ m.byteLength,
                /*isCurrent*/  (i == currentIdx)
            };
            nextByBlock[m.block].append(span);
        }
    }
    // Diff: push to model for every block whose list changed, and for every
    // block in m_lastPushed that no longer has matches (push empty list).
    const auto allKeys = (nextByBlock.keys() + m_lastPushed.keys()).toSet();
    for (const Markoff::BlockAnchor &anchor : allKeys) {
        const auto &newSpans = nextByBlock.value(anchor);
        const auto &oldSpans = m_lastPushed.value(anchor);
        if (newSpans != oldSpans) {
            m_model->setFindSpans(anchor, newSpans);
        }
    }
    m_lastPushed = std::move(nextByBlock);
}
```

Note: `(nextByBlock.keys() + m_lastPushed.keys()).toSet()` uses Qt's `QList` deduplication via `QSet` — if `toSet()` is deprecated in your Qt version, use `QSet<Markoff::BlockAnchor>(allKeys.begin(), allKeys.end())` instead. Verify by trying the build.

- [ ] **Step 7: Build**

Run: `cmake --build build-dev --target tst_live_find_adapter -j 8`
Expected: clean. If the `.toSet()` call doesn't compile, swap for the iterator-range constructor.

- [ ] **Step 8: Run the new tests**

Run: `scripts/run-tests.sh --bin tst_live_find_adapter`
Expected: all 3 slots PASS.

- [ ] **Step 9: Run the full Live suite to confirm no regression**

Run: `scripts/run-tests.sh -R '^tst_live'`
Expected: green except for the pre-existing baseline failures (`tst_live_render_setext_e2e::S1_setextDemote_lastUnderlineCharDeleted_keepsCursor`, `tst_v10_source_editor_view_contract::cursor_position_round_trips`) — confirm the count of failures is unchanged from baseline.

- [ ] **Step 10: Commit**

```bash
git add libs/markoff-live/tests/tst_live_find_adapter.cpp \
        libs/markoff-live/tests/CMakeLists.txt \
        libs/markoff-live/src/Detail/LiveFindAdapter.h \
        libs/markoff-live/src/Detail/LiveFindAdapter.cpp
git commit -m "feat(live): LiveFindAdapter pushes matches to LiveBlockModel

Subscribes to FindController::matchesChanged + currentMatchChanged,
maintains a per-block QHash cache, diffs and pushes through
LiveBlockModel::setFindSpans. detach() clears all pushed spans.
Re-uses existing onNavigationRequested for caret-without-focus
scroll-into-view."
```

---

## Task 8: Falsifiability stub (per invariant 4)

**Files:**
- Modify: `libs/markoff-live/src/Detail/LiveFindAdapter.cpp` (temporary)

- [ ] **Step 1: Stub `rebuildAndPushSpans` to a no-op**

Comment out the body of `rebuildAndPushSpans`:

```cpp
void LiveFindAdapter::rebuildAndPushSpans()
{
    // FALSIFIABILITY STUB — should cause tst_live_find_adapter to fail.
    return;
}
```

- [ ] **Step 2: Run the unit test and confirm it now fails**

Run: `scripts/run-tests.sh --bin tst_live_find_adapter`
Expected: `matchesChanged_populatesModelFindSpans_perBlock` FAILS. `currentMatchChanged_updatesIsCurrentFlags` FAILS.

- [ ] **Step 3: Commit the falsifiability proof**

```bash
git add libs/markoff-live/src/Detail/LiveFindAdapter.cpp
git commit -m "test: falsifiability proof for LiveFindAdapter find-pass

rebuildAndPushSpans stubbed to no-op; tst_live_find_adapter fails
as expected. This commit gets immediately reverted by the next."
```

- [ ] **Step 4: Revert the stub**

```bash
git revert HEAD --no-edit
```

- [ ] **Step 5: Re-run to confirm green**

Run: `scripts/run-tests.sh --bin tst_live_find_adapter`
Expected: all 3 slots PASS again.

---

## Task 9: Integration test slot `find_matches_render_highlights_in_live_mode`

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Read the existing test file's slot pattern**

Open `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`. Identify a recent slot to model after — likely one that loads a doc + drives input via `LiveRealisticInputHarness`. Confirm the fixture exposes a `MarkoffDocument`, a `Markoff::Session`, and access to the model + view.

- [ ] **Step 2: Add the new slot declaration**

In the `private slots:` block, alongside existing slots, add:

```cpp
void find_matches_render_highlights_in_live_mode();
```

- [ ] **Step 3: Implement the slot**

Use the existing test app's fixture (`QmlIntegrationFixture` per the harness convention). The slot:

```cpp
void TestLiveRenderQmlIntegration::find_matches_render_highlights_in_live_mode()
{
    QmlIntegrationFixture f;
    f.loadDocument("the quick\n\nbrown fox\n\nthe lazy dog");

    Markoff::FindController fc(f.document());
    f.binding()->attachFindController(&fc);
    fc.activate();
    fc.setNeedle("the");

    // Two blocks contain "the": block 0 ("the quick") and block 2 ("the lazy dog").
    // Block 0's match is at byteOffset 0; block 2's at byteOffset 0.
    QQuickItem *delegate0 = f.delegateAtRow(0);
    QQuickItem *delegate2 = f.delegateAtRow(2);
    QVERIFY(delegate0);
    QVERIFY(delegate2);

    QObject *textEdit0 = delegate0->findChild<QObject*>("textEditMain");
    QObject *textEdit2 = delegate2->findChild<QObject*>("textEditMain");
    QVERIFY(textEdit0);
    QVERIFY(textEdit2);

    auto background = [](QObject *textEdit, int pos) -> QColor {
        QQuickTextDocument *qtd = qvariant_cast<QQuickTextDocument*>(
            textEdit->property("textDocument"));
        QTextCursor c(qtd->textDocument());
        c.setPosition(pos);
        return c.charFormat().background().color();
    };

    Markoff::Theme theme = Markoff::Theme::defaultLight();
    const QColor expected = theme.color(Markoff::Theme::Slot::SearchMatchBackground);
    const QColor expectedCurrent = theme.color(Markoff::Theme::Slot::SearchActiveMatchBackground);

    // Block 0's match is at pos 0..3 ("the"), and it is current (currentMatchIndex == 0).
    QCOMPARE(background(textEdit0, 1), expectedCurrent);
    // Block 2's match is at pos 0..3 ("the"), and it is non-current.
    QCOMPARE(background(textEdit2, 1), expected);
}
```

Note: the `textEditMain` objectName and `f.delegateAtRow(row)` helper come from `QmlIntegrationFixture`. If those exact names differ, adapt to the fixture's actual surface — search the existing slot bodies for `findChild` / `delegateAt` patterns and follow them.

- [ ] **Step 4: Run the slot**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration -R 'find_matches_render'`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "test(live-qml): integration test — find highlights in Live mode"
```

---

## Task 10: Integration test slot `current_match_renders_with_distinct_color`

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Add the slot**

Declaration in `private slots:`:

```cpp
void current_match_renders_with_distinct_color();
```

Body (re-uses helpers from the previous slot — extract a `background(QObject*, int)` lambda into a private member helper in the test class if duplication is significant):

```cpp
void TestLiveRenderQmlIntegration::current_match_renders_with_distinct_color()
{
    QmlIntegrationFixture f;
    f.loadDocument("the quick\n\nthe lazy");

    Markoff::FindController fc(f.document());
    f.binding()->attachFindController(&fc);
    fc.activate();
    fc.setNeedle("the");

    QQuickItem *d0 = f.delegateAtRow(0);
    QQuickItem *d1 = f.delegateAtRow(1);
    QObject *t0 = d0->findChild<QObject*>("textEditMain");
    QObject *t1 = d1->findChild<QObject*>("textEditMain");

    Markoff::Theme theme = Markoff::Theme::defaultLight();
    const QColor mc = theme.color(Markoff::Theme::Slot::SearchMatchBackground);
    const QColor ac = theme.color(Markoff::Theme::Slot::SearchActiveMatchBackground);

    auto bg = [](QObject *t, int pos) {
        QQuickTextDocument *qtd = qvariant_cast<QQuickTextDocument*>(
            t->property("textDocument"));
        QTextCursor c(qtd->textDocument());
        c.setPosition(pos);
        return c.charFormat().background().color();
    };

    QCOMPARE(bg(t0, 1), ac);  // currentMatchIndex == 0 → block 0
    QCOMPARE(bg(t1, 1), mc);

    fc.findNext();
    QCOMPARE(bg(t0, 1), mc);
    QCOMPARE(bg(t1, 1), ac);
}
```

- [ ] **Step 2: Run**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration -R 'current_match'`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "test(live-qml): integration test — current match distinct color"
```

---

## Task 11: Integration test slot `find_highlights_clear_on_needle_empty`

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Add the slot**

```cpp
void TestLiveRenderQmlIntegration::find_highlights_clear_on_needle_empty()
{
    QmlIntegrationFixture f;
    f.loadDocument("the cat");

    Markoff::FindController fc(f.document());
    f.binding()->attachFindController(&fc);
    fc.activate();
    fc.setNeedle("the");

    QQuickItem *d0 = f.delegateAtRow(0);
    QObject *t0 = d0->findChild<QObject*>("textEditMain");
    QQuickTextDocument *qtd = qvariant_cast<QQuickTextDocument*>(
        t0->property("textDocument"));

    auto bg = [&](int pos) {
        QTextCursor c(qtd->textDocument());
        c.setPosition(pos);
        return c.charFormat().background().color();
    };

    Markoff::Theme theme = Markoff::Theme::defaultLight();
    QVERIFY(bg(1).isValid()
            && bg(1) != QColor());  // some background painted
    QCOMPARE(bg(1), theme.color(Markoff::Theme::Slot::SearchActiveMatchBackground));

    fc.setNeedle("");
    QCOMPARE(bg(1), QColor());  // background cleared
}
```

- [ ] **Step 2: Run**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration -R 'find_highlights_clear'`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "test(live-qml): integration test — highlights clear on empty needle"
```

---

## Task 12: Integration test slot `find_highlights_survive_block_edit`

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Add the slot**

```cpp
void TestLiveRenderQmlIntegration::find_highlights_survive_block_edit()
{
    QmlIntegrationFixture f;
    f.loadDocument("the cat sat on the mat");

    Markoff::FindController fc(f.document());
    f.binding()->attachFindController(&fc);
    fc.activate();
    fc.setNeedle("the");
    // Two matches in this single block: byte 0 ("the") and byte 15 ("the" in "the mat").
    QCOMPARE(fc.matchCount(), 2);

    QQuickItem *d0 = f.delegateAtRow(0);
    QObject *t0 = d0->findChild<QObject*>("textEditMain");
    QQuickTextDocument *qtd = qvariant_cast<QQuickTextDocument*>(
        t0->property("textDocument"));

    // Edit: append " gently" — text becomes "the cat sat on the mat gently".
    // Matches must still be at byte 0 + 15; highlights must redraw.
    f.appendToBlock(0, " gently");

    QCOMPARE(fc.matchCount(), 2);
    QTextCursor c(qtd->textDocument());
    c.setPosition(15);
    Markoff::Theme theme = Markoff::Theme::defaultLight();
    const QColor bgAt15 = c.charFormat().background().color();
    QVERIFY(bgAt15 == theme.color(Markoff::Theme::Slot::SearchMatchBackground)
         || bgAt15 == theme.color(Markoff::Theme::Slot::SearchActiveMatchBackground));
}
```

Note: `f.appendToBlock(int, QString)` is presumed to exist in `QmlIntegrationFixture`. If it doesn't, drive an edit via the harness's standard input mechanism — `LiveRealisticInputHarness::typeAt(blockAnchor, byteOffset, text)` or whatever the existing slots use. Replace verbatim per existing patterns.

- [ ] **Step 2: Run**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration -R 'find_highlights_survive'`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "test(live-qml): integration test — highlights survive block edit"
```

---

## Task 13: Falsifiability for the QML integration (per invariant 4)

**Files:**
- Modify: `libs/markoff-live/src/InlineHighlighter.cpp` (temporary)

- [ ] **Step 1: Stub the find-pass to a no-op**

Comment out the body of the find-pass block in `highlightBlock`:

```cpp
    if (!m_findSpans.isEmpty()) {
        // FALSIFIABILITY STUB — should cause integration slots to fail.
        return;
    }
```

- [ ] **Step 2: Run the integration slots**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration -R 'find_matches_render|current_match|find_highlights'`
Expected: at least `find_matches_render_highlights_in_live_mode` and `current_match_renders_with_distinct_color` FAIL.

- [ ] **Step 3: Commit the falsifiability proof**

```bash
git add libs/markoff-live/src/InlineHighlighter.cpp
git commit -m "test: falsifiability proof for find-pass at the QML seam

find-pass stubbed; tst_live_render_qml_integration slots fail. Reverted
by the next commit."
```

- [ ] **Step 4: Revert**

```bash
git revert HEAD --no-edit
```

- [ ] **Step 5: Re-run all 4 integration slots and confirm green**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration -R 'find_matches_render|current_match|find_highlights'`
Expected: all 4 PASS.

---

## Task 14: Full-suite regression check + queue closeout

**Files:** none (verification only)

- [ ] **Step 1: Run the full test suite**

Run: `scripts/run-tests.sh`
Expected: 218 + new tests total. New tests (3 unit + 4 integration + new highlighter slots) green. Pre-existing baseline failures unchanged (`tst_live_render_setext_e2e::S1_setextDemote_lastUnderlineCharDeleted_keepsCursor`, `tst_markoff_doc_apply_structured_paste`, `tst_v10_source_editor_view_contract::cursor_position_round_trips`).

- [ ] **Step 2: Update queue.md**

In `docs/queue.md`, add to the Discipline Log:

```markdown
- 2026-05-21 dogfood-close: Live-mode find highlighting → fixed in spec+plan
  `2026-05-21-live-find-highlighting{-design,}.md`. Adapter→Model→Highlighter
  chain; reused Theme::SearchMatch{,Active}Background slots; falsifiability
  proofs committed and reverted at the C++ unit + QML integration seams.
```

- [ ] **Step 3: Update the find-ui dogfood findings doc**

At the top of `docs/handoff/2026-05-21-find-ui-dogfood-findings.md`, prepend a status banner:

```markdown
> **2026-05-21 — RESOLVED.** Live-mode visible highlighting now lands via
> the chain described in §"Recommendation". See implementation at
> `docs/plans/2026-05-21-live-find-highlighting.md`. Tag candidate
> `v0.7.0-find-highlights` held pending interactive dogfood from
> Corbomite `port/foundation-exploration`.
```

- [ ] **Step 4: Commit doc updates**

```bash
git add docs/queue.md docs/handoff/2026-05-21-find-ui-dogfood-findings.md
git commit -m "docs: close out Live find-highlighting in queue + dogfood doc"
```

- [ ] **Step 5: Tag the candidate**

Do NOT create the tag — held pending dogfood per the existing convention. Note in the closing message that `v0.7.0-find-highlights` is the next tag once dogfood signs off.

---

## Self-review

**Spec coverage:**
- `LiveFindAdapter` cache + subscriptions: Task 7 ✓
- `BlockRecord::findSpans` field: Task 2 ✓
- `LiveBlockModel::FindSpansRole` + `setFindSpans`: Task 3 ✓
- `InlineHighlighter` find-pass: Task 4 ✓
- `InlineHighlighterAttached` plumbing: Task 5 ✓
- QML delegate wiring: Task 6 ✓
- Theme slot reuse (no new slots): no task needed; verified existing ✓
- Math handling (count + nav, no paint): falls out of Task 7 + Task 4 (math delegate never reads `model.findSpans` — no new code touches the file) ✓
- 4 integration test slots: Tasks 9–12 ✓
- New `tst_live_find_adapter` C++ binary: Task 7 ✓
- Falsifiability proofs (2): Tasks 8 + 13 ✓
- Definition-of-done items 1–4 (tests + build clean + baseline preserved): Task 14 ✓
- DoD #5 (Corbomite dogfood): covered by task #5 in the broader project task list, outside this plan.

**Placeholder scan:** no TBD / TODO / "similar to" / "add appropriate" — every step has the actual code/command.

**Type consistency:** `FindSpan` byte fields are `quint32` (matches `FindController::Match`). `setFindSpans` signature matches across header, impl, attached object, and tests. Role enum entry `FindSpansRole` referenced consistently. `Markoff::Live::FindSpan` fully qualified at every QML/metatype boundary.

**Spec contradictions:** none. The plan implements every architectural unit described in the spec; the only deviation (Code-Block delegate may have no `InlineHighlighterAttached` already) is acknowledged in Task 6 as an acceptable MVP limitation and explicitly does not block the spec's DoD.

---

## Execution

This is a 14-task plan with one independent unit per task. Recommended: inline execution via `superpowers:executing-plans` since most tasks are tightly coupled and the user is already in-session.
