// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QRectF>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/BlockKind.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

// MockTextEdit: simulates a QML TextEdit's layout state (read via Qt property system)
//
// Option B (Phase B) adds within-block visual-line motion via positionAt(x, y).
// The mock returns a deterministic position so within-block motion tests can
// assert observable state without a real QTextDocument layout.
class MockTextEdit : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle CONSTANT)
    Q_PROPERTY(qreal contentHeight READ contentHeight CONSTANT)
    Q_PROPERTY(qreal width READ width CONSTANT)
public:
    QRectF m_cursorRect;
    qreal  m_contentHeight = 20.0;
    qreal  m_width = 200.0;
    int    m_positionAtReturn = 7;
    QRectF cursorRectangle() const { return m_cursorRect; }
    qreal  contentHeight()   const { return m_contentHeight; }
    qreal  width()           const { return m_width; }
    Q_INVOKABLE int positionAt(double /*x*/, double /*y*/) const {
        return m_positionAtReturn;
    }
};

// Helper: load markdown into a D2 document and wait for the model rows to
// populate via the structureChanged/mapChanged → onD2Changed pipeline.
static bool waitForModelRows(LiveListModelBinding &binding,
                              Markoff::MarkoffDocument &doc,
                              const QByteArray &content,
                              int expectedRows,
                              int timeoutMs = 2000)
{
    doc.loadFromMarkdown(content);
    if (binding.model()->rowCount() == expectedRows)
        return true;
    QSignalSpy spy(doc.idListProxy(), &Markoff::IdListProxy::structureChanged);
    if (!spy.wait(timeoutMs))
        return binding.model()->rowCount() == expectedRows;
    return binding.model()->rowCount() == expectedRows;
}

class TestE2NavArrows : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---- D1: controller is exposed on the binding ----

    void controller_is_exposed_on_binding() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        QVERIFY(binding.navigationController() != nullptr);
    }

    void controller_returns_not_handled_for_arbitrary_key() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        // Truly unrecognised keys still fall through.
        QCOMPARE(nav->tryHandle(Qt::Key_A, 0, 0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
        QCOMPARE(nav->tryHandle(Qt::Key_Space, 0, 0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
        // Ctrl+Down has no specific handler → NotHandled (reaches here only after
        // Ctrl-prefixed paths above declined to claim it).
        QCOMPARE(nav->tryHandle(Qt::Key_Down, Qt::ControlModifier, 0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    // ---- D2: previousNavigableRow / nextNavigableRow ----

    void prev_navigable_row_at_zero_returns_minus_one() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // Load 4 paragraphs (3 blank lines → 4 blocks).
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->previousNavigableRow(0), -1);
    }

    void prev_navigable_row_walks_back() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->previousNavigableRow(3), 2);
        QCOMPARE(nav->previousNavigableRow(2), 1);
        QCOMPARE(nav->previousNavigableRow(1), 0);
    }

    void next_navigable_row_at_last_returns_minus_one() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        const int N = binding.model()->rowCount();
        QCOMPARE(N, 4);
        QCOMPARE(nav->nextNavigableRow(N - 1), -1);
    }

    void next_navigable_row_walks_forward() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->nextNavigableRow(0), 1);
        QCOMPARE(nav->nextNavigableRow(1), 2);
        QCOMPARE(nav->nextNavigableRow(2), 3);
    }

    void single_block_doc_both_directions_return_minus_one() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "only one block", 1));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->previousNavigableRow(0), -1);
        QCOMPARE(nav->nextNavigableRow(0), -1);
    }

    void navigable_row_lands_on_block_only_horizontal_rule() {
        // New semantics (spec §4 R-arrow-into): HR is isBlockOnly, so
        // previousNavigableRow / nextNavigableRow now LAND on it rather than
        // skipping it. The chokepoint resolves TextCaret→BlockSelected for HR
        // automatically. Arrow keys land BlockSelected on HR; a second press
        // escapes to the far-side text block.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // Layout: paragraph (0), HR (1), paragraph (2).
        QVERIFY(waitForModelRows(binding, doc, "alpha\n\n---\n\nbeta", 3));
        QCOMPARE(binding.model()->recordAt(1).kind, BlockKind::HorizontalRule);

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        // Forward from row 0: land on HR at row 1 (not skip to 2).
        QCOMPARE(nav->nextNavigableRow(0), 1);
        // Forward from HR at row 1: land on text at row 2.
        QCOMPARE(nav->nextNavigableRow(1), 2);
        // Backward from row 2: land on HR at row 1 (not skip to 0).
        QCOMPARE(nav->previousNavigableRow(2), 1);
        // Backward from HR at row 1: land on text at row 0.
        QCOMPARE(nav->previousNavigableRow(1), 0);
    }

    // ---- E1 + E2: Up/Down with mock edit item ----

    void up_at_visual_top_line_crosses_to_prev_block_column_preserved() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        // Mock edit for block 1 at visual top (cursorRect.y == 0 < height*0.5 == 10)
        MockTextEdit mockEdit;
        mockEdit.m_cursorRect = QRectF(42.0, 0, 2, 20);  // x=42 = desired column
        mockEdit.m_contentHeight = 20.0;

        QSignalSpy hintSpy(cs, &LiveCursorState::visualLineHintChanged);

        const int result = nav->tryHandle(Qt::Key_Up, Qt::NoModifier,
                                          /*blockIndex=*/1, /*qtPos=*/4,
                                          &mockEdit, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        // desiredVisualX should be set to cursorRect.x() = 42
        QCOMPARE(cs->desiredVisualX(), 42.0);
        // The hint was emitted (set to LastLine) then cleared synchronously.
        QVERIFY(hintSpy.count() >= 1);
        // Cursor should be at row 0 (resolved immediately since row exists)
        QCOMPARE(cs->focusedAnchorRow(), 0);
    }

    void up_at_non_top_line_moves_caret_within_block() {
        // Option B contract: within-block visual-line Up is handled by the
        // controller (no longer falls through to TextEdit's native nav).
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Beta multiline content");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && sv);

        MockTextEdit mockEdit;
        mockEdit.m_cursorRect = QRectF(0, 25, 2, 20);  // not at top
        mockEdit.m_contentHeight = 40.0;
        mockEdit.m_positionAtReturn = 3;  // mock returns position 3 for any (x,y)

        const int result = nav->tryHandle(Qt::Key_Up, Qt::NoModifier,
                                          0, 4, &mockEdit, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        // Within-block motion: selection collapsed to (block 0, position 3).
        QCOMPARE(sv->activeBlock(), 0);
        QCOMPARE(sv->activeQtPos(), 3);
    }

    void up_at_row_zero_with_no_prev_returns_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        MockTextEdit mockEdit;
        mockEdit.m_cursorRect = QRectF(0, 0, 2, 20);  // at top
        mockEdit.m_contentHeight = 20.0;

        // At row 0 with no previous row → Handled (consumed at boundary)
        const int result = nav->tryHandle(Qt::Key_Up, Qt::NoModifier,
                                          0, 0, &mockEdit, QStringLiteral("OnlyBlock"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
    }

    void down_at_visual_bottom_line_crosses_to_next_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        // Mock edit for block 0 at visual bottom:
        // contentHeight=20, cursorRect.bottom() = 20 > 20 - 10 = 10 → at bottom
        MockTextEdit mockEdit;
        mockEdit.m_cursorRect = QRectF(30.0, 5, 2, 15);  // bottom = 5+15 = 20
        mockEdit.m_contentHeight = 20.0;

        QSignalSpy hintSpy(cs, &LiveCursorState::visualLineHintChanged);

        const int result = nav->tryHandle(Qt::Key_Down, Qt::NoModifier,
                                          0, 5, &mockEdit, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(cs->desiredVisualX(), 30.0);
        // The hint was emitted (set to FirstLine) then cleared synchronously.
        QVERIFY(hintSpy.count() >= 1);
        // Cursor should be at row 1 (resolved immediately since row exists)
        QCOMPARE(cs->focusedAnchorRow(), 1);
    }

    void down_preserves_existing_desired_visual_x() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        // Set desiredVisualX to 99 before the Down press (simulating consecutive Down)
        cs->setDesiredVisualX(99.0);

        MockTextEdit mockEdit;
        mockEdit.m_cursorRect = QRectF(30.0, 5, 2, 15);  // at bottom
        mockEdit.m_contentHeight = 20.0;

        nav->tryHandle(Qt::Key_Down, Qt::NoModifier, 0, 5, &mockEdit, QStringLiteral("Alpha"));
        // Should reuse existing desiredVisualX, not overwrite with cursorRect.x()
        QCOMPARE(cs->desiredVisualX(), 99.0);
    }

    // ---- E3: Left at qtPos 0 ----

    void left_at_qtpos_0_crosses_to_prev_block_end_clears_visual_x() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        // Set desiredVisualX to something non-zero; Left should clear it
        cs->setDesiredVisualX(55.0);

        const int result = nav->tryHandle(Qt::Key_Left, Qt::NoModifier,
                                          /*blockIndex=*/1, /*qtPos=*/0,
                                          nullptr, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(cs->desiredVisualX(), -1.0);  // cleared
        // Cursor should be set to end of block 0 ("Alpha" len=5)
        QCOMPARE(cs->focusedQtPos(), 5);
    }

    void left_at_nonzero_qtpos_moves_caret_within_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && sv);

        const int result = nav->tryHandle(Qt::Key_Left, Qt::NoModifier,
                                          1, 3, nullptr, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(sv->activeBlock(), 1);
        QCOMPARE(sv->activeQtPos(), 2);  // 3 - 1
    }

    void left_at_row_0_qtpos_0_returns_handled_at_boundary() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        // At the very first block's pos 0: Handled (consumed at boundary)
        const int result = nav->tryHandle(Qt::Key_Left, Qt::NoModifier,
                                          0, 0, nullptr, QStringLiteral("OnlyBlock"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
    }

    // ---- E4: Right at end ----

    void right_at_end_crosses_to_next_block_start_clears_visual_x() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        cs->setDesiredVisualX(77.0);

        const int result = nav->tryHandle(Qt::Key_Right, Qt::NoModifier,
                                          /*blockIndex=*/0, /*qtPos=*/5,  // len("Alpha")=5
                                          nullptr, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(cs->desiredVisualX(), -1.0);  // cleared
        QCOMPARE(cs->focusedQtPos(), 0);  // beginning of next block
    }

    void right_at_non_end_moves_caret_within_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && sv);

        const int result = nav->tryHandle(Qt::Key_Right, Qt::NoModifier,
                                          0, 2, nullptr, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(sv->activeBlock(), 0);
        QCOMPARE(sv->activeQtPos(), 3);  // 2 + 1
    }

    void right_at_last_row_end_returns_handled_at_boundary() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        // At the last block's end: Handled (consumed at boundary)
        const int result = nav->tryHandle(Qt::Key_Right, Qt::NoModifier,
                                          0, 9, nullptr, QStringLiteral("OnlyBlock"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
    }
};

QTEST_MAIN(TestE2NavArrows)
#include "tst_live_render_e2_nav_arrows.moc"
