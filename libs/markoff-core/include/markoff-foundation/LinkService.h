// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QUrl>

#include <markoff-foundation/LinkActivation.h>
#include <markoff-foundation/LinkKind.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT LinkService : public QObject {
    Q_OBJECT
public:
    explicit LinkService(QObject *parent = nullptr);
    ~LinkService() override;

    virtual LinkKind classify(const QString &linkText) const = 0;
    virtual QUrl resolve(const QString &linkText,
                         const QString &fromContext = {}) const = 0;
    virtual void activate(const LinkActivation &);
    virtual void notifyHover(const LinkActivation &, const QPoint &globalPos);
    virtual void notifyHoverLeft(const QString &linkText);

Q_SIGNALS:
    void linkActivated(const Markoff::LinkActivation &);
    void linkHovered(const Markoff::LinkActivation &, const QPoint &globalPos);
    void linkHoverLeft(const QString &linkText);
};

}  // namespace Markoff
