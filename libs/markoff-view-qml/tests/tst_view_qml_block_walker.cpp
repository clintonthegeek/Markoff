// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "../src/BlockWalker.h"
#include <markoff/view/qml/BlockKind.h>

using namespace Markoff::View::Qml;

class TstBlockWalker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_input_returns_empty_list() {
        QCOMPARE(BlockWalker::walk(QString()).size(), 0);
    }

    void single_paragraph_one_block() {
        const QString src = QStringLiteral("Hello world.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[0].text.trimmed(), QStringLiteral("Hello world."));
    }

    void two_paragraphs_separated_by_blank_line() {
        const QString src = QStringLiteral("First.\n\nSecond.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[1].kind, BlockKind::Paragraph);
    }

    void heading_level_one() {
        const QString src = QStringLiteral("# Title\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Heading);
        QCOMPARE(blocks[0].headingLevel, 1);
        QCOMPARE(blocks[0].text, QStringLiteral("Title"));
    }

    void heading_level_three() {
        const QString src = QStringLiteral("### Sub\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Heading);
        QCOMPARE(blocks[0].headingLevel, 3);
    }

    void horizontal_rule_dashes() {
        const QString src = QStringLiteral("---\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::HorizontalRule);
    }

    void horizontal_rule_asterisks() {
        const QString src = QStringLiteral("***\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::HorizontalRule);
    }

    void image_block() {
        const QString src = QStringLiteral("![Alt text](http://example.com/img.png)\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Image);
        QCOMPARE(blocks[0].imageAlt, QStringLiteral("Alt text"));
        QCOMPARE(blocks[0].imageSrc, QStringLiteral("http://example.com/img.png"));
    }

    void image_with_title() {
        const QString src = QStringLiteral("![A](u \"title here\")\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Image);
        QCOMPARE(blocks[0].imageTitle, QStringLiteral("title here"));
    }

    void fenced_code_block_with_language() {
        const QString src = QStringLiteral("```python\nprint('hi')\n```\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[0].codeLanguage, QStringLiteral("python"));
        QCOMPARE(blocks[0].codeText, QStringLiteral("print('hi')\n"));
    }

    void fenced_code_block_no_language() {
        const QString src = QStringLiteral("```\nplain\n```\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[0].codeLanguage, QString());
        QCOMPARE(blocks[0].codeText, QStringLiteral("plain\n"));
    }

    void code_block_inside_does_not_split_on_blank_lines() {
        const QString src = QStringLiteral("```\nline1\n\nline2\n```\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[0].codeText, QStringLiteral("line1\n\nline2\n"));
    }

    void mixed_doc_paragraph_heading_hr_image_codeblock_paragraph() {
        const QString src = QStringLiteral(
            "First paragraph.\n\n"
            "# Heading\n\n"
            "---\n\n"
            "![alt](u.png)\n\n"
            "```rust\nfn main() {}\n```\n\n"
            "Last paragraph.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 6);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[1].kind, BlockKind::Heading);
        QCOMPARE(blocks[2].kind, BlockKind::HorizontalRule);
        QCOMPARE(blocks[3].kind, BlockKind::Image);
        QCOMPARE(blocks[4].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[5].kind, BlockKind::Paragraph);
    }

    void unterminated_code_block_treated_as_codeblock_to_eof() {
        const QString src = QStringLiteral("```python\nprint(1)\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
    }

    void leading_blank_lines_ignored() {
        const QString src = QStringLiteral("\n\n\nHello.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
    }
};

QTEST_APPLESS_MAIN(TstBlockWalker)
#include "tst_view_qml_block_walker.moc"
