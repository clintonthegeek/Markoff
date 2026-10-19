// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

using namespace Markoff;

namespace {
struct CandidateDocument {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};

CandidateDocument openCandidate(const QString& key, const QByteArray& source)
{
    auto providerResult = VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocument result{std::move(providerResult).value(), {}};
    auto opened = MarkoffDocument::open(*result.provider, {key, 1}, VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    result.document = std::move(opened).value();
    if (result.document->importInitialMarkdown(source).state
        != VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial Markdown import failed");
    return result;
}
}

class TstFoundationEditSequence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fresh_doc_starts_at_zero() {
        auto fixture = openCandidate(QStringLiteral("edit-seq-empty"), {});
        auto& d = *fixture.document;
        QCOMPARE(d.d2EditSequence(), quint64{0});
    }

    void apply_flat_edit_increments() {
        auto fixture = openCandidate(QStringLiteral("edit-seq-edit"), QByteArrayLiteral("x"));
        auto& d = *fixture.document;
        const auto block = d.iterateBlocks().front();
        const auto seq0 = d.d2EditSequence();
        QCOMPARE(d.applyBlockEdit({block, 1, 0, QByteArrayLiteral("x")}).state,
                 VNext::ActionPublicationState::committed);
        QVERIFY(d.d2EditSequence() > seq0);
    }

    void undo_increments() {
        auto fixture = openCandidate(QStringLiteral("edit-seq-undo"), QByteArrayLiteral("x"));
        auto& d = *fixture.document;
        const auto block = d.iterateBlocks().front();
        QCOMPARE(d.applyBlockEdit({block, 1, 0, QByteArrayLiteral("y")}).state,
                 VNext::ActionPublicationState::committed);
        const auto seq1 = d.d2EditSequence();
        d.undoD2();
        QVERIFY(d.d2EditSequence() > seq1);
    }

    void redo_increments() {
        auto fixture = openCandidate(QStringLiteral("edit-seq-redo"), QByteArrayLiteral("x"));
        auto& d = *fixture.document;
        const auto block = d.iterateBlocks().front();
        QCOMPARE(d.applyBlockEdit({block, 1, 0, QByteArrayLiteral("y")}).state,
                 VNext::ActionPublicationState::committed);
        d.undoD2();
        const auto seq2 = d.d2EditSequence();
        d.redoD2();
        QVERIFY(d.d2EditSequence() > seq2);
    }

    void monotonic_under_burst() {
        auto fixture = openCandidate(QStringLiteral("edit-seq-burst"), QByteArrayLiteral("x"));
        auto& d = *fixture.document;
        const auto block = d.iterateBlocks().front();
        quint64 prev = d.d2EditSequence();
        for (int i = 0; i < 20; ++i) {
            const auto text = d.blockText(block);
            const auto result = d.applyBlockEdit({block,
                static_cast<uint32_t>(text.size()), 0, QByteArrayLiteral("a")});
            if (result.state != VNext::ActionPublicationState::committed) {
                const std::string diagnostic = result.diagnostic
                    ? std::string(result.diagnostic->message()) : "no diagnostic";
                QFAIL(diagnostic.c_str());
            }
            const auto cur = d.d2EditSequence();
            QVERIFY(cur > prev);
            prev = cur;
        }
    }

    void attribute_write_increments() {
        // An attribute write changes what the block shows, so the content
        // version must advance: version-keyed caches (canvas layout, inline
        // spans, the embed render cache) would otherwise keep the old state.
        auto fixture = openCandidate(QStringLiteral("edit-seq-attr"), QByteArrayLiteral("x"));
        auto& d = *fixture.document;
        const auto block = d.iterateBlocks().front();
        const auto seq0 = d.d2EditSequence();
        const auto blockSeq0 = d.blockEditSequence(block);
        QCOMPARE(d.setBlockAttribute(block, AttrNames::Checked, true).state,
                 VNext::ActionPublicationState::committed);
        QVERIFY(d.d2EditSequence() > seq0);
        QVERIFY(d.blockEditSequence(block) > blockSeq0);
    }

    void attribute_removal_increments() {
        auto fixture = openCandidate(QStringLiteral("edit-seq-attr-remove"),
                                     QByteArrayLiteral("x"));
        auto& d = *fixture.document;
        const auto block = d.iterateBlocks().front();
        QCOMPARE(d.setBlockAttribute(block, AttrNames::Checked, true).state,
                 VNext::ActionPublicationState::committed);
        const auto seq0 = d.d2EditSequence();
        QCOMPARE(d.removeBlockAttribute(block, AttrNames::Checked).state,
                 VNext::ActionPublicationState::committed);
        QVERIFY(d.d2EditSequence() > seq0);
    }

    void kind_change_increments() {
        auto fixture = openCandidate(QStringLiteral("edit-seq-kind"), QByteArrayLiteral("x"));
        auto& d = *fixture.document;
        const auto block = d.iterateBlocks().front();
        const auto seq0 = d.d2EditSequence();
        const auto blockSeq0 = d.blockEditSequence(block);
        QCOMPARE(d.changeBlockKind(block, BlockKind::Heading).state,
                 VNext::ActionPublicationState::committed);
        QVERIFY(d.d2EditSequence() > seq0);
        QVERIFY(d.blockEditSequence(block) > blockSeq0);
    }

};

QTEST_MAIN(TstFoundationEditSequence)
#include "tst_foundation_edit_sequence.moc"
