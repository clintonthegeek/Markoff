// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include "markoff/EmbedRegistry.h"
#include "markoff/MarkdownRenderChild.h"

#include <QtTest/QtTest>

using namespace Markoff;

class tst_MarkoffEmbedRegistry : public QObject
{
    Q_OBJECT

private slots:
    void dispatchesRegisteredExtension();
    void noMatchReturnsNull();
    void extensionIsCaseInsensitive();
};

namespace {
struct CountingChild : MarkdownRenderChild
{
    static int s_constructed;
    CountingChild() { ++s_constructed; }
};
int CountingChild::s_constructed = 0;
} // namespace

void tst_MarkoffEmbedRegistry::dispatchesRegisteredExtension()
{
    CountingChild::s_constructed = 0;
    EmbedRegistry reg;
    reg.registerExtension(QStringLiteral("png"),
                          [](const EmbedRequest &) {
                              return std::make_unique<CountingChild>();
                          });
    EmbedRequest req;
    req.targetPath = QStringLiteral("foo/bar.png");
    auto child = reg.dispatch(req);
    QVERIFY(child);
    QCOMPARE(CountingChild::s_constructed, 1);
}

void tst_MarkoffEmbedRegistry::noMatchReturnsNull()
{
    EmbedRegistry reg;
    reg.registerExtension(QStringLiteral("png"), [](const EmbedRequest &) {
        return std::make_unique<MarkdownRenderChild>();
    });
    EmbedRequest req;
    req.targetPath = QStringLiteral("foo/bar.jpg");
    QCOMPARE(reg.dispatch(req), nullptr);
}

void tst_MarkoffEmbedRegistry::extensionIsCaseInsensitive()
{
    EmbedRegistry reg;
    int count = 0;
    reg.registerExtension(QStringLiteral("PNG"),
                          [&count](const EmbedRequest &) {
                              ++count;
                              return std::make_unique<MarkdownRenderChild>();
                          });
    EmbedRequest req;
    req.targetPath = QStringLiteral("foo/BAR.Png");
    QVERIFY(reg.dispatch(req));
    QCOMPARE(count, 1);
}

QTEST_MAIN(tst_MarkoffEmbedRegistry)
#include "tst_markoff_embed_registry.moc"
