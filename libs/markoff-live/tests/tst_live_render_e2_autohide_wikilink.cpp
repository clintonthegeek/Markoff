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

class TestE2AutohideWikilink : public QObject {
    Q_OBJECT
private slots:
    void wikilink_plain_reveal_caret_in_page() {
        QTextDocument doc;
        doc.setPlainText("See [[My Page]] here");
        // "[[" 4-5; "My Page" 6-12; "]]" 13-14
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto wl = [](SourceSpan &s){ s.isWikilink = true; };
        const int pStart = 4, pEnd = 14;
        h.setInlineSpans({
            delimiterSpan(4,  2, pStart, pEnd, wl),  // [[
            contentSpan  (6,  7,                 wl),  // My Page
            delimiterSpan(13, 2, pStart, pEnd, wl),  // ]]
        });
        h.setLocalCaretPosition(9);  // inside page name
        QTRY_VERIFY(!isHidden(formatAt(&doc, 4)));   // [[
        QTRY_VERIFY(!isHidden(formatAt(&doc, 13)));  // ]]
    }

    void wikilink_hides_when_caret_outside() {
        QTextDocument doc;
        doc.setPlainText("See [[My Page]] here");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto wl = [](SourceSpan &s){ s.isWikilink = true; };
        const int pStart = 4, pEnd = 14;
        h.setInlineSpans({
            delimiterSpan(4,  2, pStart, pEnd, wl),
            contentSpan  (6,  7,                 wl),
            delimiterSpan(13, 2, pStart, pEnd, wl),
        });
        h.setLocalCaretPosition(0);
        QTRY_VERIFY(isHidden(formatAt(&doc, 4)));
        QTRY_VERIFY(isHidden(formatAt(&doc, 13)));
    }

    void wikilink_aliased_reveals_all_when_caret_in_alias() {
        QTextDocument doc;
        doc.setPlainText("[[Page|alias]]");
        // "[[" 0-1; "Page" 2-5; "|" 6; "alias" 7-11; "]]" 12-13
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto wl = [](SourceSpan &s){ s.isWikilink = true; };
        const int pStart = 0, pEnd = 13;
        h.setInlineSpans({
            delimiterSpan(0,  2, pStart, pEnd, wl),  // [[
            delimiterSpan(2,  4, pStart, pEnd, wl),  // Page (markup when alias present)
            delimiterSpan(6,  1, pStart, pEnd, wl),  // |
            contentSpan  (7,  5,                 wl),  // alias
            delimiterSpan(12, 2, pStart, pEnd, wl),  // ]]
        });
        h.setLocalCaretPosition(9);  // inside alias
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));  // [[
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));  // Page
        QTRY_VERIFY(!isHidden(formatAt(&doc, 6)));  // |
        QTRY_VERIFY(!isHidden(formatAt(&doc, 12))); // ]]
    }
};

QTEST_MAIN(TestE2AutohideWikilink)
#include "tst_live_render_e2_autohide_wikilink.moc"
