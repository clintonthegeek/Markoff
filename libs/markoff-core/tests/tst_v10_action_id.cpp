// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/ActionId.h>

class TestActionId : public QObject {
    Q_OBJECT
private slots:
    void enum_values_are_distinct() {
        QVERIFY(static_cast<int>(Markoff::ActionId::Bold) !=
                static_cast<int>(Markoff::ActionId::Italic));
        QVERIFY(static_cast<int>(Markoff::ActionId::HeadingLevel0) !=
                static_cast<int>(Markoff::ActionId::HeadingLevel6));
    }
};
QTEST_GUILESS_MAIN(TestActionId)
#include "tst_v10_action_id.moc"
