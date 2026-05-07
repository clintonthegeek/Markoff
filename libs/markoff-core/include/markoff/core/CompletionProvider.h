// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QSet>

#include <markoff/core/CompletionCandidate.h>
#include <markoff/core/CompletionContext.h>
#include <markoff/core/CompletionTrigger.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

class MARKOFF_CORE_EXPORT CompletionProvider : public QObject {
    Q_OBJECT
public:
    explicit CompletionProvider(QObject *parent = nullptr);
    ~CompletionProvider() override;

    virtual QSet<CompletionTrigger> handledTriggers() const = 0;
    virtual QList<CompletionCandidate>
        candidatesFor(const CompletionContext &, quint64 requestId) = 0;

Q_SIGNALS:
    void candidatesReady(quint64 requestId, QList<Markoff::CompletionCandidate>);
};

}  // namespace Markoff
