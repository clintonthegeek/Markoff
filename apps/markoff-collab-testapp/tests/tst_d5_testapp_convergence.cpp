// SPDX-License-Identifier: GPL-3.0-or-later
//
// Convergence tests through InMemoryTransport + CollabConsumer.
//
// NOTE: Two documents built via independent loadFromMarkdown() calls have
// divergent CRDT histories (each uses its own replica_id for seed insertions),
// so they cannot converge via op exchange alone. These tests build initial
// document state by routing A's D2 ops to B *before* any concurrent edits,
// matching the correct collab model used by tst_d5_two_doc_convergence.
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include "InMemoryTransport.h"
#include "CollabConsumer.h"

class TstD5TestappConvergence : public QObject {
    Q_OBJECT
private slots:
    void routedConvergence_twoReplicas() {
        // A authors initial content via collab ops; B receives via transport.
        Markoff::MarkoffDocument a(quint16(1)), b(quint16(2));

        InMemoryTransport ta("A"), tb("B");
        ta.connectPeer(&tb);
        tb.connectPeer(&ta);

        CollabConsumer ca(&a, &ta);
        CollabConsumer cb(&b, &tb);

        // A creates "Hello\n" block — op routes to B via push→deliverFromPeer.
        Markoff::BlockId blk;
        {
            Markoff::UndoLog::Transaction t(a.d2UndoLog());
            blk = a.d2InsertBlock(Markoff::BlockId{}, Markoff::BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk, 0, 0, QByteArrayLiteral("Hello\n"), t);
        }

        // B should now have the same block.
        QVERIFY(!b.iterateBlocks().empty());
        QCOMPARE(b.blockText(b.iterateBlocks().front()), QByteArray("Hello\n"));

        // A appends '!' — op routes to B synchronously.
        {
            Markoff::UndoLog::Transaction t(a.d2UndoLog());
            a.d2ApplyBufferEdit(blk, 5, 0, QByteArrayLiteral("!"), t);
        }

        const QByteArray bText = b.blockText(b.iterateBlocks().front());
        QVERIFY2(bText.endsWith(QByteArray("!\n")),
                 qPrintable("b text was: " + QString::fromUtf8(bText)));
    }

    void bidirectional_bothEditsConverge() {
        // A and B start with the same CRDT state (A creates, B receives).
        Markoff::MarkoffDocument a(quint16(1)), b(quint16(2));

        InMemoryTransport ta("A"), tb("B");
        ta.connectPeer(&tb);
        tb.connectPeer(&ta);
        CollabConsumer ca(&a, &ta);
        CollabConsumer cb(&b, &tb);

        // A creates "AB\n" block; B receives via transport.
        Markoff::BlockId blk;
        {
            Markoff::UndoLog::Transaction t(a.d2UndoLog());
            blk = a.d2InsertBlock(Markoff::BlockId{}, Markoff::BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk, 0, 0, QByteArrayLiteral("AB\n"), t);
        }
        QVERIFY(!b.iterateBlocks().empty());
        QCOMPARE(b.blockText(b.iterateBlocks().front()), QByteArray("AB\n"));

        // a inserts 'X' at offset 0; b inserts 'Y' at offset 2.
        // Because transport is synchronous, each edit reaches the peer immediately.
        {
            Markoff::UndoLog::Transaction t(a.d2UndoLog());
            a.d2ApplyBufferEdit(a.iterateBlocks().front(), 0, 0,
                                QByteArrayLiteral("X"), t);
        }
        {
            Markoff::UndoLog::Transaction t(b.d2UndoLog());
            b.d2ApplyBufferEdit(b.iterateBlocks().front(), 2, 0,
                                QByteArrayLiteral("Y"), t);
        }

        // Both should converge to identical text containing X and Y.
        const QByteArray aText = a.blockText(a.iterateBlocks().front());
        const QByteArray bText = b.blockText(b.iterateBlocks().front());
        QCOMPARE(aText, bText);
        QVERIFY(aText.contains('X'));
        QVERIFY(aText.contains('Y'));
    }
};
QTEST_MAIN(TstD5TestappConvergence)
#include "tst_d5_testapp_convergence.moc"
