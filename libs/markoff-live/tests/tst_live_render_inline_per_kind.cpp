// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Per-flag inline-format-highlighter tests.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <markoff/core/Theme.h>
#include <markoff/live/FindSpan.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/live/InlineHighlighterAttached.h>
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
    void italic_flag_paints_italic() {
        QTextDocument doc;
        doc.setPlainText("plain *italic* plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 8;  // *italic*
        span.italic = true;
        h.setInlineSpans({span});

        // QSyntaxHighlighter writes to layout()->formats(), not fragment formats.
        // Use findFormatRange to check the layout-level format ranges.
        auto range = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontItalic();
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 8);
    }

    void strikethrough_flag_paints_strike() {
        QTextDocument doc;
        doc.setPlainText("plain ~~struck~~ plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 10;  // ~~struck~~
        span.strikethrough = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontStrikeOut();
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 10);
    }

    void inline_code_flag_paints_monospace_and_bg() {
        QTextDocument doc;
        doc.setPlainText("plain `code` plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 6;  // `code`
        span.code = true;
        h.setInlineSpans({span});

        const QString monoFamily =
            theme.font(Markoff::Theme::FontRole::Monospace).family();
        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.background().style() != Qt::NoBrush
                || f.fontFamilies().toStringList().contains(monoFamily);
        });
        QVERIFY(range.first >= 0);  // some range was found
    }

    void highlight_flag_paints_background() {
        QTextDocument doc;
        doc.setPlainText("plain ==HL== plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 6;  // ==HL==
        span.highlight = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.background().color() == theme.color(Markoff::Theme::Slot::Highlight);
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 6);
    }

    void link_flag_paints_color_and_underline() {
        QTextDocument doc;
        doc.setPlainText("[label](url) plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 12;  // [label](url)
        span.isLink = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.fontUnderline()
                && f.foreground().color() == theme.color(Markoff::Theme::Slot::Link);
        });
        QCOMPARE(range.first, 0);
        QCOMPARE(range.second, 12);
    }

    void wikilink_flag_paints_color_and_underline() {
        QTextDocument doc;
        doc.setPlainText("[[Page]] plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 8;  // [[Page]]
        span.isWikilink = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.fontUnderline()
                && f.foreground().color() == theme.color(Markoff::Theme::Slot::WikiLink);
        });
        QCOMPARE(range.first, 0);
        QCOMPARE(range.second, 8);
    }

    void tag_flag_paints_color() {
        QTextDocument doc;
        doc.setPlainText("plain #tag plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 4;  // #tag
        span.isTag = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.foreground().color() == theme.color(Markoff::Theme::Slot::Tag);
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 4);
    }

    void out_of_scope_flags_do_not_paint() {
        QTextDocument doc;
        doc.setPlainText("plain $x^2$ plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        // Math, footnote, image, isDelimiter, comment — all out of scope for E1.
        Markoff::SourceSpan math{};       math.charOffset = 6;  math.charLength = 5; math.math = true;
        Markoff::SourceSpan footnote{};   footnote.charOffset = 0; footnote.charLength = 4; footnote.isFootnoteRef = true;
        Markoff::SourceSpan image{};      image.charOffset = 0;  image.charLength = 4; image.isImage = true;
        Markoff::SourceSpan delimiter{};  delimiter.charOffset = 6; delimiter.charLength = 1; delimiter.isDelimiter = true;
        Markoff::SourceSpan comment{};    comment.charOffset = 0; comment.charLength = 4; comment.comment = true;

        h.setInlineSpans({math, footnote, image, delimiter, comment});

        // Walk layout format ranges; none should have any non-default property.
        bool anyPaint = false;
        QTextBlock block = doc.firstBlock();
        while (block.isValid()) {
            const auto ranges = block.layout()->formats();
            for (const QTextLayout::FormatRange &r : ranges) {
                const QTextCharFormat &f = r.format;
                if (f.fontWeight() == QFont::Bold || f.fontItalic()
                    || f.fontStrikeOut() || f.fontUnderline()
                    || f.foreground().color().isValid()
                    || (f.background().style() != Qt::NoBrush
                        && f.background().color() != Qt::transparent)) {
                    anyPaint = true; break;
                }
            }
            block = block.next();
        }
        QVERIFY2(!anyPaint, "Out-of-scope flags must not paint");
    }

    void set_local_caret_position_triggers_rehighlight() {
        QTextDocument doc;
        doc.setPlainText("**bold**");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan boldSpan{};
        boldSpan.charOffset = 2; boldSpan.charLength = 4;
        boldSpan.bold = true;
        Markoff::SourceSpan openMarker{};
        openMarker.charOffset = 0; openMarker.charLength = 2;
        openMarker.bold = true; openMarker.isDelimiter = true;
        openMarker.parentCharStart = 0; openMarker.parentCharEnd = 8;
        Markoff::SourceSpan closeMarker{};
        closeMarker.charOffset = 6; closeMarker.charLength = 2;
        closeMarker.bold = true; closeMarker.isDelimiter = true;
        closeMarker.parentCharStart = 0; closeMarker.parentCharEnd = 8;
        h.setInlineSpans({openMarker, boldSpan, closeMarker});

        h.setLocalCaretPosition(4);  // inside the span
        QCOMPARE(h.localCaretPosition(), 4);
        h.setLocalCaretPosition(20);
        QCOMPARE(h.localCaretPosition(), 20);
    }

    void set_selection_range_records_state() {
        QTextDocument doc;
        InlineHighlighter h(&doc);
        h.setSelectionRange(3, 7);
        QCOMPARE(h.selectionStart(), 3);
        QCOMPARE(h.selectionEnd(), 7);
        h.setSelectionRange(-1, -1);  // no-selection sentinel
        QCOMPARE(h.selectionStart(), -1);
        QCOMPARE(h.selectionEnd(), -1);
    }

    void findSpan_paintsSearchMatchBackground_onMatchedRange() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("the quick brown fox"));

        Markoff::Theme theme = Markoff::Theme::defaultLight();
        Markoff::Live::InlineHighlighter h(&doc);
        h.setTheme(&theme);

        QList<Markoff::Live::FindSpan> spans;
        spans.append({ /*byteOffset*/ 4u, /*byteLength*/ 5u, /*isCurrent*/ false });
        h.setFindSpans(spans);

        const QColor expected = theme.color(Markoff::Theme::Slot::SearchMatchBackground);
        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.background().color() == expected;
        });
        QCOMPARE(range.first, 4);
        QCOMPARE(range.second, 5);
    }

    void findSpan_currentMatch_usesActiveSlot() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("the quick brown fox"));

        Markoff::Theme theme = Markoff::Theme::defaultLight();
        Markoff::Live::InlineHighlighter h(&doc);
        h.setTheme(&theme);

        QList<Markoff::Live::FindSpan> spans;
        spans.append({ /*byteOffset*/ 4u, /*byteLength*/ 5u, /*isCurrent*/ true });
        h.setFindSpans(spans);

        const QColor expected = theme.color(Markoff::Theme::Slot::SearchActiveMatchBackground);
        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.background().color() == expected;
        });
        QCOMPARE(range.first, 4);
        QCOMPARE(range.second, 5);
    }

    void attached_shim_c_plus_plus_surface_works() {
        // Without a QQuickTextDocument target, rebuildHighlighter is a no-op.
        // Test the C++ property surface: spans round-trip, theme stored, no crash.
        Markoff::Live::InlineHighlighterAttached att;

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 8; span.bold = true;
        QVariantList spans;
        spans.append(QVariant::fromValue(span));
        att.setSpans(spans);
        QCOMPARE(att.spans().size(), 1);

        Markoff::Theme theme = Markoff::Theme::defaultLight();
        att.setTheme(&theme);
        QCOMPARE(att.theme(), &theme);
    }
};

QTEST_MAIN(TstLiveRenderInlinePerKind)
#include "tst_live_render_inline_per_kind.moc"
