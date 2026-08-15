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

    void enter_at_start_of_non_first_block_inserts_empty_para_before() {
        // The idx > 0 branch of paragraphEnter's at-start path: insert via
        // enterAtEnd(prevBlock), which lands a new empty para before `block`.
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        const auto blocks = doc.iterateBlocks();
        const BlockId bravo = blocks[1];
        auto r = StructuralKeyHandler::handle(doc, bravo, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 3);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral("Alpha"));
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral(""));     // new empty para
        QCOMPARE(doc.blockText(after[2]), QByteArrayLiteral("Bravo"));
        QCOMPARE(r.caretBlock, after[1]);   // caret in the new empty para
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

    void listitem_backspace_at_start_indent0_delists_not_merges() {
        // CM `deleteMarkupBackward` parity (P7.2d addendum, 2026-08-15
        // user-approved behavior change): Backspace at content-start of an
        // indent-0 ListItem de-lists it in place. It must NOT merge into
        // the previous block.
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        const BlockId first = blocks[0];
        const BlockId second = blocks[1];
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Backspace,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 2);           // no merge: still two blocks
        QCOMPARE(after[0], first);
        QCOMPARE(after[1], second);
        QCOMPARE(doc.blockKind(second), BlockKind::Paragraph);   // de-listed
        QCOMPARE(doc.blockText(second), QByteArrayLiteral("two"));
        const auto secondAttrs = doc.blockAttrs(second);
        auto msIt = secondAttrs.find(AttrNames::MarkerStyle);
        if (msIt != secondAttrs.end())
            QVERIFY(std::get<QString>(msIt.value()).isEmpty());
        QCOMPARE(doc.blockKind(first), BlockKind::ListItem);      // previous block untouched
        QCOMPARE(doc.blockText(first), QByteArrayLiteral("one"));
        QCOMPARE(r.caretBlock, second);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void listitem_backspace_at_start_indent0_delists_tail_keeps_numbering() {
        // De-listing an item can only ever SPLIT a contiguous ordered-list
        // run, never merge two runs (unlike Tab-outdent). The split-off
        // tail's own first MarkerNumber is already the correct seed for its
        // now-standalone run, so no renumbering is needed/performed.
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("1. one\n2. two\n3. three\n"));
        const auto blocks = doc.iterateBlocks();
        const BlockId first = blocks[0];
        const BlockId second = blocks[1];
        const BlockId third = blocks[2];
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Backspace,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(doc.blockKind(second), BlockKind::Paragraph);
        QCOMPARE(doc.blockKind(first), BlockKind::ListItem);
        QCOMPARE(std::get<int>(doc.blockAttrs(first).value(AttrNames::MarkerNumber)), 1);
        QCOMPARE(doc.blockKind(third), BlockKind::ListItem);
        // Tail item keeps its own original number — it's now a standalone
        // one-item run, and CommonMark takes an ordered list's start number
        // from its own first item, so "3." is correct, not "2.".
        QCOMPARE(std::get<int>(doc.blockAttrs(third).value(AttrNames::MarkerNumber)), 3);
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

    void listitem_enter_empty_at_indent_gt0_outdents_not_exits() {
        // CM insertNewlineContinueMarkup: Enter on an EMPTY nested item outdents
        // one level; only an empty TOP-level item exits the list entirely.
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const BlockId second = doc.iterateBlocks()[1];
        { auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Tab,
                                                Qt::NoModifier, 0u); QVERIFY(r.handled); }
        QCOMPARE(std::get<int>(doc.blockAttrs(second).value(Markoff::AttrNames::IndentLevel)), 1);
        // Empty its content (simulate an empty nested list line).
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(second, 0,
                static_cast<uint32_t>(doc.blockText(second).size()), QByteArray{}, t);
        }
        const int countBefore = int(doc.iterateBlocks().size());
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), countBefore);  // no new item, no merge
        QCOMPARE(doc.blockKind(second), BlockKind::ListItem);    // still a list item
        QCOMPARE(std::get<int>(doc.blockAttrs(second).value(Markoff::AttrNames::IndentLevel)), 0);
        QCOMPARE(r.caretBlock, second);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void listitem_enter_mid_split_ordered_list_renumbers() {
        // CM insertNewlineContinueMarkup + renumberList: splitting item 2 of a
        // 3-item ordered list must leave the list numbered 1,2,3,4 (not 1,2,2,3).
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("1. one\n2. two\n3. three\n"));
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 3);
        const BlockId itemTwo = blocks[1];
        // Split "two" after "tw" (byte 2).
        auto r = StructuralKeyHandler::handle(doc, itemTwo, Qt::Key_Return,
                                              Qt::NoModifier, 2u);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 4);
        for (auto id : after) QCOMPARE(doc.blockKind(id), BlockKind::ListItem);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral("one"));
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral("tw"));
        QCOMPARE(doc.blockText(after[2]), QByteArrayLiteral("o"));
        QCOMPARE(doc.blockText(after[3]), QByteArrayLiteral("three"));
        QCOMPARE(std::get<int>(doc.blockAttrs(after[0]).value(Markoff::AttrNames::MarkerNumber)), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(after[1]).value(Markoff::AttrNames::MarkerNumber)), 2);
        QCOMPARE(std::get<int>(doc.blockAttrs(after[2]).value(Markoff::AttrNames::MarkerNumber)), 3);
        QCOMPARE(std::get<int>(doc.blockAttrs(after[3]).value(Markoff::AttrNames::MarkerNumber)), 4);
        QCOMPARE(r.caretBlock, after[2]);
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

    void codeblock_enter_inserts_soft_break_not_split() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("```\ncode\n```\n"));
        BlockId code;
        for (auto id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::CodeBlock) { code = id; break; }
        QVERIFY(!code.isNull());
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(code).size());
        const int blockCountBefore = int(doc.iterateBlocks().size());
        auto r = StructuralKeyHandler::handle(doc, code, Qt::Key_Return,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), blockCountBefore);  // no split
        QCOMPARE(r.caretBlock, code);
        // Buffer grew by exactly the inserted '\n'; caret advanced past it.
        QCOMPARE(static_cast<uint32_t>(doc.blockText(code).size()), endByte + 1u);
        QCOMPARE(r.caretByteInBlock, endByte + 1u);
    }

    void codeblock_tab_inserts_four_spaces() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("```\ncode\n```\n"));
        BlockId code;
        for (auto id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::CodeBlock) { code = id; break; }
        QVERIFY(!code.isNull());
        const QByteArray before = doc.blockText(code);
        auto r = StructuralKeyHandler::handle(doc, code, Qt::Key_Tab,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(doc.blockText(code), QByteArray("    ") + before);
        QCOMPARE(r.caretByteInBlock, 4u);
    }

    void blockquote_empty_enter_exits_to_paragraph() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("> quoted\n"));
        BlockId q;
        for (auto id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::BlockQuote) { q = id; break; }
        QVERIFY(!q.isNull());
        // Empty the buffer (simulate an empty quote line).
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(q, 0,
                static_cast<uint32_t>(doc.blockText(q).size()), QByteArray{}, t);
        }
        auto r = StructuralKeyHandler::handle(doc, q, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(doc.blockKind(q), BlockKind::Paragraph);
    }

    void hr_enter_inserts_paragraph_after() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("text\n\n---\n\nmore\n"));
        BlockId hr;
        for (auto id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::HorizontalRule) { hr = id; break; }
        QVERIFY(!hr.isNull());
        const int before = int(doc.iterateBlocks().size());
        auto r = StructuralKeyHandler::handle(doc, hr, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), before + 1);
        QCOMPARE(doc.blockKind(r.caretBlock), BlockKind::Paragraph);
        QCOMPARE(r.caretByteInBlock, 0u);
    }
};

QTEST_MAIN(TstStructuralKeyHandler)
#include "tst_structural_key_handler.moc"
