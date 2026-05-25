// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Combined-flag tests for InlineHighlighter.

#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>

using namespace Markoff::Live;

// Helper: get the merged QTextCharFormat at a document position by
// combining all layout format ranges that cover that position.
static QTextCharFormat formatAt(const QTextDocument &doc, int pos)
{
    QTextBlock block = doc.findBlock(pos);
    if (!block.isValid()) return {};
    const int blockPos = block.position();
    const int localPos = pos - blockPos;
    QTextCharFormat merged;
    const auto ranges = block.layout()->formats();
    for (const QTextLayout::FormatRange &r : ranges) {
        if (localPos >= r.start && localPos < r.start + r.length) {
            // Merge: later ranges override earlier for the same property.
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

class TstLiveRenderInlineCombined : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void bold_and_italic_on_one_span() {
        QTextDocument doc;
        doc.setPlainText("***foo***");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 9;
        span.bold = true; span.italic = true;
        h.setInlineSpans({span});

        const QTextCharFormat f = formatAt(doc, 4);  // inside foo
        QCOMPARE(f.fontWeight(), int(QFont::Bold));
        QVERIFY(f.fontItalic());
    }

    void link_with_inner_bold_span() {
        QTextDocument doc;
        doc.setPlainText("[**foo**](url)");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan link{};
        link.charOffset = 0; link.charLength = 14;
        link.isLink = true;

        Markoff::SourceSpan bold{};
        bold.charOffset = 1; bold.charLength = 7;  // **foo**
        bold.bold = true;

        h.setInlineSpans({link, bold});

        // Position 4 (inside foo): both bold and underline.
        const QTextCharFormat f1 = formatAt(doc, 4);
        QCOMPARE(f1.fontWeight(), int(QFont::Bold));
        QVERIFY(f1.fontUnderline());

        // Position 12 (in "url", outside bold): underline but not bold.
        const QTextCharFormat f2 = formatAt(doc, 12);
        QVERIFY(f2.fontUnderline());
        QVERIFY(f2.fontWeight() != int(QFont::Bold));
    }

    void strikethrough_and_code_on_one_span() {
        QTextDocument doc;
        doc.setPlainText("~~`x`~~");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 7;
        span.strikethrough = true; span.code = true;
        h.setInlineSpans({span});

        const QTextCharFormat f = formatAt(doc, 3);  // on the 'x'
        QVERIFY(f.fontStrikeOut());
        QVERIFY(f.background().style() != Qt::NoBrush);
    }
};

QTEST_MAIN(TstLiveRenderInlineCombined)
#include "tst_live_render_inline_combined.moc"
