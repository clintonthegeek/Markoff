// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/MarkdownRenderChild.h"

#include <QHash>
#include <QString>

#include <functional>
#include <memory>

namespace Markoff {

namespace Vault {
class ResourceProvider;
}

struct EmbedRequest
{
    QString targetPath;
    QString subpath;
    Vault::ResourceProvider *resources = nullptr;
    int depth = 0;
};

using EmbedFactory = std::function<std::unique_ptr<MarkdownRenderChild>(
    const EmbedRequest &)>;

class EmbedRegistry
{
public:
    virtual ~EmbedRegistry() = default;

    virtual void registerExtension(const QString &ext, EmbedFactory factory)
    {
        m_factories.insert(ext.toLower(), std::move(factory));
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
