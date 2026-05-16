// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "KindDispatch.h"

class TestKindDispatch : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void text_inline_kinds_share_a_class() {
        QCOMPARE(Markoff::Live::delegateClassFor("paragraph"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor("heading"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor("blockquote"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor("list-item"),
                 QStringLiteral("text-inline"));
    }

    void other_kinds_are_distinct() {
        QCOMPARE(Markoff::Live::delegateClassFor("code-block"),
                 QStringLiteral("code-block"));
        QCOMPARE(Markoff::Live::delegateClassFor("math"),
                 QStringLiteral("math"));
        QCOMPARE(Markoff::Live::delegateClassFor("hr"),
                 QStringLiteral("hr"));
        QCOMPARE(Markoff::Live::delegateClassFor("image"),
                 QStringLiteral("image"));
    }

    void unknown_kind_falls_back_to_text_inline() {
        QCOMPARE(Markoff::Live::delegateClassFor("plugin-future"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor(""),
                 QStringLiteral("text-inline"));
    }
};

QTEST_MAIN(TestKindDispatch)
#include "tst_kind_dispatch.moc"
