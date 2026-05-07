// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>

#include <memory>
#include <vector>

#include <markoff-foundation/CompletionCandidate.h>
#include <markoff-foundation/CompletionContext.h>
#include <markoff-foundation/CompletionProvider.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT CompletionRegistry : public QObject {
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
