// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
struct CandidateFixture {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};
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

class TstD2ApplyBlockEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void existingBlock_insertChar_appearsInBlockText();
    void existingBlock_removeBytes_dropsThem();
    void unknownBlock_silentlyIgnored();
};

void TstD2ApplyBlockEdit::existingBlock_insertChar_appearsInBlockText()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("block-edit-insert"));
    MarkoffDocument& doc = *fixture.document;
    BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"}).state,
             VNext::ActionPublicationState::committed);
    QCOMPARE(doc.blockText(blk), QByteArray("hello!"));
}

void TstD2ApplyBlockEdit::existingBlock_removeBytes_dropsThem()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello world"), QStringLiteral("block-edit-remove"));
    MarkoffDocument& doc = *fixture.document;
    BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.applyBlockEdit(BlockEdit{blk, 5, 6, ""}).state,
             VNext::ActionPublicationState::committed);
    QCOMPARE(doc.blockText(blk), QByteArray("hello"));
}

void TstD2ApplyBlockEdit::unknownBlock_silentlyIgnored()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("block-edit-unknown"));
    MarkoffDocument& doc = *fixture.document;
    std::array<uint8_t, 16> actor{};
    actor[0] = 9;
    const BlockId fakeId = *BlockId::fromStructuralIdentity(actor, 9999);
    QCOMPARE(doc.applyBlockEdit(BlockEdit{fakeId, 0, 0, "x"}).state,
             VNext::ActionPublicationState::rejected);
}

QTEST_GUILESS_MAIN(TstD2ApplyBlockEdit)
#include "tst_d2_apply_block_edit.moc"
