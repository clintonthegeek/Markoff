// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Cross-delegate sanity tests for InlineHighlighter.

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

class TstLiveRenderInlineCrossDelegate : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void bold_in_heading_renders_bold() {
        QTextDocument doc;
        doc.setPlainText("Heading with **bold** word");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 13; span.charLength = 8;
        span.bold = true;
        h.setInlineSpans({span});

        QCOMPARE(formatAt(doc, 15).fontWeight(), int(QFont::Bold));
    }

    void italic_in_blockquote_renders_italic() {
        QTextDocument doc;
        doc.setPlainText("> A *quoted* note");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 4; span.charLength = 8;
        span.italic = true;
        h.setInlineSpans({span});

        QVERIFY(formatAt(doc, 6).fontItalic());
    }

    void inline_code_in_list_item_renders_bg() {
        QTextDocument doc;
        doc.setPlainText("- item with `code` inside");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 12; span.charLength = 6;
        span.code = true;
        h.setInlineSpans({span});

        QVERIFY(formatAt(doc, 14).background().style() != Qt::NoBrush);
    }

    void link_in_heading_renders_link_color_and_underline() {
        QTextDocument doc;
        doc.setPlainText("Heading [target](url)");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 8; span.charLength = 13;
        span.isLink = true;
        h.setInlineSpans({span});

        const QTextCharFormat f = formatAt(doc, 10);
        QVERIFY(f.fontUnderline());
        QCOMPARE(f.foreground().color(), theme.color(Markoff::Theme::Slot::Link));
    }

    void tag_in_blockquote_renders_tag_color() {
        QTextDocument doc;
        doc.setPlainText("> Quoted with #tag");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 14; span.charLength = 4;
        span.isTag = true;
        h.setInlineSpans({span});

        QCOMPARE(formatAt(doc, 15).foreground().color(),
                 theme.color(Markoff::Theme::Slot::Tag));
    }

    void highlight_in_list_item_renders_bg() {
        QTextDocument doc;
        doc.setPlainText("- ==important== item");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 2; span.charLength = 13;
        span.highlight = true;
        h.setInlineSpans({span});

        QCOMPARE(formatAt(doc, 5).background().color(),
                 theme.color(Markoff::Theme::Slot::Highlight));
    }
};

QTEST_MAIN(TstLiveRenderInlineCrossDelegate)
#include "tst_live_render_inline_cross_delegate.moc"
