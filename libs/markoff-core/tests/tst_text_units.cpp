// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/TextUnits.h>

using namespace Markoff::TextUnits;

class TstTextUnits : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void checkedUnicodeBoundaries()
    {
        const QByteArray text("A\xF0\x9F\x98\x80\xC3\xA9", 7);
        QCOMPARE(checkedByteToQtPos(text, 0), std::optional<qsizetype>(0));
        QCOMPARE(checkedByteToQtPos(text, 1), std::optional<qsizetype>(1));
        QCOMPARE(checkedByteToQtPos(text, 5), std::optional<qsizetype>(3));
        QCOMPARE(checkedByteToQtPos(text, 7), std::optional<qsizetype>(4));
        QCOMPARE(checkedQtPosToByte(text, 0), std::optional<qsizetype>(0));
        QCOMPARE(checkedQtPosToByte(text, 1), std::optional<qsizetype>(1));
        QCOMPARE(checkedQtPosToByte(text, 3), std::optional<qsizetype>(5));
        QCOMPARE(checkedQtPosToByte(text, 4), std::optional<qsizetype>(7));
    }

    void rejectsSplitScalarsAndOutOfRangePositions()
    {
        const QByteArray text("A\xF0\x9F\x98\x80\xC3\xA9", 7);
        QVERIFY(!checkedByteToQtPos(text, -1));
        QVERIFY(!checkedByteToQtPos(text, 2)); // inside the four-byte scalar
        QVERIFY(!checkedByteToQtPos(text, 8));
        QVERIFY(!checkedQtPosToByte(text, -1));
        QVERIFY(!checkedQtPosToByte(text, 2)); // between UTF-16 surrogates
        QVERIFY(!checkedQtPosToByte(text, 5));
    }

    void rejectsMalformedUtf8Everywhere()
    {
        const QList<QByteArray> malformed{
            QByteArray::fromHex("c0af"),       // overlong
            QByteArray::fromHex("e282"),       // truncated
            QByteArray::fromHex("eda080"),     // surrogate scalar
            QByteArray::fromHex("f4908080"),   // above U+10FFFF
            QByteArray::fromHex("80"),         // stray continuation
            QByteArray::fromHex("41ff")        // valid prefix, invalid suffix
        };
        for (const QByteArray& text : malformed) {
            QVERIFY(!checkedByteToQtPos(text, 0));
            QVERIFY(!checkedQtPosToByte(text, 0));
        }
    }

    void emptyTextHasOneValidBoundary()
    {
        const QByteArray empty;
        QCOMPARE(checkedByteToQtPos(empty, 0), std::optional<qsizetype>(0));
        QCOMPARE(checkedQtPosToByte(empty, 0), std::optional<qsizetype>(0));
        QVERIFY(!checkedByteToQtPos(empty, 1));
        QVERIFY(!checkedQtPosToByte(empty, 1));
    }
};

QTEST_MAIN(TstTextUnits)
#include "tst_text_units.moc"
