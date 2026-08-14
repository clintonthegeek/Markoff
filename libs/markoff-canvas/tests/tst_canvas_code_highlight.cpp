// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4.6 — code-block syntax highlighting (spec §5.3's "already in core"
// Kf6SyntaxHighlightService, wired through BlockLayoutCache::rebuildInline's
// atomic path per the T7 finding: token-color formats are appended to the
// SAME QTextLayout::FormatRange list InlineFormatting builds, so they ride
// the one setFormats() call rather than a second out-of-band one).
//
// Real end-to-end proof through View (same shape as
// tst_canvas_inline_formatting.cpp's code_fence_hides_per_block): load a
// fenced Python block, realize it, and read colors back off the layout's
// own formats() via View::codeTokenColorAt (new test/inspection accessor,
// same pattern as isDelimiterHiddenAt/lineNaturalWidth).

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

using Markoff::BlockId;
using Markoff::Canvas::View;
using Markoff::Theme;

namespace {

// Default Theme::defaultLight()/defaultDark() do not set colors for any of
// the CodeKeyword/CodeControlFlow/CodeNumber/... slots (a pre-existing gap
// noted separately, not this task's to fix) — build a theme that does, so
// the test actually exercises token-color *painting*, not just "did a
// format range with an invalid color get produced".
Theme themeWithCodeTokenColors()
{
    Theme t = Theme::defaultLight();
    t.setColor(Theme::Slot::CodeKeyword,     QColor("#ff00ff"));
    t.setColor(Theme::Slot::CodeControlFlow, QColor("#00ffff"));
    t.setColor(Theme::Slot::CodeNumber,      QColor("#ffaa00"));
    return t;
}

}  // namespace

class TstCanvasCodeHighlight : public QObject {
    Q_OBJECT

private slots:
    void python_fence_tokens_get_theme_colors();
    void unknown_language_key_stays_plain_monospace();
};

// The falsification target (plan's own line): feed the wrong language key
// and this test's keyword-color assertion must fail. See the plan's
// findings log entry for the throwaway commit SHAs.
void TstCanvasCodeHighlight::python_fence_tokens_get_theme_colors()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("```python\ndef foo():\n    return 1\n```\n\nOther\n");

    View view;
    view.resize(400, 300);
    view.setTheme(themeWithCodeTokenColors());
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId code = blocks[0];
    const QByteArray text = doc.blockText(code);
    QVERIFY(text.startsWith("```python"));
    QCOMPARE(view.realizedBlockCount() >= 1, true);

    const int defByte    = text.indexOf("def");
    const int returnByte = text.indexOf("return");
    const int oneByte    = text.indexOf('1', returnByte);
    QVERIFY(defByte >= 0);
    QVERIFY(returnByte >= 0);
    QVERIFY(oneByte >= 0);

    const Theme theme = view.theme();
    QCOMPARE(view.codeTokenColorAt(code, defByte),
             theme.color(Theme::Slot::CodeKeyword));
    QCOMPARE(view.codeTokenColorAt(code, returnByte),
             theme.color(Theme::Slot::CodeControlFlow));
    QCOMPARE(view.codeTokenColorAt(code, oneByte),
             theme.color(Theme::Slot::CodeNumber));

    // The fence line itself carries no token color (it isn't code content).
    QVERIFY(!view.codeTokenColorAt(code, 0).isValid());
}

// Spec's own fallback wording: "a service miss renders plain monospace".
// An info string the syntax service does not recognize must not crash and
// must leave every content byte uncolored (BlockPresentation's plain
// CodeBlock foreground/monospace stays as painted).
void TstCanvasCodeHighlight::unknown_language_key_stays_plain_monospace()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("```not-a-real-language-xyz\nhello world\n```\n\nOther\n");

    View view;
    view.resize(400, 300);
    view.setTheme(themeWithCodeTokenColors());
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId code = blocks[0];
    const QByteArray text = doc.blockText(code);
    const int helloByte = text.indexOf("hello");
    QVERIFY(helloByte >= 0);

    QVERIFY(!view.codeTokenColorAt(code, helloByte).isValid());
}

QTEST_MAIN(TstCanvasCodeHighlight)
#include "tst_canvas_code_highlight.moc"
