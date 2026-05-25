// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/CursorPos.h>

class TestCursorPos : public QObject {
    Q_OBJECT
private slots:
    void defaults_are_one_one() {
        Markoff::CursorPos p;
        QCOMPARE(p.line, 1);
        QCOMPARE(p.column, 1);
    }
    void aggregate_init_works() {
        Markoff::CursorPos p{42, 7};
        QCOMPARE(p.line, 42);
        QCOMPARE(p.column, 7);
    }
};
QTEST_GUILESS_MAIN(TestCursorPos)
#include "tst_v10_cursor_pos.moc"
