// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LIVE_APP_MARKDOWN_LINK_SERVICE_H
#define MARKOFF_LIVE_APP_MARKDOWN_LINK_SERVICE_H

#include <QObject>
#include <QString>

#include <markoff/core/LinkService.h>

class MarkdownLinkService : public Markoff::LinkService {
    Q_OBJECT
public:
    explicit MarkdownLinkService(QObject *parent = nullptr);

    Markoff::LinkKind classify(const QString &t) const override;
    QUrl     resolve(const QString &t, const QString &fromCtx) const override;
    void     activate(const Markoff::LinkActivation &a) override;
    void     notifyHover(const Markoff::LinkActivation &a, const QPoint &p) override;
    void     notifyHoverLeft(const QString &t) override;

Q_SIGNALS:
    void openRequested(const QString &path, const QString &section, const QString &blockRef);
    void statusMessage(const QString &);
};

#endif  // MARKOFF_LIVE_APP_MARKDOWN_LINK_SERVICE_H
