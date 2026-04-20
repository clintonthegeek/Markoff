// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 3a SectionLayout test: lays out each of the six MVP content types
// in isolation and asserts a single-graphics-item-subtree result.

#include "corbomite/readingview/CodeBlockHighlighter.h"
#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/SectionLayout.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace Corbomite::ReadingView;

class TestSectionLayoutBasic : public QObject
{
    Q_OBJECT

private slots:
    void heading();
    void paragraphBold();
    void codeBlockHasLanguageTag();
    void unorderedList();
    void horizontalRule();
    void blockquote();
    void renderedShapeIsNonEmpty();

private:
    SectionLayout::Context ctx();
    std::unique_ptr<StyleManager> m_styles;
};

SectionLayout::Context TestSectionLayoutBasic::ctx()
{
    if (!m_styles)
        m_styles.reset(StyleManager::makeObsidianDefault(Theme::Light));
    SectionLayout::Context c;
    c.styles = m_styles.get();
    c.theme = Theme::Light;
    c.contentWidth = 800.0;
    return c;
}

static int countChildrenOfType(QGraphicsItemGroup *g, int type)
{
    int n = 0;
    for (auto *child : g->childItems()) {
        if (child->type() == type) ++n;
    }
    return n;
}

void TestSectionLayoutBasic::heading()
{
    SectionLayout lay;
    ReadingSection sec;
    auto *g = lay.layoutSection(sec, QStringLiteral("# Hello\n"), ctx());
    QVERIFY(g != nullptr);
    QCOMPARE(countChildrenOfType(g, QGraphicsTextItem::Type), 1);
    auto children = g->childItems();
    QGraphicsTextItem *ti = nullptr;
    for (auto *c : children) {
        if (c->type() == QGraphicsTextItem::Type) {
            ti = static_cast<QGraphicsTextItem *>(c);
            break;
        }
    }
    QVERIFY(ti != nullptr);
    // H1 font size should exceed Body's 14pt default.
    QVERIFY(ti->font().pointSizeF() > 14.0);
    delete g;
}

void TestSectionLayoutBasic::paragraphBold()
{
    SectionLayout lay;
    ReadingSection sec;
    auto *g = lay.layoutSection(
        sec, QStringLiteral("This is **bold** text.\n"), ctx());
    QVERIFY(g != nullptr);
    QGraphicsTextItem *ti = nullptr;
    for (auto *c : g->childItems()) {
        if (c->type() == QGraphicsTextItem::Type) {
            ti = static_cast<QGraphicsTextItem *>(c);
            break;
        }
    }
    QVERIFY(ti != nullptr);
    // Walk the document; at least one char should be bold-weighted.
    QTextDocument *doc = ti->document();
    bool anyBold = false;
    for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
        for (auto it = blk.begin(); it != blk.end(); ++it) {
            if (!it.fragment().isValid()) continue;
            const QTextCharFormat cf = it.fragment().charFormat();
            if (cf.fontWeight() >= QFont::Bold) { anyBold = true; break; }
        }
        if (anyBold) break;
    }
    QVERIFY(anyBold);
    delete g;
}

void TestSectionLayoutBasic::codeBlockHasLanguageTag()
{
    SectionLayout lay;
    ReadingSection sec;
    auto *g = lay.layoutSection(
        sec,
        QStringLiteral("```python\nprint('x')\n```\n"),
        ctx());
    QVERIFY(g != nullptr);
    // A highlighter must have been spawned for the fenced block.
    QVERIFY(!lay.ownedHighlighters().isEmpty());

    // At least one QTextBlock in at least one QGraphicsTextItem carries
    // BlockCodeLanguage == python.
    bool found = false;
    for (auto *c : g->childItems()) {
        if (c->type() != QGraphicsTextItem::Type) continue;
        auto *ti = static_cast<QGraphicsTextItem *>(c);
        auto *doc = ti->document();
        for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
            if (blk.blockFormat().property(QTextFormat::BlockCodeLanguage)
                    .toString() == QStringLiteral("python")) {
                found = true;
                break;
            }
        }
        if (found) break;
    }
    QVERIFY(found);
    delete g;
}

void TestSectionLayoutBasic::unorderedList()
{
    SectionLayout lay;
    ReadingSection sec;
    auto *g = lay.layoutSection(
        sec,
        QStringLiteral("- one\n- two\n- three\n"),
        ctx());
    QVERIFY(g != nullptr);

    // Lists render as a single QGraphicsTextItem with 3 QTextBlocks in its
    // embedded QTextList.
    QGraphicsTextItem *ti = nullptr;
    for (auto *c : g->childItems()) {
        if (c->type() == QGraphicsTextItem::Type) {
            ti = static_cast<QGraphicsTextItem *>(c);
            break;
        }
    }
    QVERIFY(ti != nullptr);

    int listBlockCount = 0;
    QTextDocument *doc = ti->document();
    for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
        if (blk.textList()) ++listBlockCount;
    }
    QCOMPARE(listBlockCount, 3);
    delete g;
}

void TestSectionLayoutBasic::horizontalRule()
{
    SectionLayout lay;
    ReadingSection sec;
    auto *g = lay.layoutSection(sec, QStringLiteral("---\n"), ctx());
    QVERIFY(g != nullptr);
    QCOMPARE(countChildrenOfType(g, QGraphicsLineItem::Type), 1);
    delete g;
}

void TestSectionLayoutBasic::blockquote()
{
    SectionLayout lay;
    ReadingSection sec;
    auto *g = lay.layoutSection(sec, QStringLiteral("> quoted\n"), ctx());
    QVERIFY(g != nullptr);
    // Must have at least one text item (the quote body).
    QVERIFY(countChildrenOfType(g, QGraphicsTextItem::Type) >= 1);
    // The left-border bar is a QGraphicsRectItem.
    int rectCount = 0;
    for (auto *c : g->childItems()) {
        if (c->type() == 3) // QGraphicsRectItem::Type == 3
            ++rectCount;
    }
    QVERIFY(rectCount >= 1);
    delete g;
}

void TestSectionLayoutBasic::renderedShapeIsNonEmpty()
{
    SectionLayout lay;
    ReadingSection sec;
    auto *g = lay.layoutSection(
        sec, QStringLiteral("# Hi\n\nBody.\n"), ctx());
    QVERIFY(g != nullptr);
    // SHA-256 digest is 32 bytes.
    QCOMPARE(sec.renderedShape().size(), 32);
    delete g;
}

QTEST_MAIN(TestSectionLayoutBasic)
#include "tst_sectionlayout_basic.moc"
