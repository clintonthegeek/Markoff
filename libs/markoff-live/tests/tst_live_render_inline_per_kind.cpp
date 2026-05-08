// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Per-flag inline-format-highlighter tests.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>

using namespace Markoff::Live;

// Helper: find first format range where pred(fmt) is true.
template <class Pred>
static QPair<int,int> findFormatRange(const QTextDocument &doc, Pred pred) {
    int start = -1, end = -1;
    QTextBlock block = doc.firstBlock();
    while (block.isValid()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            if (pred(frag.charFormat())) {
                if (start < 0) start = block.position() + frag.position() - block.position();
                end = start + frag.length();
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
};

QTEST_MAIN(TstLiveRenderInlinePerKind)
#include "tst_live_render_inline_per_kind.moc"
