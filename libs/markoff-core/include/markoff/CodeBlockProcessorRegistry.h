// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <QHash>
#include <QString>

#include <functional>

namespace Markoff {

struct CodeBlockContext
{
    QString sourcePath;
    QString language;
};

using CodeBlockProcessor =
    std::function<bool(const QString &source, void *node,
                       const CodeBlockContext &ctx)>;

class CodeBlockProcessorRegistry
{
public:
    virtual ~CodeBlockProcessorRegistry() = default;

    virtual void registerLanguage(const QString &lang, CodeBlockProcessor proc)
    {
        m_procs.insert(lang, std::move(proc));
    }

    virtual bool dispatch(const QString &lang,
                          const QString &source,
                          void *node,
                          const CodeBlockContext &ctx) const
    {
        const auto it = m_procs.constFind(lang);
        if (it == m_procs.constEnd()) return false;
        return it.value()(source, node, ctx);
    }

    virtual bool hasLanguage(const QString &lang) const
    {
        return m_procs.contains(lang);
    }

private:
    QHash<QString, CodeBlockProcessor> m_procs;
};

} // namespace Markoff
