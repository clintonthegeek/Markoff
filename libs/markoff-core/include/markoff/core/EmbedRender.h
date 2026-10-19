// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <markoff/core/CodeBlockProcessor.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/RenderedBlock.h>

namespace Markoff {

/// What the front-end sees for one request: a cached artifact, a pending job,
/// or a failure. `Missing` means "never requested"; asking again is free.
struct MARKOFF_CORE_EXPORT EmbedArtifact {
    enum class State { Missing, Pending, Ready, Failed };
    State          state = State::Missing;
    RenderedBlock  block;
    QString        error;
};

/// Stable cache key for a request. Front-ends and the runtime must agree on it
/// (the completion signal carries only this string). Width is bucketed to whole
/// DIP and DPR to two decimals so sub-pixel churn does not thrash the cache.
MARKOFF_CORE_EXPORT QString embedArtifactKey(const RenderRequest &request);

/// The embed runtime contract. Declared in core so a leaf can consume it without
/// depending on any runtime or backend; implemented by `markoff-embed` (or by a
/// host that supplies its own). A null service means "no embedded rendering".
class MARKOFF_CORE_EXPORT EmbedRenderService : public QObject {
    Q_OBJECT
public:
    explicit EmbedRenderService(QObject *parent = nullptr);
    ~EmbedRenderService() override;

    /// Non-blocking. Never renders; answers from the cache.
    virtual EmbedArtifact artifact(const RenderRequest &request) const = 0;

    /// Ask for `request`. Cheap and idempotent: a duplicate of a pending or
    /// finished request is ignored. When it settles the service emits
    /// artifactReady/artifactFailed with `embedArtifactKey(request)`.
    virtual void request(const RenderRequest &request) = 0;

    virtual QStringList supportedLanguages() const = 0;

    /// Human-readable backend name for a language ("mermaid-rs", "pikchr"),
    /// or empty when unsupported. For diagnostics, not layout.
    virtual QString engineDescription(const QString &language) const = 0;

Q_SIGNALS:
    /// Emitted on the service's thread (the front-end's, when it owns one).
    void artifactReady(const QString &key);
    void artifactFailed(const QString &key, const QString &error);
};

}  // namespace Markoff
