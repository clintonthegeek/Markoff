// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextBlock>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>

using namespace Markoff;

// Read the fontPointSize of the first highlighted format range in the document.
// QSyntaxHighlighter stores formats in block.layout()->formats(), not in
// QTextFragment::charFormat().
static qreal firstHighlightedPointSize(const QTextDocument &doc) {
    QTextBlock block = doc.firstBlock();
    while (block.isValid()) {
        const auto fmts = block.layout()->formats();
        for (const QTextLayout::FormatRange &fr : fmts) {
            if (fr.length > 0)
                return fr.format.fontPointSize();
        }
        block = block.next();
    }
    return -1.0;
}

class TstInlineHighlighterFontScale : public QObject {
    Q_OBJECT
private slots:
    void code_span_pt_size_scales_with_font_scale() {
        Theme t = Theme::defaultLight();
        QTextDocument doc;
        doc.setPlainText("foo bar baz");
        Live::InlineHighlighter hl(&doc);
        hl.setTheme(&t);

        SourceSpan codeSpan;
        codeSpan.charOffset = 0;
        codeSpan.charLength = 3;
        codeSpan.code       = true;
        hl.setInlineSpans({codeSpan});

        const qreal basePt = firstHighlightedPointSize(doc);
        QVERIFY(basePt > 0);

        hl.setFontScale(2.0);
        const qreal scaledPt = firstHighlightedPointSize(doc);
        QCOMPARE(scaledPt, basePt * 2.0);
    }

    void set_font_scale_triggers_rehighlight() {
        Theme t = Theme::defaultLight();
        QTextDocument doc;
        doc.setPlainText("hello");
        Live::InlineHighlighter hl(&doc);
        hl.setTheme(&t);
        SourceSpan code;
        code.charOffset = 0; code.charLength = 5; code.code = true;
        hl.setInlineSpans({code});

        const qreal beforePt = firstHighlightedPointSize(doc);
        QVERIFY(beforePt > 0);
        hl.setFontScale(1.5);
        const qreal afterPt = firstHighlightedPointSize(doc);
        QCOMPARE(afterPt, beforePt * 1.5);
    }

    void set_same_font_scale_is_a_noop() {
        Theme t = Theme::defaultLight();
        QTextDocument doc;
        doc.setPlainText("x");
        Live::InlineHighlighter hl(&doc);
        hl.setTheme(&t);
        hl.setFontScale(1.5);
        hl.setFontScale(1.5);  // must not crash, must keep state
        QCOMPARE(hl.fontScale(), 1.5);
    }
};

QTEST_GUILESS_MAIN(TstInlineHighlighterFontScale)
#include "tst_live_render_inline_highlighter_font_scale.moc"
