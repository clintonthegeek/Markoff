// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim. See VaultResourceProvider.h for context.
#pragma once

#include <QString>

class QWidget;

namespace Corbomite::Core {

class MarkdownRenderChild
{
public:
    MarkdownRenderChild() = default;
    virtual ~MarkdownRenderChild() = default;

    void setRenderedText(const QString &text) { m_text = text; }
    const QString &renderedText() const { return m_text; }

    // Stub: real implementation attaches a widget to parent. Phase A
    // callers that reach this path have already been told the runtime is
    // broken until Phase B.
    virtual void mountInto(QWidget * /*parent*/) {}

private:
    QString m_text;
};

} // namespace Corbomite::Core
