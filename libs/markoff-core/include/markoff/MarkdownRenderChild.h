// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <QString>

class QWidget;

namespace Markoff {

class MarkdownRenderChild
{
public:
    MarkdownRenderChild();
    virtual ~MarkdownRenderChild();

    void setRenderedText(const QString &text);
    const QString &renderedText() const;

    // Default no-op. Host adapters (e.g. Corbomite::Core::MarkdownRenderChild)
    // override to attach a widget to the given parent.
    virtual void mountInto(QWidget *parent);

private:
    QString m_text;
};

} // namespace Markoff
