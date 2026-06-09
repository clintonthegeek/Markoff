# Styled-View Read-Only Table Rendering — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render `BlockKind::Table` blocks as real, read-only Qt `QTextTable` grids in the `markoff-styled` view leaf (edit via Source mode).

**Architecture:** A table block's GFM pipe-source buffer stays canonical in `MarkoffDocument`. The view's `QTextDocument` shows it as a native `QTextTable` frame. Because `SourceTextDocumentBinding`'s reverse path is a *whole-document* text diff that would clobber any frame, we add an **opaque-block seam**: when a view registers an `OpaqueBlockRenderer`, the binding switches its reverse path to per-block reconciliation that re-renders opaque blocks via a callback and leaves unchanged frames alone. The seam is inert when no renderer is set, so `markoff-source` is unaffected.

**Tech Stack:** C++20, Qt6 (Widgets/Gui/Test), CMake, QtTest. Build `build-dev`; tests via `scripts/run-tests.sh` (offscreen).

**Spec:** `docs/specs/2026-05-30-styled-table-rendering-design.md`

**Conventions for every file touched:** SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later`; `tr()` for user strings; cap builds at `-j 8`; tests run offscreen via the harness.

**Build/test commands used throughout:**
- Build: `cmake --build build-dev -j 8`
- One binary: `scripts/run-tests.sh --bin <name>`
- Fast baseline: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`

---

## Phase 0 — Gating spike: a QTextTable frame survives a reverse sync

**This phase gates the whole design (spec §10 R1).** If a frame cannot be kept alive across a reverse sync via the seam, stop and rethink before building anything else.

### Task 0.1: Falsifiability — prove a frame is clobbered WITHOUT the seam

**Files:**
- Create: `libs/markoff-core/tests/tst_binding_opaque_block.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt` (register the test)

- [ ] **Step 1: Write the failing test (frame survival, no seam yet)**

IMPORTANT (learned from `tst_binding_reverse.cpp:3-5`): `QTextDocument::contentsChange` only fires when a layout is installed, so binding tests use a **`QPlainTextEdit::document()`**, not a raw `QTextDocument`. And `d2DocumentChanged` is **debounced via `QTimer::singleShot(0,...)`**, so pump with `QCoreApplication::processEvents()` after each model edit.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QTextFrame>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

static void pumpEvents() { QCoreApplication::processEvents(); }

namespace {
int frameCount(QTextDocument *doc) {
    int n = 0;
    const auto kids = doc->rootFrame()->childFrames();
    for (QTextFrame *f : kids)
        if (qobject_cast<QTextTable *>(f)) ++n;
    return n;
}
// Insert a trivial 2x2 QTextTable over [startPos,endPos) of the doc.
void materializeTrivialTable(QTextDocument *doc, int startPos, int endPos) {
    QTextCursor c(doc);
    c.setPosition(startPos);
    c.setPosition(endPos, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    QTextTableFormat tf;
    tf.setBorder(1);
    c.insertTable(2, 2, tf);
}
}  // namespace

class TstBindingOpaqueBlock : public QObject {
    Q_OBJECT
private slots:
    void frame_survives_unrelated_block_edit() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(
            "para A\n\n| a | b |\n|---|---|\n| c | d |\n\npara B");

        QPlainTextEdit edit;                 // installs a layout (see note above)
        QTextDocument *qdoc = edit.document();
        SourceTextDocumentBinding binding;
        binding.setTextDocument(qdoc);
        binding.setMarkoffDocument(&doc);    // seeds qdoc from widgetFlatView

        // Find the table block's document region by block number. Blocks:
        // 0 = "para A", 1..N = table rows, last = "para B". For the spike we
        // materialize over the whole middle region between the two paragraphs.
        const int startPos = qdoc->findBlockByNumber(1).position();
        int endPos = qdoc->characterCount() - 1;
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
            if (b.text().startsWith("para B")) { endPos = b.position() - 1; break; }
        }
        materializeTrivialTable(qdoc, startPos, endPos);
        QCOMPARE(frameCount(qdoc), 1);

        // Mutate an UNRELATED block (para A) and let d2DocumentChanged fire.
        const auto blocks = doc.iterateBlocks();
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(blocks.front(), 0, 0, QByteArray("X"), t);
        }
        pumpEvents();  // debounced d2DocumentChanged → binding reverse pass

        // THE ASSERTION: the frame must still exist.
        QCOMPARE(frameCount(qdoc), 1);
    }
};

QTEST_MAIN(TstBindingOpaqueBlock)
#include "tst_binding_opaque_block.moc"
```

- [ ] **Step 2: Register the test in CMake**

`libs/markoff-core/tests/CMakeLists.txt` uses explicit registration (no helper macro). Add next to `tst_binding_reverse` (it needs Gui+Widgets for QTextDocument/QTextTable):

```cmake
add_executable(tst_binding_opaque_block tst_binding_opaque_block.cpp)
add_test(NAME tst_binding_opaque_block COMMAND tst_binding_opaque_block)
target_link_libraries(tst_binding_opaque_block PRIVATE Qt6::Test Qt6::Gui Qt6::Widgets markoff_core)
set_tests_properties(tst_binding_opaque_block PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build and run; verify it FAILS (frame clobbered)**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_binding_opaque_block
```
Expected: FAIL on the final `QCOMPARE(frameCount, 1)` (got 0) — proves the whole-doc reverse diff destroys the frame. If `d2DocumentChanged` is debounced and doesn't fire synchronously, the test may instead pass spuriously — if so, pump the event loop with `QTest::qWait(50)` before the final assertion and confirm it then FAILS. Record which.

- [ ] **Step 4: Commit the falsifiability proof**

```bash
git add libs/markoff-core/tests/tst_binding_opaque_block.cpp libs/markoff-core/tests/CMakeLists.txt
git commit -m "test(core): falsifiable frame-clobber proof for opaque-block seam (RED)"
```

### Task 0.2: Implement the opaque-block seam (make 0.1 GREEN)

**Files:**
- Create: `libs/markoff-core/include/markoff/core/OpaqueBlockRenderer.h`
- Modify: `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Modify: `libs/markoff-core/tests/tst_binding_opaque_block.cpp` (supply a test renderer)

- [ ] **Step 1: Add the OpaqueBlockRenderer interface**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>

class QTextCursor;

namespace Markoff {

/// View-supplied hook letting SourceTextDocumentBinding render selected blocks
/// as opaque document objects (e.g. a QTextTable frame) instead of flat text.
/// The binding stays view-agnostic: it knows only "this block is opaque" and
/// "(re)build it here". Registering a renderer switches the binding's reverse
/// path to per-block reconciliation; with no renderer the binding is unchanged.
class OpaqueBlockRenderer {
public:
    virtual ~OpaqueBlockRenderer() = default;

    /// True if `id` (of kind `kind`) should be rendered as an opaque object.
    virtual bool isOpaque(BlockId id, BlockKind kind) const = 0;

    /// (Re)build the opaque representation for `id`. The binding has already
    /// selected+removed the block's previous document region and positioned
    /// `at` at the insertion point. The callee inserts its object and leaves
    /// `at` at the region end. Returns the number of QTextDocument characters
    /// the inserted representation occupies.
    virtual int renderOpaque(QTextCursor &at, BlockId id) = 0;
};

}  // namespace Markoff
```

- [ ] **Step 2: Add seam members + mode branch to the binding header**

In `SourceTextDocumentBinding.h`, add the include and a forward use, a setter, and private helpers/members:

```cpp
// near the other includes:
// (forward-declared to keep the header light)
namespace Markoff { class OpaqueBlockRenderer; }
```

Add to the public section (after `setTextDocument`):

```cpp
    /// Register a renderer for opaque blocks. nullptr (default) → the binding
    /// uses its original whole-document reverse diff (markoff-source path).
    void setOpaqueRenderer(Markoff::OpaqueBlockRenderer *r);
```

Add to the private section:

```cpp
    void reverseSyncWholeDoc();   ///< original path (no opaque renderer)
    void reverseSyncPerBlock();   ///< opaque-aware path
    Markoff::OpaqueBlockRenderer *m_opaqueRenderer = nullptr;
```

- [ ] **Step 3: Implement the setter and split onD2DocumentChanged**

In `SourceTextDocumentBinding.cpp`, add the include `#include <markoff/core/OpaqueBlockRenderer.h>` and `#include <QTextTable>` / `#include <QTextFrame>`.

Add the setter:

```cpp
void SourceTextDocumentBinding::setOpaqueRenderer(Markoff::OpaqueBlockRenderer *r)
{
    if (m_opaqueRenderer == r) return;
    m_opaqueRenderer = r;
    // Re-seed: if the renderer is set after the document was already loaded,
    // the initial plain-text seed has no frames. Rebuild opaque-aware.
    syncQtDocumentFromMarkoff();
}
```

Replace the body of `onD2DocumentChanged` so the existing diff moves into `reverseSyncWholeDoc` and a branch is added:

```cpp
void SourceTextDocumentBinding::onD2DocumentChanged()
{
    if (m_applyingLocalEdit) return;
    if (!m_textDocument) return;
    if (!m_subscribedDoc) return;

    if (m_opaqueRenderer) reverseSyncPerBlock();
    else                  reverseSyncWholeDoc();

    // Caret re-assert (unchanged; applies to both modes).
    if (m_pendingCaret) {
        const int pos = sepViewPosOf(m_pendingCaret->block,
                                     m_pendingCaret->offsetInBlock);
        emitCaret(pos, pos);
        m_pendingCaret.reset();
    }
}
```

Move the existing prefix/suffix diff (old lines 510–545, WITHOUT the caret block) verbatim into a new `reverseSyncWholeDoc()`.

- [ ] **Step 3b: Make initial seeding opaque-aware (CRITICAL — lockstep precondition)**

`syncQtDocumentFromMarkoff()` currently does one `setPlainText(widgetFlatView())`. A multi-line table block becomes several plain `QTextBlock`s, breaking the "1 model block ↔ 1 doc top-level element" assumption `reverseSyncPerBlock` relies on. When a renderer is set, seed block-by-block so each opaque block starts life as a single frame:

```cpp
void SourceTextDocumentBinding::syncQtDocumentFromMarkoff()
{
    if (!m_subscribedDoc || !m_textDocument) return;

    if (!m_opaqueRenderer) {
        // Original path (markoff-source): single setPlainText of the flat view.
        const QString text = QString::fromUtf8(m_subscribedDoc->widgetFlatView());
        if (m_textDocument->toPlainText() == text) return;
        m_applyingRemoteEdit = true;
        m_textDocument->setPlainText(text);
        m_applyingRemoteEdit = false;
        return;
    }

    // Opaque-aware seed: build the document block-by-block so opaque blocks
    // are frames from the start (atomic top-level elements → clean lockstep).
    m_applyingRemoteEdit = true;
    m_textDocument->clear();
    QTextCursor c(m_textDocument);
    const auto blocks = m_subscribedDoc->iterateBlocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        const BlockId id = blocks[i];
        if (i > 0) c.insertBlock();  // single-'\n' separator (WP unification)
        if (m_opaqueRenderer->isOpaque(id, m_subscribedDoc->blockKind(id))) {
            m_opaqueRenderer->renderOpaque(c, id);
        } else {
            c.insertText(QString::fromUtf8(m_subscribedDoc->blockText(id)));
        }
    }
    m_applyingRemoteEdit = false;
}
```

NOTE: `insertTable` itself creates surrounding blocks; verify the separator handling so there is exactly one empty line's worth of structure between a frame and its neighbours (a frame is its own top-level element — you may need to NOT `insertBlock()` immediately before/after a frame). Adjust by inspecting `toPlainText()`/`childFrames()` in the test. This is the fiddly bit; the materialize test (Task 1.2) + render test (Task 2.2) pin it down.

- [ ] **Step 4: Implement reverseSyncPerBlock (minimal: frame survival)**

```cpp
void SourceTextDocumentBinding::reverseSyncPerBlock()
{
    MarkoffDocument *doc = m_subscribedDoc;
    const auto blocks = doc->iterateBlocks();

    m_applyingRemoteEdit = true;

    // Walk model blocks in lockstep with QTextDocument top-level elements.
    // A normal block is a run of one QTextBlock; an opaque block is a
    // QTextTable child frame tagged setComment("markoff-table:<id>").
    QTextBlock qblk = m_textDocument->begin();
    for (size_t i = 0; i < blocks.size(); ++i) {
        const BlockId id = blocks[i];
        const BlockKind kind = doc->blockKind(id);
        const bool opaque = m_opaqueRenderer->isOpaque(id, kind);

        if (opaque) {
            // Does a frame for this id already exist at the current position?
            QTextTable *existing = qobject_cast<QTextTable *>(
                qblk.isValid() ? m_textDocument->frameAt(qblk.position())
                               : nullptr);
            const QString want = QStringLiteral("markoff-table:")
                                 + QString::number(id.raw());
            const bool present = existing
                && existing->frameFormat().comment() == want;
            if (present) {
                // Frame exists; leave it. Advance past the frame.
                qblk = existing->lastCursorPosition().block().next();
                continue;
            }
            // Build a fresh frame at qblk's position.
            QTextCursor c(m_textDocument);
            c.setPosition(qblk.isValid() ? qblk.position()
                                         : m_textDocument->characterCount() - 1);
            m_opaqueRenderer->renderOpaque(c, id);
            qblk = c.block().isValid() ? c.block().next() : m_textDocument->end();
            continue;
        }

        // Normal block: ensure its text matches blockText(id).
        if (qblk.isValid()) {
            const QString want = QString::fromUtf8(doc->blockText(id));
            if (qblk.text() != want) {
                QTextCursor c(qblk);
                c.select(QTextCursor::BlockUnderCursor);
                // BlockUnderCursor includes the leading separator; re-handle:
                c.setPosition(qblk.position());
                c.setPosition(qblk.position() + qblk.length() - 1,
                              QTextCursor::KeepAnchor);
                c.insertText(want);
            }
            qblk = qblk.next();
        }
    }

    m_applyingRemoteEdit = false;
}
```

NOTE: `BlockId`'s stable key is `id.raw()` (uint64; confirmed in `BlockId.h`). `QTextDocument::frameAt(int)` exists in Qt6 — verify it returns the enclosing `QTextTable` for a position at the frame; if a position just before the frame doesn't resolve, walk `rootFrame()->childFrames()` and match the comment instead. This is a research step, not a guess to ship.

- [ ] **Step 5: Add a test renderer + flip 0.1 to assert via the seam**

In `tst_binding_opaque_block.cpp`, add a renderer and register it before the edit:

```cpp
class TableTestRenderer : public Markoff::OpaqueBlockRenderer {
public:
    bool isOpaque(Markoff::BlockId, Markoff::BlockKind kind) const override {
        return kind == Markoff::BlockKind::Table;
    }
    int renderOpaque(QTextCursor &at, Markoff::BlockId) override {
        const int before = at.position();
        QTextTableFormat tf; tf.setBorder(1);
        // Comment key must match reverseSyncPerBlock's format.
        at.insertTable(2, 2, tf);
        return at.position() - before;
    }
};
```

Wire it: after `binding.setMarkoffDocument(&doc)`, call
`TableTestRenderer rnd; binding.setOpaqueRenderer(&rnd);` then trigger a reverse
pass (e.g. a no-op `d2ApplyBufferEdit` or `QTest::qWait`) so the frame
materializes through the seam. Then run the unrelated-edit mutation and assert
`frameCount == 1`.

(The renderer must set the same `setComment("markoff-table:<id>")` the binding
checks; thread the id through `renderOpaque`'s param.)

- [ ] **Step 6: Build and run; verify GREEN**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_binding_opaque_block
```
Expected: PASS — the frame survives the unrelated edit.

- [ ] **Step 7: Verify markoff-source is unaffected**

```
scripts/run-tests.sh -R 'tst_source'
```
Expected: all source-leaf tests PASS (seam inert without a renderer).

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff/core/OpaqueBlockRenderer.h \
        libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h \
        libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-core/tests/tst_binding_opaque_block.cpp
git commit -m "feat(core): opaque-block seam in SourceTextDocumentBinding (frame survives reverse sync)"
```

### Task 0.3: Coordinate integrity — a block AFTER the frame stays correct

**Files:**
- Modify: `libs/markoff-core/tests/tst_binding_opaque_block.cpp`

- [ ] **Step 1: Add a test asserting "para B" text is intact after the seam runs**

```cpp
void block_after_frame_intact() {
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("para A\n\n| a | b |\n|---|---|\n| c | d |\n\npara B");
    QPlainTextEdit edit;
    QTextDocument *qdoc = edit.document();
    SourceTextDocumentBinding binding;
    TableTestRenderer rnd;
    binding.setTextDocument(qdoc);
    binding.setMarkoffDocument(&doc);
    binding.setOpaqueRenderer(&rnd);   // re-seeds opaque-aware (Task 0.2 Step 3/3b)

    // The frame exists from the opaque-aware seed.
    QCOMPARE(frameCount(qdoc), 1);
    // The last paragraph must still read "para B" verbatim.
    bool found = false;
    for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next())
        if (b.text() == "para B") { found = true; break; }
    QVERIFY(found);
}
```

- [ ] **Step 2: Build, run, verify PASS**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_binding_opaque_block
```
Expected: PASS (both slots).

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/tests/tst_binding_opaque_block.cpp
git commit -m "test(core): block-after-frame coordinate integrity for opaque seam"
```

**PHASE 0 EXIT GATE:** `tst_binding_opaque_block` green + `tst_source` suite green. If green, the architecture holds — proceed. If the frame cannot be kept alive, STOP and revisit the spec.

---

## Phase 1 — TableFrame: parse + materialize (styled-side)

### Task 1.1: ParsedTable + pipe-table parser (TDD)

**Files:**
- Create: `libs/markoff-styled/src/TableFrame.h`
- Create: `libs/markoff-styled/src/TableFrame.cpp`
- Create: `libs/markoff-styled/tests/tst_styled_table_parse.cpp`
- Modify: `libs/markoff-styled/CMakeLists.txt` (add TableFrame.cpp to the lib sources)
- Modify: `libs/markoff-styled/tests/CMakeLists.txt` (`add_styled_test(tst_styled_table_parse)`)

- [ ] **Step 1: Write the failing parse test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include "../src/TableFrame.h"
using namespace Markoff::Styled;

class TstStyledTableParse : public QObject {
    Q_OBJECT
private slots:
    void parses_3x2_with_alignment() {
        const QByteArray src =
            "| H1 | H2 |\n| :--- | ---: |\n| a | b |\n| c | d |";
        ParsedTable t = parsePipeTable(src);
        QVERIFY(t.ok);
        QCOMPARE(t.header.size(), 2);
        QCOMPARE(t.header.at(0), QString("H1"));
        QCOMPARE(t.body.size(), 2);
        QCOMPARE(t.body.at(0).at(1), QString("b"));
        QCOMPARE(t.alignments.at(0), Qt::AlignLeft);
        QCOMPARE(t.alignments.at(1), Qt::AlignRight);
    }
    void center_alignment() {
        ParsedTable t = parsePipeTable("| H |\n| :---: |\n| x |");
        QVERIFY(t.ok);
        QCOMPARE(t.alignments.at(0), Qt::AlignHCenter);
    }
    void ragged_row_padded_to_header() {
        ParsedTable t = parsePipeTable("| A | B |\n|---|---|\n| only |");
        QVERIFY(t.ok);
        QCOMPARE(t.body.at(0).size(), 2);
        QCOMPARE(t.body.at(0).at(1), QString());
    }
    void non_table_is_not_ok() {
        QVERIFY(!parsePipeTable("just a paragraph").ok);
        QVERIFY(!parsePipeTable("| no separator row |").ok);
    }
};
QTEST_MAIN(TstStyledTableParse)
#include "tst_styled_table_parse.moc"
```

- [ ] **Step 2: Run; verify it fails to compile (TableFrame.h missing)**

```
cmake --build build-dev -j 8
```
Expected: compile error — `TableFrame.h` not found.

- [ ] **Step 3: Write TableFrame.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <Qt>

class QTextCursor;

namespace Markoff::Styled {

struct ParsedTable {
    QStringList          header;
    QList<QStringList>   body;
    QList<Qt::Alignment> alignments;
    bool                 ok = false;
};

/// Tokenize a GFM pipe-table buffer. `ok==false` if it is not a valid
/// table (caller degrades to text rendering).
ParsedTable parsePipeTable(const QByteArray &buffer);

/// Insert a read-only QTextTable for `t` at `at`; returns the doc-char span.
/// Sets frame comment `commentKey` so the binding can identify it.
int materializeTable(QTextCursor &at, const ParsedTable &t,
                     const QString &commentKey, qreal fontScale);

}  // namespace Markoff::Styled
```

- [ ] **Step 4: Write parsePipeTable in TableFrame.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableFrame.h"

#include <QTextCursor>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTextBlockFormat>
#include <QTextCharFormat>

namespace Markoff::Styled {

namespace {
QStringList splitRow(const QString &line) {
    QString s = line.trimmed();
    if (s.startsWith('|')) s.remove(0, 1);
    if (s.endsWith('|')) s.chop(1);
    QStringList cells = s.split('|');
    for (QString &c : cells) c = c.trimmed();
    return cells;
}
Qt::Alignment alignFor(const QString &spec) {
    const QString s = spec.trimmed();
    const bool l = s.startsWith(':');
    const bool r = s.endsWith(':');
    if (l && r) return Qt::AlignHCenter;
    if (r)      return Qt::AlignRight;
    return Qt::AlignLeft;
}
bool isSeparatorRow(const QStringList &cells) {
    if (cells.isEmpty()) return false;
    for (const QString &c : cells) {
        const QString s = c.trimmed();
        if (s.isEmpty()) return false;
        for (QChar ch : s)
            if (ch != ':' && ch != '-') return false;
        if (!s.contains('-')) return false;  // must have >=1 dash
    }
    return true;
}
}  // namespace

ParsedTable parsePipeTable(const QByteArray &buffer) {
    ParsedTable t;
    const QString text = QString::fromUtf8(buffer);
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    if (lines.size() < 2) return t;  // need header + separator

    const QStringList header = splitRow(lines.at(0));
    const QStringList sep    = splitRow(lines.at(1));
    if (!isSeparatorRow(sep)) return t;
    if (header.isEmpty()) return t;

    const int cols = header.size();
    t.header = header;
    for (int c = 0; c < cols; ++c)
        t.alignments.append(c < sep.size() ? alignFor(sep.at(c)) : Qt::AlignLeft);

    for (int i = 2; i < lines.size(); ++i) {
        QStringList row = splitRow(lines.at(i));
        while (row.size() < cols) row.append(QString());
        if (row.size() > cols) row = row.mid(0, cols);
        t.body.append(row);
    }
    t.ok = true;
    return t;
}

}  // namespace Markoff::Styled
```

- [ ] **Step 5: Add TableFrame.cpp to the styled lib + register the test**

In `libs/markoff-styled/CMakeLists.txt`, add `src/TableFrame.cpp` to the library's source list (next to `src/FormatPass.cpp`).
In `libs/markoff-styled/tests/CMakeLists.txt` (explicit registration, no helper) add:

```cmake
add_executable(tst_styled_table_parse tst_styled_table_parse.cpp)
add_test(NAME tst_styled_table_parse COMMAND tst_styled_table_parse)
target_link_libraries(tst_styled_table_parse PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_table_parse PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 6: Build, run, verify PASS**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_parse
```
Expected: PASS (all 4 slots).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-styled/src/TableFrame.h libs/markoff-styled/src/TableFrame.cpp \
        libs/markoff-styled/tests/tst_styled_table_parse.cpp \
        libs/markoff-styled/CMakeLists.txt libs/markoff-styled/tests/CMakeLists.txt
git commit -m "feat(styled): pipe-table parser (ParsedTable + parsePipeTable)"
```

### Task 1.2: materializeTable → QTextTable (TDD)

**Files:**
- Modify: `libs/markoff-styled/src/TableFrame.cpp`
- Create: `libs/markoff-styled/tests/tst_styled_table_materialize.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing materialize test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextTable>
#include <QTextFrame>
#include "../src/TableFrame.h"
using namespace Markoff::Styled;

class TstStyledTableMaterialize : public QObject {
    Q_OBJECT
private slots:
    void builds_3x2_grid_with_text_and_alignment() {
        ParsedTable t = parsePipeTable(
            "| H1 | H2 |\n| :--- | ---: |\n| a | b |\n| c | d |");
        QVERIFY(t.ok);
        QTextDocument doc;
        QTextCursor c(&doc);
        const int span = materializeTable(c, t, "markoff-table:42", 1.0);
        QVERIFY(span > 0);

        QTextTable *tbl = nullptr;
        for (QTextFrame *f : doc.rootFrame()->childFrames())
            if (auto *x = qobject_cast<QTextTable *>(f)) tbl = x;
        QVERIFY(tbl);
        QCOMPARE(tbl->rows(), 3);     // header + 2 body
        QCOMPARE(tbl->columns(), 2);
        QCOMPARE(tbl->cellAt(0,0).firstCursorPosition().block().text(),
                 QString("H1"));
        QCOMPARE(tbl->cellAt(2,1).firstCursorPosition().block().text(),
                 QString("d"));
        QCOMPARE(tbl->frameFormat().comment(), QString("markoff-table:42"));
        // Column 1 right-aligned on every row.
        QCOMPARE(tbl->cellAt(0,1).firstCursorPosition().blockFormat()
                     .alignment(), Qt::AlignRight);
    }
};
QTEST_MAIN(TstStyledTableMaterialize)
#include "tst_styled_table_materialize.moc"
```

- [ ] **Step 2: Run; verify it fails (materializeTable unimplemented → link error)**

```
cmake --build build-dev -j 8
```
Expected: link/compile error (no `materializeTable` definition).

- [ ] **Step 3: Implement materializeTable**

```cpp
int materializeTable(QTextCursor &at, const ParsedTable &t,
                     const QString &commentKey, qreal fontScale) {
    const int rows = 1 + t.body.size();
    const int cols = t.header.size();
    const int before = at.position();

    QTextTableFormat tf;
    tf.setBorder(1);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setCellPadding(4.0 * fontScale);
    tf.setCellSpacing(0);
    tf.setBorderCollapse(true);
    tf.setComment(commentKey);
    QTextTable *table = at.insertTable(rows, cols, tf);

    auto fill = [&](int r, int c, const QString &text, bool header) {
        QTextCursor cc = table->cellAt(r, c).firstCursorPosition();
        QTextBlockFormat bf = cc.blockFormat();
        if (c < t.alignments.size()) bf.setAlignment(t.alignments.at(c));
        cc.setBlockFormat(bf);
        QTextCharFormat cf;
        if (header) cf.setFontWeight(QFont::Bold);
        cc.insertText(text, cf);
    };
    for (int c = 0; c < cols; ++c)
        fill(0, c, c < t.header.size() ? t.header.at(c) : QString(), true);
    for (int r = 0; r < t.body.size(); ++r)
        for (int c = 0; c < cols; ++c)
            fill(r + 1, c, t.body.at(r).value(c), false);

    return at.position() - before;
}
```

(Add `#include <QFont>` to TableFrame.cpp.)

- [ ] **Step 4: Register the test, build, run, verify PASS**

Add to `libs/markoff-styled/tests/CMakeLists.txt`:

```cmake
add_executable(tst_styled_table_materialize tst_styled_table_materialize.cpp)
add_test(NAME tst_styled_table_materialize COMMAND tst_styled_table_materialize)
target_link_libraries(tst_styled_table_materialize PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_table_materialize PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```
```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_materialize
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled/src/TableFrame.cpp \
        libs/markoff-styled/tests/tst_styled_table_materialize.cpp \
        libs/markoff-styled/tests/CMakeLists.txt
git commit -m "feat(styled): materialize ParsedTable into a read-only QTextTable"
```

---

## Phase 2 — Wire into the styled Editor

### Task 2.1: StyledTableRenderer (OpaqueBlockRenderer impl)

**Files:**
- Create: `libs/markoff-styled/src/StyledTableRenderer.h`
- Create: `libs/markoff-styled/src/StyledTableRenderer.cpp`
- Modify: `libs/markoff-styled/CMakeLists.txt`

- [ ] **Step 1: Write the renderer**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/OpaqueBlockRenderer.h>
namespace Markoff { class MarkoffDocument; }
namespace Markoff::Styled {

class StyledTableRenderer : public Markoff::OpaqueBlockRenderer {
public:
    void setMarkoffDocument(Markoff::MarkoffDocument *d) { m_doc = d; }
    void setFontScale(qreal s) { m_fontScale = s; }
    bool isOpaque(Markoff::BlockId id, Markoff::BlockKind kind) const override;
    int  renderOpaque(QTextCursor &at, Markoff::BlockId id) override;
private:
    Markoff::MarkoffDocument *m_doc = nullptr;
    qreal m_fontScale = 1.0;
};

}  // namespace Markoff::Styled
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyledTableRenderer.h"
#include "TableFrame.h"
#include <QString>
#include <markoff/core/MarkoffDocument.h>

namespace Markoff::Styled {

bool StyledTableRenderer::isOpaque(Markoff::BlockId id,
                                   Markoff::BlockKind kind) const {
    if (kind != Markoff::BlockKind::Table || !m_doc) return false;
    return parsePipeTable(m_doc->blockText(id)).ok;  // degrade-to-text valve
}

int StyledTableRenderer::renderOpaque(QTextCursor &at, Markoff::BlockId id) {
    ParsedTable t = parsePipeTable(m_doc->blockText(id));
    if (!t.ok) return 0;
    const QString key = QStringLiteral("markoff-table:")
                        + QString::number(id.raw());  // matches reverseSyncPerBlock
    return materializeTable(at, t, key, m_fontScale);
}

}  // namespace Markoff::Styled
```

NOTE: the `id` → stable-key conversion must match what `reverseSyncPerBlock` uses (Task 0.2 Step 4). Use the same `BlockId` accessor in both. Fix both call sites together.

- [ ] **Step 2: Add to the lib, build (no test yet — exercised in 2.2)**

Add `src/StyledTableRenderer.cpp` to `libs/markoff-styled/CMakeLists.txt`.
```
cmake --build build-dev -j 8
```
Expected: compiles.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-styled/src/StyledTableRenderer.h \
        libs/markoff-styled/src/StyledTableRenderer.cpp \
        libs/markoff-styled/CMakeLists.txt
git commit -m "feat(styled): StyledTableRenderer (OpaqueBlockRenderer for tables)"
```

### Task 2.2: FormatPass skips Table + Editor registers the renderer

**Files:**
- Modify: `libs/markoff-styled/src/FormatPass.cpp:525` (the `else` fallthrough)
- Modify: `libs/markoff-styled/src/Editor.cpp` (construct renderer; register on binding)
- Modify: `libs/markoff-styled/include/markoff/styled/Editor.h` (own the renderer)
- Create: `libs/markoff-styled/tests/tst_styled_table_render.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`

- [ ] **Step 1: Write the integration test (RED)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextFrame>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/styled/Editor.h>
using namespace Markoff;

class TstStyledTableRender : public QObject {
    Q_OBJECT
private slots:
    void table_renders_as_frame_in_editor() {
        MarkoffDocument doc;
        doc.loadFromMarkdown(
            "intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro");
        Styled::Editor editor;
        editor.setDocument(&doc);
        QTest::qWait(50);  // let reverse sync + format pass settle
        QTextDocument *qdoc = editor.textEdit()->document();
        int frames = 0;
        for (QTextFrame *f : qdoc->rootFrame()->childFrames())
            if (qobject_cast<QTextTable *>(f)) ++frames;
        QCOMPARE(frames, 1);
        // The trailing paragraph still reads "outro".
        bool outro = false;
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next())
            if (b.text() == "outro") outro = true;
        QVERIFY(outro);
    }
};
QTEST_MAIN(TstStyledTableRender)
#include "tst_styled_table_render.moc"
```

- [ ] **Step 2: Run; verify it fails (no frame — Editor not wired yet)**

Register it first in `libs/markoff-styled/tests/CMakeLists.txt`:

```cmake
add_executable(tst_styled_table_render tst_styled_table_render.cpp)
add_test(NAME tst_styled_table_render COMMAND tst_styled_table_render)
target_link_libraries(tst_styled_table_render PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_table_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_render
```
Expected: FAIL (frames == 0).

- [ ] **Step 3: FormatPass — explicit Table skip**

In `FormatPass.cpp`, the block-format `while` loop's trailing `else` (line ~525) currently calls `applyParagraph`. Add an explicit Table branch BEFORE the `else` that does nothing for the block format AND guard the inline-span loop to skip Table:

```cpp
} else if (kind == Markoff::BlockKind::Table) {
    // Rendered as an opaque QTextTable frame by the binding's opaque
    // renderer; FormatPass must not touch its block format or spans.
} else {
    applyParagraph(blkCursor, fontScale);
}
```

And wrap the inline-span application (the `for (const Markoff::SourceSpan &span : spans)` block) so it is skipped when `kind == BlockKind::Table`.

- [ ] **Step 4: Editor — own + register the renderer**

In `Editor.h` add a member `Markoff::Styled::StyledTableRenderer *m_tableRenderer = nullptr;` (forward-declare the type).
In `Editor.cpp::setDocument`, after the binding is created and before/at `m_binding->setMarkoffDocument(doc)`:

```cpp
if (!m_tableRenderer) m_tableRenderer = new StyledTableRenderer();
m_tableRenderer->setMarkoffDocument(doc);
m_tableRenderer->setFontScale(m_fontScale);
m_binding->setOpaqueRenderer(m_tableRenderer);
```

(Include `"StyledTableRenderer.h"`. Delete in destructor or parent it.)

- [ ] **Step 5: Build, run, verify PASS**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_render
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-styled/src/FormatPass.cpp \
        libs/markoff-styled/src/Editor.cpp \
        libs/markoff-styled/include/markoff/styled/Editor.h \
        libs/markoff-styled/tests/tst_styled_table_render.cpp \
        libs/markoff-styled/tests/CMakeLists.txt
git commit -m "feat(styled): Editor renders Table blocks as QTextTable; FormatPass skips Table"
```

### Task 2.3: FormatPass coordinate source for blocks after a frame

**Files:**
- Modify: `libs/markoff-styled/tests/tst_styled_table_render.cpp` (add a slot)
- Modify: `libs/markoff-styled/src/FormatPass.cpp` + binding doc-range query (only if the test fails)

- [ ] **Step 1: Add a coordinate-integrity slot**

```cpp
void inline_span_after_table_lands_correctly() {
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "| A | B |\n|---|---|\n| 1 | 2 |\n\nsome **bold** word");
    Styled::Editor editor;
    editor.setDocument(&doc);
    QTest::qWait(50);
    QTextDocument *qdoc = editor.textEdit()->document();
    // Find the "some bold word" block; the chars under "bold" must be bold.
    QTextBlock target;
    for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next())
        if (b.text().contains("bold")) target = b;
    QVERIFY(target.isValid());
    const int idx = target.text().indexOf("bold");
    QTextCursor c(qdoc);
    c.setPosition(target.position() + idx + 1);
    QVERIFY(c.charFormat().fontWeight() == QFont::Bold);
}
```

- [ ] **Step 2: Run. If PASS, the naive coordinate path already works — skip Step 3.**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_render
```
If FAIL (bold landed on the wrong characters because FormatPass computed `startQt` from flat bytes that don't match the frame's doc span): implement Step 3.

- [ ] **Step 3 (conditional): Binding exposes per-block document ranges; FormatPass consumes them**

Add `int SourceTextDocumentBinding::documentStartPosForBlock(BlockId) const` (and length) populated during `reverseSyncPerBlock`, returning the QTextDocument position where each block's region begins (accounting for frame spans). In `FormatPass::apply`, when the source has opaque blocks, take `startQt`/`endQt` from this query instead of `byteOffsetToQtPos(flatBytes, ...)`. Guard so the flat-byte path is used verbatim when no opaque renderer/blocks exist.

- [ ] **Step 4: Build, run, verify PASS**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_render
```
Expected: PASS (all slots).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix(styled): correct inline-format coordinates for blocks after a table frame"
```

---

## Phase 3 — Read-only enforcement

### Task 3.1: Swallow edit keys inside a frame (TDD)

**Files:**
- Modify: `libs/markoff-styled/src/StructuralTextEdit.cpp`
- Create: `libs/markoff-styled/tests/tst_styled_table_readonly.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing read-only test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextFrame>
#include <QTextCursor>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/styled/Editor.h>
using namespace Markoff;

class TstStyledTableReadonly : public QObject {
    Q_OBJECT
private slots:
    void typing_in_cell_does_not_mutate_model() {
        MarkoffDocument doc;
        doc.loadFromMarkdown("| A | B |\n|---|---|\n| 1 | 2 |");
        Styled::Editor editor; editor.setDocument(&doc);
        QTest::qWait(50);
        QTextDocument *qdoc = editor.textEdit()->document();
        QTextTable *tbl = nullptr;
        for (QTextFrame *f : qdoc->rootFrame()->childFrames())
            if (auto *x = qobject_cast<QTextTable*>(f)) tbl = x;
        QVERIFY(tbl);
        // Place caret in cell (1,0) and type.
        QTextCursor cc = tbl->cellAt(1,0).firstCursorPosition();
        editor.textEdit()->setTextCursor(cc);
        const quint64 seqBefore = doc.d2EditSequence();
        QTest::keyClicks(editor.textEdit(), "ZZZ");
        QCOMPARE(doc.d2EditSequence(), seqBefore);  // no model mutation
    }
};
QTEST_MAIN(TstStyledTableReadonly)
#include "tst_styled_table_readonly.moc"
```

- [ ] **Step 2: Register + run; verify FAIL (typing mutates / changes frame)**

Register in `libs/markoff-styled/tests/CMakeLists.txt`:

```cmake
add_executable(tst_styled_table_readonly tst_styled_table_readonly.cpp)
add_test(NAME tst_styled_table_readonly COMMAND tst_styled_table_readonly)
target_link_libraries(tst_styled_table_readonly PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_table_readonly PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_readonly
```
Expected: FAIL.

- [ ] **Step 3: Swallow edits inside a frame in keyPressEvent**

In `StructuralTextEdit::keyPressEvent`, at the very top (before undo/redo + structural routing):

```cpp
if (textCursor().currentTable() != nullptr) {
    // Tables are read-only in the styled view; edit them in Source mode.
    // Let navigation/selection keys through; swallow anything that edits.
    switch (e->key()) {
        case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_Up:   case Qt::Key_Down:
        case Qt::Key_Home: case Qt::Key_End:
        case Qt::Key_PageUp: case Qt::Key_PageDown:
            QTextEdit::keyPressEvent(e); return;
        default:
            if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
                // allow Ctrl+C (copy) and friends; block Ctrl+V/X edits.
                if (e->key() == Qt::Key_C) { QTextEdit::keyPressEvent(e); return; }
            }
            e->accept(); return;  // swallow edit
    }
}
```

- [ ] **Step 4: Build, run, verify PASS**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_readonly
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled/src/StructuralTextEdit.cpp \
        libs/markoff-styled/tests/tst_styled_table_readonly.cpp \
        libs/markoff-styled/tests/CMakeLists.txt
git commit -m "feat(styled): read-only enforcement for table cells (edit via Source mode)"
```

### Task 3.2: Defensive forward-path early-return for opaque regions

**Files:**
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp` (`onQtContentsChange`)
- Modify: `libs/markoff-core/tests/tst_binding_opaque_block.cpp`

- [ ] **Step 1: Add a test that a forward contentsChange inside a frame is ignored**

In `tst_binding_opaque_block.cpp`, add a slot that materializes via the seam, then directly invokes a cell edit (simulating a contentsChange inside the frame) and asserts the model `d2EditSequence` is unchanged. (Drive through the real `QTextDocument::contentsChange` by inserting text into a cell with `m_applyingRemoteEdit` false.)

- [ ] **Step 2: Run; verify behavior (may already pass via read-only guard)**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_binding_opaque_block
```
If FAIL: implement Step 3. If PASS (the keyPress guard already prevents any forward edit reaching here): record that and skip Step 3.

- [ ] **Step 3 (conditional): early-return in onQtContentsChange**

After resolving `hitStart` in `onQtContentsChange`, if `m_opaqueRenderer && m_opaqueRenderer->isOpaque(hitStart->blockId, doc->blockKind(hitStart->blockId))`, set `m_applyingLocalEdit=false` and `return` (the edit must not reach the model; the reverse pass will restore the frame).

- [ ] **Step 4: Build, run, verify PASS; commit**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_binding_opaque_block
git add -A
git commit -m "fix(core): forward path ignores edits landing inside an opaque block"
```

---

## Phase 4 — Regression, parser landmine, baseline

### Task 4.1: No-opaque regression

- [ ] **Step 1: Run the full styled + source suites; confirm green**

```
cmake --build build-dev -j 8
scripts/run-tests.sh -R 'tst_styled'
scripts/run-tests.sh -R 'tst_source'
```
Expected: all PASS. (Seam inert without a renderer; FormatPass Table branch only affects Table blocks.)

- [ ] **Step 2: If any pre-existing styled test regressed, fix the code (not the test) and re-run.**

### Task 4.2: Parser landmine check (empty-pipe-row split)

**Files:**
- Create: `libs/markoff-core/tests/tst_table_empty_row_no_split.cpp` (or add a slot to `tst_table_block_loading.cpp`)
- Modify: relevant tests CMakeLists if a new file

- [ ] **Step 1: Write a test that a table with a blank-ish body row stays ONE block**

```cpp
void empty_body_row_does_not_split_table() {
    MarkoffDocument doc;
    doc.loadFromMarkdown("| A | B |\n|---|---|\n|   |   |\n| 1 | 2 |");
    int tables = 0;
    for (auto id : doc.iterateBlocks())
        if (doc.blockKind(id) == BlockKind::Table) ++tables;
    QCOMPARE(tables, 1);
}
```

- [ ] **Step 2: Run.**

```
cmake --build build-dev -j 8
scripts/run-tests.sh --bin <the test binary>
```
- If PASS: the current `markoff-parser/TableHandler` does not have the historical empty-pipe-row split bug. Record the negative result (keep the test as a guard). Commit.
- If FAIL: the table split into >1 block. Fix at the **parser layer** (`markoff-parser/TableHandler` / vendored grammar — require ≥1 hyphen per delimiter cell; consolidate-parser-logic policy). File the finding in `docs/queue.md`, fix, re-run to green, commit.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "test(core): guard against empty-pipe-row table split (parser landmine)"
```

### Task 4.3: Save round-trip of an untouched materialized table

**Files:**
- Modify: `libs/markoff-styled/tests/tst_styled_table_render.cpp`

- [ ] **Step 1: Add a round-trip slot**

```cpp
void untouched_table_round_trips_byte_for_byte() {
    const QByteArray src =
        "intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro";
    MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    Styled::Editor editor; editor.setDocument(&doc);
    QTest::qWait(50);
    // Materializing the frame must not have mutated the model buffer.
    const QByteArray saved = doc.serializeForSave();
    QVERIFY(saved.contains("| A | B |"));
    QVERIFY(saved.contains("|---|---|") || saved.contains("| --- | --- |"));
}
```

- [ ] **Step 2: Build, run, verify PASS; commit**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_styled_table_render
git add -A
git commit -m "test(styled): untouched materialized table round-trips on save"
```

### Task 4.4: Full baseline

- [ ] **Step 1: Run the fast baseline; confirm no new failures vs 254/257**

```
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```
Expected: 254/257 baseline preserved plus the new table tests green (3 known offscreen flakes still excluded/failing as documented). Investigate any NEW failure before proceeding.

### Task 4.5: Docs

**Files:**
- Modify: `libs/markoff-styled/CLAUDE.md` (Table now renders read-only; note the opaque seam + deferred editable work)
- Modify: `libs/markoff-core/CLAUDE.md` (document `OpaqueBlockRenderer` + per-block reverse mode)
- Modify: `docs/queue.md` (record deferred: in-grid edit, structural ops, alignment context menu, source-reveal flip; any Discipline Log smells)
- Modify: `CLAUDE.md` (session status banner)

- [ ] **Step 1: Update the four docs as above.**

- [ ] **Step 2: Commit**

```bash
git add libs/markoff-styled/CLAUDE.md libs/markoff-core/CLAUDE.md docs/queue.md CLAUDE.md
git commit -m "docs: styled read-only tables landed; opaque-block seam + deferred editable work"
```

---

## Done-when

- `tst_binding_opaque_block`, `tst_styled_table_parse`, `tst_styled_table_materialize`, `tst_styled_table_render`, `tst_styled_table_readonly` all green.
- A pipe table renders as a real bordered grid (alignment honored) in the styled view; edit keys inside it are inert; the rest of the document edits normally.
- `tst_source` + existing `tst_styled` suites green (seam inert without a renderer).
- Save round-trips an untouched table byte-for-byte.
- Fast baseline preserved (254/257 + new tests).
- Docs updated; deferred editable work recorded.

## Notes carried from research (read before starting)

- **Reverse path is whole-document** (`SourceTextDocumentBinding.cpp:500-545`): `expected = widgetFlatView()` vs `actual = toPlainText()`, prefix/suffix diff. This is why the seam is mandatory, not optional. Phase 0 proves the fix before anything else.
- **Signal order** (`Editor.cpp:83-90`): scroll-capture connects first; binding's `onD2DocumentChanged` connects via `setMarkoffDocument` (line 89); StyleApplier's `onD2Changed` connects via its `setMarkoffDocument` (line 90) — i.e. binding reverse sync runs BEFORE FormatPass. Good: the frame exists before FormatPass computes following-block coordinates. Keep this order.
- **BlockId stable key:** `id.raw()` (uint64, from `BlockId.h`). Task 0.2 and Task 2.1 both use `QString::number(id.raw())` in the `markoff-table:<raw>` comment — keep them identical.
- **`d2DocumentChanged` is debounced** — tests use `QTest::qWait(50)` to let the reverse pass + format pass settle before asserting.
- **One agent fabricated findings earlier this session** — trust only directly-read code. Re-verify any `309f9ce^` prior-art detail before porting.
```
