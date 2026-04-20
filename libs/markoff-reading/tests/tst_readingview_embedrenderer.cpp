// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QByteArray>
#include <QHash>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>

#include "corbomite/core/EmbedDepthGuard.h"
#include "corbomite/core/EmbedRegistry.h"
#include "corbomite/core/MarkdownRenderChild.h"
#include "corbomite/core/VaultResourceProvider.h"
#include "corbomite/readingview/EmbedRenderer.h"
#include "corbomite/storage/LinkResolver.h"
#include "corbomite/storage/MetadataCache.h"

using namespace Corbomite::ReadingView;

namespace {

/// In-memory VaultResourceProvider: maps path -> markdown text. Used
/// by EmbedRenderer tests so the renderer can call `resolveEmbed(path)`
/// without touching the filesystem.
class InMemoryResources : public Corbomite::Core::VaultResourceProvider
{
public:
    void addNote(const QString &path, const QString &content)
    {
        m_notes.insert(path, content);
    }
    QUrl resolveImage(const QString &name) const override
    {
        return QUrl(QStringLiteral("file:///fake/") + name);
    }
    QByteArray loadImageBytes(const QString &) const override { return {}; }
    std::optional<QString> resolveEmbed(const QString &name) const override
    {
        const auto it = m_notes.constFind(name);
        if (it == m_notes.constEnd()) return std::nullopt;
        return it.value();
    }
    QUrl resolveWikiLink(const QString &target) const override
    {
        return QUrl(QStringLiteral("vault:///") + target);
    }
    bool wikiLinkExists(const QString &target) const override
    {
        return m_notes.contains(target);
    }

private:
    QHash<QString, QString> m_notes;
};

} // namespace

class TstReadingViewEmbedRenderer : public QObject
{
    Q_OBJECT

private slots:
    void testEmbedWholeNote();
    void testEmbedHeadingSection();
    void testEmbedBlockRef();
    void testSelfEmbedStopsAtCap();
    void testDepthGuardPlaceholderCarriesTarget();
};

void TstReadingViewEmbedRenderer::testEmbedWholeNote()
{
    InMemoryResources resources;
    resources.addNote(QStringLiteral("Target.md"),
                      QStringLiteral("# Target\n\nBody text.\n"));

    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer r(&reg, /*cache=*/nullptr, &resources);
    auto child = r.render({QStringLiteral("Target.md"),
                           QString(),
                           &resources,
                           /*depth=*/1});
    QVERIFY(child != nullptr);
    QVERIFY(child->renderedText().contains(QStringLiteral("Body text")));
}

void TstReadingViewEmbedRenderer::testEmbedHeadingSection()
{
    InMemoryResources resources;
    resources.addNote(
        QStringLiteral("Target.md"),
        QStringLiteral(
            "# First\nIgnored.\n\n## Second\nWanted.\n\n## Third\nAlso "
            "ignored.\n"));

    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer r(&reg, /*cache=*/nullptr, &resources);
    auto child = r.render({QStringLiteral("Target.md"),
                           QStringLiteral("#Second"),
                           &resources,
                           /*depth=*/1});
    QVERIFY(child);
    const QString text = child->renderedText();
    QVERIFY2(text.contains(QStringLiteral("Wanted")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("Ignored")), qPrintable(text));
}

void TstReadingViewEmbedRenderer::testEmbedBlockRef()
{
    InMemoryResources resources;
    resources.addNote(
        QStringLiteral("Target.md"),
        QStringLiteral("intro paragraph\n\nblock body ^blk\n\nouter\n"));

    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer r(&reg, /*cache=*/nullptr, &resources);
    auto child = r.render({QStringLiteral("Target.md"),
                           QStringLiteral("#^blk"),
                           &resources,
                           /*depth=*/1});
    QVERIFY(child);
    const QString text = child->renderedText();
    QVERIFY2(text.contains(QStringLiteral("block body")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("intro paragraph")),
             qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("outer")), qPrintable(text));
}

void TstReadingViewEmbedRenderer::testSelfEmbedStopsAtCap()
{
    // Self.md embeds itself. Each recursion increments depth; at the
    // sixth attempt the depth guard rejects and the placeholder is
    // substituted inline.
    InMemoryResources resources;
    resources.addNote(QStringLiteral("Self.md"), QStringLiteral("![[Self]]\n"));

    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer r(&reg, /*cache=*/nullptr, &resources);
    // Register a .md factory that delegates back to renderMarkdown; this
    // lets the nested-embed expansion pass route through the registry.
    reg.registerExtension(
        QStringLiteral("md"),
        [&](const Corbomite::Core::EmbedRequest &req) {
            return r.renderMarkdown(req);
        });

    auto child = r.render({QStringLiteral("Self.md"),
                           QString(),
                           &resources,
                           /*depth=*/1});
    QVERIFY(child);
    const QString text = child->renderedText();
    QVERIFY2(text.contains(QStringLiteral("embed depth exceeded")),
             qPrintable(text));
    // Safety: if this test ever hangs, infinite recursion slipped past the
    // guard; the test harness timeout catches it as a failure.
}

void TstReadingViewEmbedRenderer::testDepthGuardPlaceholderCarriesTarget()
{
    // `placeholderTarget` returns the raw target string so host widgets
    // can wire a clickable onClick handler (Obsidian `oJ` parity).
    const QString label = QStringLiteral("Note.md");
    QCOMPARE(Corbomite::Core::EmbedDepthGuard::placeholderTarget(label), label);

    // A request at exactly the cap (`depth >= 5`) produces the placeholder
    // text embedded in the rendered child.
    InMemoryResources resources;
    resources.addNote(QStringLiteral("Note.md"), QStringLiteral("body\n"));
    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer r(&reg, /*cache=*/nullptr, &resources);
    auto child = r.render({QStringLiteral("Note.md"),
                           QString(),
                           &resources,
                           /*depth=*/5});
    QVERIFY(child);
    QVERIFY(child->renderedText().contains(
        QStringLiteral("embed depth exceeded")));
    QVERIFY(child->renderedText().contains(QStringLiteral("Note.md")));
}

QTEST_GUILESS_MAIN(TstReadingViewEmbedRenderer)
#include "tst_readingview_embedrenderer.moc"
