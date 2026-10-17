// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/BlockId.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
struct CandidateFixture { std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
                          std::unique_ptr<Markoff::MarkoffDocument> document; };
CandidateFixture openCandidate(const QByteArray& source, const QString& key)
{
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    CandidateFixture fixture{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider, {key, 1},
        Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(source).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}
}

using namespace Markoff;

class TstD2Undo : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undoUndoesLastEdit();
    void redoRestoresEdit();
    void undoForBlock_picksRecentBlockEntry();
    void canUndoForBlock_returns_false_when_no_history();
    void canUndoForBlock_returns_true_after_edit();
    void undoForBlock_via_anchor_undoes_edit();
    void undo_bumpsBlockEditSequence();
    void redo_bumpsBlockEditSequence();
};

void TstD2Undo::undoUndoesLastEdit()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("undo-basic"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    QCOMPARE(doc.blockText(blk), QByteArray("hello!"));
    doc.undoD2();
    QCOMPARE(doc.blockText(blk), QByteArray("hello"));
}

void TstD2Undo::redoRestoresEdit()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("undo-redo"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    doc.undoD2();
    doc.redoD2();
    QCOMPARE(doc.blockText(blk), QByteArray("hello!"));
}

void TstD2Undo::undoForBlock_picksRecentBlockEntry()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello\n\nworld"), QStringLiteral("undo-block"));
    MarkoffDocument& doc = *fixture.document;
    const auto blocks = doc.iterateBlocks();
    const BlockId blkA = blocks[0], blkB = blocks[1];
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blkA, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blkB, 5, 0, "?"}).state,
             VNext::ActionPublicationState::committed);
    // Undo only blkA's last edit
    doc.undoForBlock(blkA);
    QCOMPARE(doc.blockText(blkA), QByteArray("hello"));
    QCOMPARE(doc.blockText(blkB), QByteArray("world?"));  // blkB unaffected
}

void TstD2Undo::canUndoForBlock_returns_false_when_no_history()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("undo-none"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    BlockId anchor = blk;
    QVERIFY(!doc.canUndoForBlock(anchor));
}

void TstD2Undo::canUndoForBlock_returns_true_after_edit()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("undo-can"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    BlockId anchor = blk;
    QVERIFY(doc.canUndoForBlock(anchor));
}

void TstD2Undo::undoForBlock_via_anchor_undoes_edit()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello\n\nworld"), QStringLiteral("undo-anchor"));
    MarkoffDocument& doc = *fixture.document;
    const auto blocks = doc.iterateBlocks();
    const BlockId blkA = blocks[0], blkB = blocks[1];
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blkA, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blkB, 5, 0, "?"}).state,
             VNext::ActionPublicationState::committed);
    BlockId anchorA = blkA;
    doc.undoForBlock(anchorA);
    QCOMPARE(doc.blockText(blkA), QByteArray("hello"));
    QCOMPARE(doc.blockText(blkB), QByteArray("world?"));
}

void TstD2Undo::undo_bumpsBlockEditSequence()
{
    // The undo dispatcher mutates the CRDT Buffer directly (BufferT branch),
    // bypassing applyBlockEdit/d2ApplyBufferEdit entirely — those are the
    // only other places blockEditSequence normally advances. Without this,
    // any consumer keyed on (BlockId, blockEditSequence) — namely
    // InlineParseCache — would see no change and keep serving spans parsed
    // from the pre-undo text (Corbomite Cluster K: phantom link styling
    // survived an undo until an unrelated edit finally bumped the sequence).
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("undo-sequence"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    const quint64 seqBeforeUndo = doc.blockEditSequence(blk);
    doc.undoD2();
    QVERIFY(doc.blockEditSequence(blk) > seqBeforeUndo);
}

void TstD2Undo::redo_bumpsBlockEditSequence()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("redo-sequence"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    doc.undoD2();
    const quint64 seqBeforeRedo = doc.blockEditSequence(blk);
    doc.redoD2();
    QVERIFY(doc.blockEditSequence(blk) > seqBeforeRedo);
}

QTEST_GUILESS_MAIN(TstD2Undo)
#include "tst_d2_undo.moc"
