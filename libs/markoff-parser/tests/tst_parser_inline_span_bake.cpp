// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-parser/Document.h>
#include <markoff-parser/SourceSpan.h>

using namespace Markoff;

class TstParserInlineSpanBake : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void empty_doc_has_no_blocks() {
        auto doc = Document::fromMarkdown("");
        QVERIFY(doc != nullptr);
        QVERIFY(doc->topLevelBlocks().isEmpty());
    }

    void single_paragraph_no_formatting_has_empty_or_minimal_spans() {
        auto doc = Document::fromMarkdown("hello world");
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, TopLevelBlock::Kind::Paragraph);
        // Spans may be empty (no formatting) or contain non-formatting
        // structural markers; we only assert offset bounds for any present.
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            QVERIFY(s.utf8Offset >= 0);
            QVERIFY(s.utf8Offset + s.utf8Length <= blocks[0].byteEnd - blocks[0].byteStart);
            QVERIFY(s.charOffset >= 0);
        }
    }

    void paragraph_with_bold_has_block_relative_bold_span() {
        auto doc = Document::fromMarkdown("**bold** trailing");
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 1);

        bool foundBold = false;
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            if (s.bold && !s.isDelimiter) {
                foundBold = true;
                // The bold content "bold" sits at chars [2, 6) of the block
                // ("**" delimiters + "bold" content). Block-relative offsets:
                QCOMPARE(s.charOffset, 2);
                QCOMPARE(s.charLength, 4);
                QVERIFY(s.utf8Offset >= 0);
            }
        }
        QVERIFY2(foundBold, "expected a bold non-delimiter span in the paragraph");
    }

    void multiple_paragraphs_each_have_their_own_spans() {
        auto doc = Document::fromMarkdown(
            "first **paragraph** here\n\nsecond *one* there");
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);

        // First paragraph contains a bold span; offsets relative to first block.
        bool firstHasBold = false;
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            if (s.bold && !s.isDelimiter) {
                firstHasBold = true;
                // "paragraph" sits at chars [8, 17) of "first **paragraph** here":
                //   "first " = 6, "**" = 2 → content starts at 8, length 9.
                QCOMPARE(s.charOffset, 8);
                QCOMPARE(s.charLength, 9);
            }
        }
        QVERIFY(firstHasBold);

        // Second paragraph contains an italic span; offsets relative to
        // *second* block. If the bake leaked offsets, this would be off
        // by sizeof(first paragraph) + 2 (the "\n\n").
        bool secondHasItalic = false;
        for (const SourceSpan &s : blocks[1].inlineSpans) {
            if (s.italic && !s.isDelimiter) {
                secondHasItalic = true;
                // "one" sits at chars [8, 11) of "second *one* there":
                //   "second " = 7, "*" = 1 → content starts at 8, length 3.
                QCOMPARE(s.charOffset, 8);
                QCOMPARE(s.charLength, 3);
            }
        }
        QVERIFY(secondHasItalic);
    }

    void heading_inline_spans_block_relative() {
        auto doc = Document::fromMarkdown("# title with **bold**\n");
        const auto blocks = doc->topLevelBlocks();
        QVERIFY(blocks.size() >= 1);
        QCOMPARE(blocks[0].kind, TopLevelBlock::Kind::AtxHeading);

        // The "# " prefix is part of the block's source, so the bold content
        // "bold" should sit at chars [15, 19) of "# title with **bold**":
        //   "# title with " = 13, "**" = 2 → content starts at 15, length 4.
        bool foundBold = false;
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            if (s.bold && !s.isDelimiter) {
                foundBold = true;
                QCOMPARE(s.charOffset, 15);
                QCOMPARE(s.charLength, 4);
            }
        }
        QVERIFY(foundBold);
    }

    void code_block_has_no_inline_spans_for_format() {
        // Fenced code block content shouldn't be inline-formatted.
        auto doc = Document::fromMarkdown("```\n**not bold**\n```\n");
        const auto blocks = doc->topLevelBlocks();
        QVERIFY(blocks.size() >= 1);
        QCOMPARE(blocks[0].kind, TopLevelBlock::Kind::FencedCodeBlock);

        // No bold-formatted non-delimiter span should appear; if any spans
        // are present, none should claim bold inline format on the
        // **not bold** content.
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            QVERIFY(!(s.bold && !s.isDelimiter));
        }
    }
};

QTEST_MAIN(TstParserInlineSpanBake)
#include "tst_parser_inline_span_bake.moc"
