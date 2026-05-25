// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <markoff/core/MarkdownRenderChild.h>

#include <QHash>
#include <QString>

#include <functional>
#include <memory>

namespace Markoff {

namespace Vault {
class ResourceProvider;
}

/// Per-embed dispatch context. The factory receives this and returns a
/// `MarkdownRenderChild` that the host mounts into the rendering surface.
struct EmbedRequest
{
    QString targetPath;                          ///< `![[targetPath]]`
    QString subpath;                             ///< `#section` or `^block`
    Vault::ResourceProvider *resources = nullptr;
    int depth = 0;                               ///< for EmbedDepthGuard
};

using EmbedFactory = std::function<std::unique_ptr<MarkdownRenderChild>(
    const EmbedRequest &)>;

/// Per-document registry mapping file extensions to embed factories. Restored
/// 2026-05-20 driven by Corbomite port pull. The shape preserves the
/// master-side API (one extension-keyed factory; first-registered-wins) so
/// existing consumers like Corbomite's `EmbedRegistrar` proxy can migrate
/// without rework. No default factories ship in markoff-core — hosts register
/// concretes for the formats they support (`png`, `pdf`, `md`, etc.).
class EmbedRegistry
{
public:
    virtual ~EmbedRegistry() = default;

    virtual void registerExtension(const QString &ext, EmbedFactory factory)
    {
        m_factories.insert(ext.toLower(), std::move(factory));
    }

    virtual void unregisterExtension(const QString &ext)
    {
        m_factories.remove(ext.toLower());
    }

    virtual bool hasExtension(const QString &ext) const
    {
        return m_factories.contains(ext.toLower());
    }

    virtual std::unique_ptr<MarkdownRenderChild>
    dispatch(const EmbedRequest &req) const
    {
        const int dot = req.targetPath.lastIndexOf(QLatin1Char('.'));
        if (dot < 0) return nullptr;
        const QString ext = req.targetPath.mid(dot + 1).toLower();
        const auto it = m_factories.constFind(ext);
        if (it == m_factories.constEnd()) return nullptr;
        return it.value()(req);
    }

private:
    QHash<QString, EmbedFactory> m_factories;
};

} // namespace Markoff
