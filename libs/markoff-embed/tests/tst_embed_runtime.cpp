// SPDX-License-Identifier: GPL-3.0-or-later
//
// Embed runtime contract tests: a fake backend proves the socket (registry →
// request → worker → artifact → signal) without choosing a real engine.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <memory>

#include <markoff/core/CodeBlockProcessorRegistry.h>
#include <markoff/core/EmbedRender.h>
#include <markoff/embed/EmbedRuntime.h>

using namespace Markoff;

namespace {

class FakeSvgProcessor final : public CodeBlockProcessor {
public:
    explicit FakeSvgProcessor(std::atomic<int> *renders, int version = kEmbedContractVersion)
        : m_renders(renders), m_version(version) {}

    QStringList languages() const override { return {QStringLiteral("fake")}; }
    int contractVersion() const override { return m_version; }

    RenderedBlock render(const RenderRequest &request) override
    {
        ++(*m_renders);
        RenderedBlock block;
        block.kind = RenderedBlock::Kind::Svg;
        block.svg = QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
            "<rect width=\"40\" height=\"20\" fill=\"#336699\"/></svg>").toUtf8();
        block.fallbackText = request.source;
        return block;
    }

private:
    std::atomic<int> *m_renders;
    int m_version;
};

RenderRequest makeRequest(const QString &source, const QString &language = QStringLiteral("fake"))
{
    RenderRequest request;
    request.language = language;
    request.source = source;
    request.availableWidth = QSizeF(-1, -1);
    request.foreground = QColor(20, 20, 20);
    request.background = QColor(255, 255, 255);
    request.themeKey = QStringLiteral("test-theme");
    return request;
}

}  // namespace

class TstEmbedRuntime : public QObject {
    Q_OBJECT

private slots:
    // The whole socket: request → Pending → worker render → rasterise → signal
    // → Ready, and a second request is a cache hit with no backend call.
    void requestRendersRasterisesAndCaches()
    {
        std::atomic<int> renders{0};
        CodeBlockProcessorRegistry registry;
        registry.registerProcessor(std::make_shared<FakeSvgProcessor>(&renders));
        Embed::EmbedRuntime runtime(&registry);

        const RenderRequest request = makeRequest(QStringLiteral("graph TD; A-->B"));
        const QString key = embedArtifactKey(request);

        QCOMPARE(runtime.artifact(request).state, EmbedArtifact::State::Missing);
        QSignalSpy ready(&runtime, &EmbedRenderService::artifactReady);
        QSignalSpy failed(&runtime, &EmbedRenderService::artifactFailed);

        runtime.request(request);
        QCOMPARE(runtime.artifact(request).state, EmbedArtifact::State::Pending);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(ready.first().first().toString(), key);

        const EmbedArtifact artifact = runtime.artifact(request);
        QCOMPARE(artifact.state, EmbedArtifact::State::Ready);
        QCOMPARE(artifact.block.kind, RenderedBlock::Kind::Svg);
        QVERIFY(!artifact.block.image.isNull());
        QCOMPARE(artifact.block.image.size(), QSize(40, 20));
        QCOMPARE(artifact.block.fallbackText, request.source);

        // Cache hit: no second render, no second signal.
        runtime.request(request);
        QTest::qWait(50);
        QCOMPARE(renders.load(), 1);
        QCOMPARE(ready.count(), 1);
    }

    void svgIsRasterisedAtTheRequestedDprAndWidth()
    {
        std::atomic<int> renders{0};
        CodeBlockProcessorRegistry registry;
        registry.registerProcessor(std::make_shared<FakeSvgProcessor>(&renders));
        Embed::EmbedRuntime runtime(&registry);

        RenderRequest request = makeRequest(QStringLiteral("dpr"));
        request.devicePixelRatio = 2.0;
        QSignalSpy ready(&runtime, &EmbedRenderService::artifactReady);
        runtime.request(request);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(runtime.artifact(request).block.image.size(), QSize(80, 40));
        QCOMPARE(runtime.artifact(request).block.image.devicePixelRatio(), 2.0);

        RenderRequest narrow = makeRequest(QStringLiteral("narrow"));
        narrow.availableWidth = QSizeF(20, -1);
        runtime.request(narrow);
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(runtime.artifact(narrow).block.image.size(), QSize(20, 10));
    }

    void missingBackendFailsWithoutAPendingJob()
    {
        std::atomic<int> renders{0};
        CodeBlockProcessorRegistry registry;
        Embed::EmbedRuntime runtime(&registry);

        const RenderRequest request = makeRequest(QStringLiteral("x"), QStringLiteral("graphviz"));
        QSignalSpy failed(&runtime, &EmbedRenderService::artifactFailed);
        runtime.request(request);
        QCOMPARE(failed.count(), 1);
        const EmbedArtifact artifact = runtime.artifact(request);
        QCOMPARE(artifact.state, EmbedArtifact::State::Failed);
        QVERIFY2(artifact.error.contains(QStringLiteral("no backend")),
                 qPrintable(artifact.error));
        QCOMPARE(renders.load(), 0);
        QVERIFY(runtime.supportedLanguages().isEmpty());
    }

    void backendFromAnotherContractVersionIsRefused()
    {
        std::atomic<int> renders{0};
        CodeBlockProcessorRegistry registry;
        registry.registerProcessor(
            std::make_shared<FakeSvgProcessor>(&renders, kEmbedContractVersion + 1));
        Embed::EmbedRuntime runtime(&registry);

        const RenderRequest request = makeRequest(QStringLiteral("x"));
        QSignalSpy failed(&runtime, &EmbedRenderService::artifactFailed);
        runtime.request(request);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(runtime.artifact(request).state, EmbedArtifact::State::Failed);
        QCOMPARE(renders.load(), 0);
    }

    void differentSourceIsANewArtifact()
    {
        std::atomic<int> renders{0};
        CodeBlockProcessorRegistry registry;
        registry.registerProcessor(std::make_shared<FakeSvgProcessor>(&renders));
        Embed::EmbedRuntime runtime(&registry);

        const RenderRequest first = makeRequest(QStringLiteral("one"));
        const RenderRequest second = makeRequest(QStringLiteral("two"));
        QVERIFY(embedArtifactKey(first) != embedArtifactKey(second));

        QSignalSpy ready(&runtime, &EmbedRenderService::artifactReady);
        runtime.request(first);
        QTRY_COMPARE(ready.count(), 1);
        runtime.request(second);
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(renders.load(), 2);
        QCOMPARE(runtime.cachedArtifactCount(), 2);
    }

    void cacheEvictsLeastRecentlyUsed()
    {
        std::atomic<int> renders{0};
        CodeBlockProcessorRegistry registry;
        registry.registerProcessor(std::make_shared<FakeSvgProcessor>(&renders));
        Embed::EmbedRuntime runtime(&registry);
        runtime.setCacheLimit(2);

        QSignalSpy ready(&runtime, &EmbedRenderService::artifactReady);
        const RenderRequest a = makeRequest(QStringLiteral("a"));
        const RenderRequest b = makeRequest(QStringLiteral("b"));
        const RenderRequest c = makeRequest(QStringLiteral("c"));
        runtime.request(a);
        runtime.request(b);
        QTRY_COMPARE(ready.count(), 2);
        runtime.request(c);
        QTRY_COMPARE(ready.count(), 3);
        QCOMPARE(runtime.cachedArtifactCount(), 2);
        QCOMPARE(runtime.artifact(a).state, EmbedArtifact::State::Missing);
        QCOMPARE(runtime.artifact(c).state, EmbedArtifact::State::Ready);
    }

    void engineDescriptionNamesTheBackendAndContract()
    {
        std::atomic<int> renders{0};
        CodeBlockProcessorRegistry registry;
        registry.registerProcessor(std::make_shared<FakeSvgProcessor>(&renders));
        Embed::EmbedRuntime runtime(&registry);
        QVERIFY(runtime.supportedLanguages().contains(QStringLiteral("fake")));
        QVERIFY(runtime.engineDescription(QStringLiteral("fake"))
                    .contains(QStringLiteral("embed contract")));
        QVERIFY(runtime.engineDescription(QStringLiteral("graphviz")).isEmpty());
    }
};

QTEST_MAIN(TstEmbedRuntime)
#include "tst_embed_runtime.moc"
