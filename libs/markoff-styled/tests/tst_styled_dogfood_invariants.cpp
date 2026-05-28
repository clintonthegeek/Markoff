// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QScrollBar>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

class TstStyledDogfoodInvariants : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void hash_gate_skips_unchanged_blocks() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // 10 paragraph blocks separated by blank lines.
        doc.loadFromMarkdown(QByteArrayLiteral(
            "a\n\nb\n\nc\n\nd\n\ne\n\nf\n\ng\n\nh\n\ni\n\nj"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // After initial load, the styler has run at least once and
        // populated the hash for every block. Counter starts at 0.
        // Tickle the document with a single-character edit in block 0.
        const quint64 skipsBefore = e.styleApplierHashSkips();
        Q_UNUSED(skipsBefore);

        // Append "X" to the first block (block 0 byte range is [0,1)).
        doc.applyFlatEdit(1, 1, QByteArrayLiteral("X"),
                          Markoff::Origin::UserEdit);

        // Spin the event loop so the debounced d2DocumentChanged fires
        // and StyleApplier::applyFormats runs.
        QTRY_VERIFY(e.styleApplierHashSkips() > 0);
        // 9 of 10 blocks should be hash-skipped on this pass.
        QCOMPARE(e.styleApplierHashSkips(), quint64(9));
    }

    void kind_transition_paragraph_to_heading() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("plain"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const std::vector<Markoff::BlockId> blocks = doc.iterateBlocks();
        QVERIFY(!blocks.empty());
        const Markoff::BlockId id = blocks[0];  // first block

        // Sanity: starts as Paragraph.
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Paragraph);

        // Prepend "## " to the block content, turning it into a heading.
        doc.applyFlatEdit(0, 0, QByteArrayLiteral("## "),
                          Markoff::Origin::UserEdit);

        // StyleApplier should infer Heading and emit Cmd::changeKind
        // on the next event-loop tick. Wait for the model to update.
        QTRY_COMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);

        // And the QTextBlock should now render at heading size.
        const QTextBlock blk =
            e.textEdit()->document()->findBlockByNumber(0);
        QTRY_VERIFY(blk.charFormat().fontPointSize() > 11.0);
    }

    void scroll_preserved_on_inplace_edit() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // Build a 50-block document so scrolling matters.
        QByteArray src;
        for (int i = 0; i < 50; ++i) {
            src += QByteArrayLiteral("paragraph ");
            src += QByteArray::number(i);
            src += QByteArrayLiteral("\n\n");
        }
        // Drop the final separator.
        src.chop(2);
        doc.loadFromMarkdown(src);
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // Scroll to roughly the middle.
        auto *bar = e.textEdit()->verticalScrollBar();
        QVERIFY(bar->maximum() > 0);  // sanity
        const int target = bar->maximum() / 2;
        bar->setValue(target);
        QCOMPARE(bar->value(), target);

        // Append a char to the first block via flat edit at position 1
        // (mid-block on the first block). In-place edit, no structural
        // change.
        doc.applyFlatEdit(1, 1, QByteArrayLiteral("X"),
                          Markoff::Origin::UserEdit);

        // Spin the event loop for the d2 cycle.
        QTest::qWait(50);

        // Scroll position must be preserved (in-place edit, no
        // structural change). Allow a tiny tolerance for layout drift
        // in the edited block.
        QVERIFY(qAbs(bar->value() - target) <= 5);
    }

    void heading_chars_render_at_heading_size() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // Trailing context required: tree-sitter parses a bare "# x" with no
        // following newline as a Paragraph, not an ATX Heading (same quirk as
        // bare "---"). A heading in a real document always has surrounding
        // structure.
        doc.loadFromMarkdown(QByteArrayLiteral("# Heading\n\nbody text"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // Assert the ACTUAL character format (via a cursor selection), NOT
        // QTextBlock::charFormat() (the block default). Regression guard for
        // the 2026-05-27 dogfood bug: the heading block's '#' delimiter span
        // triggers a mergeCharFormat pass after the block-format pass; if the
        // heading size lives only in the block default (setBlockCharFormat),
        // the merge wipes it and the heading renders at body size. The block
        // default test passed while the visible text was unstyled — so this
        // checks the characters that actually render.
        QTextDocument *qdoc = e.textEdit()->document();
        QTextCursor c(qdoc);
        c.setPosition(2);  // inside "Heading"
        c.setPosition(3, QTextCursor::KeepAnchor);
        QVERIFY(c.charFormat().fontPointSize() > 11.0);
    }

    void typing_at_boundary_does_not_wipe_or_leap() {
        // End-to-end regression guard for the 2026-05-27 dogfood report:
        // boundary drift in the reverse sync caused applyFlatEdit to map the
        // qtPos to the wrong block, triggering a spurious setPlainText that
        // wiped all formatting and leaped the caret to end-of-document.
        // RT1–RT4 (boundary-correct forward path, normalize-on-edit,
        // incremental reverse sync) fixed the root causes; this slot proves
        // the fix holds at the widget level.
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# Heading\n\nbody one\n\nbody two"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.textEdit()->document();
        // Heading char is styled (>11pt) before the edit.
        QTextCursor hc(qdoc); hc.setPosition(2); hc.setPosition(3, QTextCursor::KeepAnchor);
        QVERIFY(hc.charFormat().fontPointSize() > 11.0);

        // Type a space at the boundary between "# Heading" and "body one"
        // (qtPos = end of "# Heading" = 9, just before the "\n\n").
        QTextCursor c(qdoc);
        c.setPosition(9);
        e.textEdit()->setTextCursor(c);
        QTest::keyClicks(e.textEdit(), QStringLiteral(" "));
        QTest::qWait(50);

        // 1. Heading styling survived (no setPlainText wipe).
        QTextCursor hc2(qdoc); hc2.setPosition(2); hc2.setPosition(3, QTextCursor::KeepAnchor);
        QVERIFY(hc2.charFormat().fontPointSize() > 11.0);
        // 2. Caret did not leap to end-of-document.
        QVERIFY(e.textEdit()->textCursor().position() < qdoc->characterCount() - 1);
        // 3. The space landed at the end of the heading block (boundary-correct):
        //    block 0's text is now "# Heading " (trailing space), not "body one"
        //    gaining a leading space.
        const Markoff::BlockId b0 = doc.iterateBlocks()[0];
        QCOMPARE(doc.blockText(b0), QByteArrayLiteral("# Heading "));
    }

    void enter_at_paragraph_end_creates_block_and_places_caret() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.textEdit()->document();
        QTextCursor c(qdoc);
        c.setPosition(5);  // end of "Alpha"
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Return);
        QTest::qWait(80);

        // 1. A new (empty) block was created between Alpha and Bravo.
        QCOMPARE(int(doc.iterateBlocks().size()), 3);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        // 2. Caret landed at the start of the new empty block (sep-view pos 6),
        //    NOT stranded in the gap nor at the start of "Bravo".
        QCOMPARE(e.textEdit()->textCursor().position(), 6);
        // WP unification: the QTextDocument plain text adds exactly one
        // '\n' (one new QTextBlock for the empty block), not three blank
        // lines. Length grew by 1, not 4.
        QCOMPARE(qdoc->toPlainText(), QStringLiteral("Alpha\n\nBravo"));
    }

    void enter_at_document_end_creates_block() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.textEdit()->document());
        c.setPosition(5);  // end of "Alpha"
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(int(doc.iterateBlocks().size()), 2);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        QCOMPARE(e.textEdit()->textCursor().position(), 6);
        QCOMPARE(e.textEdit()->document()->toPlainText(), QStringLiteral("Alpha\n"));
    }

    void enter_mid_paragraph_splits_with_caret_at_new_block() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));  // one block
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.textEdit()->document());
        c.setPosition(5);  // between Alpha and Bravo
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Return);
        QTest::qWait(80);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Alpha"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("Bravo"));
        QCOMPARE(e.textEdit()->textCursor().position(), 6);
        QCOMPARE(e.textEdit()->document()->toPlainText(), QStringLiteral("Alpha\nBravo"));
    }

    void paragraph_margins_present_on_every_block() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // Wait for the styler to run.
        QTest::qWait(50);

        QTextDocument *qdoc = e.textEdit()->document();
        // Both QTextBlocks should carry non-zero top + bottom margins so
        // the visible inter-paragraph gap is layout-driven, not whitespace.
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
            QTextBlockFormat bf = b.blockFormat();
            QVERIFY2(bf.topMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 topMargin=%2")
                                .arg(b.blockNumber()).arg(bf.topMargin())));
            QVERIFY2(bf.bottomMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 bottomMargin=%2")
                                .arg(b.blockNumber()).arg(bf.bottomMargin())));
        }
    }

    void backspace_at_block_start_merges_with_caret_at_join() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.textEdit()->document());
        c.setPosition(6);  // start of "Bravo" (WP unification: single '\n' separator)
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Backspace);
        QTest::qWait(80);

        // Blocks merged into one "AlphaBravo".
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 1);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("AlphaBravo"));
        // Caret at the join point = end of "Alpha" = sep-view pos 5.
        QCOMPARE(e.textEdit()->textCursor().position(), 5);
    }
};

QTEST_MAIN(TstStyledDogfoodInvariants)
#include "tst_styled_dogfood_invariants.moc"
