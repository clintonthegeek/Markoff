// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Edge-case tests for InlineHighlighter.

#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>

using namespace Markoff::Live;

static QTextCharFormat formatAt(const QTextDocument &doc, int pos)
{
    QTextBlock block = doc.findBlock(pos);
    if (!block.isValid()) return {};
    const int localPos = pos - block.position();
    QTextCharFormat merged;
    for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
        if (localPos >= r.start && localPos < r.start + r.length) {
            if (r.format.hasProperty(QTextFormat::FontWeight))
                merged.setFontWeight(r.format.fontWeight());
            if (r.format.hasProperty(QTextFormat::FontItalic))
                merged.setFontItalic(r.format.fontItalic());
            if (r.format.hasProperty(QTextFormat::TextUnderlineStyle))
                merged.setFontUnderline(r.format.fontUnderline());
            if (r.format.hasProperty(QTextFormat::FontStrikeOut))
                merged.setFontStrikeOut(r.format.fontStrikeOut());
            if (r.format.hasProperty(QTextFormat::BackgroundBrush))
                merged.setBackground(r.format.background());
            if (r.format.hasProperty(QTextFormat::ForegroundBrush))
                merged.setForeground(r.format.foreground());
        }
    }
    return merged;
}

class TstLiveRenderInlineEdgeCases : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void span_at_start_of_block() {
        QTextDocument doc; doc.setPlainText("**bold** plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 0; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});
        QCOMPARE(formatAt(doc, 2).fontWeight(), int(QFont::Bold));
    }

    void span_at_end_of_block() {
        QTextDocument doc; doc.setPlainText("plain **bold**");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 6; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});
        QCOMPARE(formatAt(doc, 8).fontWeight(), int(QFont::Bold));
    }

    void span_covering_whole_block() {
        QTextDocument doc; doc.setPlainText("**all bold**");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 0; s.charLength = 12; s.bold = true;
        h.setInlineSpans({s});
        QCOMPARE(formatAt(doc, 6).fontWeight(), int(QFont::Bold));
    }

    void empty_span_no_paint_no_crash() {
        QTextDocument doc; doc.setPlainText("plain text");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 5; s.charLength = 0; s.bold = true;
        h.setInlineSpans({s});  // must not crash
        QVERIFY(formatAt(doc, 5).fontWeight() != int(QFont::Bold));
    }

    void no_theme_no_paint() {
        QTextDocument doc; doc.setPlainText("plain **bold** plain");
        InlineHighlighter h(&doc);  // no theme set
        Markoff::SourceSpan s{}; s.charOffset = 6; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});  // must not crash; must not paint
        QVERIFY(formatAt(doc, 8).fontWeight() != int(QFont::Bold));
    }

    void marker_spanning_list_item_text() {
        QTextDocument doc; doc.setPlainText("- **bold** in list");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 2; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});
        QCOMPARE(formatAt(doc, 4).fontWeight(), int(QFont::Bold));
    }

    void span_longer_than_doc_clamped_safely() {
        QTextDocument doc; doc.setPlainText("short");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 0; s.charLength = 1000; s.bold = true;
        h.setInlineSpans({s});  // QSyntaxHighlighter::setFormat clamps internally
        // Assert no crash + bold applied to the visible part.
        QCOMPARE(formatAt(doc, 2).fontWeight(), int(QFont::Bold));
    }

    void no_in_scope_flags_no_paint() {
        QTextDocument doc; doc.setPlainText("plain $x$ plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 6; s.charLength = 3; s.math = true;
        h.setInlineSpans({s});
        const QTextCharFormat f = formatAt(doc, 7);
        QVERIFY(!f.fontStrikeOut());
        QVERIFY(f.fontWeight() != int(QFont::Bold));
    }

    void multiline_paragraph_block_relative_offsets_apply_to_correct_line() {
        // Regression: a markoff CRDT block can span multiple lines (multi-line
        // paragraph — no blank line between lines). Qt splits the QTextDocument
        // on `\n` into multiple QTextBlocks, and QSyntaxHighlighter::highlightBlock
        // is called once per QTextBlock with line-relative setFormat indices.
        // Spans use *block-relative* offsets, so highlightBlock must translate
        // by subtracting currentBlock().position() before applying setFormat.
        // Without that, line-1's spans get applied to line 2's positions and
        // vice-versa, producing the "Statu** Draft, peing usereview" garbage
        // observed when rendering multi-line paragraphs.
        QTextDocument doc;
        doc.setPlainText("**Date:** 2026-04-28\n**Status:** Draft");
        // Sanity: Qt did split the document into two blocks at the `\n`.
        QCOMPARE(doc.blockCount(), 2);

        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        // Block-relative spans: line-1 "Date:" bold at 2..6 (len 5);
        // line-2 "Status:" bold at 23..29 (len 7); offset 21 = `*` of line-2's
        // opening bold delimiter.
        Markoff::SourceSpan dateBold{}; dateBold.charOffset = 2; dateBold.charLength = 5; dateBold.bold = true;
        Markoff::SourceSpan statusBold{}; statusBold.charOffset = 23; statusBold.charLength = 7; statusBold.bold = true;
        h.setInlineSpans({dateBold, statusBold});

        // Line 1 (positions 0..19 in document coords): "Date:" at doc pos 2..6 must be bold.
        QCOMPARE(formatAt(doc, 2).fontWeight(),  int(QFont::Bold));
        QCOMPARE(formatAt(doc, 6).fontWeight(),  int(QFont::Bold));

        // Line 1 at doc pos 9 (the space before " 2026-...") must NOT be bold.
        // Pre-fix bug: line-1's "Date:" span (offset 2 len 5) gets applied AGAIN
        // at line-2's positions 2..6 ("Statu") because highlightBlock for line 2
        // uses span.charOffset directly as a line-relative index.
        // Post-fix: line-2's positions 2..6 are NOT bold from line-1's span.
        // Doc position of line-2 char 2 = 21 (line-2 start) + 2 = 23.
        // But pre-fix bug would bold doc pos 23 from "Date:" span AND from
        // "Status" span — both would converge there. Test the pre-fix
        // misbehaviour at a position that is bold ONLY under the bug:
        // line-2 char 6 ("u" of "Status") = doc pos 27. Under correct behaviour
        // this IS bold (covered by statusBold span at offset 23 len 7).
        // The unambiguous discriminator: line-2 char 7 = doc pos 28 ("s" of
        // "Status:"). Under correct behaviour: bold (statusBold spans 23..29).
        // Under the bug: NOT bold (line-1's "Date:" at 2..6 misapplied at
        // line-2 chars 2..6 = doc 23..27, which doesn't reach 28; line-2's
        // own statusBold at off=23 misapplied as line-relative-23 which is
        // beyond the line's length so filtered out — leaving doc 28 unstyled).
        QCOMPARE(formatAt(doc, 28).fontWeight(), int(QFont::Bold));
    }
};

QTEST_MAIN(TstLiveRenderInlineEdgeCases)
#include "tst_live_render_inline_edge_cases.moc"
