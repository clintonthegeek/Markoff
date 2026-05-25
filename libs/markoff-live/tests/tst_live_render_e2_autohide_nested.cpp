// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include "E2TestHelpers.h"

using namespace Markoff;
using Markoff::Live::InlineHighlighter;
using namespace E2Test;

class TestE2AutohideNested : public QObject {
    Q_OBJECT
private slots:
    void caret_in_italic_inside_bold_reveals_both() {
        QTextDocument doc;
        doc.setPlainText("**a _b_ c**");
        // "**" 0-1; "a " 2-3; "_" 4; "b" 5; "_" 6; " c" 7-8; "**" 9-10
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold      = [](SourceSpan &s){ s.bold = true; };
        auto boldItalic= [](SourceSpan &s){ s.bold = true; s.italic = true; };
        auto italic    = [](SourceSpan &s){ s.italic = true; };
        // Bold parent range: 0..10. Italic parent range: 4..6.
        h.setInlineSpans({
            delimiterSpan(0,  2, 0, 10, bold),
            contentSpan  (2,  2,        bold),
            delimiterSpan(4,  1, 4,  6, italic),
            contentSpan  (5,  1,        boldItalic),
            delimiterSpan(6,  1, 4,  6, italic),
            contentSpan  (7,  2,        bold),
            delimiterSpan(9,  2, 0, 10, bold),
        });
        h.setLocalCaretPosition(5);  // inside italic 'b'
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));   // ** opening
        QTRY_VERIFY(!isHidden(formatAt(&doc, 1)));   // ** opening
        QTRY_VERIFY(!isHidden(formatAt(&doc, 4)));   // _ opening
        QTRY_VERIFY(!isHidden(formatAt(&doc, 6)));   // _ closing
        QTRY_VERIFY(!isHidden(formatAt(&doc, 9)));   // ** closing
        QTRY_VERIFY(!isHidden(formatAt(&doc, 10)));  // ** closing
    }

    void caret_outside_hides_all_markers() {
        QTextDocument doc;
        doc.setPlainText("**a _b_ c**");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold      = [](SourceSpan &s){ s.bold = true; };
        auto boldItalic= [](SourceSpan &s){ s.bold = true; s.italic = true; };
        auto italic    = [](SourceSpan &s){ s.italic = true; };
        h.setInlineSpans({
            delimiterSpan(0,  2, 0, 10, bold),
            contentSpan  (2,  2,        bold),
            delimiterSpan(4,  1, 4,  6, italic),
            contentSpan  (5,  1,        boldItalic),
            delimiterSpan(6,  1, 4,  6, italic),
            contentSpan  (7,  2,        bold),
            delimiterSpan(9,  2, 0, 10, bold),
        });
        h.setLocalCaretPosition(-1);  // no local caret
        QTRY_VERIFY(isHidden(formatAt(&doc, 0)));   // ** hidden
        QTRY_VERIFY(isHidden(formatAt(&doc, 4)));   // _ hidden
        QTRY_VERIFY(isHidden(formatAt(&doc, 6)));   // _ hidden
        QTRY_VERIFY(isHidden(formatAt(&doc, 9)));   // ** hidden
    }
};

QTEST_MAIN(TestE2AutohideNested)
#include "tst_live_render_e2_autohide_nested.moc"
