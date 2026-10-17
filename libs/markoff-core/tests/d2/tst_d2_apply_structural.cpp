// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/StructuralOp.h>
#include <markoff/core/BlockKind.h>
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
bool accepted(const Markoff::VNext::ActionPublicationResult& result)
{
    return result.state == Markoff::VNext::ActionPublicationState::committed
        || result.state == Markoff::VNext::ActionPublicationState::committedHistoryPending;
}
}

using namespace Markoff;

class TstD2ApplyStructural : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void insertEntry_appendsBlock();
    void allInsertionPathsShareOneDocumentAllocator();
    void removeEntry_dropsBlockFromIteration();
    void changeKind_updatesKindTagMap();
};

void TstD2ApplyStructural::insertEntry_appendsBlock()
{
    auto fixture = openCandidate(QByteArrayLiteral("seed"), QStringLiteral("struct-insert"));
    MarkoffDocument& doc = *fixture.document;
    const auto existing = doc.iterateBlocks().front();
    QVERIFY(accepted(doc.applyStructural(
        StructuralOp{StructuralOp::InsertEntry{existing, BlockKind::Paragraph}})));
    QCOMPARE(doc.iterateBlocks().size(), size_t(2));
}

void TstD2ApplyStructural::allInsertionPathsShareOneDocumentAllocator()
{
    auto fixture = openCandidate(QByteArrayLiteral("fixture"), QStringLiteral("struct-paths"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId fixtureBlock = doc.iterateBlocks().front();
    QVERIFY(accepted(doc.applyStructural(
        StructuralOp{StructuralOp::InsertEntry{fixtureBlock, BlockKind::Paragraph}})));
    const auto blocks = doc.iterateBlocks();
    QVERIFY(accepted(doc.applyStructural(
        StructuralOp{StructuralOp::InsertEntry{blocks.back(), BlockKind::Paragraph}})));
    const auto after = doc.iterateBlocks();
    QCOMPARE(after.size(), size_t(3));
    QVERIFY(after[0] != after[1]);
    QVERIFY(after[0] != after[2]);
    QVERIFY(after[1] != after[2]);
}

void TstD2ApplyStructural::removeEntry_dropsBlockFromIteration()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("struct-remove"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    QVERIFY(accepted(doc.applyStructural(StructuralOp{StructuralOp::RemoveEntry{blk}})));
    QCOMPARE(doc.iterateBlocks().size(), size_t(0));
}

void TstD2ApplyStructural::changeKind_updatesKindTagMap()
{
    auto fixture = openCandidate(QByteArrayLiteral("hello"), QStringLiteral("struct-kind"));
    MarkoffDocument& doc = *fixture.document;
    const BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(blk), BlockKind::Paragraph);
    QVERIFY(accepted(doc.applyStructural(
        StructuralOp{StructuralOp::ChangeKind{blk, BlockKind::Heading}})));
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
}

QTEST_GUILESS_MAIN(TstD2ApplyStructural)
#include "tst_d2_apply_structural.moc"
