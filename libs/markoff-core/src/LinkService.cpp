// SPDX-License-Identifier: GPL-3.0-or-later
//
// D2 adapter note (Phase 12.3):
// LinkService is a thin event-relay with no document reference. It does not
// hold a MarkoffDocument* and therefore cannot call doc.linkRefMap() directly.
// D2 link resolution (looking up a link target in the document's link-ref map)
// belongs to the view layer: the view holds both the MarkoffDocument and the
// LinkService, and may call doc.linkRefMap().get(id) before delegating to
// LinkService::activate(). No changes to this file are required for Phase 12.
#include <markoff/core/LinkService.h>

namespace Markoff {

LinkService::LinkService(QObject *parent) : QObject(parent) {}
LinkService::~LinkService() = default;

void LinkService::activate(const LinkActivation &a)
{
    Q_EMIT linkActivated(a);
}

void LinkService::notifyHover(const LinkActivation &a, const QPoint &p)
{
    Q_EMIT linkHovered(a, p);
}

void LinkService::notifyHoverLeft(const QString &t)
{
    Q_EMIT linkHoverLeft(t);
}

}  // namespace Markoff
