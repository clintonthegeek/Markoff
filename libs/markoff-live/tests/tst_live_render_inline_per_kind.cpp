// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Per-flag inline-format-highlighter tests.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>

using namespace Markoff::Live;

// Helper: find first format range where pred(fmt) is true.
// QSyntaxHighlighter stores formats in QTextBlock::layout()->formats(), not in
// QTextFragment::charFormat(), so we read from the layout format ranges.
template <class Pred>
static QPair<int,int> findFormatRange(const QTextDocument &doc, Pred pred) {
    int start = -1, end = -1;
    QTextBlock block = doc.firstBlock();
    while (block.isValid()) {
        const int blockPos = block.position();
        const auto layoutFmts = block.layout()->formats();
        for (const QTextLayout::FormatRange &fr : layoutFmts) {
            if (pred(fr.format)) {
                if (start < 0) start = blockPos + fr.start;
                end = blockPos + fr.start + fr.length;
            }
        }
        block = block.next();
    }
    if (start < 0) return {-1, 0};
    return {start, end - start};
}

class TstLiveRenderInlinePerKind : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void empty_spans_no_paint() {
        QTextDocument doc;
        doc.setPlainText("plain text");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        h.setInlineSpans({});
        auto bold = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontWeight() == QFont::Bold;
        });
        QCOMPARE(bold.first, -1);
    }

    void bold_flag_paints_bold_weight() {
        QTextDocument doc;
        doc.setPlainText("plain **bold** plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        // Ensure BoldEmphasis is configured to be bold in defaultLight.
        QVERIFY(theme.isBold(Markoff::Theme::Slot::BoldEmphasis)
                || theme.color(Markoff::Theme::Slot::BoldEmphasis).isValid());

        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 8;  // covers **bold**
        span.bold = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontWeight() == QFont::Bold;
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 8);
    }
};

QTEST_MAIN(TstLiveRenderInlinePerKind)
#include "tst_live_render_inline_per_kind.moc"
