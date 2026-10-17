// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
struct CandidateDocument {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

CandidateDocument openCandidate(const QByteArray& source)
{
    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocument fixture{std::move(providerResult).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("structured-paste"), 1}, Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(source).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}
}

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
        auto fixture = openCandidate(QByteArrayLiteral("Body.\n"));
        auto& doc = *fixture.document;

        QJsonArray blocks;
        {
            QJsonObject b;
            b["kind"] = "paragraph";
            b["text"] = "Pasted para.";
            blocks.append(b);
        }
        {
            // Heading buffers store the raw source range (with `## ` prefix)
            // — LiveClipboardController copies `doc->blockText(id)` directly,
            // so the clipboard payload for a heading already has `## ` in
            // `text`. reconstructFlatMarkdown emits it verbatim and the
            // receiving side's kind-transition re-detects Heading from the
            // prefix.
            QJsonObject b;
            b["kind"] = "heading";
            b["text"] = "## Pasted Heading";
            QJsonObject attrs;
            attrs["level"] = 2;
            b["attrs"] = attrs;
            blocks.append(b);
        }

        // Replace the full block content. Post-B1, the block buffer for
        // "Body.\n" is content-only — "Body." (5 bytes) — so flat range = [0, 5).
        QCOMPARE(doc.applyStructuredPaste(0, 5, blocks).state,
                 Markoff::VNext::ActionPublicationState::committed);

        const QByteArray flat = flatText(doc);
        QVERIFY2(flat.contains("Pasted para."), flat.constData());
        QVERIFY2(flat.contains("## Pasted Heading"), flat.constData());
        QVERIFY2(!flat.contains("Body."), flat.constData());
        // The paste produced 2 JSON blocks → 2 blocks in the document.
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
    }

    void single_block_paste_replaces_content() {
        // Replace entire block content. Post-B1, "X\n" loads to buffer "X"
        // (1 byte, flat [0,1)). "Y" has no embedded newlines → intra-block
        // edit, existing block kept.
        auto fixture = openCandidate(QByteArrayLiteral("X\n"));
        auto& doc = *fixture.document;

        QJsonArray blocks;
        QJsonObject b;
        b["kind"] = "paragraph";
        b["text"] = "Y";
        blocks.append(b);

        QCOMPARE(doc.applyStructuredPaste(0, 1, blocks).state,
                 Markoff::VNext::ActionPublicationState::committed);

        const QByteArray flat = flatText(doc);
        QVERIFY2(flat.contains("Y"), flat.constData());
        QVERIFY2(!flat.contains("X"), flat.constData());
        // Still one block
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
    }

    void insertsIntoInitializedEmptyDocumentAsOneAcceptedAction()
    {
        auto fixture = openCandidate({});
        auto& doc = *fixture.document;
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
        QVERIFY(doc.blockText(doc.iterateBlocks().front()).isEmpty());
        QSignalSpy actionSpy(&doc, &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QJsonObject first;
        first["kind"] = "paragraph";
        first["text"] = "first";
        QJsonObject second;
        second["kind"] = "heading";
        second["text"] = "## second";
        QJsonArray blocks{first, second};

        const auto paste = doc.applyStructuredPaste(0, 0, blocks);
        const std::string pasteError = paste.diagnostic
            ? std::string(paste.diagnostic->message()) : "paste did not commit";
        QVERIFY2(paste.state == Markoff::VNext::ActionPublicationState::committed,
                 pasteError.c_str());
        QCOMPARE(actionSpy.size(), 1);
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
        QCOMPARE(flatText(doc), QByteArrayLiteral("first## second"));

        doc.undoD2();
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
        QVERIFY(doc.blockText(doc.iterateBlocks().front()).isEmpty());
        doc.redoD2();
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
        QCOMPARE(flatText(doc), QByteArrayLiteral("first## second"));
    }

    void replacesCrossBlockRangeInOneAcceptedAction()
    {
        auto fixture = openCandidate(QByteArrayLiteral("hello\n\nworld"));
        auto& doc = *fixture.document;
        QSignalSpy actionSpy(&doc, &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QJsonObject block;
        block["kind"] = "paragraph";
        block["text"] = "X";
        QJsonArray blocks{block};

        QCOMPARE(doc.applyStructuredPaste(1, 8, blocks).state,
                 Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(actionSpy.size(), 1);
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
        QCOMPARE(doc.blockText(doc.iterateBlocks()[0]), QByteArrayLiteral("h"));
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral("Xld"));

        doc.undoD2();
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
        QCOMPARE(doc.blockText(doc.iterateBlocks()[0]), QByteArrayLiteral("hello"));
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral("world"));
        doc.redoD2();
        QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
        QCOMPARE(doc.blockText(doc.iterateBlocks()[0]), QByteArrayLiteral("h"));
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral("Xld"));
    }

};

QTEST_MAIN(TestApplyStructuredPaste)
#include "tst_markoff_doc_apply_structured_paste.moc"
