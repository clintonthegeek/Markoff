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

/// Cluster J phase 5 — media-stub placeholder factories (`.pdf`, `.mp3`,
/// `.wav`, `.mp4`, `.webm`). Contract: each factory produces a
/// MarkdownRenderChild whose rendered text contains both the filename
/// and a "preview not yet available" hint, and different media kinds
/// must produce distinct prefixes so hosts can distinguish them from
/// the rendered text alone.
class TstReadingViewEmbedMediaStubs : public QObject
{
    Q_OBJECT

private slots:
    void testPdfPlaceholder();
    void testAudioPlaceholder();
    void testVideoPlaceholder();
    void testMediaKindsAreDistinct();
};

static QString placeholderTextFor(const QString &ext)
{
    static InMemoryResources resources;
    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer renderer(&reg, nullptr, &resources);
    registerBuiltinEmbedFactories(reg, renderer);

    const QString path = QStringLiteral("sample.") + ext;
    Corbomite::Core::EmbedRequest req{path, QString(), &resources, 1};
    auto child = reg.dispatch(req);
    return child ? child->renderedText() : QString();
}

void TstReadingViewEmbedMediaStubs::testPdfPlaceholder()
{
    const QString text = placeholderTextFor(QStringLiteral("pdf"));
    QVERIFY2(!text.isEmpty(), "pdf factory must be registered");
    QVERIFY2(text.contains(QStringLiteral("sample.pdf")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("preview not yet available")),
             qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("PDF")), qPrintable(text));
}

void TstReadingViewEmbedMediaStubs::testAudioPlaceholder()
{
    for (const char *ext : {"mp3", "wav"}) {
        const QString text = placeholderTextFor(QLatin1String(ext));
        QVERIFY2(!text.isEmpty(), ext);
        QVERIFY2(text.contains(QStringLiteral("sample.")
                               + QLatin1String(ext)),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("preview not yet available")),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Audio")), qPrintable(text));
    }
}

void TstReadingViewEmbedMediaStubs::testVideoPlaceholder()
{
    for (const char *ext : {"mp4", "webm"}) {
        const QString text = placeholderTextFor(QLatin1String(ext));
        QVERIFY2(!text.isEmpty(), ext);
        QVERIFY2(text.contains(QStringLiteral("sample.")
                               + QLatin1String(ext)),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("preview not yet available")),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Video")), qPrintable(text));
    }
}

void TstReadingViewEmbedMediaStubs::testMediaKindsAreDistinct()
{
    const QString pdf = placeholderTextFor(QStringLiteral("pdf"));
    const QString mp3 = placeholderTextFor(QStringLiteral("mp3"));
    const QString mp4 = placeholderTextFor(QStringLiteral("mp4"));
    QVERIFY(!pdf.isEmpty() && !mp3.isEmpty() && !mp4.isEmpty());
    QVERIFY2(pdf != mp3, "PDF and Audio prefixes must differ");
    QVERIFY2(mp3 != mp4, "Audio and Video prefixes must differ");
    QVERIFY2(pdf != mp4, "PDF and Video prefixes must differ");
}

QTEST_GUILESS_MAIN(TstReadingViewEmbedMediaStubs)
#include "tst_readingview_embed_media_stubs.moc"
