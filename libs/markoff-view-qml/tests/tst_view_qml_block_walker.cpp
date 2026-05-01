// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "../src/BlockWalker.h"
#include <markoff/view/qml/BlockKind.h>
#include <markoff-parser/Document.h>

using namespace Markoff::View::Qml;

/// Stage C-2 rewrite: BlockWalker now consumes a parsed
/// `Markoff::Document` (the foundation's tree-sitter snapshot) instead of
/// regex-walking a source string. Records carry source-faithful text.
class TstBlockWalker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void null_document_returns_empty_list() {
        QCOMPARE(BlockWalker::walk(nullptr).size(), 0);
    }

    void empty_document_returns_empty_list() {
        auto doc = Markoff::Document::fromMarkdown(QString());
        QCOMPARE(BlockWalker::walk(doc.get()).size(), 0);
    }

    void single_paragraph_one_block() {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("Hello world.\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        // Source-faithful: includes the trailing newline that's part of
        // the paragraph block's byte range in the body.
        QCOMPARE(blocks[0].text.trimmed(), QStringLiteral("Hello world."));
        // Source-faithful invariant: source == text.
        QCOMPARE(blocks[0].source, blocks[0].text);
    }

    void two_paragraphs_separated_by_blank_line() {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("First.\n\nSecond.\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[1].kind, BlockKind::Paragraph);
    }

    void heading_text_is_source_faithful_includes_marker() {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("# Title\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Heading);
        QCOMPARE(blocks[0].headingLevel, 1);
        // Stage C-2/C-3: text includes the "# " marker (source-faithful).
        QVERIFY2(blocks[0].text.contains(QStringLiteral("# Title")),
                 qPrintable(blocks[0].text));
        QCOMPARE(blocks[0].source, blocks[0].text);
    }

    void heading_level_three_marker_count() {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("### Sub\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Heading);
        QCOMPARE(blocks[0].headingLevel, 3);
        QVERIFY(blocks[0].text.startsWith(QStringLiteral("### ")));
    }

    void thematic_break_maps_to_horizontal_rule() {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("---\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::HorizontalRule);
    }

    void fenced_code_block_text_includes_fences() {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("```python\nprint('hi')\n```\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[0].codeLanguage, QStringLiteral("python"));
        // Source-faithful: fences are part of the text the delegate renders.
        QVERIFY(blocks[0].text.contains(QStringLiteral("```python")));
        QVERIFY(blocks[0].text.contains(QStringLiteral("print('hi')")));
        QCOMPARE(blocks[0].source, blocks[0].text);
    }

    void mixed_doc_kinds_in_order() {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
            "First paragraph.\n\n"
            "# Heading\n\n"
            "---\n\n"
            "```rust\nfn main() {}\n```\n\n"
            "Last paragraph.\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 5);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[1].kind, BlockKind::Heading);
        QCOMPARE(blocks[2].kind, BlockKind::HorizontalRule);
        QCOMPARE(blocks[3].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[4].kind, BlockKind::Paragraph);
    }

    void leading_blank_lines_ignored() {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("\n\n\nHello.\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
    }

    /// Setext headings (text underlined with === or ---) map to Heading.
    void setext_heading_maps_to_heading() {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("Title\n=====\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Heading);
    }

    /// UTF-8 multi-byte chars in block text are translated correctly to
    /// QString character offsets.
    void utf8_block_text_is_translated_correctly() {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("Héllo wörld émoji.\n"));
        const auto blocks = BlockWalker::walk(doc.get());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].text.trimmed(), QStringLiteral("Héllo wörld émoji."));
    }
};

QTEST_APPLESS_MAIN(TstBlockWalker)
#include "tst_view_qml_block_walker.moc"
