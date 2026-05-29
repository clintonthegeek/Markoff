// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QtCore/qnamespace.h>

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/StructuralKeyHandler.h>

using namespace Markoff;

class TstStructuralKeyHandler : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void paragraph_enter_at_end_creates_block_after() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        const auto blocks = doc.iterateBlocks();
        const BlockId first = blocks[0];
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(first).size());

        auto r = StructuralKeyHandler::handle(doc, first, Qt::Key_Return,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 3);
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral(""));
        QCOMPARE(r.caretBlock, after[1]);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void paragraph_enter_mid_splits() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::NoModifier, 5u);  // after "Alpha"
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 2);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral("Alpha"));
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral("Bravo"));
        QCOMPARE(r.caretBlock, after[1]);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void paragraph_shift_enter_soft_break() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::ShiftModifier, 5u);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), 1);  // no split
        QCOMPARE(doc.blockText(b), QByteArrayLiteral("Alpha\nBravo"));
        QCOMPARE(r.caretBlock, b);
        QCOMPARE(r.caretByteInBlock, 6u);  // after the inserted '\n'
    }

    void paragraph_backspace_at_start_merges() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        const auto blocks = doc.iterateBlocks();
        auto r = StructuralKeyHandler::handle(doc, blocks[1], Qt::Key_Backspace,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 1);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral("AlphaBravo"));
        QCOMPARE(r.caretByteInBlock, 5u);  // join at end of "Alpha"
        QCOMPARE(r.caretBlock, after[0]);  // caret lands in the surviving merged block
    }

    void paragraph_backspace_mid_not_handled() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Backspace,
                                              Qt::NoModifier, 3u);
        QVERIFY(!r.handled);
        QCOMPARE(doc.blockText(b), QByteArrayLiteral("Alpha"));  // untouched
    }

    void paragraph_delete_at_end_merges_next() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        const auto blocks = doc.iterateBlocks();
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(blocks[0]).size());
        auto r = StructuralKeyHandler::handle(doc, blocks[0], Qt::Key_Delete,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), 1);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[0]), QByteArrayLiteral("AlphaBravo"));
        QCOMPARE(r.caretBlock, blocks[0]);
        QCOMPARE(r.caretByteInBlock, endByte);
    }

    void heading_enter_at_end_creates_block_after() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# H\n\nbody"));
        const auto blocks = doc.iterateBlocks();
        const BlockId head = blocks[0];
        QCOMPARE(doc.blockKind(head), BlockKind::Heading);
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(head).size());
        auto r = StructuralKeyHandler::handle(doc, head, Qt::Key_Return,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 3);
        QCOMPARE(doc.blockKind(after[0]), BlockKind::Heading);  // heading untouched
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral(""));
        QCOMPARE(r.caretBlock, after[1]);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void enter_at_start_of_first_block_inserts_empty_para_before() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 2);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral(""));   // new empty para first
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral("Alpha"));
        QCOMPARE(r.caretBlock, after[0]);   // caret in the new empty para
        QCOMPARE(r.caretByteInBlock, 0u);
    }
};

QTEST_MAIN(TstStructuralKeyHandler)
#include "tst_structural_key_handler.moc"
