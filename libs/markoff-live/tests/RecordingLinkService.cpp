// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingLinkService.h"

namespace Markoff::LiveTest {

Markoff::LinkKind RecordingLinkService::classify(const QString &t) const {
    if (t.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
        return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("mailto:"), Qt::CaseInsensitive))
        return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive))
        return Markoff::LinkKind::File;
    if (t.startsWith(QStringLiteral("[[")) && t.endsWith(QStringLiteral("]]")))
        return Markoff::LinkKind::WikiLink;
    if (t.startsWith(QLatin1Char('#')))
        return Markoff::LinkKind::Tag;
    return Markoff::LinkKind::Unknown;
}

void RecordingLinkService::activate(const Markoff::LinkActivation &a) {
    activations.append(a);
    Markoff::LinkService::activate(a);
}

void RecordingLinkService::notifyHover(const Markoff::LinkActivation &a, const QPoint &p) {
    hovers.append(a);
    Markoff::LinkService::notifyHover(a, p);
}

void RecordingLinkService::notifyHoverLeft(const QString &t) {
    hoverLefts.append(t);
    Markoff::LinkService::notifyHoverLeft(t);
}

}  // namespace Markoff::LiveTest
