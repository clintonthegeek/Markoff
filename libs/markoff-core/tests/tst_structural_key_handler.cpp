// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QtCore/qnamespace.h>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/StructuralKeyHandler.h>
#include <markoff/core/UndoLog.h>

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

    void listitem_enter_at_end_inserts_item_after() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        const BlockId first = blocks[0];
        QCOMPARE(doc.blockKind(first), BlockKind::ListItem);
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(first).size());
        auto r = StructuralKeyHandler::handle(doc, first, Qt::Key_Return,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 3);
        QCOMPARE(doc.blockKind(after[1]), BlockKind::ListItem);
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral(""));
        QCOMPARE(r.caretBlock, after[1]);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void listitem_enter_empty_at_indent0_exits_list() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n"));
        const BlockId b = doc.iterateBlocks()[0];
        // Empty the item buffer (simulate an empty list line).
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(b, 0,
                static_cast<uint32_t>(doc.blockText(b).size()), QByteArray{}, t);
        }
        QCOMPARE(doc.blockKind(b), BlockKind::ListItem);
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(doc.blockKind(b), BlockKind::Paragraph);  // demoted out of list
    }

    void listitem_tab_indents() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        const BlockId second = blocks[1];  // has a preceding item at indent 0
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Tab,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        const auto attrs = doc.blockAttrs(second);
        QVERIFY(attrs.contains(Markoff::AttrNames::IndentLevel));
        QCOMPARE(std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)), 1);
    }

    void listitem_tab_first_item_refused() {
        // No preceding sibling at the same level → indent refused (but key consumed).
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const BlockId first = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, first, Qt::Key_Tab,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);  // consumed
        const auto attrs = doc.blockAttrs(first);
        const int indent = attrs.contains(Markoff::AttrNames::IndentLevel)
            ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;
        QCOMPARE(indent, 0);  // not indented
    }

    void listitem_backtab_outdents() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const BlockId second = doc.iterateBlocks()[1];
        // First indent it to 1.
        { auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Tab,
                                                Qt::NoModifier, 0u); QVERIFY(r.handled); }
        QCOMPARE(std::get<int>(doc.blockAttrs(second).value(Markoff::AttrNames::IndentLevel)), 1);
        // Now Shift+Tab arrives as Key_Backtab.
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Backtab,
                                              Qt::ShiftModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(std::get<int>(doc.blockAttrs(second).value(Markoff::AttrNames::IndentLevel)), 0);
    }

    void listitem_backspace_at_start_indent0_merges() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        auto r = StructuralKeyHandler::handle(doc, blocks[1], Qt::Key_Backspace,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), 1);
        const auto merged = doc.iterateBlocks();
        QCOMPARE(r.caretBlock, merged[0]);
        QCOMPARE(r.caretByteInBlock, 3u);  // end of "one"
    }

    void listitem_delete_at_end_merges_next() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(blocks[0]).size());
        auto r = StructuralKeyHandler::handle(doc, blocks[0], Qt::Key_Delete,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), 1);
        QCOMPARE(r.caretBlock, blocks[0]);
        QCOMPARE(r.caretByteInBlock, endByte);
    }

    void listitem_tab_preserves_caret() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- hello\n"));
        const BlockId second = doc.iterateBlocks()[1];  // "hello"
        // Caret mid-content (byte 3 of "hello"); Tab should indent but keep caret.
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Tab,
                                              Qt::NoModifier, 3u);
        QVERIFY(r.handled);
        QCOMPARE(std::get<int>(doc.blockAttrs(second).value(Markoff::AttrNames::IndentLevel)), 1);
        QCOMPARE(r.caretBlock, second);
        QCOMPARE(r.caretByteInBlock, 3u);  // caret preserved, not reset to 0
    }

    void listitem_enter_mid_splits() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- onetwo\n"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::NoModifier, 3u);  // after "one"
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 2);
        QCOMPARE(doc.blockKind(after[0]), BlockKind::ListItem);
        QCOMPARE(doc.blockKind(after[1]), BlockKind::ListItem);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral("one"));
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral("two"));
        QCOMPARE(r.caretBlock, after[1]);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void listitem_backspace_at_start_indent_gt0_outdents() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const BlockId second = doc.iterateBlocks()[1];
        // Indent it to 1 first.
        { auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Tab,
                                                Qt::NoModifier, 0u); QVERIFY(r.handled); }
        QCOMPARE(std::get<int>(doc.blockAttrs(second).value(Markoff::AttrNames::IndentLevel)), 1);
        const int countBefore = int(doc.iterateBlocks().size());
        // Backspace at start with indent>0 should OUTDENT, not merge.
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Backspace,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), countBefore);  // no merge
        QCOMPARE(std::get<int>(doc.blockAttrs(second).value(Markoff::AttrNames::IndentLevel)), 0);
    }
};

QTEST_MAIN(TstStructuralKeyHandler)
#include "tst_structural_key_handler.moc"
