// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QColor>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Cursor.h>

class TstD5RemoteCursorState : public QObject {
    Q_OBJECT
private slots:
    void setRemoteCursor_storesAndEmits() {
        Markoff::MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("text\n"));
        QSignalSpy spy(&doc, &Markoff::MarkoffDocument::remoteCursorChanged);

        Markoff::Cursor c = Markoff::NoCursor{};
        doc.setRemoteCursor(quint16(2), c, QColor("#ff0000"), QStringLiteral("Bob"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).value<quint16>(), quint16(2));
    }
    void clearRemoteCursor_emitsClear() {
        Markoff::MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("text\n"));
        doc.setRemoteCursor(quint16(3), Markoff::NoCursor{}, QColor("#00ff00"), QStringLiteral("Carol"));
        QSignalSpy spy(&doc, &Markoff::MarkoffDocument::remoteCursorCleared);
        doc.clearRemoteCursor(quint16(3));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).value<quint16>(), quint16(3));
    }
    void clearAllRemoteCursors_emitsForEach() {
        Markoff::MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("text\n"));
        doc.setRemoteCursor(2, Markoff::NoCursor{}, QColor("#0000ff"), "X");
        doc.setRemoteCursor(3, Markoff::NoCursor{}, QColor("#00ff00"), "Y");
        QSignalSpy spy(&doc, &Markoff::MarkoffDocument::remoteCursorCleared);
        doc.clearAllRemoteCursors();
        QCOMPARE(spy.count(), 2);
    }
};
QTEST_MAIN(TstD5RemoteCursorState)
#include "tst_d5_remote_cursor_state.moc"
