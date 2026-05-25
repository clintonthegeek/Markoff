// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarkdownLinkService.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/LinkActivation.h>
#include <markoff/core/LinkKind.h>

MarkdownLinkService::MarkdownLinkService(QObject *parent)
    : Markoff::LinkService(parent) {}

Markoff::LinkKind MarkdownLinkService::classify(const QString &t) const
{
    if (t.startsWith(QStringLiteral("http://"),  Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("mailto:"),  Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("[[")) && t.endsWith(QStringLiteral("]]")))
        return Markoff::LinkKind::WikiLink;
    if (t.startsWith(QLatin1Char('#')))                                 return Markoff::LinkKind::Tag;
    return Markoff::LinkKind::Unknown;
}

QUrl MarkdownLinkService::resolve(const QString &t, const QString &) const
{
    if (classify(t) == Markoff::LinkKind::External) return QUrl(t);
    return {};
}

void MarkdownLinkService::activate(const Markoff::LinkActivation &a)
{
    qInfo() << "[link] activated:" << a.rawText
            << "kind=" << int(a.kind)
            << "page=" << a.page
            << "section=" << a.section
            << "blockRef=" << a.blockRef
            << "alias=" << a.alias
            << "modifiers=" << int(a.modifiers);

    if (a.kind == Markoff::LinkKind::WikiLink) {
        QFileInfo here(a.fromContext);
        QFileInfo target(here.dir(), a.page + QStringLiteral(".md"));
        if (target.exists())
            Q_EMIT openRequested(target.absoluteFilePath(), a.section, a.blockRef);
        else
            qInfo() << "[link]   (would open" << target.absoluteFilePath() << "but not found)";
    } else {
        QDesktopServices::openUrl(a.resolvedTarget.isEmpty()
            ? QUrl(a.rawText) : a.resolvedTarget);
    }
    Markoff::LinkService::activate(a);
}

void MarkdownLinkService::notifyHover(const Markoff::LinkActivation &a, const QPoint &)
{
    Q_EMIT statusMessage(tr("Ctrl+click to open: %1").arg(
        a.page.isEmpty() ? a.rawText : a.page));
    Markoff::LinkService::notifyHover(a, {});
}

void MarkdownLinkService::notifyHoverLeft(const QString &t)
{
    Q_EMIT statusMessage({});
    Markoff::LinkService::notifyHoverLeft(t);
}
