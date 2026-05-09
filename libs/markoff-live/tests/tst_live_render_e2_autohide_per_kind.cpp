// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>
#include "E2TestHelpers.h"

using namespace Markoff;
using Markoff::Live::InlineHighlighter;
using namespace E2Test;

class TestE2AutohidePerKind : public QObject {
    Q_OBJECT
private slots:
    void bold_markers_hidden_when_caret_outside_span() {
        QTextDocument doc;
        doc.setPlainText("Some **bold** here");
        // "Some " = 0-4; "**" = 5-6; "bold" = 7-10; "**" = 11-12; " here" = 13-17
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(5,  2, 5, 12, bold),
            contentSpan  (7,  4,         bold),
            delimiterSpan(11, 2, 5, 12, bold),
        });
        h.setLocalCaretPosition(0);  // "Some " — outside span
        QTRY_VERIFY(isHidden(formatAt(&doc, 5)));   // first '*'
        QTRY_VERIFY(isHidden(formatAt(&doc, 6)));   // second '*'
        QTRY_VERIFY(isHidden(formatAt(&doc, 11)));  // third '*'
        QTRY_VERIFY(isHidden(formatAt(&doc, 12)));  // fourth '*'
    }

    void bold_markers_revealed_when_caret_inside_span() {
        QTextDocument doc;
        doc.setPlainText("Some **bold** here");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(5,  2, 5, 12, bold),
            contentSpan  (7,  4,         bold),
            delimiterSpan(11, 2, 5, 12, bold),
        });
        h.setLocalCaretPosition(8);  // inside the bold content
        QTRY_VERIFY(!isHidden(formatAt(&doc, 5)));
        QTRY_VERIFY(!isHidden(formatAt(&doc, 6)));
        QTRY_VERIFY(!isHidden(formatAt(&doc, 11)));
        QTRY_VERIFY(!isHidden(formatAt(&doc, 12)));
    }

    void italic_strike_highlight_code_follow_same_predicate() {
        struct Case { const char *name; void (*setKind)(SourceSpan &); QString text; int markerLen; };
        const Case cases[] = {
            {"italic",    [](SourceSpan &s){ s.italic = true; },       QStringLiteral("_x_"),   1},
            {"strike",    [](SourceSpan &s){ s.strikethrough = true; }, QStringLiteral("~~x~~"), 2},
            {"highlight", [](SourceSpan &s){ s.highlight = true; },    QStringLiteral("==x=="), 2},
            {"code",      [](SourceSpan &s){ s.code = true; },         QStringLiteral("`x`"),   1},
        };
        for (const Case &c : cases) {
            QTextDocument doc;
            doc.setPlainText(c.text);
            Theme theme;
            InlineHighlighter h(&doc);
            h.setTheme(&theme);
            const int contentStart = c.markerLen;
            const int parentEnd = c.text.length();
            h.setInlineSpans({
                delimiterSpan(0, c.markerLen, 0, parentEnd, c.setKind),
                contentSpan  (contentStart, 1, c.setKind),
                delimiterSpan(contentStart + 1, c.markerLen, 0, parentEnd, c.setKind),
            });
            h.setLocalCaretPosition(-1);  // no local caret
            QTRY_VERIFY2(isHidden(formatAt(&doc, 0)),
                         qPrintable(QStringLiteral("%1 first marker").arg(c.name)));
            h.setLocalCaretPosition(contentStart);
            QTRY_VERIFY2(!isHidden(formatAt(&doc, 0)),
                         qPrintable(QStringLiteral("%1 first marker after caret-in").arg(c.name)));
        }
    }
};

QTEST_MAIN(TestE2AutohidePerKind)
#include "tst_live_render_e2_autohide_per_kind.moc"
