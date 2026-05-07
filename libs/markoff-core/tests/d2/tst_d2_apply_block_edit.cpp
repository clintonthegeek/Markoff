// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockKind.h>

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
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    QCOMPARE(doc.blockText(blk), QByteArray("hello!"));
}

void TstD2ApplyBlockEdit::existingBlock_removeBytes_dropsThem()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello world");
    doc.applyBlockEdit(BlockEdit{blk, 5, 6, ""});  // remove " world"
    QCOMPARE(doc.blockText(blk), QByteArray("hello"));
}

void TstD2ApplyBlockEdit::unknownBlock_silentlyIgnored()
{
    MarkoffDocument doc(1);
    BlockId fakeId = BlockId::fromRaw(9999);
    // Should not crash
    doc.applyBlockEdit(BlockEdit{fakeId, 0, 0, "x"});
}

QTEST_GUILESS_MAIN(TstD2ApplyBlockEdit)
#include "tst_d2_apply_block_edit.moc"
