// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <markoff/core/MarkoffCoreExport.h>

#include <QString>

class QWidget;

namespace Markoff {

/// Lifecycle-tied widget/scene-node subtree produced by post-processors,
/// code-block processors, and embed renderers. Restored 2026-05-20 driven by
/// Corbomite port pull on EmbedRegistry — the type was retired with the old
/// leaves but is the result-holder shape EmbedFactory expects.
class MARKOFF_CORE_EXPORT MarkdownRenderChild
{
public:
    MarkdownRenderChild();
    virtual ~MarkdownRenderChild();

    void setRenderedText(const QString &text);
    const QString &renderedText() const;

    /// Default no-op. Host adapters override to attach a widget to `parent`.
    virtual void mountInto(QWidget *parent);

private:
    QString m_text;
};

} // namespace Markoff
