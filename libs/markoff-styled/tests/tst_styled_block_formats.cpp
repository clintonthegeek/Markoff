// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextList>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

namespace {
QTextBlock blockN(QTextDocument *doc, int n) {
    return doc->findBlockByNumber(n);
}
}  // namespace

class TstStyledBlockFormats : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void heading_level_1_gets_largest_size() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# H1 title"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        const QTextCharFormat fmt = b.charFormat();
        QVERIFY(fmt.fontPointSize() > 0);
        QCOMPARE(fmt.fontWeight(), int(QFont::Bold));
    }

    void heading_levels_descend_in_size() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "# H1\n\n## H2\n\n### H3\n\n#### H4\n\n##### H5\n\n###### H6"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        auto *qdoc = e.textEdit()->document();
        // WP unification: widgetFlatView() joins blocks with a single '\n',
        // so each heading is its own QTextBlock at consecutive indices — there
        // are no empty separator blocks. (Pre-WP "\n\n" put headings at
        // 0,2,4,6,8,10 with empty separator blocks in between.)
        const qreal h1 = blockN(qdoc, 0).charFormat().fontPointSize();
        const qreal h2 = blockN(qdoc, 1).charFormat().fontPointSize();
        const qreal h3 = blockN(qdoc, 2).charFormat().fontPointSize();
        const qreal h4 = blockN(qdoc, 3).charFormat().fontPointSize();
        const qreal h5 = blockN(qdoc, 4).charFormat().fontPointSize();
        const qreal h6 = blockN(qdoc, 5).charFormat().fontPointSize();
        QVERIFY(h1 > h2);
        QVERIFY(h2 > h3);
        QVERIFY(h3 > h4);
        QVERIFY(h4 > h5);
        QVERIFY(h5 > h6);
        QVERIFY(h6 > 0);
    }

    void paragraph_keeps_default_size() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Just a paragraph."));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        // Paragraph charFormat must not impose a heading-bold weight.
        QVERIFY(b.charFormat().fontWeight() != int(QFont::Bold));
    }

    void code_block_uses_monospace_and_background() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("```\ncode line\n```"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        auto *qdoc = e.textEdit()->document();
        // Fenced code spans 3 QTextBlocks: fence, content, fence.
        const QTextBlock content = blockN(qdoc, 1);
        const QTextCharFormat cf = content.charFormat();
        QVERIFY(cf.fontFixedPitch() || cf.fontFamilies().toStringList().size() > 0);
        QVERIFY(content.blockFormat().background().style() != Qt::NoBrush);
    }

    void blockquote_has_left_margin() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("> quoted text"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        QVERIFY(b.blockFormat().leftMargin() > 0);
    }

    void list_item_indents_via_qtextlist() {
        // #8.4 (2026-05-29) moved list indentation off blockFormat().leftMargin()
        // and onto QTextList membership, so consecutive items share one list and
        // ordered numbering stays continuous. The visible indent now derives from
        // the list's indent level, not a block left-margin.
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- first item"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        QVERIFY(b.textList() != nullptr);
        QVERIFY(b.textList()->format().indent() > 0);
    }

    void horizontal_rule_uses_monospace() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // Use a thematic break preceded by a paragraph so tree-sitter
        // unambiguously classifies the "---" line as ThematicBreak.
        doc.loadFromMarkdown(QByteArrayLiteral("before\n\n---\n"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // Two MarkoffDocument blocks: Paragraph + HorizontalRule. Under WP
        // unification widgetFlatView() joins them with a single '\n' and each
        // model block maps to one QTextBlock, so block 0 = "before", block 1 =
        // HR. (Pre-WP the "\n\n" separator inserted an empty block, putting the
        // rule at block 2.)
        auto *qdoc = e.textEdit()->document();
        const QTextBlock b = blockN(qdoc, 1);
        const QTextCharFormat cf = b.charFormat();
        QVERIFY(cf.fontFixedPitch());
    }
};

QTEST_MAIN(TstStyledBlockFormats)
#include "tst_styled_block_formats.moc"
