// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

#include <markoff/core/CodeBlockProcessorRegistry.h>
#include <markoff/core/EmbedRender.h>

namespace Markoff::Embed {

/// Family embed runtime: the "machinery" module. Owns the artifact cache, the
/// worker pool and SVG rasterisation; backends (CodeBlockProcessor) are
/// registered into a CodeBlockProcessorRegistry it reads. A host may link this
/// and some backends, link nothing (leaves fall back to source), or replace it
/// with its own EmbedRenderService.
///
/// Threading: the public API is UI-thread affine. render() runs on the pool;
/// completion is delivered back with a queued invocation, so signals arrive on
/// the thread that owns this object.
class EmbedRuntime final : public Markoff::EmbedRenderService {
    Q_OBJECT
public:
    explicit EmbedRuntime(Markoff::CodeBlockProcessorRegistry *registry,
                          QObject *parent = nullptr);
    ~EmbedRuntime() override;

    EmbedArtifact artifact(const RenderRequest &request) const override;
    void request(const RenderRequest &request) override;

    QStringList supportedLanguages() const override;
    QString engineDescription(const QString &language) const override;

    /// Cached artifacts kept before the least-recently used is dropped.
    void setCacheLimit(int entries);
    void clearCache();
    int cachedArtifactCount() const;

private:
    void finish(const QString &key, const RenderRequest &request, RenderedBlock block);
    void store(const QString &key, EmbedArtifact artifact);
    void touch(const QString &key);
    void evictIfNeeded();

    Markoff::CodeBlockProcessorRegistry *m_registry = nullptr;
    QHash<QString, EmbedArtifact> m_cache;
    QList<QString> m_lru;          // most recently used last
    QSet<QString> m_pending;
    QThreadPool m_pool;
    int m_limit = 64;
};

}  // namespace Markoff::Embed
