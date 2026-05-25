// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>

#include <memory>
#include <vector>

#include <markoff/core/CompletionCandidate.h>
#include <markoff/core/CompletionContext.h>
#include <markoff/core/CompletionProvider.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

class MARKOFF_CORE_EXPORT CompletionRegistry : public QObject {
    Q_OBJECT
public:
    explicit CompletionRegistry(QObject *parent = nullptr);
    ~CompletionRegistry() override;

    void registerProvider(std::shared_ptr<CompletionProvider>);
    void unregisterProvider(CompletionProvider *);
    QList<CompletionCandidate>
        gather(const CompletionContext &, quint64 requestId);

Q_SIGNALS:
    void candidatesReady(quint64 requestId, QList<Markoff::CompletionCandidate>);

private:
    std::vector<std::shared_ptr<CompletionProvider>> m_providers;
};

}  // namespace Markoff
