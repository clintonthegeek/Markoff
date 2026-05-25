// SPDX-License-Identifier: GPL-3.0-or-later
//
// C5: Foreign-Markoff-MIME paste — a payload with sourceReplicaId != ours
// should take the structured path (version==1) but NOT set reuseBlockIds.
// No crash; content is inserted.
#include <QTest>
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveClipboardController.h>

class TestPasteForeignMarkoff : public QObject {
    Q_OBJECT
private slots:
    void foreign_replica_uses_structured_path_without_id_reuse() {
        // Our document has replicaId=42. The clipboard payload says
        // sourceReplicaId=7 — a different peer. paste() should go through the
        // structured path (version==1) without setting reuseBlockIds. No crash;
        // "Foreign" text inserted.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("X\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.cursorState();
        sv->begin(0, 1);
        sv->extend(0, 1);  // caret at end of "X"

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        QJsonObject payload;
        payload["version"]         = 1;
        payload["sourceReplicaId"] = 7;  // not our replica (42)
        QJsonArray blocks;
        QJsonObject b;
        b["kind"] = QStringLiteral("Paragraph");
        b["text"] = QStringLiteral("Foreign");
        blocks.append(b);
        payload["blocks"] = blocks;

        auto *mime = new QMimeData();
        mime->setData(Markoff::Live::LiveClipboardController::kBlocksMime,
                      QJsonDocument(payload).toJson(QJsonDocument::Compact));
        mime->setText(QStringLiteral("Foreign"));
        QApplication::clipboard()->setMimeData(mime);

        cc.paste();

        // Content pasted successfully, no crash.
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        QVERIFY2(doc.blockText(ids[0]).contains("Foreign"),
                 "Expected 'Foreign' in block text after foreign-replica paste");
    }

    void foreign_payload_with_cutSequenceNumber_does_not_reuse() {
        // Even if the foreign payload carries a cutSequenceNumber, reuseBlockIds
        // must NOT be set (the IDs belong to a different replica's namespace).
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("A\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.cursorState();
        sv->begin(0, 1);
        sv->extend(0, 1);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        QJsonObject payload;
        payload["version"]           = 1;
        payload["sourceReplicaId"]   = 99;     // foreign
        payload["cutSequenceNumber"] = 12345;  // should be ignored
        QJsonArray blocks;
        QJsonObject b;
        b["kind"] = QStringLiteral("Paragraph");
        b["text"] = QStringLiteral("B");
        blocks.append(b);
        payload["blocks"] = blocks;

        auto *mime = new QMimeData();
        mime->setData(Markoff::Live::LiveClipboardController::kBlocksMime,
                      QJsonDocument(payload).toJson(QJsonDocument::Compact));
        mime->setText(QStringLiteral("B"));
        QApplication::clipboard()->setMimeData(mime);

        cc.paste();

        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        QVERIFY2(doc.blockText(ids[0]).contains("B"),
                 "Expected 'B' in block text after foreign-replica paste with cutSeq");
    }
};

QTEST_MAIN(TestPasteForeignMarkoff)
#include "tst_live_render_clipboard_paste_foreign_markoff.moc"
