// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/ReadingView.h"
#include "corbomite/readingview/SectionLayout.h"
#include "corbomite/readingview/styling/StyleManager.h"

// SpanRenderer is internal; use its known property slot id (0x100001).
static constexpr int kWikiLinkTargetProperty = 0x100001;

#include <QAbstractTextDocumentLayout>
#include <QGraphicsItem>
#include <QGraphicsItemGroup>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace Corbomite::ReadingView;

class TestSectionLayoutWikiLinks : public QObject
{
    Q_OBJECT
private slots:
    void plainWikiLinkCarriesTarget();
    void aliasedWikiLinkUsesDisplayAndCarriesTarget();
    void clickEmitsWikiLinkActivated();
};

static QGraphicsTextItem *firstTextItem(QGraphicsItem *root)
{
    QList<QGraphicsItem *> stack{ root };
    while (!stack.isEmpty()) {
        auto *it = stack.takeLast();
        if (auto *t = qgraphicsitem_cast<QGraphicsTextItem *>(it))
            return t;
        for (auto *c : it->childItems()) stack << c;
    }
    return nullptr;
}

static QString findWikiTarget(QTextDocument *doc, const QString &displayText)
{
    for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
        for (auto it = blk.begin(); it != blk.end(); ++it) {
            if (!it.fragment().isValid()) continue;
            const QString text = it.fragment().text();
            if (!text.contains(displayText)) continue;
            const QVariant v = it.fragment().charFormat()
                                 .property(kWikiLinkTargetProperty);
            if (v.isValid()) return v.toString();
        }
    }
    return {};
}

void TestSectionLayoutWikiLinks::plainWikiLinkCarriesTarget()
{
    std::unique_ptr<StyleManager> styles(
        StyleManager::makeObsidianDefault(Theme::Light));
    SectionLayout lay;
    ReadingSection sec;
    SectionLayout::Context ctx;
    ctx.styles = styles.get();
    ctx.theme = Theme::Light;
    ctx.contentWidth = 600.0;

    auto *g = lay.layoutSection(
        sec, QStringLiteral("A [[Foo]] link.\n"), ctx);
    QVERIFY(g);
    auto *ti = firstTextItem(g);
    QVERIFY(ti);
    QCOMPARE(findWikiTarget(ti->document(), QStringLiteral("Foo")),
             QStringLiteral("Foo"));
    delete g;
}

void TestSectionLayoutWikiLinks::aliasedWikiLinkUsesDisplayAndCarriesTarget()
{
    std::unique_ptr<StyleManager> styles(
        StyleManager::makeObsidianDefault(Theme::Light));
    SectionLayout lay;
    ReadingSection sec;
    SectionLayout::Context ctx;
    ctx.styles = styles.get();
    ctx.theme = Theme::Light;
    ctx.contentWidth = 600.0;

    auto *g = lay.layoutSection(
        sec, QStringLiteral("See [[Bar|the Bar page]].\n"), ctx);
    QVERIFY(g);
    auto *ti = firstTextItem(g);
    QVERIFY(ti);
    const QString plain = ti->document()->toPlainText();
    QVERIFY(plain.contains(QStringLiteral("the Bar page")));
    QVERIFY(!plain.contains(QStringLiteral("[[")));

    QCOMPARE(findWikiTarget(ti->document(),
                             QStringLiteral("the Bar page")),
             QStringLiteral("Bar"));
    delete g;
}

void TestSectionLayoutWikiLinks::clickEmitsWikiLinkActivated()
{
    ReadingView rv;
    rv.resize(600, 400);
    rv.setContentWidth(500.0);
    rv.setPlainText(QStringLiteral("Go to [[Target]] please.\n"));
    rv.show();
    QTest::qWait(30);

    QSignalSpy spy(&rv, &ReadingView::wikiLinkActivated);

    // Find the fragment screen position by walking the first section's text.
    QVERIFY(!rv.sections().isEmpty());
    QGraphicsItem *root = rv.sections().first()->graphicsItem();
    QVERIFY(root);
    auto *ti = firstTextItem(root);
    QVERIFY(ti);

    // Locate the character position of "Target" in the document.
    QTextDocument *doc = ti->document();
    const QString plain = doc->toPlainText();
    const int idx = plain.indexOf(QStringLiteral("Target"));
    QVERIFY(idx >= 0);

    // Compute a point inside the wiki link.
    const QRectF bb = ti->boundingRect();
    const QPointF itemPoint(10.0, bb.height() / 2.0);
    const QPointF scenePt = ti->mapToScene(itemPoint);
    const QPoint viewPt = rv.mapFromScene(scenePt);

    QTest::mouseClick(rv.viewport(), Qt::LeftButton, Qt::KeyboardModifiers(),
                      viewPt);
    QTest::qWait(20);

    // The click may land on the wiki link or nearby text; iterate across the
    // visible paragraph line to find the activation point.
    if (spy.isEmpty()) {
        for (int x = 0; x < int(bb.width()); x += 10) {
            const QPointF p(x, bb.height() / 2.0);
            const QPoint vp = rv.mapFromScene(ti->mapToScene(p));
            QTest::mouseClick(rv.viewport(), Qt::LeftButton, {}, vp);
            if (!spy.isEmpty()) break;
        }
    }
    QVERIFY(!spy.isEmpty());
    QCOMPARE(spy.first().first().toString(), QStringLiteral("Target"));
}

QTEST_MAIN(TestSectionLayoutWikiLinks)
#include "tst_sectionlayout_wiki_links.moc"
