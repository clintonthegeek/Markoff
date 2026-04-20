// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/SectionLayout.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <QGraphicsItem>
#include <QGraphicsItemGroup>
#include <QTest>
#include <QtSvgWidgets/QGraphicsSvgItem>

using namespace Corbomite::ReadingView;

class TestSectionLayoutMermaid : public QObject
{
    Q_OBJECT
private slots:
    void fencedMermaidRendersToSvgItem();
};

static QGraphicsSvgItem *findSvgItem(QGraphicsItem *root)
{
    QList<QGraphicsItem *> stack{ root };
    while (!stack.isEmpty()) {
        auto *it = stack.takeLast();
        if (it->type() == QGraphicsSvgItem::Type)
            return static_cast<QGraphicsSvgItem *>(it);
        for (auto *c : it->childItems()) stack << c;
    }
    return nullptr;
}

void TestSectionLayoutMermaid::fencedMermaidRendersToSvgItem()
{
    std::unique_ptr<StyleManager> styles(
        StyleManager::makeObsidianDefault(Theme::Light));
    SectionLayout lay;
    ReadingSection sec;
    SectionLayout::Context ctx;
    ctx.styles = styles.get();
    ctx.theme = Theme::Light;
    ctx.contentWidth = 600.0;

    const QString md = QStringLiteral(
        "```mermaid\n"
        "graph TD;\n"
        "A-->B;\n"
        "```\n");
    auto *g = lay.layoutSection(sec, md, ctx);
    QVERIFY(g);

    auto *svg = findSvgItem(g);
    QVERIFY2(svg != nullptr,
             "mmdr must have produced a QGraphicsSvgItem; if it failed to "
             "render the layout should fall back but this test asserts the "
             "normal path. Check mmdr availability if this fires.");

    QVERIFY(svg->boundingRect().width() > 0);
    QVERIFY(svg->boundingRect().height() > 0);
    delete g;
}

QTEST_MAIN(TestSectionLayoutMermaid)
#include "tst_sectionlayout_mermaid.moc"
