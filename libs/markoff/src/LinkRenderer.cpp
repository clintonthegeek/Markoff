// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/LinkRenderer.h"

namespace Markoff {

LinkRenderer::LinkRenderer(QObject *parent)
    : QObject(parent)
{
}

void LinkRenderer::emitFileLinkActivated(const FileLinkRequest &req)
{
    Q_EMIT fileLinkActivated(req.linkText, req.sourceId, req.fromPath);
}

void LinkRenderer::emitFileLinkHovered(const FileLinkRequest &req)
{
    Q_EMIT fileLinkHovered(req.linkText, req.sourceId, req.anchorHint);
}

void LinkRenderer::emitTagHovered(const QString &tag, const QString &sourceId)
{
    Q_EMIT tagHovered(tag, sourceId);
}

void LinkRenderer::emitExternalLinkActivated(const QUrl &url, const QString &sourceId)
{
    Q_EMIT externalLinkActivated(url, sourceId);
}

void LinkRenderer::emitExternalLinkHovered(const QUrl &url, const QString &sourceId)
{
    Q_EMIT externalLinkHovered(url, sourceId);
}

} // namespace Markoff
