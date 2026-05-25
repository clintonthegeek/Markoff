// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

class TestE2NavWord : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void ctrl_left_inside_block_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        // qtPos > 0: let TextEdit handle it natively
        QCOMPARE(nav->tryHandle(Qt::Key_Left, Qt::ControlModifier,
                                0, 5, nullptr, QStringLiteral("hello world")),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    // ctrl_left_at_block_start_crosses_to_prev_block_end moved to
    // tst_live_render_e2_nav_word_qml.cpp (chokepoint).

    void ctrl_left_at_first_block_returns_handled_at_boundary() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        QVERIFY(nav);
        QCOMPARE(nav->tryHandle(Qt::Key_Left, Qt::ControlModifier,
                                0, 0, nullptr, QStringLiteral("OnlyBlock")),
                 static_cast<int>(LiveNavigationController::Handled));
    }

    void ctrl_right_inside_block_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        // qtPos < blockText.length(): let TextEdit handle it natively
        QCOMPARE(nav->tryHandle(Qt::Key_Right, Qt::ControlModifier,
                                0, 3, nullptr, QStringLiteral("hello world")),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    // ctrl_right_at_block_end_crosses_to_next_block_start moved to
    // tst_live_render_e2_nav_word_qml.cpp (chokepoint).

    void ctrl_right_at_last_block_end_returns_handled_at_boundary() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        QVERIFY(nav);
        QCOMPARE(nav->tryHandle(Qt::Key_Right, Qt::ControlModifier,
                                0, 9, nullptr, QStringLiteral("OnlyBlock")),
                 static_cast<int>(LiveNavigationController::Handled));
    }
};

QTEST_MAIN(TestE2NavWord)
#include "tst_live_render_e2_nav_word.moc"
