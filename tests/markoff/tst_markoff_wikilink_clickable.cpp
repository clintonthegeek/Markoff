// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cluster J phase 3 — positive integration test for the wikilink
// clickability feature.
//
// Before phase 3: `[[Target]]` rendered with highlight styling but was
// not a clickable anchor (MarkdownHighlighter didn't set anchorHref);
// no TextControl->Editor bridge existed; Editor::linkClicked was
// declared but never fired.
//
// After phase 3: MarkdownHighlighter stamps every wikilink content
// span with anchorHref="wikilink://<target>"; the Editor subscribes to
// each MarkdownTextItem's TextControl::linkActivated/linkHovered and
// routes them through Markoff::LinkRenderer with honest source
// strings; Editor::linkClicked fires through that chokepoint.
//
// This test uses the Editor::testActivateLink / testHoverLink hooks
// (QT_TESTLIB_LIB-guarded) to synthesize the anchor-activation path
// without needing a real QWidget click, which keeps the harness
// lightweight.

#include <QSignalSpy>
#include <QString>
#include <QUrl>
#include <QtTest>

#include <markoff/Editor.h>
#include <markoff/LinkRenderer.h>

// Private header — reached via tests/markoff/CMakeLists.txt's
// target_include_directories entry pointing at libs/markoff/src.
#include "MarkdownHighlighter.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

class TstMarkoffWikilinkClickable : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void wikilinkActivationFiresLinkClickedWithHonestSource()
    {
        Markoff::Editor editor;
        editor.setPlainText(QStringLiteral("See [[Target]]."));
        editor.setCurrentNotePath(QStringLiteral("/vault/note.md"));

        auto *renderer = editor.linkRenderer();
        QVERIFY(renderer != nullptr);

        QSignalSpy clickSpy(&editor, &Markoff::Editor::linkClicked);
        QSignalSpy rendererSpy(renderer, &Markoff::LinkRenderer::fileLinkActivated);

        editor.testActivateLink(QStringLiteral("wikilink://Target"));

        QCOMPARE(clickSpy.count(), 1);
        QCOMPARE(clickSpy.first().at(0).toString(), QStringLiteral("Target"));

        QCOMPARE(rendererSpy.count(), 1);
        const auto args = rendererSpy.first();
        QCOMPARE(args.at(0).toString(), QStringLiteral("Target"));
        // Honest source identifier: starts with "markoff:", never "bases".
        const QString sourceId = args.at(1).toString();
        QVERIFY2(sourceId.startsWith(QStringLiteral("markoff:")),
                 qPrintable(QStringLiteral("sourceId was: ") + sourceId));
        QVERIFY(sourceId != QStringLiteral("bases"));
        QCOMPARE(args.at(2).toString(), QStringLiteral("/vault/note.md"));
    }

    void wikilinkHoverRoutesThroughLinkRenderer()
    {
        Markoff::Editor editor;
        editor.setPlainText(QStringLiteral("Hover [[Alpha]]."));
        editor.setCurrentNotePath(QStringLiteral("/vault/alpha.md"));

        auto *renderer = editor.linkRenderer();
        QVERIFY(renderer != nullptr);

        QSignalSpy hoverSpy(renderer, &Markoff::LinkRenderer::fileLinkHovered);
        editor.testHoverLink(QStringLiteral("wikilink://Alpha"));

        QCOMPARE(hoverSpy.count(), 1);
        const auto args = hoverSpy.first();
        QCOMPARE(args.at(0).toString(), QStringLiteral("Alpha"));
        QVERIFY(args.at(1).toString().startsWith(QStringLiteral("markoff:")));
    }

    void externalLinkActivationRoutesThroughExternalChannel()
    {
        Markoff::Editor editor;
        editor.setPlainText(QStringLiteral("[docs](https://example.com)"));

        auto *renderer = editor.linkRenderer();
        QVERIFY(renderer != nullptr);

        QSignalSpy extSpy(renderer,
                          &Markoff::LinkRenderer::externalLinkActivated);
        QSignalSpy fileSpy(renderer, &Markoff::LinkRenderer::fileLinkActivated);

        editor.testActivateLink(QStringLiteral("https://example.com"));

        QCOMPARE(extSpy.count(), 1);
        QCOMPARE(fileSpy.count(), 0);
        const auto args = extSpy.first();
        QCOMPARE(args.at(0).toUrl(), QUrl(QStringLiteral("https://example.com")));
        QVERIFY(args.at(1).toString().startsWith(QStringLiteral("markoff:")));
    }

    void highlighterStampsAnchorHrefOnWikilinkSpans()
    {
        // Unit-test the MarkdownHighlighter in isolation: feed it a
        // hand-crafted span map for `Jump to [[Target]].` and assert
        // that the content span (`Target`) comes out with
        // QTextCharFormat::anchorHref = "wikilink://Target". This is
        // the invariant that TextControl's QWidgetTextControl anchor
        // detection relies on to fire linkActivated / linkHovered.
        QTextDocument doc;
        Markoff::MarkdownHighlighter hl(&doc);
        doc.setPlainText(QStringLiteral("Jump to [[Target]]."));

        // Span map — `[[` delimiter, `Target` content, `]]` delimiter.
        QList<Markoff::SourceSpan> spans;
        Markoff::SourceSpan lb;
        lb.charOffset = 8;
        lb.charLength = 2;
        lb.isWikilink = true;
        lb.isDelimiter = true;
        spans.append(lb);

        Markoff::SourceSpan content;
        content.charOffset = 10;
        content.charLength = 6; // "Target"
        content.isWikilink = true;
        content.isDelimiter = false;
        spans.append(content);

        Markoff::SourceSpan rb;
        rb.charOffset = 16;
        rb.charLength = 2;
        rb.isWikilink = true;
        rb.isDelimiter = true;
        spans.append(rb);

        hl.setSpanMap(spans);
        hl.rehighlight();

        // QSyntaxHighlighter stores formats on the QTextLayout as a
        // FormatRange overlay — NOT on the document's underlying
        // QTextCharFormat storage, so QTextCursor::charFormat() won't
        // see them. Walk the layout's formats list instead. This is
        // the same layer QWidgetTextControl reads to detect
        // click-on-anchor in production.
        QString anchorHref;
        bool anchorFound = false;
        QTextBlock b = doc.firstBlock();
        const auto ranges = b.layout()->formats();
        for (const auto &r : ranges) {
            if (!r.format.anchorHref().isEmpty()) {
                anchorFound = true;
                anchorHref = r.format.anchorHref();
                break;
            }
        }
        QVERIFY2(anchorFound,
                 "Expected anchorHref on the [[Target]] content span");
        QCOMPARE(anchorHref, QStringLiteral("wikilink://Target"));
    }
};

QTEST_MAIN(TstMarkoffWikilinkClickable)
#include "tst_markoff_wikilink_clickable.moc"
