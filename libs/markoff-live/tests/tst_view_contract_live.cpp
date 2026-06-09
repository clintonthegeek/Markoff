// SPDX-License-Identifier: GPL-3.0-or-later
// MarkdownView contract — live leaf (spec
// docs/specs/2026-06-09-markdownview-contract-v2-design.md §4.1,
// plan Task 7). Cursor slots now; read-only slots arrive in Task 8.
//
// Drives the REAL Markoff::Live::EditorWidget (QQuickWidget hosting
// EditorContent.qml → LiveView) and reads/writes cursor position through
// the BASE MarkdownView pointer — invariant 5: the production callsite
// (Corbomite's polymorphic `MarkdownView *activeLeaf()`), not a
// binding-only synonym.
//
// Cursor authority (L3, docs/specs/2026-05-22-cursor-authority-decision.md):
// the chokepoint is canonical. setCursorPosition WRITES through
// LiveCursorState::requestTextCaretAtRow; cursorPosition READS
// LiveCursorState::currentTextCaret. No new cursor store is involved.
//
// Timing note: requestTextCaretAtRow stages a *pending* focus that only
// resolves into the canonical cursor once the target row's QML delegate
// has registered via delegateAvailable (developmental history §A;
// chokepoint spec §5.1). init() therefore waits for the model rows and
// settles the event loop before any slot runs, and every read-after-write
// goes through QTRY_*.

#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/EditorWidget.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveListModelBinding.h>

#include "../../markoff-core/tests/ViewContractChecks.h"

using Markoff::Live::EditorWidget;
using Markoff::Live::LiveCursorState;

class TstViewContractLive : public QObject {
    Q_OBJECT

    Markoff::MarkoffDocument *m_doc    = nullptr;
    EditorWidget             *m_widget = nullptr;

    // The contract is exercised through the base pointer (invariant 5).
    Markoff::MarkdownView *baseView() const { return m_widget; }
    LiveCursorState *cursorState() const {
        return m_widget->binding()->cursorState();
    }

private Q_SLOTS:
    void init() {
        m_doc = new Markoff::MarkoffDocument(1);
        m_doc->loadFromMarkdown(ViewContract::fixture());
        m_widget = new EditorWidget;
        m_widget->resize(800, 600);
        m_widget->setDocument(m_doc);
        m_widget->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_widget));
        // Wait for the model rows, then let the ListView realise its
        // delegates (delegateAvailable registration) so chokepoint
        // requests can resolve.
        QTRY_COMPARE(m_widget->binding()->model()->rowCount(), 3);
        QTRY_VERIFY(cursorState()->isDelegateRegistered(
            cursorState()->blockAnchorAt(2)));
        QTest::qWait(50);
    }

    void cleanup() {
        delete m_widget;
        m_widget = nullptr;
        delete m_doc;
        m_doc = nullptr;
    }

    /// Empirical pin of the fixture's flat-line model (spec §3: each
    /// block contributes 1 + internal-'\n' lines). Block 1 — the fenced
    /// code block — retains its fences AND internal newlines in the
    /// buffer, so the flat lines are:
    ///   1 = "alpha one", 2 = "```", 3 = "code line", 4 = "```",
    ///   5 = "omega end".
    /// If this slot fails, the expected line numbers in the slots below
    /// are stale, not the production mapping.
    void fixture_line_model_is_as_expected() {
        const auto ids = m_doc->iterateBlocks();
        QCOMPARE(int(ids.size()), 3);
        QCOMPARE(m_doc->blockText(ids[0]), QByteArray("alpha one"));
        QCOMPARE(m_doc->blockText(ids[1]), QByteArray("```\ncode line\n```"));
        QCOMPARE(m_doc->blockText(ids[2]), QByteArray("omega end"));
    }

    void cursorPosition_maps_flat_lines() {
        // Place the caret in "omega end" (model row 2, qtPos 6) through
        // the chokepoint, then read through the BASE pointer.
        cursorState()->requestTextCaretAtRow(2, 6);
        QTRY_COMPARE(baseView()->cursorPosition().line, 5);
        QCOMPARE(baseView()->cursorPosition().column, 7);
    }

    void setCursorPosition_routes_through_chokepoint() {
        baseView()->setCursorPosition({5, 7});
        QTRY_VERIFY(cursorState()->currentTextCaret().has_value());
        QTRY_COMPARE(cursorState()->currentTextCaret()->cachedQtPos,
                     quint32(6));
        // A line inside the code block: line 3 = "code line", col 1 →
        // block row 1, intra-block qtPos 4 (just past the first '\n'
        // of "```\ncode line\n```").
        baseView()->setCursorPosition({3, 1});
        QTRY_VERIFY(cursorState()->currentTextCaret().has_value());
        QTRY_COMPARE(cursorState()->currentTextCaret()->cachedQtPos,
                     quint32(4));
    }

    void setCursorPosition_clamps_out_of_range() {
        // Never a no-op: an out-of-range line clamps to the end of the
        // last block ("omega end", line 5).
        baseView()->setCursorPosition({9999, 99});
        QTRY_VERIFY(cursorState()->currentTextCaret().has_value());
        QTRY_VERIFY(baseView()->cursorPosition().line >= 5);
    }

    void cursorPositionChanged_signal_fires() {
        QSignalSpy spy(m_widget,
                       &Markoff::MarkdownView::cursorPositionChanged);
        QVERIFY(spy.isValid());
        cursorState()->requestTextCaretAtRow(0, 2);
        QTRY_VERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toInt(), 1);   // line 1 ("alpha one")
        QCOMPARE(spy.last().at(1).toInt(), 3);   // qtPos 2 → column 3
    }
};

QTEST_MAIN(TstViewContractLive)
#include "tst_view_contract_live.moc"
