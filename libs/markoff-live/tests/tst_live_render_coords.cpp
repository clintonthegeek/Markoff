// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QByteArray>

#include <markoff/live/Coordinates.h>

using namespace Markoff::Live::Detail::Coordinates;

class TstLiveRenderCoords : public QObject {
    Q_OBJECT
private Q_SLOTS:

    // ---- byteToQtPos ----

    void byteToQtPos_empty() {
        QCOMPARE(byteToQtPos(QByteArray(), 0), qsizetype(0));
    }

    void byteToQtPos_ascii_identity() {
        const QByteArray buf = QByteArray("hello");
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 3), qsizetype(3));
        QCOMPARE(byteToQtPos(buf, 5), qsizetype(5));
    }

    void byteToQtPos_two_byte_sequence() {
        // U+00E9 LATIN SMALL LETTER E WITH ACUTE: UTF-8 = 0xC3 0xA9 (2 bytes),
        // UTF-16 = 1 QChar.
        const QByteArray buf = QByteArray("\xC3\xA9");
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 2), qsizetype(1));
    }

    void byteToQtPos_three_byte_sequence() {
        // U+20AC EURO SIGN: UTF-8 = 0xE2 0x82 0xAC (3 bytes), UTF-16 = 1 QChar.
        const QByteArray buf = QByteArray("\xE2\x82\xAC");
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 3), qsizetype(1));
    }

    void byteToQtPos_four_byte_sequence() {
        // U+1F600 GRINNING FACE: UTF-8 = 0xF0 0x9F 0x98 0x80 (4 bytes),
        // UTF-16 = 2 QChars (surrogate pair).
        const QByteArray buf = QByteArray("\xF0\x9F\x98\x80");
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 4), qsizetype(2));
    }

    void byteToQtPos_mixed() {
        // "a€b": a(1)+€(3)+b(1) = 5 bytes; a(1)+€(1)+b(1) = 3 QChars.
        // Note: "\xACb" would be interpreted as one hex escape (0xACb) since
        // 'b' is a hex digit. Split the literal to terminate the escape cleanly.
        const QByteArray buf = QByteArray("a\xE2\x82\xAC" "b");
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 1), qsizetype(1));
        QCOMPARE(byteToQtPos(buf, 4), qsizetype(2));
        QCOMPARE(byteToQtPos(buf, 5), qsizetype(3));
    }

    void byteToQtPos_clamp_past_end() {
        const QByteArray buf = QByteArray("hi");
        QCOMPARE(byteToQtPos(buf, 100), qsizetype(2));
    }

    // ---- qtPosToByte ----

    void qtPosToByte_empty() {
        QCOMPARE(qtPosToByte(QByteArray(), 0), qsizetype(0));
    }

    void qtPosToByte_ascii_identity() {
        const QByteArray buf = QByteArray("hello");
        QCOMPARE(qtPosToByte(buf, 0), qsizetype(0));
        QCOMPARE(qtPosToByte(buf, 3), qsizetype(3));
        QCOMPARE(qtPosToByte(buf, 5), qsizetype(5));
    }

    void qtPosToByte_two_byte_sequence() {
        const QByteArray buf = QByteArray("\xC3\xA9");
        QCOMPARE(qtPosToByte(buf, 0), qsizetype(0));
        QCOMPARE(qtPosToByte(buf, 1), qsizetype(2));
    }

    void qtPosToByte_four_byte_sequence() {
        const QByteArray buf = QByteArray("\xF0\x9F\x98\x80");
        QCOMPARE(qtPosToByte(buf, 0), qsizetype(0));
        QCOMPARE(qtPosToByte(buf, 2), qsizetype(4));
    }

    // ---- round-trip ----

    void roundtrip_byte_to_qtpos_and_back() {
        // "café 😀 bar" — mix of ASCII, 2-byte, and 4-byte UTF-8 sequences.
        const QString str = QStringLiteral("café \U0001F600 bar");
        const QByteArray utf8 = str.toUtf8();
        // Invariant: byteToQtPos(qtPosToByte(byteToQtPos(x))) == byteToQtPos(x).
        // qtPosToByte(qtPos) returns the code-point boundary at or after `qtPos`
        // QChars, so byteToQtPos of that boundary must equal qtPos. This holds
        // even when byteOff falls inside a multi-byte sequence.
        for (qsizetype byteOff = 0; byteOff <= utf8.size(); ++byteOff) {
            const qsizetype qtPos  = byteToQtPos(utf8, byteOff);
            const qsizetype back   = qtPosToByte(utf8, qtPos);
            const qsizetype verify = byteToQtPos(utf8, back);
            QVERIFY2(verify == qtPos,
                     qPrintable(QString("byteOff=%1 qtPos=%2 back=%3 verify=%4")
                                    .arg(byteOff).arg(qtPos).arg(back).arg(verify)));
        }
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderCoords)
#include "tst_live_render_coords.moc"
