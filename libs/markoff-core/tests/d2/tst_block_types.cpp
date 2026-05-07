// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/StructuralOp.h>
#include <markoff/core/BlockKind.h>
#include <variant>

class TstBlockTypes : public QObject {
    Q_OBJECT
private slots:
    void blockKind_paragraphAndHeadingDistinct();
    void blockEdit_constructionAndAccess();
    void structuralOp_insertEntryVariant();
};

void TstBlockTypes::blockKind_paragraphAndHeadingDistinct() {
    QVERIFY(Markoff::BlockKind::Paragraph != Markoff::BlockKind::Heading);
}

void TstBlockTypes::blockEdit_constructionAndAccess() {
    Markoff::BlockEdit edit{
        Markoff::BlockId::fromRaw(1), /*offset=*/3u, /*removed=*/0u, /*inserted=*/QByteArray("x")
    };
    QCOMPARE(edit.blockId, Markoff::BlockId::fromRaw(1));
    QCOMPARE(edit.withinBlockByteOffset, 3u);
    QCOMPARE(edit.removedBytes, 0u);
    QCOMPARE(edit.insertedUtf8, QByteArray("x"));
}

void TstBlockTypes::structuralOp_insertEntryVariant() {
    Markoff::StructuralOp op;
    op.payload = Markoff::StructuralOp::InsertEntry{
        Markoff::BlockId::fromRaw(1), Markoff::BlockKind::Paragraph
    };
    QVERIFY(std::holds_alternative<Markoff::StructuralOp::InsertEntry>(op.payload));
}

QTEST_GUILESS_MAIN(TstBlockTypes)
#include "tst_block_types.moc"
