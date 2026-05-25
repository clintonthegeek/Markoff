// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include "E2TestHelpers.h"

using namespace Markoff;
using Markoff::Live::InlineHighlighter;
using namespace E2Test;

class TestE2AutohideSelection : public QObject {
    Q_OBJECT
private slots:
    void selection_touching_span_reveals_markers() {
        QTextDocument doc;
        doc.setPlainText("a **b** c");
        // "a " 0-1; "**" 2-3; "b" 4; "**" 5-6; " c" 7-8
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        h.setLocalCaretPosition(-1);
        h.setSelectionRange(0, 4);   // selection from start to inside span
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));  // first '**' revealed
        QTRY_VERIFY(!isHidden(formatAt(&doc, 5)));  // closing '**' revealed
    }

    void selection_fully_outside_does_not_reveal() {
        QTextDocument doc;
        doc.setPlainText("a **b** c");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        h.setLocalCaretPosition(-1);
        h.setSelectionRange(7, 9);  // " c" — fully past span
        QTRY_VERIFY(isHidden(formatAt(&doc, 2)));
    }

    void selection_exactly_touches_parent_boundary_reveals() {
        QTextDocument doc;
        doc.setPlainText("a **b** c");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        h.setLocalCaretPosition(-1);
        // Selection endpoint exactly at parentCharStart — should reveal (endpoint-touch).
        h.setSelectionRange(0, 2);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));
    }
};

QTEST_MAIN(TestE2AutohideSelection)
#include "tst_live_render_e2_autohide_selection.moc"
