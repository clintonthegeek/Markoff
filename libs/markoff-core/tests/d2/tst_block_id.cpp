// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/BlockId.h>

class TstBlockId : public QObject {
    Q_OBJECT
private slots:
    void defaultConstructed_isNull();
    void distinctRawIds_compareUnequal();
    void sameRawId_comparesEqual();
    void hashable_canKeyQHash();
};

void TstBlockId::defaultConstructed_isNull() {
    Markoff::BlockId id;
    QVERIFY(id.isNull());
}
void TstBlockId::distinctRawIds_compareUnequal() {
    QVERIFY(Markoff::BlockId::fromRaw(1) != Markoff::BlockId::fromRaw(2));
}
void TstBlockId::sameRawId_comparesEqual() {
    QVERIFY(Markoff::BlockId::fromRaw(42) == Markoff::BlockId::fromRaw(42));
}
void TstBlockId::hashable_canKeyQHash() {
    QHash<Markoff::BlockId, QString> map;
    map.insert(Markoff::BlockId::fromRaw(1), QStringLiteral("one"));
    QCOMPARE(map.value(Markoff::BlockId::fromRaw(1)), QStringLiteral("one"));
}

QTEST_GUILESS_MAIN(TstBlockId)
#include "tst_block_id.moc"
