// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>

using namespace Markoff;

class TstFoundationScopedPosition : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void boundary_tokens_are_scoped_and_follow_edits()
    {
        auto provider = VNext::makeEphemeralDocumentStorageProvider();
        QVERIFY(provider.ok());
        auto opened = MarkoffDocument::open(*provider.value(),
            {QStringLiteral("scoped-position"), 1}, VNext::StorageMode::ephemeral);
        QVERIFY(opened.ok());
        auto& doc = *opened.value();
        QCOMPARE(doc.importInitialMarkdown(QByteArrayLiteral("hello\n\nworld")).state,
                 VNext::ActionPublicationState::committed);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(blocks.size(), size_t{2});

        const auto token = doc.boundaryTokenAt(blocks[0], 3, false);
        QVERIFY(token.ok());
        QCOMPARE(doc.applyBlockEdit({blocks[0], 0, 0, QByteArrayLiteral("X")}).state,
                 VNext::ActionPublicationState::committed);
        const auto resolved = doc.resolveBoundaryToken(blocks[0], token.value());
        QVERIFY(resolved.ok());
        QCOMPARE(resolved.value(), quint32{4});

        const auto wrongScope = doc.resolveBoundaryToken(blocks[1], token.value());
        QVERIFY(!wrongScope.ok());
        QCOMPARE(wrongScope.error().code(), CollabText::ErrorCode::wrong_stream);
    }
};

QTEST_MAIN(TstFoundationScopedPosition)
#include "tst_foundation_scoped_position.moc"
