// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/CodeSpan.h>
#include <markoff/core/CodeTokenKind.h>

using namespace Markoff;

class TstFoundationCodeToken : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void code_span_default_kind_is_default() {
        CodeSpan s;
        QCOMPARE(s.kind, CodeTokenKind::Default);
        QCOMPARE(s.offset, quint32(0));
    }

    void code_span_carries_range_and_kind() {
        CodeSpan s;
        s.offset = 5; s.length = 7; s.kind = CodeTokenKind::Keyword;
        QCOMPARE(s.offset, quint32(5));
        QCOMPARE(s.length, quint32(7));
        QCOMPARE(s.kind, CodeTokenKind::Keyword);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCodeToken)
#include "tst_foundation_code_token.moc"
