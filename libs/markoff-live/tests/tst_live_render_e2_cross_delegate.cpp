// SPDX-License-Identifier: GPL-3.0-or-later
//
// E2 C4: Cross-delegate caret isolation test.
// Verifies that only the focused delegate reveals its markers;
// unfocused delegates (caretPosition=-1) keep markers hidden.

#include <QTest>
#include <QTextDocument>
#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include "E2TestHelpers.h"

using namespace Markoff;
using Markoff::Live::InlineHighlighter;
using namespace E2Test;

class TestE2CrossDelegate : public QObject {
    Q_OBJECT
private slots:

    // C4 slot 1: caret in one delegate reveals its markers; unfocused
    // delegate (caret=-1) keeps its markers hidden.
    void caret_in_one_delegate_hides_markers_in_other() {
        QTextDocument doc1;
        QTextDocument doc2;
        doc1.setPlainText("**bold**");
        doc2.setPlainText("**also bold**");
        Theme theme;

        InlineHighlighter h1(&doc1);
        InlineHighlighter h2(&doc2);
        h1.setTheme(&theme);
        h2.setTheme(&theme);

        auto bold = [](SourceSpan &s){ s.bold = true; };
        // "**bold**": delimiters at 0-1 and 6-7, content at 2-5
        h1.setInlineSpans({
            delimiterSpan(0, 2, 0, 8, bold),
            contentSpan  (2, 4,        bold),
            delimiterSpan(6, 2, 0, 8, bold),
        });
        // "**also bold**": delimiters at 0-1 and 11-12, content at 2-10
        h2.setInlineSpans({
            delimiterSpan(0,  2, 0, 13, bold),
            contentSpan  (2,  9,         bold),
            delimiterSpan(11, 2, 0, 13, bold),
        });

        // Delegate 1 has focus: caret inside span → markers revealed.
        h1.setLocalCaretPosition(4);
        // Delegate 2 is unfocused: caret=-1 → markers hidden.
        h2.setLocalCaretPosition(-1);

        QTRY_VERIFY(!isHidden(formatAt(&doc1, 0)));  // delegate 1 opening ** revealed
        QTRY_VERIFY(!isHidden(formatAt(&doc1, 6)));  // delegate 1 closing ** revealed
        QTRY_VERIFY(isHidden(formatAt(&doc2, 0)));   // delegate 2 opening ** hidden
        QTRY_VERIFY(isHidden(formatAt(&doc2, 11)));  // delegate 2 closing ** hidden
    }

    // C4 slot 2: setting caretPosition=-1 hides markers in the previously
    // focused delegate (simulates focus leaving).
    void caret_minus_one_hides_markers() {
        QTextDocument doc;
        doc.setPlainText("**bold**");
        Theme theme;

        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(0, 2, 0, 8, bold),
            contentSpan  (2, 4,        bold),
            delimiterSpan(6, 2, 0, 8, bold),
        });

        // Focus in — markers appear.
        h.setLocalCaretPosition(4);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));

        // Focus out — markers hide.
        h.setLocalCaretPosition(-1);
        QTRY_VERIFY(isHidden(formatAt(&doc, 0)));
        QTRY_VERIFY(isHidden(formatAt(&doc, 6)));
    }
};

QTEST_MAIN(TestE2CrossDelegate)
#include "tst_live_render_e2_cross_delegate.moc"
