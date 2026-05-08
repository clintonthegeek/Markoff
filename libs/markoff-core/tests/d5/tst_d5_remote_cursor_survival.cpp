// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QColor>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Cursor.h>
#include <markoff/core/UndoLog.h>

class TstD5RemoteCursorSurvival : public QObject {
    Q_OBJECT
private slots:
    void localInsertBefore_remoteCursorShifts() {
        Markoff::MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("0123456789\n"));
        const auto blocks = doc.iterateBlocks();
        QVERIFY(!blocks.empty());
        Markoff::BlockId bid = blocks.front();

        // Place remote cursor at byte offset 5
        Markoff::TextCaret tc;
        tc.block = bid;
        tc.positionAnchor = doc.textAnchorAt(bid, 5, false);
        tc.cachedByteOffset = 5;
        doc.setRemoteCursor(quint16(2), Markoff::Cursor(tc),
                            QColor("#f00"), QStringLiteral("Bob"));

        // Insert "XYZ" at offset 2 — cursor at 5 should become 8
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(bid, 2, 0, QByteArrayLiteral("XYZ"), t);
        }

        const Markoff::Cursor after = doc.remoteCursorOf(quint16(2));
        const auto *tc2 = std::get_if<Markoff::TextCaret>(&after);
        QVERIFY(tc2 != nullptr);
        QCOMPARE(tc2->cachedByteOffset, quint32(8));
    }

    void localDeleteBefore_remoteCursorShifts() {
        Markoff::MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("0123456789\n"));
        const auto blocks = doc.iterateBlocks();
        QVERIFY(!blocks.empty());
        Markoff::BlockId bid = blocks.front();

        Markoff::TextCaret tc;
        tc.block = bid;
        tc.positionAnchor = doc.textAnchorAt(bid, 7, false);
        tc.cachedByteOffset = 7;
        doc.setRemoteCursor(quint16(3), Markoff::Cursor(tc),
                            QColor("#0f0"), QStringLiteral("Carol"));

        // Delete 3 bytes at offset 2 — cursor at 7 should become 4
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(bid, 2, 3, QByteArray{}, t);
        }

        const Markoff::Cursor after = doc.remoteCursorOf(quint16(3));
        const auto *tc2 = std::get_if<Markoff::TextCaret>(&after);
        QVERIFY(tc2 != nullptr);
        QCOMPARE(tc2->cachedByteOffset, quint32(4));
    }
};
QTEST_MAIN(TstD5RemoteCursorSurvival)
#include "tst_d5_remote_cursor_survival.moc"
