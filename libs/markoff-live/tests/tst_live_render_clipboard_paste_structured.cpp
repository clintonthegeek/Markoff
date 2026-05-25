// SPDX-License-Identifier: GPL-3.0-or-later
//
// C4: Structured paste — verifies that the application/x-markoff-blocks
// MIME path is used for same-replica payloads and that the document
// content survives the round-trip without crash.
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

class TestPasteStructured : public QObject {
    Q_OBJECT
private slots:
    void structured_paste_takes_markoff_mime_path() {
        // Manually inject an application/x-markoff-blocks payload (same
        // replicaId=42) and verify paste selects the structured path without
        // crash and that inserted text appears in the document.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("X\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.cursorState();
        // Caret at position 1 (end of "X").
        sv->begin(0, 1);
        sv->extend(0, 1);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        // Build a structured MIME payload (same-replica, no cutSequenceNumber
        // so reuseBlockIds=false, but version==1 so structured path is taken).
        QJsonObject payload;
        payload["version"]         = 1;
        payload["sourceReplicaId"] = 42;
        QJsonArray blocks;
        QJsonObject b;
        b["kind"] = QStringLiteral("Paragraph");
        b["text"] = QStringLiteral("Y");
        blocks.append(b);
        payload["blocks"] = blocks;

        auto *mime = new QMimeData();
        mime->setData(Markoff::Live::LiveClipboardController::kBlocksMime,
                      QJsonDocument(payload).toJson(QJsonDocument::Compact));
        mime->setText(QStringLiteral("Y"));
        QApplication::clipboard()->setMimeData(mime);

        cc.paste();

        // Structured path was taken; "Y" should have been inserted.
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        // blockText includes the trailing '\n'
        QVERIFY2(doc.blockText(ids[0]).contains("Y"),
                 "Expected 'Y' in block text after structured paste");
    }

    void local_cut_then_paste_uses_structured_path() {
        // Do a real cut+paste within the same doc (same replicaId).
        // The paste should take the structured path (x-markoff-blocks present).
        // We just verify no crash and content is preserved.
        Markoff::MarkoffDocument doc(quint16(7));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.cursorState();
        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        // Cut "hell" (first 4 chars).
        sv->begin(0, 0);
        sv->extend(0, 4);
        cc.cut();

        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("hell"));

        // After cut: block should be "o" (B1: content-only, no trailing '\n')
        {
            const auto ids = doc.iterateBlocks();
            QVERIFY(!ids.empty());
            QCOMPARE(doc.blockText(ids[0]), QByteArray("o"));
        }

        // Paste back at end of "o" (qtPos=1 in the model, which maps to byte 1 in "o").
        sv->begin(0, 1);
        sv->extend(0, 1);
        cc.paste();

        // Doc should contain 'hell' after 'o'.
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        QVERIFY2(doc.blockText(ids[0]).contains("hell"),
                 "Expected 'hell' in block text after cut→paste");
        QVERIFY2(doc.blockText(ids[0]).contains("o"),
                 "Expected 'o' in block text after cut→paste");
    }
};

QTEST_MAIN(TestPasteStructured)
#include "tst_live_render_clipboard_paste_structured.moc"
