// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/CompletionRegistry.h>

namespace Markoff {

CompletionRegistry::CompletionRegistry(QObject *parent) : QObject(parent) {}
CompletionRegistry::~CompletionRegistry() = default;

void CompletionRegistry::registerProvider(std::shared_ptr<CompletionProvider> p)
{
    if (!p) return;
    QObject::connect(p.get(), &CompletionProvider::candidatesReady,
                     this,    &CompletionRegistry::candidatesReady);
    m_providers.push_back(std::move(p));
}

void CompletionRegistry::unregisterProvider(CompletionProvider *raw)
{
    for (auto it = m_providers.begin(); it != m_providers.end(); ++it) {
        if (it->get() == raw) { m_providers.erase(it); break; }
    }
}

QList<CompletionCandidate>
CompletionRegistry::gather(const CompletionContext &ctx, quint64 reqId)
{
    QList<CompletionCandidate> out;
    for (auto &p : m_providers) {
        if (!p->handledTriggers().contains(ctx.trigger)) continue;
        out << p->candidatesFor(ctx, reqId);
    }
    return out;
}

}  // namespace Markoff
