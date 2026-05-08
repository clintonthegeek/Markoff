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
};

QTEST_MAIN(TstLiveRenderInlineEdgeCases)
#include "tst_live_render_inline_edge_cases.moc"
