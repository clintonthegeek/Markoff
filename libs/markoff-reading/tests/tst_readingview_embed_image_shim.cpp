// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>

#include "corbomite/core/EmbedRegistry.h"
#include "corbomite/core/MarkdownRenderChild.h"
#include "corbomite/core/VaultResourceProvider.h"
#include "corbomite/readingview/EmbedRenderer.h"

using namespace Corbomite::ReadingView;

namespace {

class InMemoryResources : public Corbomite::Core::VaultResourceProvider
{
public:
    QUrl resolveImage(const QString &name) const override
    {
        return QUrl(QStringLiteral("file:///fake/") + name);
    }
    QByteArray loadImageBytes(const QString &) const override { return {}; }
    std::optional<QString> resolveEmbed(const QString &) const override
    {
        return std::nullopt;
    }
    QUrl resolveWikiLink(const QString &target) const override
    {
        return QUrl(QStringLiteral("vault:///") + target);
    }
    bool wikiLinkExists(const QString &) const override { return false; }
};

} // namespace

/// Cluster J phase 5 — image built-in registration (wikilink-shim).
/// Contract: `![[foo.png]]` renders identically to `![](foo.png)` — i.e.
/// the factory emits a MarkdownRenderChild whose `renderedText()` is
/// exactly the markdown image snippet. The actual image pixel-blit
/// happens when a host routes that text back through SpanRenderer; this
/// test asserts the wire-level equivalence.
class TstReadingViewEmbedImageShim : public QObject
{
    Q_OBJECT

private slots:
    void testImageShimPng();
    void testImageShimAllExtensions();
    void testShimMatchesInlineImageSource();
};

void TstReadingViewEmbedImageShim::testImageShimPng()
{
    InMemoryResources resources;
    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer renderer(&reg, nullptr, &resources);
    registerBuiltinEmbedFactories(reg, renderer);

    Corbomite::Core::EmbedRequest req{QStringLiteral("foo.png"),
                                      QString(), &resources, 1};
    auto child = reg.dispatch(req);
    QVERIFY2(child != nullptr, "png extension must be registered");
    QCOMPARE(child->renderedText(), QStringLiteral("![](foo.png)"));
}

void TstReadingViewEmbedImageShim::testImageShimAllExtensions()
{
    InMemoryResources resources;
    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer renderer(&reg, nullptr, &resources);
    registerBuiltinEmbedFactories(reg, renderer);

    for (const char *ext : {"png", "jpg", "jpeg", "gif", "svg", "webp"}) {
        const QString path = QStringLiteral("img.") + QLatin1String(ext);
        Corbomite::Core::EmbedRequest req{path, QString(), &resources, 1};
        auto child = reg.dispatch(req);
        QVERIFY2(child != nullptr, ext);
        const QString expected = QStringLiteral("![](") + path
                                 + QStringLiteral(")");
        QCOMPARE(child->renderedText(), expected);
    }
}

void TstReadingViewEmbedImageShim::testShimMatchesInlineImageSource()
{
    // Obsidian parity: `![[foo.png]]` is semantically identical to
    // `![](foo.png)`. This test documents that the registry's rendered
    // text is byte-for-byte the inline image token a SpanRenderer
    // consumer would see from a native `![](...)` source — no alias,
    // no subpath processing.
    InMemoryResources resources;
    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer renderer(&reg, nullptr, &resources);
    registerBuiltinEmbedFactories(reg, renderer);

    const QString path = QStringLiteral("path/to/diagram.svg");
    Corbomite::Core::EmbedRequest req{path, QString(), &resources, 1};
    auto child = reg.dispatch(req);
    QVERIFY(child);
    // Expected shim output matches what a markdown source `![](...)` parse
    // would hand to the inline-image pipeline.
    QCOMPARE(child->renderedText(),
             QStringLiteral("![](path/to/diagram.svg)"));
}

QTEST_GUILESS_MAIN(TstReadingViewEmbedImageShim)
#include "tst_readingview_embed_image_shim.moc"
