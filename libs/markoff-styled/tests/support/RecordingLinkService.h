// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPoint>
#include <QString>
#include <QVector>

#include <markoff/core/LinkActivation.h>
#include <markoff/core/LinkKind.h>
#include <markoff/core/LinkService.h>

class RecordingLinkService : public Markoff::LinkService {
public:
    struct ActivateCall   { Markoff::LinkActivation activation; };
    struct HoverCall      { Markoff::LinkActivation activation; QPoint globalPos; };
    struct HoverLeftCall  { QString linkText; };

    QVector<ActivateCall>  activates;
    QVector<HoverCall>     hovers;
    QVector<HoverLeftCall> hoverLefts;

    Markoff::LinkKind classify(const QString &) const override {
        return Markoff::LinkKind::External;
    }
    QUrl resolve(const QString &linkText, const QString & = {}) const override {
        return QUrl(linkText);
    }
    void activate(const Markoff::LinkActivation &a) override {
        activates.push_back({a});
        Markoff::LinkService::activate(a);
    }
    void notifyHover(const Markoff::LinkActivation &a, const QPoint &g) override {
        hovers.push_back({a, g});
        Markoff::LinkService::notifyHover(a, g);
    }
    void notifyHoverLeft(const QString &t) override {
        hoverLefts.push_back({t});
        Markoff::LinkService::notifyHoverLeft(t);
    }
};
