// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/CodeBlockProcessorRegistry.h>

namespace Markoff {

CodeBlockProcessorRegistry::CodeBlockProcessorRegistry(QObject *parent)
    : QObject(parent) {}
CodeBlockProcessorRegistry::~CodeBlockProcessorRegistry() = default;

void CodeBlockProcessorRegistry::registerProcessor(
    std::shared_ptr<CodeBlockProcessor> p)
{
    if (!p) return;
    const QString lang = p->language();
    m_byLang.insert(lang, std::move(p));
    Q_EMIT processorRegistered(lang);
}

void CodeBlockProcessorRegistry::unregisterProcessor(const QString &lang)
{
    if (m_byLang.remove(lang) > 0) Q_EMIT processorUnregistered(lang);
}

std::shared_ptr<CodeBlockProcessor>
CodeBlockProcessorRegistry::processorFor(const QString &lang) const
{
    return m_byLang.value(lang);
}

QStringList CodeBlockProcessorRegistry::registeredLanguages() const
{
    return m_byLang.keys();
}

}  // namespace Markoff
