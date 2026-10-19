// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/embed/EmbedRuntime.h>

#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QSvgRenderer>

#include <memory>

namespace Markoff::Embed {

namespace {

/// Rasterise an SVG artifact at the request's DPR, fitting `availableWidth`
/// when one was given. Returns a null image on an invalid document so the
/// front-end can fall back to source.
QImage rasteriseSvg(const QByteArray &svg, const RenderRequest &request)
{
    if (svg.isEmpty()) return {};
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return {};

    QSizeF size = renderer.defaultSize();
    if (!size.isValid() || size.isEmpty()) size = QSizeF(600, 400);
    if (request.availableWidth.width() > 0
        && size.width() > request.availableWidth.width()) {
        const qreal scale = request.availableWidth.width() / size.width();
        size = QSizeF(request.availableWidth.width(), size.height() * scale);
    }

    const qreal dpr = qMax(1.0, request.devicePixelRatio);
    QImage image(qCeil(size.width() * dpr), qCeil(size.height() * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(dpr);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();
    return image;
}

}  // namespace

EmbedRuntime::EmbedRuntime(Markoff::CodeBlockProcessorRegistry *registry,
                           QObject *parent)
    : Markoff::EmbedRenderService(parent), m_registry(registry)
{
    m_pool.setMaxThreadCount(2);
}

EmbedRuntime::~EmbedRuntime()
{
    // Workers capture a QPointer to this object; wait so none outlives us.
    m_pool.waitForDone();
}

EmbedArtifact EmbedRuntime::artifact(const RenderRequest &request) const
{
    return m_cache.value(embedArtifactKey(request));
}

void EmbedRuntime::request(const RenderRequest &request)
{
    const QString key = embedArtifactKey(request);
    if (m_cache.contains(key) || m_pending.contains(key))
        return;

    const auto fail = [this, key](const QString &error) {
        EmbedArtifact a;
        a.state = EmbedArtifact::State::Failed;
        a.error = error;
        a.block.kind = RenderedBlock::Kind::Error;
        a.block.error = error;
        a.block.fallbackText = QString();
        store(key, a);
        emit artifactFailed(key, error);
    };

    if (!m_registry) {
        fail(tr("no embed renderer registry"));
        return;
    }
    const auto processor = m_registry->processorFor(request.language.toLower());
    if (!processor) {
        fail(tr("no backend for language '%1'").arg(request.language));
        return;
    }
    if (processor->contractVersion() != kEmbedContractVersion) {
        fail(tr("backend for '%1' was built against embed contract %2 (this runtime speaks %3)")
                 .arg(request.language)
                 .arg(processor->contractVersion())
                 .arg(kEmbedContractVersion));
        return;
    }

    EmbedArtifact pending;
    pending.state = EmbedArtifact::State::Pending;
    m_cache.insert(key, pending);
    m_pending.insert(key);

    QPointer<EmbedRuntime> alive(this);
    m_pool.start([alive, processor, request, key] {
        RenderedBlock block = processor->render(request);
        if (block.fallbackText.isEmpty())
            block.fallbackText = request.source;   // what the front-end paints on a miss
        if (QPointer<EmbedRuntime> self = alive) {
            QMetaObject::invokeMethod(
                self, [self, key, request, block = std::move(block)] {
                    if (self) self->finish(key, request, block);
                }, Qt::QueuedConnection);
        }
    });
}

QStringList EmbedRuntime::supportedLanguages() const
{
    return m_registry ? m_registry->registeredLanguages() : QStringList{};
}

QString EmbedRuntime::engineDescription(const QString &language) const
{
    const auto processor = m_registry
        ? m_registry->processorFor(language.toLower()) : nullptr;
    if (!processor) return {};
    return QStringLiteral("%1 (embed contract %2)")
        .arg(language.toLower())
        .arg(processor->contractVersion());
}

void EmbedRuntime::setCacheLimit(int entries)
{
    m_limit = qMax(1, entries);
    evictIfNeeded();
}

void EmbedRuntime::clearCache()
{
    m_cache.clear();
    m_lru.clear();
    m_pending.clear();
}

int EmbedRuntime::cachedArtifactCount() const
{
    return int(m_cache.size());
}

void EmbedRuntime::finish(const QString &key, const RenderRequest &request,
                          RenderedBlock block)
{
    if (!m_pending.remove(key))
        return;   // cache was cleared while the render was in flight

    EmbedArtifact artifact;
    if (block.kind == RenderedBlock::Kind::Svg) {
        // The runtime owns rasterisation: backends return vectors, so DPI,
        // width fitting and transparency are solved once for every engine.
        block.image = rasteriseSvg(block.svg, request);
        if (block.image.isNull()) {
            artifact.state = EmbedArtifact::State::Failed;
            artifact.error = tr("backend for '%1' returned an invalid SVG document")
                                 .arg(request.language);
            block.kind = RenderedBlock::Kind::Error;
            block.error = artifact.error;
        }
    }

    if (artifact.state == EmbedArtifact::State::Failed) {
        artifact.block = block;
        store(key, artifact);
        emit artifactFailed(key, artifact.error);
        return;
    }

    artifact.state = EmbedArtifact::State::Ready;
    artifact.block = block;
    store(key, artifact);
    emit artifactReady(key);
}

void EmbedRuntime::store(const QString &key, EmbedArtifact artifact)
{
    m_cache.insert(key, artifact);
    m_pending.remove(key);
    touch(key);
    evictIfNeeded();
}

void EmbedRuntime::touch(const QString &key)
{
    m_lru.removeAll(key);
    m_lru.append(key);
}

void EmbedRuntime::evictIfNeeded()
{
    while (m_cache.size() > m_limit && !m_lru.isEmpty()) {
        const QString victim = m_lru.first();
        if (m_pending.contains(victim))
            break;
        m_lru.removeFirst();
        m_cache.remove(victim);
    }
}

}  // namespace Markoff::Embed
