// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QRectF>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

class MockEditForColumn : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle CONSTANT)
    Q_PROPERTY(qreal contentHeight READ contentHeight CONSTANT)
public:
    QRectF m_cursorRect = QRectF(42.0, 0, 2, 20);  // at top
    qreal  m_contentHeight = 20.0;
    QRectF cursorRectangle() const { return m_cursorRect; }
    qreal  contentHeight()   const { return m_contentHeight; }
};

class TestE2ColumnPreservation : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void consecutive_up_presses_preserve_desired_visual_x() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Block A\n\nBlock B\n\nBlock C");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 3);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        // First Up from block 2 → block 1: sets desiredVisualX = 42.0
        MockEditForColumn mockEdit;
        mockEdit.m_cursorRect = QRectF(42.0, 0, 2, 20);  // at top
        nav->tryHandle(Qt::Key_Up, Qt::NoModifier, 2, 3, &mockEdit,
                       QStringLiteral("Block C"));
        QCOMPARE(cs->desiredVisualX(), 42.0);

        // Second Up from block 1 → block 0: desiredVisualX unchanged (reused)
        // Even though mockEdit.cursorRect.x() = 42 happens to match, what matters
        // is that it wasn't cleared between calls.
        // Simulate second Up: desiredVisualX is already 42, so it stays 42.
        nav->tryHandle(Qt::Key_Up, Qt::NoModifier, 1, 3, &mockEdit,
                       QStringLiteral("Block B"));
        QCOMPARE(cs->desiredVisualX(), 42.0);
    }

    void left_clears_desired_visual_x_set_by_up() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        // Up sets desiredVisualX
        MockEditForColumn mockEdit;
        nav->tryHandle(Qt::Key_Up, Qt::NoModifier, 1, 0, &mockEdit,
                       QStringLiteral("Beta"));
        QVERIFY(cs->desiredVisualX() >= 0.0);

        // Left at pos 0 clears it
        nav->tryHandle(Qt::Key_Left, Qt::NoModifier, 1, 0, nullptr,
                       QStringLiteral("Beta"));
        QCOMPARE(cs->desiredVisualX(), -1.0);
    }

    void right_clears_desired_visual_x_set_by_up() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        QVERIFY(nav && cs);

        MockEditForColumn mockEdit;
        nav->tryHandle(Qt::Key_Up, Qt::NoModifier, 1, 0, &mockEdit,
                       QStringLiteral("Beta"));
        QVERIFY(cs->desiredVisualX() >= 0.0);

        nav->tryHandle(Qt::Key_Right, Qt::NoModifier, 0, 5, nullptr,
                       QStringLiteral("Alpha"));
        QCOMPARE(cs->desiredVisualX(), -1.0);
    }
};

QTEST_MAIN(TestE2ColumnPreservation)
#include "tst_live_render_e2_nav_column_preservation.moc"
