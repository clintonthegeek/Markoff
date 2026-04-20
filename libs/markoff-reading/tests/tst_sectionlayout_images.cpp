// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/SectionLayout.h"
#include "corbomite/readingview/VaultResourceProvider.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <QBuffer>
#include <QGraphicsItem>
#include <QGraphicsItemGroup>
#include <QGraphicsTextItem>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>

using namespace Corbomite::ReadingView;

namespace {
class InMemoryProvider : public VaultResourceProvider
{
public:
    QByteArray pngBytes;
    QUrl resolveImage(const QString &) const override { return {}; }
    QByteArray loadImageBytes(const QString &name) const override
    {
        if (name == QStringLiteral("image.png")) return pngBytes;
        return {};
    }
    std::optional<QString> resolveEmbed(const QString &) const override
    {
        return std::nullopt;
    }
    QUrl resolveWikiLink(const QString &) const override { return {}; }
    bool wikiLinkExists(const QString &) const override { return false; }
};
} // namespace

class TestSectionLayoutImages : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void inlineImageResolves();
    void absentImageFallsBackToAlt();

private:
    std::unique_ptr<StyleManager> m_styles;
    InMemoryProvider m_provider;
};

void TestSectionLayoutImages::initTestCase()
{
    m_styles.reset(StyleManager::makeObsidianDefault(Theme::Light));

    // Build a 32x32 red-square PNG in memory.
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QBuffer buf(&m_provider.pngBytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    buf.close();
    QVERIFY(!m_provider.pngBytes.isEmpty());
}

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

void TestSectionLayoutImages::inlineImageResolves()
{
    SectionLayout lay;
    ReadingSection sec;

    SectionLayout::Context ctx;
    ctx.styles = m_styles.get();
    ctx.theme = Theme::Light;
    ctx.contentWidth = 400.0;
    ctx.vaultProvider = &m_provider;

    const QString md =
        QStringLiteral("Here is an image ![red](image.png) inline.\n");
    auto *g = lay.layoutSection(sec, md, ctx);
    QVERIFY(g != nullptr);

    auto *ti = firstTextItem(g);
    QVERIFY(ti != nullptr);

    // Walk the doc and find a QTextImageFormat.
    QTextDocument *doc = ti->document();
    bool foundImage = false;
    for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
        for (auto it = blk.begin(); it != blk.end(); ++it) {
            if (!it.fragment().isValid()) continue;
            const QTextCharFormat cf = it.fragment().charFormat();
            if (cf.isImageFormat()) {
                QTextImageFormat imf = cf.toImageFormat();
                if (imf.width() == 32 && imf.height() == 32)
                    foundImage = true;
            }
        }
    }
    QVERIFY(foundImage);
    delete g;
}

void TestSectionLayoutImages::absentImageFallsBackToAlt()
{
    SectionLayout lay;
    ReadingSection sec;

    SectionLayout::Context ctx;
    ctx.styles = m_styles.get();
    ctx.theme = Theme::Light;
    ctx.contentWidth = 400.0;
    ctx.vaultProvider = &m_provider;

    const QString md =
        QStringLiteral("Missing ![absent](nope.png) here.\n");
    auto *g = lay.layoutSection(sec, md, ctx);
    QVERIFY(g != nullptr);

    auto *ti = firstTextItem(g);
    QVERIFY(ti != nullptr);

    // Expect the plain-text body to include "[absent]".
    const QString plain = ti->document()->toPlainText();
    QVERIFY2(plain.contains(QStringLiteral("[absent]")),
             qPrintable(plain));

    delete g;
}

QTEST_MAIN(TestSectionLayoutImages)
#include "tst_sectionlayout_images.moc"
