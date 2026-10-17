// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QCoreApplication>

#include <markoff/core/Cmd/Edit.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/BlockEdit.h>
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

static QByteArray fullText(const MarkoffDocument &doc) {
    QByteArray out;
    for (BlockId id : doc.iterateBlocks())
        out += doc.blockText(id);
    return out;
}

class TstFoundationCmdEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undo_wrapper_reverts_last_local_edit() {
        auto fixture = openCandidate(QStringLiteral("cmd-undo"));
        auto &doc = *fixture.document;
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("ab")).state,
                 VNext::ActionPublicationState::committed);
        const auto block = doc.iterateBlocks().front();
        QCOMPARE(doc.applyBlockEdit({block, 2, 0, QByteArrayLiteral("c")}).state,
                 VNext::ActionPublicationState::committed);
        QCoreApplication::processEvents();
        Cmd::undo(doc);
        QCoreApplication::processEvents();
        QCOMPARE(fullText(doc), QByteArray("ab"));
    }

    void redo_wrapper_reapplies() {
        auto fixture = openCandidate(QStringLiteral("cmd-redo"));
        auto &doc = *fixture.document;
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("ab")).state,
                 VNext::ActionPublicationState::committed);
        const auto block = doc.iterateBlocks().front();
        QCOMPARE(doc.applyBlockEdit({block, 2, 0, QByteArrayLiteral("c")}).state,
                 VNext::ActionPublicationState::committed);
        QCoreApplication::processEvents();
        Cmd::undo(doc);
        QCoreApplication::processEvents();
        Cmd::redo(doc);
        QCoreApplication::processEvents();
        QCOMPARE(fullText(doc), QByteArray("abc"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdEdit)
#include "tst_foundation_cmd_edit.moc"
