// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QHash>

#include <markoff/CursorPos.h>
#include <markoff/TextSpan.h>
#include <markoff/FoldSpec.h>

using namespace Markoff;

class TstValueTypes : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cursorPosEquality() {
        const CursorPos a{3, 7};
        const CursorPos b{3, 7};
        const CursorPos c{3, 8};
        QCOMPARE(a, b);
        QVERIFY(!(a == c));
    }

    void textSpanOrdering() {
        const TextSpan a{0, 5};
        const TextSpan b{0, 10};
        QVERIFY(a.length == 5);
        QVERIFY(b.contains(a.offset));
        QVERIFY(!a.contains(8));
    }

    void foldSpecHashesByLine() {
        QSet<FoldSpec> s;
        s.insert({2, 3});
        s.insert({2, 3});
        s.insert({2, 5});  // different level: distinct entry
        QCOMPARE(s.size(), 2);
    }

    void textSpanQSetRoundTrips() {
        QSet<TextSpan> s;
        s.insert({0, 5});
        s.insert({0, 5});
        s.insert({6, 3});
        QCOMPARE(s.size(), 2);
    }
};

QTEST_MAIN(TstValueTypes)
#include "tst_value_types.moc"
