// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>

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
        const qreal h1 = blockN(qdoc, 0).charFormat().fontPointSize();
        const qreal h2 = blockN(qdoc, 2).charFormat().fontPointSize();
        const qreal h3 = blockN(qdoc, 4).charFormat().fontPointSize();
        const qreal h4 = blockN(qdoc, 6).charFormat().fontPointSize();
        const qreal h5 = blockN(qdoc, 8).charFormat().fontPointSize();
        const qreal h6 = blockN(qdoc, 10).charFormat().fontPointSize();
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

    void list_item_has_left_margin() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- first item"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        QVERIFY(b.blockFormat().leftMargin() > 0);
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

        // Document has 2 MarkoffDocument blocks: Paragraph + HorizontalRule.
        // The QTextDocument uses flatView (blocks joined by "\n\n"), giving:
        // block 0 = "before", block 1 = "\n" (separator), block 2 = "---".
        auto *qdoc = e.textEdit()->document();
        const QTextBlock b = blockN(qdoc, 2);
        const QTextCharFormat cf = b.charFormat();
        QVERIFY(cf.fontFixedPitch() || cf.fontFamilies().toStringList().size() > 0);
    }
};

QTEST_MAIN(TstStyledBlockFormats)
#include "tst_styled_block_formats.moc"
