// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/LinkService.h>

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
