// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QJsonObject>
#include <QJsonDocument>

#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstMarkoffEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void insertion_classifies_correctly() {
        MarkoffEdit e;
        e.oldStart = 5;
        e.oldEnd = 5;
        e.newText = "x";
        QVERIFY(e.isInsertion());
        QVERIFY(!e.isDeletion());
        QVERIFY(!e.isReplacement());
    }

    void deletion_classifies_correctly() {
        MarkoffEdit e;
        e.oldStart = 3;
        e.oldEnd = 7;
        e.newText.clear();
        QVERIFY(!e.isInsertion());
        QVERIFY(e.isDeletion());
        QVERIFY(!e.isReplacement());
    }

    void replacement_classifies_correctly() {
        MarkoffEdit e;
        e.oldStart = 3;
        e.oldEnd = 7;
        e.newText = "abc";
        QVERIFY(!e.isInsertion());
        QVERIFY(!e.isDeletion());
        QVERIFY(e.isReplacement());
    }

    void json_roundtrip_insertion() {
        MarkoffEdit a;
        a.oldStart = 12;
        a.oldEnd = 12;
        a.newText = QByteArray("héllo", 6);  // UTF-8: é is 2 bytes
        const QJsonObject json = a.toJson();
        const MarkoffEdit b = MarkoffEdit::fromJson(json);
        QCOMPARE(b.oldStart, a.oldStart);
        QCOMPARE(b.oldEnd, a.oldEnd);
        QCOMPARE(b.newText, a.newText);
    }

    void json_roundtrip_replacement() {
        MarkoffEdit a;
        a.oldStart = 0;
        a.oldEnd = 11;
        a.newText = "goodbye";
        const QJsonObject json = a.toJson();
        const MarkoffEdit b = MarkoffEdit::fromJson(json);
        QCOMPARE(b.oldStart, a.oldStart);
        QCOMPARE(b.oldEnd, a.oldEnd);
        QCOMPARE(b.newText, a.newText);
    }

    void json_roundtrip_empty_deletion() {
        MarkoffEdit a;
        a.oldStart = 4;
        a.oldEnd = 9;
        a.newText.clear();
        const QJsonObject json = a.toJson();
        const MarkoffEdit b = MarkoffEdit::fromJson(json);
        QCOMPARE(b.oldStart, a.oldStart);
        QCOMPARE(b.oldEnd, a.oldEnd);
        QCOMPARE(b.newText, a.newText);
    }
};

QTEST_MAIN(TstMarkoffEdit)
#include "tst_markoff_edit.moc"
