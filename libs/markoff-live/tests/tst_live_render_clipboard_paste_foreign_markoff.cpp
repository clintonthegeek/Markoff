// SPDX-License-Identifier: GPL-3.0-or-later
//
// Structured Markoff MIME paste imports block content without carrying source
// document identity.
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
    void structured_payload_uses_only_version_and_blocks() {
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
        payload["version"] = 1;
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

};

QTEST_MAIN(TestPasteForeignMarkoff)
#include "tst_live_render_clipboard_paste_foreign_markoff.moc"
