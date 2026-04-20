// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include "markoff/DefaultMermaidRenderer.h"
#include "markoff/vault/DefaultLinkResolver.h"
#include "markoff/vault/DefaultMetadataCache.h"
#include "markoff/vault/DefaultMetadataParser.h"
#include "markoff/vault/DefaultResourceProvider.h"

#include <QtTest/QtTest>

class tst_MarkoffDefaultVaultProvider : public QObject
{
    Q_OBJECT

private slots:
    void resourceProviderReturnsEmpty();
    void linkResolverReturnsEmpty();
    void metadataCacheMisses();
    void metadataParserReturnsEmpty();
    void mermaidRendererReturnsEmpty();
};

void tst_MarkoffDefaultVaultProvider::resourceProviderReturnsEmpty()
{
    Markoff::Vault::DefaultResourceProvider rp;
    QVERIFY(rp.resolveWikiLink(QStringLiteral("x")).isEmpty());
    QVERIFY(!rp.resolveEmbed(QStringLiteral("x")).has_value());
    QVERIFY(rp.resolveImage(QStringLiteral("x")).isEmpty());
    QVERIFY(rp.loadImageBytes(QStringLiteral("x")).isEmpty());
    QVERIFY(!rp.wikiLinkExists(QStringLiteral("x")));
}

void tst_MarkoffDefaultVaultProvider::linkResolverReturnsEmpty()
{
    Markoff::Vault::DefaultLinkResolver lr;
    QCOMPARE(lr.resolve(QStringLiteral("link"), QStringLiteral("from.md")),
             QString{});
}

void tst_MarkoffDefaultVaultProvider::metadataCacheMisses()
{
    Markoff::Vault::DefaultMetadataCache mc;
    QCOMPARE(mc.getFileCache(QStringLiteral("any/path.md")), nullptr);
}

void tst_MarkoffDefaultVaultProvider::metadataParserReturnsEmpty()
{
    Markoff::Vault::DefaultMetadataParser mp;
    Markoff::Vault::DefaultLinkResolver lr;
    const auto result = mp.parse(QByteArray{}, QStringLiteral("x"), lr);
    QVERIFY(!result.cache.headings.has_value());
    QVERIFY(!result.cache.blocks.has_value());
    QVERIFY(!result.cache.sections.has_value());
}

void tst_MarkoffDefaultVaultProvider::mermaidRendererReturnsEmpty()
{
    Markoff::DefaultMermaidRenderer mr;
    QVERIFY(mr.renderSvg(QStringLiteral("graph TD")).isEmpty());
}

QTEST_MAIN(tst_MarkoffDefaultVaultProvider)
#include "tst_markoff_default_vault_provider.moc"
