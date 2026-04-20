// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "corbomite/core/VaultResourceProvider.h"
#include "corbomite/readingview/LinkRenderer.h"

using namespace Corbomite::ReadingView;

namespace {

/// Minimal VaultResourceProvider stub for LinkRenderer unit tests.
/// Resolves wikilinks to `vault:///<target>` URLs; everything else
/// returns a stub value. No filesystem / metadata touched.
class StubResources : public Corbomite::Core::VaultResourceProvider
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
    bool wikiLinkExists(const QString &) const override { return true; }
};

} // namespace

class TstReadingViewLinkRenderer : public QObject
{
    Q_OBJECT

private slots:
    void testFileLinkEmitsHonestSource();
    void testFileLinkWithoutResourcesPassesLinkText();
    void testTagLinkSignal();
    void testExternalLinkSignal();
    void testClickedSignal();
};

void TstReadingViewLinkRenderer::testFileLinkEmitsHonestSource()
{
    StubResources resources;
    LinkRenderer r(&resources);
    QSignalSpy spy(&r, &LinkRenderer::linkHovered);
    r.emitFileLink({QStringLiteral("Target"),
                    QStringLiteral("From.md"),
                    QStringLiteral("markoff:reading"),
                    QPoint(10, 20)});
    QCOMPARE(spy.count(), 1);
    const auto args = spy.first();
    // Target resolved via StubResources -> "vault:///Target"
    QVERIFY(args[0].toString().contains(QStringLiteral("Target")));
    QCOMPARE(args[1].toString(), QStringLiteral("markoff:reading"));
    QCOMPARE(args[2].value<QPoint>(), QPoint(10, 20));
}

void TstReadingViewLinkRenderer::testFileLinkWithoutResourcesPassesLinkText()
{
    LinkRenderer r(nullptr);
    QSignalSpy spy(&r, &LinkRenderer::linkHovered);
    r.emitFileLink({QStringLiteral("Plain"),
                    QStringLiteral("From.md"),
                    QStringLiteral("markoff:reading"),
                    QPoint()});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first()[0].toString(), QStringLiteral("Plain"));
}

void TstReadingViewLinkRenderer::testTagLinkSignal()
{
    LinkRenderer r(nullptr);
    QSignalSpy spy(&r, &LinkRenderer::tagHovered);
    r.emitTagLink(QStringLiteral("#project"), QStringLiteral("markoff:reading"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first()[0].toString(), QStringLiteral("#project"));
    QCOMPARE(spy.first()[1].toString(), QStringLiteral("markoff:reading"));
}

void TstReadingViewLinkRenderer::testExternalLinkSignal()
{
    LinkRenderer r(nullptr);
    QSignalSpy spy(&r, &LinkRenderer::externalLinkActivated);
    r.emitExternalLink(QUrl(QStringLiteral("https://example.com/")),
                       QStringLiteral("markoff:reading"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first()[0].toUrl().toString(),
             QStringLiteral("https://example.com/"));
    QCOMPARE(spy.first()[1].toString(), QStringLiteral("markoff:reading"));
}

void TstReadingViewLinkRenderer::testClickedSignal()
{
    LinkRenderer r(nullptr);
    QSignalSpy spy(&r, &LinkRenderer::linkClicked);
    r.emitFileLinkClicked(QStringLiteral("Target"),
                          QStringLiteral("markoff:reading"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first()[0].toString(), QStringLiteral("Target"));
    QCOMPARE(spy.first()[1].toString(), QStringLiteral("markoff:reading"));
}

QTEST_APPLESS_MAIN(TstReadingViewLinkRenderer)
#include "tst_readingview_linkrenderer.moc"
