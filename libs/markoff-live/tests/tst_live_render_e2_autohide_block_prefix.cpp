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

class TestE2AutohideBlockPrefix : public QObject {
    Q_OBJECT
private slots:
    // B6: Heading prefix
    void heading_prefix_hides_when_no_local_caret() {
        QTextDocument doc;
        doc.setPlainText("# My heading");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto heading = [](SourceSpan &s){ s.isHeading = true; s.headingLevel = 1; };
        h.setInlineSpans({
            delimiterSpan(0, 2, 0, 11, heading),  // "# "
        });
        h.setLocalCaretPosition(-1);
        QTRY_VERIFY(isHidden(formatAt(&doc, 0)));
        QTRY_VERIFY(isHidden(formatAt(&doc, 1)));
    }

    void heading_prefix_reveals_when_caret_anywhere_in_block() {
        QTextDocument doc;
        doc.setPlainText("# My heading");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto heading = [](SourceSpan &s){ s.isHeading = true; s.headingLevel = 1; };
        h.setInlineSpans({
            delimiterSpan(0, 2, 0, 11, heading),
        });
        h.setLocalCaretPosition(8);  // inside "heading"
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
        h.setLocalCaretPosition(11); // at end
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    }

    // B7: Code fence
    void code_fence_hides_when_no_local_caret() {
        QTextDocument doc;
        doc.setPlainText("```python");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto fence = [](SourceSpan &s){ s.isCodeBlockFence = true; };
        h.setInlineSpans({
            delimiterSpan(0, 9, 0, 9, fence),
        });
        h.setLocalCaretPosition(-1);
        QTRY_VERIFY(isHidden(formatAt(&doc, 0)));
        QTRY_VERIFY(isHidden(formatAt(&doc, 8)));
    }

    void code_fence_reveals_when_caret_in_block() {
        QTextDocument doc;
        doc.setPlainText("```python");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto fence = [](SourceSpan &s){ s.isCodeBlockFence = true; };
        h.setInlineSpans({
            delimiterSpan(0, 9, 0, 9, fence),
        });
        h.setLocalCaretPosition(5);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    }

    // B8: List bullet and blockquote always shown
    void list_bullet_always_shown() {
        QTextDocument doc;
        doc.setPlainText("- An item");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto listMarker = [](SourceSpan &s){ s.isListMarker = true; };
        h.setInlineSpans({
            delimiterSpan(0, 2, 0, 9, listMarker),
        });
        h.setLocalCaretPosition(-1);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
        h.setLocalCaretPosition(5);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    }

    void blockquote_marker_always_shown() {
        QTextDocument doc;
        doc.setPlainText("> A quote");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bq = [](SourceSpan &s){ s.isBlockquoteMarker = true; };
        h.setInlineSpans({
            delimiterSpan(0, 2, 0, 9, bq),
        });
        h.setLocalCaretPosition(-1);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
        h.setLocalCaretPosition(5);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    }
};

QTEST_MAIN(TestE2AutohideBlockPrefix)
#include "tst_live_render_e2_autohide_block_prefix.moc"
