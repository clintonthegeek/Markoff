// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
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
};

QTEST_MAIN(TstStyledDogfoodInvariants)
#include "tst_styled_dogfood_invariants.moc"
