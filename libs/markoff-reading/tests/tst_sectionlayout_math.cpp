// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/SectionLayout.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <QGraphicsItem>
#include <QGraphicsItemGroup>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace Corbomite::ReadingView;

class TestSectionLayoutMath : public QObject
{
    Q_OBJECT
private slots:
    void inlineAndDisplayMathProduceRenderedOutput();
};

static QList<QGraphicsItem *> flatten(QGraphicsItem *root)
{
    QList<QGraphicsItem *> out;
    QList<QGraphicsItem *> stack{ root };
    while (!stack.isEmpty()) {
        auto *it = stack.takeLast();
        out << it;
        for (auto *c : it->childItems()) stack << c;
    }
    return out;
}

void TestSectionLayoutMath::inlineAndDisplayMathProduceRenderedOutput()
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
        "An inline $x=1$ example.\n\n"
        "$$\ny = 2\n$$\n");
    auto *g = lay.layoutSection(sec, md, ctx);
    QVERIFY(g);

    const auto items = flatten(g);

    // Display math should render as a QGraphicsPixmapItem with height > 0.
    bool sawPixmap = false;
    for (auto *it : items) {
        if (auto *p = qgraphicsitem_cast<QGraphicsPixmapItem *>(it)) {
            const QSize sz = p->pixmap().size();
            if (sz.height() > 0 && sz.width() > 0) {
                sawPixmap = true;
                break;
            }
        }
    }
    QVERIFY(sawPixmap);

    // Inline math should leave an ObjectReplacementCharacter in the paragraph
    // document with our ReadingMathObject type.
    bool sawInlineMath = false;
    for (auto *it : items) {
        auto *t = qgraphicsitem_cast<QGraphicsTextItem *>(it);
        if (!t) continue;
        QTextDocument *doc = t->document();
        for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
            for (auto fit = blk.begin(); fit != blk.end(); ++fit) {
                if (!fit.fragment().isValid()) continue;
                const QTextCharFormat cf = fit.fragment().charFormat();
                if (cf.objectType() > 0 && cf.isValid()) {
                    // Recognise any of our inline math markers.
                    const QString src =
                        cf.property(QTextFormat::UserProperty + 10)
                          .toString();
                    if (!src.isEmpty()) sawInlineMath = true;
                }
            }
        }
    }
    QVERIFY(sawInlineMath);
    delete g;
}

QTEST_MAIN(TestSectionLayoutMath)
#include "tst_sectionlayout_math.moc"
