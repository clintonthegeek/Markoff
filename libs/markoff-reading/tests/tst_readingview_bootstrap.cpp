// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 0b bootstrap test: validates that the readingview library links,
// that Corbomite::ReadingView::CodeBlockHighlighter constructs cleanly,
// and that a simple fenced-code QTextDocument can be fed through
// highlight() without crashing. No rendering assertions yet — this test
// exists to prove the build wiring is sound end-to-end.

#include "corbomite/readingview/CodeBlockHighlighter.h"

#include <QTest>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>

class TestReadingViewBootstrap : public QObject
{
    Q_OBJECT

private slots:
    void constructsCleanly();
    void highlightsFencedCodeBlockWithoutCrashing();
};

void TestReadingViewBootstrap::constructsCleanly()
{
    Corbomite::ReadingView::CodeBlockHighlighter hl;
    Q_UNUSED(hl);
    QVERIFY(true);
}

void TestReadingViewBootstrap::highlightsFencedCodeBlockWithoutCrashing()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("```python\nprint(1)\n```"));

    // Tag the inner line with a code language so the highlighter has
    // something to key off of. Penelope's pipeline normally sets this
    // during documentbuilder; here we set it manually to keep the test
    // self-contained.
    QTextBlock block = doc.begin();
    while (block.isValid()) {
        if (block.text() == QStringLiteral("print(1)")) {
            QTextCursor cursor(block);
            QTextBlockFormat bf = block.blockFormat();
            bf.setProperty(QTextFormat::BlockCodeLanguage,
                           QStringLiteral("python"));
            cursor.setBlockFormat(bf);
            break;
        }
        block = block.next();
    }

    Corbomite::ReadingView::CodeBlockHighlighter hl(
        Corbomite::ReadingView::Theme::Light);
    hl.highlight(&doc);

    QVERIFY(true);
}

QTEST_MAIN(TestReadingViewBootstrap)
#include "tst_readingview_bootstrap.moc"
