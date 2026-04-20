// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim. See VaultResourceProvider.h for context.
#pragma once

#include "corbomite/core/MarkdownRenderChild.h"
#include "corbomite/core/VaultResourceProvider.h"

#include <QHash>
#include <QString>

#include <functional>
#include <memory>

namespace Corbomite::Core {

struct EmbedRequest
{
    QString targetPath;
    QString subpath;
    VaultResourceProvider *resources = nullptr;
    int depth = 0;
};

using EmbedFactory = std::function<std::unique_ptr<MarkdownRenderChild>(
    const EmbedRequest &)>;

class EmbedRegistry
{
public:
    void registerExtension(const QString &ext, EmbedFactory factory)
    {
        m_factories.insert(ext.toLower(), std::move(factory));
    }

    std::unique_ptr<MarkdownRenderChild> dispatch(const EmbedRequest &req) const
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

} // namespace Corbomite::Core
