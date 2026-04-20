// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "markoff/reading/LinkRenderer.h"

#include <markoff/vault/ResourceProvider.h>

namespace Markoff::Reading {

LinkRenderer::LinkRenderer(Markoff::Vault::ResourceProvider *resources,
                           QObject *parent)
    : QObject(parent), m_resources(resources)
{
}

void LinkRenderer::emitFileLink(const FileLinkRequest &req)
{
    QString resolved = req.linkText;
    if (m_resources) {
        const QUrl url = m_resources->resolveWikiLink(req.linkText);
        if (url.isValid() && !url.isEmpty()) {
            resolved = url.isLocalFile() ? url.toLocalFile() : url.toString();
        }
    }
    Q_EMIT linkHovered(resolved, req.sourceId, req.anchorHint);
}

void LinkRenderer::emitTagLink(const QString &tag, const QString &sourceId)
{
    Q_EMIT tagHovered(tag, sourceId);
}

void LinkRenderer::emitExternalLink(const QUrl &url, const QString &sourceId)
{
    Q_EMIT externalLinkActivated(url, sourceId);
}

void LinkRenderer::emitFileLinkClicked(const QString &target,
                                       const QString &sourceId)
{
    Q_EMIT linkClicked(target, sourceId);
}

} // namespace Markoff::Reading
