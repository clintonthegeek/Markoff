// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/CodeBlockProcessorRegistry.h>

namespace Markoff {

CodeBlockProcessorRegistry::CodeBlockProcessorRegistry(QObject *parent)
    : QObject(parent) {}
CodeBlockProcessorRegistry::~CodeBlockProcessorRegistry() = default;

void CodeBlockProcessorRegistry::registerProcessor(
    std::shared_ptr<CodeBlockProcessor> p)
{
    if (!p) return;
    for (const QString &language : p->languages()) {
        const QString lang = language.toLower();
        if (lang.isEmpty()) continue;
        m_byLang.insert(lang, p);
        Q_EMIT processorRegistered(lang);
    }
}

void CodeBlockProcessorRegistry::unregisterProcessor(const QString &lang)
{
    if (m_byLang.remove(lang) > 0) Q_EMIT processorUnregistered(lang);
}

std::shared_ptr<CodeBlockProcessor>
CodeBlockProcessorRegistry::processorFor(const QString &lang) const
{
    return m_byLang.value(lang.toLower());
}

QStringList CodeBlockProcessorRegistry::registeredLanguages() const
{
    return m_byLang.keys();
}

}  // namespace Markoff
