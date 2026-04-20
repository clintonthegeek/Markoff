// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/SectionLayout.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <QGraphicsItemGroup>
#include <QGraphicsTextItem>
#include <QTest>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextDocument>

using namespace Corbomite::ReadingView;

class TestSectionLayoutTables : public QObject
{
    Q_OBJECT
private slots:
    void gfmTableWithAlignments();
};

static QList<QGraphicsTextItem *> collectTextItems(QGraphicsItem *root)
{
    QList<QGraphicsTextItem *> out;
    QList<QGraphicsItem *> stack;
    stack << root;
    while (!stack.isEmpty()) {
        auto *it = stack.takeLast();
        if (auto *t = qgraphicsitem_cast<QGraphicsTextItem *>(it))
            out << t;
        for (auto *c : it->childItems()) stack << c;
    }
    return out;
}

void TestSectionLayoutTables::gfmTableWithAlignments()
{
    std::unique_ptr<StyleManager> styles(
        StyleManager::makeObsidianDefault(Theme::Light));

    SectionLayout lay;
    ReadingSection sec;

    // 3-column header + 2 data rows → 9 cells.
    const QString md = QStringLiteral(
        "| Left | Center | Right |\n"
        "|:-----|:------:|------:|\n"
        "| a1   | b1     | c1    |\n"
        "| a2   | b2     | c2    |\n");

    SectionLayout::Context ctx;
    ctx.styles = styles.get();
    ctx.theme = Theme::Light;
    ctx.contentWidth = 600.0;

    auto *g = lay.layoutSection(sec, md, ctx);
    QVERIFY(g != nullptr);

    const auto items = collectTextItems(g);
    QCOMPARE(items.size(), 9);

    // Read back the alignment on block format of each column's first cell.
    // Order of children is traversal-order; the layout emitted headers first
    // (L, C, R) then row1 then row2. Column-by-column cell text distinguishes.
    auto alignmentForText = [&](const QString &text) -> Qt::Alignment {
        for (auto *t : items) {
            if (t->document()->toPlainText().trimmed() == text) {
                QTextBlock blk = t->document()->firstBlock();
                return blk.blockFormat().alignment();
            }
        }
        return Qt::Alignment();
    };

    QCOMPARE(alignmentForText(QStringLiteral("Left")), Qt::AlignLeft);
    QCOMPARE(alignmentForText(QStringLiteral("Center")), Qt::AlignHCenter);
    QCOMPARE(alignmentForText(QStringLiteral("Right")), Qt::AlignRight);

    // Data cells honour column alignment too.
    QCOMPARE(alignmentForText(QStringLiteral("b1")), Qt::AlignHCenter);
    QCOMPARE(alignmentForText(QStringLiteral("c2")), Qt::AlignRight);

    delete g;
}

QTEST_MAIN(TestSectionLayoutTables)
#include "tst_sectionlayout_tables.moc"
