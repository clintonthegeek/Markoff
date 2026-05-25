// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LIVE_TESTS_RECORDING_LINK_SERVICE_H
#define MARKOFF_LIVE_TESTS_RECORDING_LINK_SERVICE_H

#include <QList>
#include <QString>

#include <markoff/core/LinkActivation.h>
#include <markoff/core/LinkService.h>

namespace Markoff::LiveTest {

/// LinkService that captures every activate/notifyHover/notifyHoverLeft
/// call for assertion in tests. Classify mirrors DefaultLinkService.
class RecordingLinkService : public Markoff::LinkService {
    Q_OBJECT
public:
    QList<Markoff::LinkActivation> activations;
    QList<Markoff::LinkActivation> hovers;
    QList<QString>                 hoverLefts;

    Markoff::LinkKind classify(const QString &t) const override;
    QUrl resolve(const QString &, const QString &) const override { return {}; }
    void activate(const Markoff::LinkActivation &a) override;
    void notifyHover(const Markoff::LinkActivation &a, const QPoint &p) override;
    void notifyHoverLeft(const QString &t) override;
};

}  // namespace Markoff::LiveTest

#endif  // MARKOFF_LIVE_TESTS_RECORDING_LINK_SERVICE_H
