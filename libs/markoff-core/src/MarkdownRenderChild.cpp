// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include <markoff/core/MarkdownRenderChild.h>

namespace Markoff {

MarkdownRenderChild::MarkdownRenderChild() = default;
MarkdownRenderChild::~MarkdownRenderChild() = default;

void MarkdownRenderChild::setRenderedText(const QString &text)
{
    m_text = text;
}

const QString &MarkdownRenderChild::renderedText() const
{
    return m_text;
}

void MarkdownRenderChild::mountInto(QWidget * /*parent*/)
{
    // Default no-op. Host adapters override.
}

} // namespace Markoff
