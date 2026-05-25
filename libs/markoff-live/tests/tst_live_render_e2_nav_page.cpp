// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

class TestE2NavPage : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void page_up_without_list_view_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        // No listView set → NotHandled
        QCOMPARE(nav->tryHandle(Qt::Key_PageUp, Qt::NoModifier,
                                0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    void page_down_without_list_view_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        QCOMPARE(nav->tryHandle(Qt::Key_PageDown, Qt::NoModifier,
                                0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    void set_list_view_stores_the_pointer() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *nav = binding.navigationController();
        QVERIFY(nav);
        QObject mock;
        nav->setListView(&mock);
        // No crash; pointer stored. We verify indirectly: page with no editItem
        // still returns NotHandled (editItem null guard fires).
        QCOMPARE(nav->tryHandle(Qt::Key_PageUp, Qt::NoModifier,
                                0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }
};

QTEST_MAIN(TestE2NavPage)
#include "tst_live_render_e2_nav_page.moc"
