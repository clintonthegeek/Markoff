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

class TestE2CaretAdjacent : public QObject {
    Q_OBJECT
private slots:
    void caret_one_before_opening_reveals() {
        QTextDocument doc;
        doc.setPlainText("x **b** y");
        // "x " 0-1; "**" 2-3; "b" 4; "**" 5-6; " y" 7-8
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        // caret at parentCharStart - 1 = 1: should reveal
        h.setLocalCaretPosition(1);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));
        // caret at parentCharStart = 2: should reveal
        h.setLocalCaretPosition(2);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));
        // caret at 0: 2 chars before parentCharStart-1 = 1 → should hide
        h.setLocalCaretPosition(0);
        QTRY_VERIFY(isHidden(formatAt(&doc, 2)));
    }

    void caret_one_after_closing_reveals() {
        QTextDocument doc;
        doc.setPlainText("x **b** y");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        // caret at parentCharEnd + 1 = 7: should reveal
        h.setLocalCaretPosition(7);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 5)));
        // caret at parentCharEnd + 2 = 8: should hide
        h.setLocalCaretPosition(8);
        QTRY_VERIFY(isHidden(formatAt(&doc, 5)));
    }
};

QTEST_MAIN(TestE2CaretAdjacent)
#include "tst_live_render_e2_autohide_caret_adjacent.moc"
