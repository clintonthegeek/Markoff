// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SearchEngine.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

using namespace Markoff;

namespace {
struct CandidateDocument {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};

CandidateDocument openCandidate(const QString &key)
{
    auto providerResult = VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok())
        throw std::runtime_error("could not create ephemeral provider");
    CandidateDocument result{std::move(providerResult).value(), {}};
    auto documentResult = MarkoffDocument::open(*result.provider,
        {key, 1}, VNext::StorageMode::ephemeral);
    if (!documentResult.ok())
        throw std::runtime_error("could not open provider-backed document");
    result.document = std::move(documentResult).value();
    return result;
}
}

class TstReplaceMatches : public QObject {
    Q_OBJECT
    static QByteArray afterReplace(const char *md, const QString &needle,
                                   const QString &repl) {
        static int nextDocument = 0;
        auto fixture = openCandidate(QStringLiteral("replace-%1").arg(nextDocument++));
        auto &doc = *fixture.document;
        if (doc.importInitialMarkdown(QByteArray(md)).state
            != VNext::ActionPublicationState::committed)
            throw std::runtime_error("could not import replace fixture");
        if (doc.replaceMatches(SearchEngine::findByBlock(doc, needle), repl).state
            != VNext::ActionPublicationState::committed)
            throw std::runtime_error("candidate replace action was not committed");
        return doc.serializeForSave();
    }
private slots:
    void single_match() {
        QCOMPARE(afterReplace("Find me here\n", "me", "you"),
                 QByteArray("Find you here\n"));
    }
    void multiple_matches_one_block() {
        QCOMPARE(afterReplace("foo foo foo\n", "foo", "bar"),
                 QByteArray("bar bar bar\n"));
    }
    void matches_across_blocks() {
        QCOMPARE(afterReplace("alpha\n\nbeta alpha\n", "alpha", "X"),
                 QByteArray("X\n\nbeta X\n"));
    }
    void length_changing_replacement_keeps_offsets() {
        QCOMPARE(afterReplace("ab ab\n", "ab", "abcd"),
                 QByteArray("abcd abcd\n"));
    }
    void replace_all_is_one_undo() {
        auto fixture = openCandidate(QStringLiteral("replace-undo"));
        auto &doc = *fixture.document;
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("foo foo foo\n")).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(doc.replaceMatches(SearchEngine::findByBlock(doc, "foo"), "bar").state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(doc.serializeForSave(), QByteArray("bar bar bar\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(), QByteArray("foo foo foo\n"));
    }
    void cross_block_replace_is_one_durable_undo_redo_action() {
        auto fixture = openCandidate(QStringLiteral("replace-cross-block"));
        const QString key = QStringLiteral("replace-cross-block");
        const QByteArray original = QByteArrayLiteral("alpha\n\nbeta alpha\n");
        QCOMPARE(fixture.document->importInitialMarkdown(original).state,
                 VNext::ActionPublicationState::committed);
        const auto hits = SearchEngine::findByBlock(*fixture.document,
            QStringLiteral("alpha"));
        QCOMPARE(hits.size(), 2);
        QCOMPARE(fixture.document->replaceMatches(hits, QStringLiteral("X")).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->serializeForSave(),
                 QByteArrayLiteral("X\n\nbeta X\n"));

        fixture.document.reset();
        auto reopened = MarkoffDocument::open(*fixture.provider,
            {key, 1}, VNext::StorageMode::ephemeral);
        QVERIFY(reopened.ok());
        fixture.document = std::move(reopened).value();
        fixture.document->undoD2();
        QCOMPARE(fixture.document->serializeForSave(), original);
        fixture.document->redoD2();
        QCOMPARE(fixture.document->serializeForSave(),
                 QByteArrayLiteral("X\n\nbeta X\n"));
    }

    void explicit_cross_block_edits_are_one_durable_undo_redo_action() {
        auto fixture = openCandidate(QStringLiteral("block-edits-batch"));
        const QString key = QStringLiteral("block-edits-batch");
        const QByteArray original = QByteArrayLiteral("alpha\n\nbeta\n");
        QCOMPARE(fixture.document->importInitialMarkdown(original).state,
                 VNext::ActionPublicationState::committed);
        const auto blocks = fixture.document->iterateBlocks();
        const QList<BlockEdit> edits{
            {blocks[0], 0, 5, QByteArrayLiteral("first")},
            {blocks[1], 0, 4, QByteArrayLiteral("second")}};
        QCOMPARE(fixture.document->applyBlockEdits(edits).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->serializeForSave(),
                 QByteArrayLiteral("first\n\nsecond\n"));

        fixture.document.reset();
        auto reopened = MarkoffDocument::open(*fixture.provider,
            {key, 1}, VNext::StorageMode::ephemeral);
        QVERIFY(reopened.ok());
        fixture.document = std::move(reopened).value();
        fixture.document->undoD2();
        QCOMPARE(fixture.document->serializeForSave(), original);
        fixture.document->redoD2();
        QCOMPARE(fixture.document->serializeForSave(),
                 QByteArrayLiteral("first\n\nsecond\n"));
    }

    void overlapping_stale_search_hits_are_rejected_without_mutation() {
        auto fixture = openCandidate(QStringLiteral("replace-overlap"));
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("abcdef")).state,
            VNext::ActionPublicationState::committed);
        const auto block = fixture.document->iterateBlocks().front();
        const QList<SearchHit> overlapping{
            {block, 1, 3}, {block, 2, 2}};
        QCOMPARE(fixture.document->replaceMatches(overlapping,
            QStringLiteral("X")).state, VNext::ActionPublicationState::rejected);
        QCOMPARE(fixture.document->blockText(block), QByteArrayLiteral("abcdef"));

        const QList<SearchHit> stale{{BlockId{}, 0, 1}};
        QCOMPARE(fixture.document->replaceMatches(stale,
            QStringLiteral("X")).state, VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->blockText(block), QByteArrayLiteral("abcdef"));
    }
    void empty_list_is_noop() {
        auto fixture = openCandidate(QStringLiteral("replace-noop"));
        auto &doc = *fixture.document;
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("nothing here\n")).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(doc.replaceMatches({}, "x").state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(doc.serializeForSave(), QByteArray("nothing here\n"));
    }
};

QTEST_MAIN(TstReplaceMatches)
#include "tst_replace_matches.moc"
