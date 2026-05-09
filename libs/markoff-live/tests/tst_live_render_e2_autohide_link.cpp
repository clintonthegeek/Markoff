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

class TestE2AutohideLink : public QObject {
    Q_OBJECT
private slots:
    void link_atomic_reveal_caret_in_display_text() {
        QTextDocument doc;
        doc.setPlainText("see [text](https://x) end");
        // "[" 4; "text" 5-8; "](" 9-10; "https://x" 11-19; ")" 20
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto link = [](SourceSpan &s){ s.isLink = true; };
        const int parentStart = 4, parentEnd = 20;
        h.setInlineSpans({
            delimiterSpan(4,  1, parentStart, parentEnd, link),   // [
            contentSpan  (5,  4,                          link),   // text
            delimiterSpan(9,  2, parentStart, parentEnd, link),   // ](
            delimiterSpan(11, 9, parentStart, parentEnd, link),   // url
            delimiterSpan(20, 1, parentStart, parentEnd, link),   // )
        });
        h.setLocalCaretPosition(7);  // inside "text"
        QTRY_VERIFY(!isHidden(formatAt(&doc, 4)));   // [
        QTRY_VERIFY(!isHidden(formatAt(&doc, 9)));   // ]
        QTRY_VERIFY(!isHidden(formatAt(&doc, 11)));  // url start
        QTRY_VERIFY(!isHidden(formatAt(&doc, 20)));  // )
    }

    void link_atomic_hide_caret_outside() {
        QTextDocument doc;
        doc.setPlainText("see [text](https://x) end");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto link = [](SourceSpan &s){ s.isLink = true; };
        const int parentStart = 4, parentEnd = 20;
        h.setInlineSpans({
            delimiterSpan(4,  1, parentStart, parentEnd, link),
            contentSpan  (5,  4,                          link),
            delimiterSpan(9,  2, parentStart, parentEnd, link),
            delimiterSpan(11, 9, parentStart, parentEnd, link),
            delimiterSpan(20, 1, parentStart, parentEnd, link),
        });
        h.setLocalCaretPosition(0);  // "see " — outside
        QTRY_VERIFY(isHidden(formatAt(&doc, 4)));   // [
        QTRY_VERIFY(isHidden(formatAt(&doc, 9)));   // ]
        QTRY_VERIFY(isHidden(formatAt(&doc, 11)));  // url
        QTRY_VERIFY(isHidden(formatAt(&doc, 20)));  // )
    }
};

QTEST_MAIN(TestE2AutohideLink)
#include "tst_live_render_e2_autohide_link.moc"
