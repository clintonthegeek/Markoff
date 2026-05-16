// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

class TestE2NavHomeEnd : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void home_without_ctrl_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        QCOMPARE(nav->tryHandle(Qt::Key_Home, Qt::NoModifier, 0, 5, nullptr, QStringLiteral("hello")),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    void end_without_ctrl_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        QCOMPARE(nav->tryHandle(Qt::Key_End, Qt::NoModifier, 0, 5, nullptr, QStringLiteral("hello")),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    void home_at_qtpos_0_without_ctrl_stays_in_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        // Even at position 0, bare Home stays in block (Qt handles it)
        QCOMPARE(nav->tryHandle(Qt::Key_Home, Qt::NoModifier, 0, 0, nullptr, QStringLiteral("hello")),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    // ctrl_home_lands_at_first_block_qtpos_0 and ctrl_end_lands_at_last_block_end
    // moved to tst_live_render_e2_nav_home_end_qml.cpp — the focus-chokepoint
    // refactor (LiveCursorState::establishFocus) gates cursor resolution on a
    // registered delegate, which a direct unit-test setup cannot provide. The
    // navigation controller's tryHandle return value remains testable here.

    void ctrl_home_returns_handled_on_empty_doc() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // Empty doc — no rows
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        const int result = nav->tryHandle(Qt::Key_Home, Qt::ControlModifier,
                                          -1, 0, nullptr, QString());
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
    }
};

QTEST_MAIN(TestE2NavHomeEnd)
#include "tst_live_render_e2_nav_home_end.moc"
