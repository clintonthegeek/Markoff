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

/// Cluster J phase 5 — verify `registerBuiltinEmbedFactories` wires the
/// `.md` built-in factory onto the EmbedRegistry. Image + media-stub
/// coverage lives in sibling test files that land in tasks 5.5 + 5.6.
class TstReadingViewEmbedBuiltins : public QObject
{
    Q_OBJECT

private slots:
    void testMarkdownBuiltinWholeNote();
    void testMarkdownBuiltinHeadingEmbed();
    void testMarkdownBuiltinBlockEmbed();
};

void TstReadingViewEmbedBuiltins::testMarkdownBuiltinWholeNote()
{
    InMemoryResources resources;
    resources.addNote(QStringLiteral("Target.md"),
                      QStringLiteral("# Title\n\nBody paragraph.\n"));

    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer renderer(&reg, nullptr, &resources);
    registerBuiltinEmbedFactories(reg, renderer);

    Corbomite::Core::EmbedRequest req{
        QStringLiteral("Target.md"), QString(), &resources, 1};
    auto child = reg.dispatch(req);
    QVERIFY2(child != nullptr,
             ".md factory must be registered and return a child");
    QVERIFY(child->renderedText().contains(QStringLiteral("Body paragraph")));
}

void TstReadingViewEmbedBuiltins::testMarkdownBuiltinHeadingEmbed()
{
    InMemoryResources resources;
    resources.addNote(
        QStringLiteral("Target.md"),
        QStringLiteral(
            "# First\nIgnored body.\n\n## Second\nWanted body.\n\n## Third\n"
            "Also ignored.\n"));

    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer renderer(&reg, nullptr, &resources);
    registerBuiltinEmbedFactories(reg, renderer);

    Corbomite::Core::EmbedRequest req{QStringLiteral("Target.md"),
                                      QStringLiteral("#Second"),
                                      &resources, 1};
    auto child = reg.dispatch(req);
    QVERIFY(child);
    const QString text = child->renderedText();
    QVERIFY2(text.contains(QStringLiteral("Wanted body")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("Ignored body")), qPrintable(text));
}

void TstReadingViewEmbedBuiltins::testMarkdownBuiltinBlockEmbed()
{
    InMemoryResources resources;
    resources.addNote(
        QStringLiteral("Target.md"),
        QStringLiteral("intro paragraph\n\nblock body ^blk\n\nouter\n"));

    Corbomite::Core::EmbedRegistry reg;
    EmbedRenderer renderer(&reg, nullptr, &resources);
    registerBuiltinEmbedFactories(reg, renderer);

    Corbomite::Core::EmbedRequest req{QStringLiteral("Target.md"),
                                      QStringLiteral("#^blk"),
                                      &resources, 1};
    auto child = reg.dispatch(req);
    QVERIFY(child);
    const QString text = child->renderedText();
    QVERIFY2(text.contains(QStringLiteral("block body")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("intro paragraph")),
             qPrintable(text));
}

QTEST_GUILESS_MAIN(TstReadingViewEmbedBuiltins)
#include "tst_readingview_embed_builtins.moc"
