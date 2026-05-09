// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/PasteMeta.h>

// Helper: concatenation of blockText() for all blocks (the D2 flat view).
static QByteArray flatText(Markoff::MarkoffDocument &doc)
{
    QByteArray out;
    for (Markoff::BlockId id : doc.iterateBlocks())
        out += doc.blockText(id);
    return out;
}

class TestApplyStructuredPaste : public QObject {
    Q_OBJECT
private slots:
    void inserts_blocks_with_kinds_preserved() {
        // Single-block document "Body.\n" (6 bytes flat [0,6)).
        // Replace [0,6) entirely with a Paragraph + Heading (2-block paste).
        // applyFlatEdit on a single block with embedded "\n\n" in newText performs
        // an intra-block split → the tail ("") is appended to the last part.
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("Body.\n");

        QJsonArray blocks;
        {
            QJsonObject b;
            b["kind"] = "Paragraph";
            b["text"] = "Pasted para.";
            blocks.append(b);
        }
        {
            QJsonObject b;
            b["kind"] = "Heading";
            b["text"] = "Pasted Heading";
            QJsonObject attrs;
            attrs["level"] = 2;
            b["attrs"] = attrs;
            blocks.append(b);
        }

        Markoff::PasteMeta meta;  // reuseBlockIds = false
        // Replace the full block content. Block 0 flat range = [0, 6).
        doc.applyStructuredPaste(0, 6, blocks, meta);

        const QByteArray flat = flatText(doc);
        QVERIFY2(flat.contains("Pasted para."), flat.constData());
        QVERIFY2(flat.contains("## Pasted Heading"), flat.constData());
        QVERIFY2(!flat.contains("Body."), flat.constData());
        // The paste produced 2 JSON blocks → 2 blocks in the document.
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
    }

    void single_block_paste_replaces_content() {
        // Replace entire block content "X\n" (2 bytes, flat [0,2)) with "Y".
        // "Y" has no embedded newlines → intra-block edit, existing block kept.
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("X\n");

        QJsonArray blocks;
        QJsonObject b;
        b["kind"] = "Paragraph";
        b["text"] = "Y";
        blocks.append(b);

        Markoff::PasteMeta meta;  // reuseBlockIds = false (default)
        doc.applyStructuredPaste(0, 2, blocks, meta);

        const QByteArray flat = flatText(doc);
        QVERIFY2(flat.contains("Y"), flat.constData());
        QVERIFY2(!flat.contains("X"), flat.constData());
        // Still one block
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
    }

    void reuse_falls_back_when_cache_miss() {
        // Verify no crash and document is valid when cutSeq is not in cache.
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("X\n");

        QJsonArray blocks;
        QJsonObject b;
        b["kind"] = "Paragraph";
        b["text"] = "Y";
        blocks.append(b);

        Markoff::PasteMeta meta;
        meta.reuseBlockIds = true;
        meta.cutSeq        = 99999;  // not in cache
        doc.applyStructuredPaste(0, 2, blocks, meta);

        // No crash; document is in a valid state with expected content.
        const QByteArray flat = flatText(doc);
        QVERIFY2(flat.contains("Y"), flat.constData());
    }
};

QTEST_MAIN(TestApplyStructuredPaste)
#include "tst_markoff_doc_apply_structured_paste.moc"
